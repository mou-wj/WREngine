#include "FrameInterpolation.h"

#include "GlobalShader.h"
#include "RenderGraphBuilder.h"
#include "RHIApi.h"
#include "RHIPipelineStateCache.h"
#include <cmath>
#include <stdexcept>

namespace Renderer
{
    using namespace RHI;
    using namespace RenderCore;

    BEGIN_SHADER_PARAMETER_STRUCT(FrameCaptureParameters)
        SHADER_PARAMETER(Core::UInt2, FrameSize)
        SHADER_PARAMETER(Core::Float2, DepthJitter)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, CapturedFrame)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, InputColor)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, InputDepth)
    END_SHADER_PARAMETER_STRUCT(FrameCaptureParameters)

    BEGIN_SHADER_PARAMETER_STRUCT(FrameLuminanceParameters)
        SHADER_PARAMETER(Core::UInt2, LumaSize)
        SHADER_PARAMETER(uint32_t, bReduction)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, LumaOutput)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PreviousFrame)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, CurrentFrame)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, LumaSource)
    END_SHADER_PARAMETER_STRUCT(FrameLuminanceParameters)

    BEGIN_SHADER_PARAMETER_STRUCT(FrameFlowParameters)
        SHADER_PARAMETER(Core::UInt2, FlowSize)
        SHADER_PARAMETER(uint32_t, bHasCoarse)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, ForwardOutput)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, BackwardOutput)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, FrameLuma)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, CoarseForward)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, CoarseBackward)
    END_SHADER_PARAMETER_STRUCT(FrameFlowParameters)

    BEGIN_SHADER_PARAMETER_STRUCT(FrameInterpolateParameters)
        SHADER_PARAMETER(Core::UInt2, FrameSize)
        SHADER_PARAMETER(Core::Float4x4, PreviousVP)
        SHADER_PARAMETER(Core::Float4x4, PreviousInverseVP)
        SHADER_PARAMETER(Core::Float4x4, CurrentVP)
        SHADER_PARAMETER(Core::Float4x4, CurrentInverseVP)
        SHADER_PARAMETER(uint32_t, DebugView)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(FRAME_DISPLAY_STORAGE, FrameOutput)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PreviousFrame)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, CurrentFrame)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, ForwardFlow)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, BackwardFlow)
        SHADER_PARAMETER_SAMPLER(FrameSampler)
    END_SHADER_PARAMETER_STRUCT(FrameInterpolateParameters)

#define DECLARE_FRAME_SHADER(Name, Parameters, File, Entry) \
    class Name : public GlobalShader \
    { \
    public: \
        DECLARE_GLOBAL_SHADER_TYPE(Name) \
        static bool ShouldCompilePermutation(const ShaderPermutationParameters&) { return true; } \
        static void ModifyShaderCompilerEnvironment(const ShaderPermutationParameters& params, ShaderCompilerEnvironment& env) \
        { \
            env.SetDefine("FRAME_GL_STORAGE", params.Platform == static_cast<uint32_t>(ERHIShaderPlatform::OpenGL)); \
        } \
        static const ShaderParametersMetadata* GetShaderParameterMetadata() { return Parameters::GetMetaData(); } \
    }; \
    IMPLEMENT_GLOBAL_SHADER_TYPE(Name, #Name, File, Entry, ERHIShaderFrequency::Compute)

    DECLARE_FRAME_SHADER(FrameCaptureCS, FrameCaptureParameters, "/tools/FrameCapture.sf", "CaptureCS");
    DECLARE_FRAME_SHADER(FrameLuminanceCS, FrameLuminanceParameters, "/tools/FrameLuminance.sf", "LuminanceCS");
    DECLARE_FRAME_SHADER(FrameFlowCS, FrameFlowParameters, "/tools/FrameFlow.sf", "FlowCS");
    DECLARE_FRAME_SHADER(FrameInterpolateCS, FrameInterpolateParameters, "/tools/FrameInterpolate.sf", "MainCS");

#undef DECLARE_FRAME_SHADER

    namespace
    {
        RenderGraphTextureUAVRef WriteImage(RenderGraphBuilder& builder, RenderGraphTextureRef image)
        {
            RenderGraphTextureUAVDesc desc;
            desc.Texture = image;
            desc.Format = image->GetDesc().Format;
            return builder.CreateTextureUAV(image->GetName() + ".UAV", desc);
        }

        RenderGraphTextureRef CreateImage(RenderGraphBuilder& builder, const std::string& name, Core::UInt2 size)
        {
            RenderGraphTextureDesc desc;
            desc.Width = size.X;
            desc.Height = size.Y;
            desc.Format = ERHIFormat::R32G32B32A32_Float;
            desc.Usage = ERHITextureCreateFlag::ShaderResource | ERHITextureCreateFlag::UAV;
            return builder.CreateTexture(name, desc);
        }

        template<class Shader, class Parameters>
        void AddCompute(RenderGraphBuilder& builder, const char* name, Parameters* params, Core::UInt2 size)
        {
            const auto* shader = GShaderMap.GetShader<Shader>(0);
            if (!shader)
            {
                throw std::runtime_error(std::string("Frame interpolation: missing ") + name + " shader");
            }
            RHIComputePipelineStateDesc desc;
            desc.ComputeShader = static_cast<RHIComputeShader*>(shader->GetRHIShader());
            auto* pipeline = RHIPipelineStateCache::GetOrCreateComputePipelineState(desc);
            builder.AddPass(name, Parameters::GetMetaData(), params, EPassFlag::Compute, [=](RHICommandListBase& list)
            {
                auto& cmd = static_cast<RHIComputeCommandList&>(list);
                cmd.SetComputePipelineState(pipeline);
                SetShaderParameters(cmd, shader, params);
                cmd.Dispatch((size.X + 7) / 8, (size.Y + 7) / 8, 1);
            });
        }
    }

    FrameInterpolation::~FrameInterpolation()
    {
        RetireFrames();
    }

    void FrameInterpolation::RetireFrames()
    {
        if (GRHIApi)
        {
            for (const auto& frame : m_Frames)
            {
                if (frame)
                {
                    const auto fence = frame->GetTracker().GetLastAccessFence();
                    if (fence.Value != 0)
                    {
                        GRHIApi->GetQueue(fence.QueueType)->WaitValue(fence.Value);
                    }
                }
            }
        }
        m_Frames = {};
    }

    bool FrameInterpolation::AddPasses(RenderGraphBuilder& builder, RenderGraphTextureRef realColor,
        RenderGraphTextureRef depth, RenderGraphTextureRef output, const Engine::SceneView& view,
        Core::Float2 depthJitter, double time, Engine::EFrameInterpolationView debugView)
    {
        if (!realColor || !depth || !output || output->GetDesc().Format != ERHIFormat::R8G8B8A8_UNorm)
        {
            throw std::invalid_argument("Frame interpolation: color, depth, and RGBA8 output are required");
        }
        const auto& desc = realColor->GetDesc();
        const Core::UInt2 size{desc.Width, desc.Height};
        if (size.X == 0 || size.Y == 0 || output->GetDesc().Width != size.X || output->GetDesc().Height != size.Y)
        {
            throw std::invalid_argument("Frame interpolation: color and output dimensions must match");
        }
        if (size != m_Size)
        {
            RetireFrames();
            RHITextureDesc frameDesc;
            frameDesc.Width = size.X;
            frameDesc.Height = size.Y;
            frameDesc.Format = ERHIFormat::R32G32B32A32_Float;
            frameDesc.Usage = ERHITextureCreateFlag::ShaderResource | ERHITextureCreateFlag::UAV;
            for (auto& frame : m_Frames)
            {
                frame = std::make_shared<RenderTexture>(frameDesc);
                frame->InitRHIResource();
            }
            Invalidate();
            m_Size = size;
        }

        const Core::Float3 viewAxis{view.ViewMatrix(2, 0), view.ViewMatrix(2, 1), view.ViewMatrix(2, 2)};
        const double interval = time - m_PreviousTime;
        const bool bInterpolate = m_bHasPrevious && interval >= 0.003 && interval <= 0.25 &&
            (view.CameraWorldPos - m_PreviousCamera).LengthSquared() <= 64.0f && Core::Dot(viewAxis, m_PreviousViewAxis) >= 0.75f;
        const uint32_t previousSlot = 1u - m_NextFrame;
        auto current = builder.RegisterExternalTexture("FrameInterpolation.Current", m_Frames[m_NextFrame].get());
        auto* capture = builder.AllocateParameter<FrameCaptureParameters>();
        capture->FrameSize = size;
        capture->DepthJitter = depthJitter;
        capture->CapturedFrame = WriteImage(builder, current);
        capture->InputColor = realColor;
        capture->InputDepth = depth;
        AddCompute<FrameCaptureCS>(builder, "FrameInterpolation.Capture", capture, size);

        if (bInterpolate)
        {
            auto previous = builder.RegisterExternalTexture("FrameInterpolation.Previous", m_Frames[previousSlot].get());
            constexpr uint32_t levelCount = 4;
            std::array<RenderGraphTextureRef, levelCount> luminance{};
            std::array<Core::UInt2, levelCount> extents{};
            for (uint32_t level = 0; level < levelCount; ++level)
            {
                const uint32_t scale = 2u << level;
                extents[level] = {(size.X + scale - 1) / scale, (size.Y + scale - 1) / scale};
                luminance[level] = CreateImage(builder, "FrameInterpolation.Luma." + std::to_string(level), extents[level]);
                auto* luma = builder.AllocateParameter<FrameLuminanceParameters>();
                luma->LumaSize = extents[level];
                luma->bReduction = level != 0;
                luma->LumaOutput = WriteImage(builder, luminance[level]);
                luma->PreviousFrame = previous;
                luma->CurrentFrame = current;
                luma->LumaSource = level == 0 ? current : luminance[level - 1];
                AddCompute<FrameLuminanceCS>(builder, "FrameInterpolation.Luminance", luma, extents[level]);
            }

            RenderGraphTextureRef forward = nullptr;
            RenderGraphTextureRef backward = nullptr;
            for (uint32_t level = levelCount; level-- > 0;)
            {
                auto* flow = builder.AllocateParameter<FrameFlowParameters>();
                flow->FlowSize = extents[level];
                flow->bHasCoarse = forward != nullptr;
                flow->FrameLuma = luminance[level];
                flow->CoarseForward = forward ? forward : luminance[level];
                flow->CoarseBackward = backward ? backward : luminance[level];
                forward = CreateImage(builder, "FrameInterpolation.Forward." + std::to_string(level), extents[level]);
                backward = CreateImage(builder, "FrameInterpolation.Backward." + std::to_string(level), extents[level]);
                flow->ForwardOutput = WriteImage(builder, forward);
                flow->BackwardOutput = WriteImage(builder, backward);
                AddCompute<FrameFlowCS>(builder, "FrameInterpolation.Flow", flow, extents[level]);
            }

            auto* interpolate = builder.AllocateParameter<FrameInterpolateParameters>();
            interpolate->FrameSize = size;
            interpolate->PreviousVP = m_PreviousVP;
            interpolate->PreviousInverseVP = m_PreviousInverseVP;
            interpolate->CurrentVP = view.ViewProjectionMatrix;
            interpolate->CurrentInverseVP = view.InvViewProjectionMatrix;
            interpolate->DebugView = static_cast<uint32_t>(debugView);
            interpolate->FrameOutput = WriteImage(builder, output);
            interpolate->PreviousFrame = previous;
            interpolate->CurrentFrame = current;
            interpolate->ForwardFlow = forward;
            interpolate->BackwardFlow = backward;
            interpolate->FrameSampler = GlobalSampler.get();
            AddCompute<FrameInterpolateCS>(builder, "FrameInterpolation.Resolve", interpolate, size);
        }

        m_PreviousVP = view.ViewProjectionMatrix;
        m_PreviousInverseVP = view.InvViewProjectionMatrix;
        m_PreviousCamera = view.CameraWorldPos;
        m_PreviousViewAxis = viewAxis;
        m_PreviousTime = time;
        m_NextFrame = previousSlot;
        m_bHasPrevious = std::isfinite(time);
        return bInterpolate;
    }
}
