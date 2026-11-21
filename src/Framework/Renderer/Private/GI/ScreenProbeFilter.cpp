#include "ScreenProbeFilter.h"

#include "GlobalShader.h"
#include "RHIPipelineStateCache.h"
#include <cmath>
#include <stdexcept>

namespace Renderer
{
    using namespace RenderCore;
    using namespace RHI;

    BEGIN_SHADER_PARAMETER_STRUCT(ScreenProbeTemporalParameters)
        SHADER_PARAMETER_STRUCT_REFERENCE(GBufferInputParameters, GBuffer)
        SHADER_PARAMETER(Core::Float4x4, CurrentInverseViewProjection)
        SHADER_PARAMETER(Core::Float4x4, PreviousViewProjection)
        SHADER_PARAMETER(Core::Float4x4, PreviousInverseViewProjection)
        SHADER_PARAMETER(Core::UInt2, FilterSize)
        SHADER_PARAMETER(uint32_t, bHistoryValid)
        SHADER_PARAMETER(uint32_t, FilterView)
        SHADER_PARAMETER(float, HistoryWeightLimit)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, RawGI)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, PreviousLighting)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, PreviousGeometry)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, CurrentLighting)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, CurrentGeometry)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, FilterOutput)
    END_SHADER_PARAMETER_STRUCT(ScreenProbeTemporalParameters)

    BEGIN_SHADER_PARAMETER_STRUCT(ScreenProbeSpatialParameters)
        SHADER_PARAMETER(Core::Float4x4, CurrentInverseViewProjection)
        SHADER_PARAMETER(Core::UInt2, FilterSize)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, TemporalLighting)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, TemporalGeometry)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, MaterialColor)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, FilterOutput)
    END_SHADER_PARAMETER_STRUCT(ScreenProbeSpatialParameters)

    class ScreenProbeTemporalCS : public GlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER_TYPE(ScreenProbeTemporalCS)
        static bool ShouldCompilePermutation(const ShaderPermutationParameters&) { return true; }
        static void ModifyShaderCompilerEnvironment(const ShaderPermutationParameters&, ShaderCompilerEnvironment&) {}
        static const ShaderParametersMetadata* GetShaderParameterMetadata() { return ScreenProbeTemporalParameters::GetMetaData(); }
    };

    class ScreenProbeSpatialCS : public GlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER_TYPE(ScreenProbeSpatialCS)
        static bool ShouldCompilePermutation(const ShaderPermutationParameters&) { return true; }
        static void ModifyShaderCompilerEnvironment(const ShaderPermutationParameters&, ShaderCompilerEnvironment&) {}
        static const ShaderParametersMetadata* GetShaderParameterMetadata() { return ScreenProbeSpatialParameters::GetMetaData(); }
    };

    IMPLEMENT_GLOBAL_SHADER_TYPE(ScreenProbeTemporalCS, "ScreenProbeTemporalCS", "/GI/ScreenProbeTemporal.sf",
        "MainCS", ERHIShaderFrequency::Compute);
    IMPLEMENT_GLOBAL_SHADER_TYPE(ScreenProbeSpatialCS, "ScreenProbeSpatialCS", "/GI/ScreenProbeSpatial.sf",
        "MainCS", ERHIShaderFrequency::Compute);

    namespace
    {
        RenderGraphTextureUAVRef WriteTexture(RenderGraphBuilder& builder, RenderGraphTextureRef texture)
        {
            RenderGraphTextureUAVDesc desc;
            desc.Texture = texture;
            desc.Format = texture->GetDesc().Format;
            return builder.CreateTextureUAV(texture->GetName() + ".ScreenProbeFilterUAV", desc);
        }

        template<class Shader, class Parameters>
        void AddCompute(RenderGraphBuilder& builder, const char* name, Parameters* params, const Core::UInt2& size)
        {
            const auto shader = GShaderMap.GetShader<Shader>(0);
            if (!shader)
            {
                throw std::runtime_error(std::string("Screen Probe Filter: missing shader for ") + name);
            }
            RHIComputePipelineStateDesc desc;
            desc.ComputeShader = static_cast<RHIComputeShader*>(shader->GetRHIShader());
            const auto pipeline = RHIPipelineStateCache::GetOrCreateComputePipelineState(desc);
            builder.AddPass(name, Parameters::GetMetaData(), params, EPassFlag::Compute, [=](RHICommandListBase& list)
            {
                auto& cmd = static_cast<RHIComputeCommandList&>(list);
                cmd.SetComputePipelineState(pipeline);
                SetShaderParameters(cmd, shader, params);
                cmd.Dispatch((size.X + 7) / 8, (size.Y + 7) / 8, 1);
            });
        }
    }

    void ScreenProbeFilter::Invalidate()
    {
        m_bHistoryValid = false;
        m_SamplePhase = 1;
    }

    void ScreenProbeFilter::AddPasses(RenderGraphBuilder& builder, const ScreenProbeFilterInput& input)
    {
        if (!input.RawGI || !input.Output || input.RawGI == input.Output || !input.GBuffer.Depth ||
            !input.GBuffer.GBufferA || !input.GBuffer.GBufferB || !input.GBuffer.GBufferC)
        {
            throw std::invalid_argument("Screen Probe Filter: separate input and output and a complete GBuffer are required");
        }
        const auto& output = input.Output->GetDesc();
        const Core::UInt2 size{output.Width, output.Height};
        if (size.X == 0 || size.Y == 0 || output.Format != ERHIFormat::R32G32B32A32_Float ||
            !std::isfinite(input.HistoryWeightLimit) || input.HistoryWeightLimit < 0.0f || input.HistoryWeightLimit >= 1.0f)
        {
            throw std::invalid_argument("Screen Probe Filter: invalid output or history weight");
        }
        for (const auto* texture : {input.RawGI, input.GBuffer.Depth, input.GBuffer.GBufferA,
            input.GBuffer.GBufferB, input.GBuffer.GBufferC})
        {
            if (texture->GetDesc().Width != size.X || texture->GetDesc().Height != size.Y)
            {
                throw std::invalid_argument("Screen Probe Filter: input dimensions must match the output");
            }
        }

        if (m_Size != size)
        {
            RHITextureDesc desc;
            desc.Width = size.X;
            desc.Height = size.Y;
            desc.Format = ERHIFormat::R32G32B32A32_Float;
            desc.Usage = ERHITextureCreateFlag::ShaderResource | ERHITextureCreateFlag::UAV;
            decltype(m_History) history;
            for (auto& slot : history)
            {
                slot.Lighting = std::make_shared<RenderTexture>(desc);
                slot.Lighting->InitRHIResource();
                slot.Geometry = std::make_shared<RenderTexture>(desc);
                slot.Geometry->InitRHIResource();
            }
            m_History = std::move(history);
            m_Size = size;
            m_NextHistorySlot = 0;
            m_bHistoryValid = false;
        }

        const auto current = m_NextHistorySlot;
        const auto previous = 1u - current;
        auto currentLighting = builder.RegisterExternalTexture("ScreenProbe.CurrentLighting", m_History[current].Lighting.get());
        auto currentGeometry = builder.RegisterExternalTexture("ScreenProbe.CurrentGeometry", m_History[current].Geometry.get());
        const bool bHistoryValid = m_bHistoryValid && (input.CameraPosition - m_PreviousCamera).LengthSquared() <= 64.0f;
        auto* temporal = builder.AllocateParameter<ScreenProbeTemporalParameters>();
        temporal->GBuffer = input.GBuffer;
        temporal->CurrentInverseViewProjection = input.InverseViewProjection;
        temporal->PreviousViewProjection = m_PreviousViewProjection;
        temporal->PreviousInverseViewProjection = m_PreviousInverseViewProjection;
        temporal->FilterSize = size;
        temporal->bHistoryValid = bHistoryValid ? 1u : 0u;
        temporal->FilterView = static_cast<uint32_t>(input.View);
        temporal->HistoryWeightLimit = input.HistoryWeightLimit;
        temporal->RawGI = input.RawGI;
        if (bHistoryValid)
        {
            temporal->PreviousLighting = builder.RegisterExternalTexture("ScreenProbe.PreviousLighting", m_History[previous].Lighting.get());
            temporal->PreviousGeometry = builder.RegisterExternalTexture("ScreenProbe.PreviousGeometry", m_History[previous].Geometry.get());
        }
        else
        {
            auto dummy = builder.RegisterExternalTexture("ScreenProbe.EmptyHistory", GlobalWhiteTexture.get());
            temporal->PreviousLighting = dummy;
            temporal->PreviousGeometry = dummy;
        }
        temporal->CurrentLighting = WriteTexture(builder, currentLighting);
        temporal->CurrentGeometry = WriteTexture(builder, currentGeometry);
        temporal->FilterOutput = WriteTexture(builder, input.Output);
        AddCompute<ScreenProbeTemporalCS>(builder, "ScreenProbeTemporal", temporal, size);

        if (input.View == EScreenProbeFilterView::Spatial)
        {
            auto* spatial = builder.AllocateParameter<ScreenProbeSpatialParameters>();
            spatial->CurrentInverseViewProjection = input.InverseViewProjection;
            spatial->FilterSize = size;
            spatial->TemporalLighting = currentLighting;
            spatial->TemporalGeometry = currentGeometry;
            spatial->MaterialColor = input.GBuffer.GBufferA;
            spatial->FilterOutput = temporal->FilterOutput;
            AddCompute<ScreenProbeSpatialCS>(builder, "ScreenProbeSpatial", spatial, size);
        }

        m_PreviousViewProjection = input.ViewProjection;
        m_PreviousInverseViewProjection = input.InverseViewProjection;
        m_PreviousCamera = input.CameraPosition;
        m_NextHistorySlot = previous;
        m_bHistoryValid = true;
        m_SamplePhase = m_SamplePhase % 65535 + 1;
    }
}
