#include "VoxelLighting.h"
#include "GlobalShader.h"
#include "RHIPipelineStateCache.h"

#include <algorithm>
#include <stdexcept>

namespace Renderer
{
    using namespace RenderCore;
    using namespace RHI;

    BEGIN_SHADER_PARAMETER_STRUCT(VoxelLightingInjectParameters)
        SHADER_PARAMETER_STRUCT_REFERENCE(WorldRadianceCacheParameters, Source)
        SHADER_PARAMETER(uint32_t, bRefreshAll)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float4>, VoxelOutput)
    END_SHADER_PARAMETER_STRUCT(VoxelLightingInjectParameters)

    BEGIN_SHADER_PARAMETER_STRUCT(VoxelLightingDebugParameters)
        SHADER_PARAMETER_STRUCT_REFERENCE(WorldRadianceCacheParameters, Source)
        SHADER_PARAMETER_STRUCT_REFERENCE(VoxelLightingParameters, Voxels)
        SHADER_PARAMETER(Core::UInt2, OutputSize)
        SHADER_PARAMETER(uint32_t, Slice)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, DebugOutput)
    END_SHADER_PARAMETER_STRUCT(VoxelLightingDebugParameters)

    class VoxelLightingInjectCS : public GlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER_TYPE(VoxelLightingInjectCS)
        static const ShaderParametersMetadata* GetShaderParameterMetadata() { return VoxelLightingInjectParameters::GetMetaData(); }
    };

    class VoxelLightingDebugCS : public GlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER_TYPE(VoxelLightingDebugCS)
        static const ShaderParametersMetadata* GetShaderParameterMetadata() { return VoxelLightingDebugParameters::GetMetaData(); }
    };

    IMPLEMENT_GLOBAL_SHADER_TYPE(VoxelLightingInjectCS, "VoxelLightingInjectCS", "/GI/VoxelLightingInject.sf", "MainCS", ERHIShaderFrequency::Compute);
    IMPLEMENT_GLOBAL_SHADER_TYPE(VoxelLightingDebugCS, "VoxelLightingDebugCS", "/GI/VoxelLightingDebug.sf", "MainCS", ERHIShaderFrequency::Compute);

    namespace
    {
        RenderGraphTextureUAVRef MakeUAV(RenderGraphBuilder& builder, RenderGraphTextureRef texture)
        {
            RenderGraphTextureUAVDesc desc{};
            desc.Texture = texture;
            desc.Format = texture->GetDesc().Format;
            return builder.CreateTextureUAV(texture->GetName() + ".VoxelUAV", desc);
        }

        template<class Shader, class Parameters>
        void AddCompute(RenderGraphBuilder& builder, const char* name, Parameters* params, uint32_t x, uint32_t y, uint32_t z)
        {
            const auto shader = GShaderMap.GetShader<Shader>(0);
            if (!shader)
            {
                throw std::runtime_error(std::string("Voxel Lighting: missing shader for ") + name);
            }
            RHIComputePipelineStateDesc desc{};
            desc.ComputeShader = static_cast<RHIComputeShader*>(shader->GetRHIShader());
            const auto pipeline = RHIPipelineStateCache::GetOrCreateComputePipelineState(desc);
            builder.AddPass(name, Parameters::GetMetaData(), params, EPassFlag::Compute, [=](RHICommandListBase& list)
            {
                auto& cmd = static_cast<RHIComputeCommandList&>(list);
                cmd.SetComputePipelineState(pipeline);
                SetShaderParameters(cmd, shader, params);
                cmd.Dispatch(x, y, z);
            });
        }
    }

    VoxelLightingParameters VoxelLighting::Update(RenderGraphBuilder& builder, const WorldRadianceCacheParameters& source)
    {
        const uint32_t size = source.GridSize;
        if (!m_Volume)
        {
            RHITextureDesc desc{};
            desc.Width = desc.Height = size;
            desc.Depth = size * 12u;
            desc.Type = ERHITextureType::Texture3D;
            desc.Format = ERHIFormat::R32G32B32A32_Float;
            desc.Usage = ERHITextureCreateFlag::ShaderResource | ERHITextureCreateFlag::UAV;
            m_Volume = std::make_shared<RenderTexture>(desc);
            m_Volume->InitRHIResource();
        }

        VoxelLightingParameters result{};
        result.VoxelOrigin = source.LevelOrigin1;
        result.VoxelGridSize = size;
        result.VoxelSpacing = source.CellSize.Y;
        result.bVoxelRefreshedAll = m_bRefreshAll ? 1u : 0u;
        result.VoxelRadiance = builder.RegisterExternalTexture("VoxelLighting.Volume", m_Volume.get());

        auto* inject = builder.AllocateParameter<VoxelLightingInjectParameters>();
        inject->Source = source;
        inject->bRefreshAll = result.bVoxelRefreshedAll;
        inject->VoxelOutput = MakeUAV(builder, result.VoxelRadiance);
        AddCompute<VoxelLightingInjectCS>(builder, "VoxelLightingInject", inject,
            (size + 3u) / 4u, (size + 3u) / 4u, (size + 3u) / 4u);
        m_bRefreshAll = false;
        return result;
    }

    void VoxelLighting::AddDebugPass(RenderGraphBuilder& builder, const WorldRadianceCacheParameters& source,
        const VoxelLightingParameters& voxels, RenderGraphTextureRef output, uint32_t slice)
    {
        auto* params = builder.AllocateParameter<VoxelLightingDebugParameters>();
        params->Source = source;
        // The debug pass needs probe initialization and update marks, but never samples the direction atlas.
        params->Source.WorldRadianceAtlas = nullptr;
        params->Voxels = voxels;
        params->OutputSize = {output->GetDesc().Width, output->GetDesc().Height};
        params->Slice = std::min(slice, voxels.VoxelGridSize - 1u);
        params->DebugOutput = MakeUAV(builder, output);
        AddCompute<VoxelLightingDebugCS>(builder, "VoxelLightingDebug", params,
            (params->OutputSize.X + 7u) / 8u, (params->OutputSize.Y + 7u) / 8u, 1u);
    }
}
