#include "AntiAliasingPostProcess.h"
#include "RHIApi.h"
#include "RHIPipelineStateCache.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Renderer {
    using namespace RenderCore;
    using namespace RHI;

    IMPLEMENT_GLOBAL_SHADER_TYPE(
        taaTAA,
        "taaTAA",
        "/tools/taaTAA.sf",
        "MainCS",
        RHI::ERHIShaderFrequency::Compute
    );

    namespace
    {
        float Halton(uint32_t index, uint32_t base)
        {
            float value = 0.0f;
            float fraction = 1.0f;
            for (; index > 0; index /= base)
            {
                fraction /= static_cast<float>(base);
                value += fraction * static_cast<float>(index % base);
            }
            return value;
        }

        RenderGraphTextureUAVRef WriteTAA(RenderGraphBuilder& builder, RenderGraphTextureRef texture)
        {
            RenderGraphTextureUAVDesc desc;
            desc.Texture = texture;
            desc.Format = texture->GetDesc().Format;
            return builder.CreateTextureUAV(texture->GetName() + ".UpsampleUAV", desc);
        }
    }

    TemporalUpsampling::~TemporalUpsampling()
    {
        RetireHistory();
    }

    void TemporalUpsampling::RetireHistory()
    {
        if (GRHIApi)
        {
            for (const auto& texture : m_History)
            {
                if (texture)
                {
                    const auto fence = texture->GetTracker().GetLastAccessFence();
                    if (fence.Value != 0)
                    {
                        GRHIApi->GetQueue(fence.QueueType)->WaitValue(fence.Value);
                    }
                }
            }
        }
        m_History = {};
    }

    void TemporalUpsampling::Invalidate()
    {
        m_bValid = false;
        m_JitterPhase = 0;
    }

    Core::UInt2 TemporalUpsampling::GetRenderSize(Core::UInt2 outputSize, const Engine::SuperResolutionSettings& settings)
    {
        auto scale = 1.0f;
        switch (settings.Preset)
        {
        case Engine::ESuperResolutionPreset::Quality: scale = 0.67f; break;
        case Engine::ESuperResolutionPreset::Balanced: scale = 0.59f; break;
        case Engine::ESuperResolutionPreset::Performance: scale = 0.5f; break;
        case Engine::ESuperResolutionPreset::Custom:
            scale = std::isfinite(settings.RenderScale) ? std::clamp(settings.RenderScale, 0.25f, 1.0f) : 1.0f;
            break;
        default: break;
        }
        return {std::max(1u, static_cast<uint32_t>(std::round(outputSize.X * scale))),
            std::max(1u, static_cast<uint32_t>(std::round(outputSize.Y * scale)))};
    }

    Core::Float2 TemporalUpsampling::JitterView(Engine::SceneView& view, bool bTemporal) const
    {
        if (!bTemporal)
        {
            return {0, 0};
        }
        const uint32_t sample = m_JitterPhase + 1;
        const Core::Float2 jitter{(Halton(sample, 2) - 0.5f) / view.Viewport.Width,
            (Halton(sample, 3) - 0.5f) / view.Viewport.Height};
        for (uint32_t i = 0; i < 4; ++i)
        {
            view.ProjectionMatrix(0, i) += 2.0f * jitter.X * view.ProjectionMatrix(3, i);
            view.ProjectionMatrix(1, i) -= 2.0f * jitter.Y * view.ProjectionMatrix(3, i);
        }
        view.RebuildDerivedMatrices();
        view.InvViewProjectionMatrix = Core::Inverse(view.ViewProjectionMatrix);
        return jitter;
    }

    void TemporalUpsampling::AddPass(RenderGraphBuilder& builder, const Engine::SceneView& view,
        const Engine::SceneView& unjittered, Core::Float2 jitterUV, const Engine::SuperResolutionSettings& settings,
        RenderGraphTextureRef color, RenderGraphTextureRef depth, RenderGraphTextureRef output,
        RenderGraphTextureRef reactiveReference)
    {
        const auto& sourceDesc = color->GetDesc();
        const auto& outputDesc = output->GetDesc();
        const Core::UInt2 renderSize{sourceDesc.Width, sourceDesc.Height};
        const Core::UInt2 outputSize{outputDesc.Width, outputDesc.Height};
        if (m_OutputSize != outputSize)
        {
            RetireHistory();
            RHITextureDesc desc;
            desc.Width = outputSize.X;
            desc.Height = outputSize.Y;
            desc.Format = ERHIFormat::R32G32B32A32_Float;
            desc.Usage = ERHITextureCreateFlag::ShaderResource | ERHITextureCreateFlag::UAV;
            for (auto& history : m_History)
            {
                history = std::make_shared<RenderTexture>(desc);
                history->InitRHIResource();
            }
            m_OutputSize = outputSize;
            Invalidate();
        }

        const auto shader = GShaderMap.GetShader<taaTAA>(0);
        if (!shader)
        {
            throw std::runtime_error("Temporal Upsampling: missing compute shader");
        }
        RHIComputePipelineStateDesc pipelineDesc;
        pipelineDesc.ComputeShader = static_cast<RHIComputeShader*>(shader->GetRHIShader());
        auto* pipeline = RHIPipelineStateCache::GetOrCreateComputePipelineState(pipelineDesc);
        const Core::Float3 viewAxis{unjittered.ViewMatrix(2, 0), unjittered.ViewMatrix(2, 1), unjittered.ViewMatrix(2, 2)};
        const bool bUseHistory = settings.bTemporal && m_bValid && m_RenderSize == renderSize &&
            (view.CameraWorldPos - m_PreviousPosition).LengthSquared() <= 64.0f &&
            Core::Dot(viewAxis, m_PreviousViewAxis) >= 0.75f;

        const uint32_t previousSlot = 1u - m_NextSlot;
        auto next = builder.RegisterExternalTexture("TAAU.NextHistory", m_History[m_NextSlot].get());
        auto* parameters = builder.AllocateParameter<TAAParameters>();
        parameters->RenderSize = renderSize;
        parameters->OutputSize = outputSize;
        parameters->JitterUV = jitterUV;
        parameters->InverseViewProjection = view.InvViewProjectionMatrix;
        parameters->PreviousViewProjection = m_PreviousViewProjection;
        parameters->BlendFactor = bUseHistory && std::isfinite(settings.HistoryWeight) ?
            std::clamp(settings.HistoryWeight, 0.0f, 0.98f) : 0.0f;
        parameters->Sharpening = std::isfinite(settings.Sharpening) ? std::clamp(settings.Sharpening, 0.0f, 1.0f) : 0.0f;
        parameters->DebugView = static_cast<uint32_t>(settings.View);
        parameters->bHasReactiveReference = reactiveReference ? 1u : 0u;
        parameters->OutputColor = WriteTAA(builder, output);
        parameters->NextHistory = WriteTAA(builder, next);
        parameters->CurrentColor = color;
        parameters->CurrentDepth = depth;
        parameters->ReactiveReference = reactiveReference ? reactiveReference : color;
        parameters->HistoryColor = bUseHistory ?
            builder.RegisterExternalTexture("TAAU.PreviousHistory", m_History[previousSlot].get()) : color;
        builder.AddPass("TemporalUpsampling", TAAParameters::GetMetaData(), parameters, EPassFlag::Compute,
            [=](RHICommandListBase& list)
            {
                auto& cmd = static_cast<RHIComputeCommandList&>(list);
                cmd.SetComputePipelineState(pipeline);
                SetShaderParameters(cmd, shader, parameters);
                cmd.Dispatch((outputSize.X + 7) / 8, (outputSize.Y + 7) / 8, 1);
            });

        m_RenderSize = renderSize;
        m_PreviousViewProjection = unjittered.ViewProjectionMatrix;
        m_PreviousPosition = view.CameraWorldPos;
        m_PreviousViewAxis = viewAxis;
        m_NextSlot = previousSlot;
        m_JitterPhase = (m_JitterPhase + 1) % 8;
        m_bValid = settings.bTemporal;
    }

    IMPLEMENT_GLOBAL_SHADER_TYPE(
        FxaaCS,
        "FxaaCS",
        "/tools/FxaaCS.sf",
        "MainCS",
        RHI::ERHIShaderFrequency::Compute
    );

} // namespace Renderer
