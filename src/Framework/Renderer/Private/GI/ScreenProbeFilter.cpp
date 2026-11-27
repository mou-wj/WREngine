#include "ScreenProbeFilter.h"

#include "GlobalShader.h"
#include "RHIPipelineStateCache.h"
#include <algorithm>
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
        SHADER_PARAMETER(Core::UInt2, GeometrySize)
        SHADER_PARAMETER(uint32_t, bHistoryValid)
        SHADER_PARAMETER(uint32_t, FilterView)
        SHADER_PARAMETER(float, HistoryWeightLimit)
        SHADER_PARAMETER(uint32_t, bUseVariance)
        // Keep storage images in low automatic binding slots on GL 4.3.
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, CurrentLighting)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, CurrentGeometry)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, CurrentMoments)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, FilterOutput)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, RawGI)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, PreviousLighting)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, PreviousGeometry)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, PreviousMoments)
    END_SHADER_PARAMETER_STRUCT(ScreenProbeTemporalParameters)

    BEGIN_SHADER_PARAMETER_STRUCT(ScreenProbeSpatialParameters)
        SHADER_PARAMETER(Core::Float4x4, CurrentInverseViewProjection)
        SHADER_PARAMETER(Core::UInt2, FilterSize)
        SHADER_PARAMETER(Core::UInt2, GeometrySize)
        SHADER_PARAMETER(uint32_t, StepSize)
        SHADER_PARAMETER(uint32_t, bComposite)
        SHADER_PARAMETER(uint32_t, bUseVariance)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, FilterOutput)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SpatialLighting)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, TemporalLighting)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, TemporalGeometry)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, TemporalMoments)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, MaterialColor)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneColor)
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
        const auto& raw = input.RawGI->GetDesc();
        const auto& depth = input.GBuffer.Depth->GetDesc();
        const Core::UInt2 geometrySize{depth.Width, depth.Height};
        if (input.SceneColor && (input.SceneColor == input.Output || input.SceneColor->GetDesc().Width != size.X ||
            input.SceneColor->GetDesc().Height != size.Y))
        {
            throw std::invalid_argument("Screen Probe Filter: composition requires a separate scene color at signal resolution");
        }
        if (raw.Width != size.X || raw.Height != size.Y || depth.Width < size.X || depth.Height < size.Y)
        {
            throw std::invalid_argument("Screen Probe Filter: signal and output sizes must match and fit the GBuffer");
        }
        for (const auto* texture : {input.GBuffer.GBufferA, input.GBuffer.GBufferB, input.GBuffer.GBufferC})
        {
            if (texture->GetDesc().Width != depth.Width || texture->GetDesc().Height != depth.Height)
            {
                throw std::invalid_argument("Screen Probe Filter: GBuffer dimensions must match depth");
            }
        }

        if (m_GeometrySize != geometrySize || m_bUseVariance != input.bUseVariance)
        {
            Invalidate();
        }
        m_GeometrySize = geometrySize;
        m_bUseVariance = input.bUseVariance;
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
                slot.Moments = std::make_shared<RenderTexture>(desc);
                slot.Moments->InitRHIResource();
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
        auto currentMoments = builder.RegisterExternalTexture("ScreenProbe.CurrentMoments", m_History[current].Moments.get());
        const bool bHistoryValid = m_bHistoryValid && (input.CameraPosition - m_PreviousCamera).LengthSquared() <= 64.0f;
        auto* temporal = builder.AllocateParameter<ScreenProbeTemporalParameters>();
        temporal->GBuffer = input.GBuffer;
        temporal->CurrentInverseViewProjection = input.InverseViewProjection;
        temporal->PreviousViewProjection = m_PreviousViewProjection;
        temporal->PreviousInverseViewProjection = m_PreviousInverseViewProjection;
        temporal->FilterSize = size;
        temporal->GeometrySize = geometrySize;
        temporal->bHistoryValid = bHistoryValid ? 1u : 0u;
        temporal->FilterView = static_cast<uint32_t>(input.View);
        temporal->HistoryWeightLimit = input.HistoryWeightLimit;
        temporal->bUseVariance = input.bUseVariance ? 1u : 0u;
        temporal->RawGI = input.RawGI;
        if (bHistoryValid)
        {
            temporal->PreviousLighting = builder.RegisterExternalTexture("ScreenProbe.PreviousLighting", m_History[previous].Lighting.get());
            temporal->PreviousGeometry = builder.RegisterExternalTexture("ScreenProbe.PreviousGeometry", m_History[previous].Geometry.get());
            temporal->PreviousMoments = builder.RegisterExternalTexture("ScreenProbe.PreviousMoments", m_History[previous].Moments.get());
        }
        else
        {
            auto dummy = builder.RegisterExternalTexture("ScreenProbe.EmptyHistory", GlobalWhiteTexture.get());
            temporal->PreviousLighting = dummy;
            temporal->PreviousGeometry = dummy;
            temporal->PreviousMoments = dummy;
        }
        temporal->CurrentLighting = WriteTexture(builder, currentLighting);
        temporal->CurrentGeometry = WriteTexture(builder, currentGeometry);
        temporal->CurrentMoments = WriteTexture(builder, currentMoments);
        temporal->FilterOutput = WriteTexture(builder, input.Output);
        AddCompute<ScreenProbeTemporalCS>(builder, "ScreenProbeTemporal", temporal, size);

        if (input.View == EScreenProbeFilterView::Spatial)
        {
            auto source = currentLighting;
            const uint32_t iterations = input.bUseVariance ? std::clamp(input.SpatialIterations, 1u, 4u) : 1u;
            for (uint32_t iteration = 0; iteration < iterations; ++iteration)
            {
                auto target = input.Output;
                if (iteration + 1 < iterations)
                {
                    auto desc = output;
                    desc.Usage = ERHITextureCreateFlag::ShaderResource | ERHITextureCreateFlag::UAV;
                    target = builder.CreateTexture("ScreenProbe.Spatial." + std::to_string(iteration), desc);
                }
                auto* spatial = builder.AllocateParameter<ScreenProbeSpatialParameters>();
                spatial->CurrentInverseViewProjection = input.InverseViewProjection;
                spatial->FilterSize = size;
                spatial->GeometrySize = geometrySize;
                spatial->StepSize = 1u << iteration;
                const bool bComposite = target == input.Output && input.SceneColor;
                spatial->bComposite = bComposite ? 1u : 0u;
                spatial->bUseVariance = temporal->bUseVariance;
                spatial->SpatialLighting = source;
                spatial->TemporalLighting = currentLighting;
                spatial->TemporalGeometry = currentGeometry;
                spatial->TemporalMoments = currentMoments;
                spatial->MaterialColor = input.GBuffer.GBufferA;
                spatial->SceneColor = bComposite ? input.SceneColor : currentLighting;
                spatial->FilterOutput = target == input.Output ? temporal->FilterOutput : WriteTexture(builder, target);
                AddCompute<ScreenProbeSpatialCS>(builder, "ScreenProbeSpatial", spatial, size);
                source = target;
            }
        }

        m_PreviousViewProjection = input.ViewProjection;
        m_PreviousInverseViewProjection = input.InverseViewProjection;
        m_PreviousCamera = input.CameraPosition;
        m_NextHistorySlot = previous;
        m_bHistoryValid = true;
        m_SamplePhase = m_SamplePhase % 65535 + 1;
    }
}
