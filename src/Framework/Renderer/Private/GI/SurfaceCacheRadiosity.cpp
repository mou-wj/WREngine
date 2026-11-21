#include "SurfaceCacheRadiosity.h"

#include "GlobalShader.h"
#include "RHIPipelineStateCache.h"
#include <cmath>
#include <stdexcept>

namespace Renderer
{
    using namespace RenderCore;
    using namespace RHI;

    BEGIN_SHADER_PARAMETER_STRUCT(SurfaceCacheRadiosityRaysParameters)
        SHADER_PARAMETER_STRUCT_REFERENCE(SurfaceCacheMaterialParameters, Cache)
        SHADER_PARAMETER(uint32_t, TileSize)
        SHADER_PARAMETER(uint32_t, TilesPerRow)
        SHADER_PARAMETER(uint32_t, RaysPerTexel)
        SHADER_PARAMETER(float, TraceDistance)
        SHADER_PARAMETER_RDG_RWSTRUCTURED_BUFFER(SurfaceCacheQuery, RadiosityQueries)
    END_SHADER_PARAMETER_STRUCT(SurfaceCacheRadiosityRaysParameters)

    BEGIN_SHADER_PARAMETER_STRUCT(SurfaceCacheRadiosityIntegrateParameters)
        SHADER_PARAMETER_STRUCT_REFERENCE(SurfaceCacheMaterialParameters, Cache)
        SHADER_PARAMETER_STRUCT_REFERENCE(SurfaceCacheLightingParameters, Lighting)
        SHADER_PARAMETER(uint32_t, TileSize)
        SHADER_PARAMETER(uint32_t, TilesPerRow)
        SHADER_PARAMETER(uint32_t, RaysPerTexel)
        SHADER_PARAMETER_RDG_STRUCTURED_BUFFER(SurfaceCacheQuery, RadiosityQueries)
        SHADER_PARAMETER_RDG_STRUCTURED_BUFFER(SurfaceCacheQueryResult, RadiosityHits)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, Output)
    END_SHADER_PARAMETER_STRUCT(SurfaceCacheRadiosityIntegrateParameters)

    BEGIN_SHADER_PARAMETER_STRUCT(SurfaceCacheRadiosityDebugParameters)
        SHADER_PARAMETER(Core::UInt2, OutputSize)
        SHADER_PARAMETER(uint32_t, Iteration)
        SHADER_PARAMETER(uint32_t, bShowDelta)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, Current)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, Previous)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, Output)
    END_SHADER_PARAMETER_STRUCT(SurfaceCacheRadiosityDebugParameters)

    class SurfaceCacheRadiosityRaysCS : public GlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER_TYPE(SurfaceCacheRadiosityRaysCS)
        static bool ShouldCompilePermutation(const ShaderPermutationParameters&) { return true; }
        static void ModifyShaderCompilerEnvironment(const ShaderPermutationParameters&, ShaderCompilerEnvironment&) {}
        static const ShaderParametersMetadata* GetShaderParameterMetadata() { return SurfaceCacheRadiosityRaysParameters::GetMetaData(); }
    };

    class SurfaceCacheRadiosityIntegrateCS : public GlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER_TYPE(SurfaceCacheRadiosityIntegrateCS)
        static bool ShouldCompilePermutation(const ShaderPermutationParameters&) { return true; }
        static void ModifyShaderCompilerEnvironment(const ShaderPermutationParameters&, ShaderCompilerEnvironment&) {}
        static const ShaderParametersMetadata* GetShaderParameterMetadata() { return SurfaceCacheRadiosityIntegrateParameters::GetMetaData(); }
    };

    class SurfaceCacheRadiosityDebugCS : public GlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER_TYPE(SurfaceCacheRadiosityDebugCS)
        static bool ShouldCompilePermutation(const ShaderPermutationParameters&) { return true; }
        static void ModifyShaderCompilerEnvironment(const ShaderPermutationParameters&, ShaderCompilerEnvironment&) {}
        static const ShaderParametersMetadata* GetShaderParameterMetadata() { return SurfaceCacheRadiosityDebugParameters::GetMetaData(); }
    };

    IMPLEMENT_GLOBAL_SHADER_TYPE(SurfaceCacheRadiosityRaysCS, "SurfaceCacheRadiosityRaysCS", "/GI/SurfaceCacheRadiosityRays.sf", "MainCS", ERHIShaderFrequency::Compute);
    IMPLEMENT_GLOBAL_SHADER_TYPE(SurfaceCacheRadiosityIntegrateCS, "SurfaceCacheRadiosityIntegrateCS", "/GI/SurfaceCacheRadiosityIntegrate.sf", "MainCS", ERHIShaderFrequency::Compute);
    IMPLEMENT_GLOBAL_SHADER_TYPE(SurfaceCacheRadiosityDebugCS, "SurfaceCacheRadiosityDebugCS", "/GI/SurfaceCacheRadiosityDebug.sf", "MainCS", ERHIShaderFrequency::Compute);

    namespace
    {
        constexpr uint32_t RadiosityTileResolution = 8;
        constexpr uint32_t RadiosityRayCount = 8;
        constexpr float RadiosityTraceDistance = 100.0f;

        RenderGraphTextureUAVRef WriteAtlas(RenderGraphBuilder& builder, RenderGraphTextureRef texture)
        {
            RenderGraphTextureUAVDesc desc;
            desc.Texture = texture;
            desc.Format = texture->GetDesc().Format;
            return builder.CreateTextureUAV(texture->GetName() + ".RadiosityUAV", desc);
        }

        RenderGraphBufferSRVRef ReadBuffer(RenderGraphBuilder& builder, RenderGraphBufferRef buffer, uint32_t count)
        {
            RenderGraphBufferSRVDesc desc;
            desc.Buffer = buffer;
            desc.Stride = buffer->GetDesc().Stride;
            desc.NumElements = count;
            return builder.CreateBufferSRV(buffer->GetName() + ".RadiositySRV", desc);
        }

        template<class ShaderType, class Parameters>
        void AddCompute(RenderGraphBuilder& builder, const char* name, Parameters* parameters, uint32_t x, uint32_t y)
        {
            auto shader = GShaderMap.GetShader<ShaderType>(0);
            if (!shader)
            {
                throw std::runtime_error(std::string("Surface Cache Radiosity: missing shader for ") + name);
            }
            RHIComputePipelineStateDesc desc;
            desc.ComputeShader = static_cast<RHIComputeShader*>(shader->GetRHIShader());
            const auto pipeline = RHIPipelineStateCache::GetOrCreateComputePipelineState(desc);
            builder.AddPass(name, Parameters::GetMetaData(), parameters, EPassFlag::Compute, [=](RHICommandListBase& list)
            {
                auto& cmd = static_cast<RHIComputeCommandList&>(list);
                cmd.SetComputePipelineState(pipeline);
                SetShaderParameters(cmd, shader, parameters);
                cmd.Dispatch(x, y, 1);
            });
        }
    }

    RenderTexture* SurfaceCacheRadiosity::GetAtlas() const
    {
        return m_Iteration != 0 ? m_Atlases[(m_Iteration - 1) % 2].get() : nullptr;
    }

    RenderGraphTextureRef SurfaceCacheRadiosity::RegisterAtlas(RenderGraphBuilder& builder, uint32_t index) const
    {
        return builder.RegisterExternalTexture("SurfaceCache.Radiosity." + std::to_string(reinterpret_cast<uintptr_t>(this)) +
            "." + std::to_string(index), m_Atlases[index].get());
    }

    SurfaceCacheLightingParameters SurfaceCacheRadiosity::Register(RenderGraphBuilder& builder,
        const SurfaceCacheLightingParameters& direct) const
    {
        auto lighting = direct;
        lighting.RadiosityTileSize = m_Iteration != 0 ? RadiosityTileResolution : 0;
        lighting.IndirectRadiance = m_Iteration != 0 ? RegisterAtlas(builder, (m_Iteration - 1) % 2) : direct.DirectRadiance;
        return lighting;
    }

    void SurfaceCacheRadiosity::AddIteration(RenderGraphBuilder& builder, const SurfaceCacheMaterialParameters& material,
        const SurfaceCacheLightingParameters& direct)
    {
        if (material.CardCount == 0 || !direct.DirectRadiance)
        {
            throw std::invalid_argument("Surface Cache Radiosity requires captured Cards and direct lighting");
        }
        const uint32_t tilesPerRow = uint32_t(std::ceil(std::sqrt(double(material.CardCount))));
        const uint32_t atlasSize = tilesPerRow * RadiosityTileResolution;
        const uint32_t queryCount = material.CardCount * RadiosityTileResolution * RadiosityTileResolution * RadiosityRayCount;
        if (!m_Atlases[0])
        {
            RHITextureDesc desc;
            desc.Width = desc.Height = atlasSize;
            desc.Format = ERHIFormat::R32G32B32A32_Float;
            desc.Usage = ERHITextureCreateFlag::UAV | ERHITextureCreateFlag::ShaderResource | ERHITextureCreateFlag::TransferSrc;
            decltype(m_Atlases) atlases;
            for (auto& atlas : atlases)
            {
                atlas = std::make_shared<RenderTexture>(desc);
                atlas->InitRHIResource();
            }
            m_Atlases = std::move(atlases);
        }
        if (m_Atlases[0]->GetRHI()->GetDesc().Width != atlasSize)
        {
            throw std::invalid_argument("Surface Cache Radiosity Card layout changed; retire and recreate the cache");
        }

        RenderGraphBufferDesc queryDesc;
        queryDesc.Stride = sizeof(SurfaceCacheQuery);
        queryDesc.Size = uint64_t(queryDesc.Stride) * queryCount;
        queryDesc.Usage = ERHIBufferUsageFlag::Structured | ERHIBufferUsageFlag::ShaderResource |
            ERHIBufferUsageFlag::UnorderedAccess;
        auto queries = builder.CreateBuffer("SurfaceCache.RadiosityQueries", queryDesc);
        RenderGraphBufferUAVDesc queryUAV;
        queryUAV.Buffer = queries;
        queryUAV.NumElements = queryCount;
        queryUAV.Stride = queryDesc.Stride;
        auto* rays = builder.AllocateParameter<SurfaceCacheRadiosityRaysParameters>();
        rays->Cache = material;
        rays->TileSize = RadiosityTileResolution;
        rays->TilesPerRow = tilesPerRow;
        rays->RaysPerTexel = RadiosityRayCount;
        rays->TraceDistance = RadiosityTraceDistance;
        rays->RadiosityQueries = builder.CreateBufferUAV("SurfaceCache.RadiosityQueries.UAV", queryUAV);
        AddCompute<SurfaceCacheRadiosityRaysCS>(builder, "SurfaceCacheRadiosityRays", rays, tilesPerRow, tilesPerRow);

        auto hits = AddSurfaceCacheQueryPass(builder, material, queries, queryCount);
        auto* integrate = builder.AllocateParameter<SurfaceCacheRadiosityIntegrateParameters>();
        integrate->Cache = material;
        integrate->Lighting = Register(builder, direct);
        integrate->TileSize = rays->TileSize;
        integrate->TilesPerRow = rays->TilesPerRow;
        integrate->RaysPerTexel = rays->RaysPerTexel;
        integrate->RadiosityQueries = ReadBuffer(builder, queries, queryCount);
        integrate->RadiosityHits = ReadBuffer(builder, hits, queryCount);
        integrate->Output = WriteAtlas(builder, RegisterAtlas(builder, m_Iteration % 2));
        AddCompute<SurfaceCacheRadiosityIntegrateCS>(builder, "SurfaceCacheRadiosityIntegrate", integrate, tilesPerRow, tilesPerRow);
        ++m_Iteration;
    }

    void SurfaceCacheRadiosity::AddDebugPass(RenderGraphBuilder& builder, const SurfaceCacheLightingParameters& direct,
        RenderGraphTextureRef output, bool bShowDelta) const
    {
        const auto current = Register(builder, direct).IndirectRadiance;
        auto* params = builder.AllocateParameter<SurfaceCacheRadiosityDebugParameters>();
        params->Current = current;
        params->Previous = m_Iteration > 1 && bShowDelta ? RegisterAtlas(builder, m_Iteration % 2) : current;
        params->Iteration = m_Iteration;
        params->bShowDelta = bShowDelta ? 1u : 0u;
        params->OutputSize = {output->GetDesc().Width, output->GetDesc().Height};
        params->Output = WriteAtlas(builder, output);
        AddCompute<SurfaceCacheRadiosityDebugCS>(builder, "SurfaceCacheRadiosityDebug", params,
            (params->OutputSize.X + 7) / 8, (params->OutputSize.Y + 7) / 8);
    }
}
