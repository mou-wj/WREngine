#pragma once

#include "GlobalShader.h"
#include "ShaderParameter.h"
#include "RHICommandList.h"
#include "ShaderCore.h"
#include "RenderResource.h"
#include "RenderGraphBuilder.h"
#include "SceneInterface.h"
#include "SceneView.h"
#include <array>

namespace Renderer {

    BEGIN_SHADER_PARAMETER_STRUCT(TAAParameters)
        SHADER_PARAMETER(Core::UInt2, RenderSize)
        SHADER_PARAMETER(Core::UInt2, OutputSize)
        SHADER_PARAMETER(Core::Float2, JitterUV)
        SHADER_PARAMETER(Core::Float4x4, InverseViewProjection)
        SHADER_PARAMETER(Core::Float4x4, PreviousViewProjection)
        SHADER_PARAMETER(float, BlendFactor)
        SHADER_PARAMETER(float, Sharpening)
        SHADER_PARAMETER(uint32_t, DebugView)
        SHADER_PARAMETER(uint32_t, bHasReactiveReference)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(TAA_DISPLAY_STORAGE, OutputColor)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, NextHistory)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, CurrentColor)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, HistoryColor)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, ReactiveReference)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, CurrentDepth)
    END_SHADER_PARAMETER_STRUCT(TAAParameters)

    class RENDERER_API TemporalUpsampling
    {
    public:
        ~TemporalUpsampling();
        static Core::UInt2 GetRenderSize(Core::UInt2 outputSize, const Engine::SuperResolutionSettings& settings);
        Core::Float2 JitterView(Engine::SceneView& view, bool bTemporal) const;
        void Invalidate();
        void AddPass(RenderCore::RenderGraphBuilder& builder, const Engine::SceneView& view,
            const Engine::SceneView& unjittered, Core::Float2 jitterUV, const Engine::SuperResolutionSettings& settings,
            RenderCore::RenderGraphTextureRef color, RenderCore::RenderGraphTextureRef depth,
            RenderCore::RenderGraphTextureRef output, RenderCore::RenderGraphTextureRef reactiveReference = nullptr);

    private:
        void RetireHistory();

        std::array<RenderCore::RenderTextureSP, 2> m_History;
        Core::UInt2 m_RenderSize{};
        Core::UInt2 m_OutputSize{};
        Core::Float4x4 m_PreviousViewProjection = Core::Float4x4::Identity();
        Core::Float3 m_PreviousPosition{};
        Core::Float3 m_PreviousViewAxis{};
        uint32_t m_NextSlot = 0;
        uint32_t m_JitterPhase = 0;
        bool m_bValid = false;
    };

    class taaTAA : public RenderCore::GlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER_TYPE(taaTAA);

        static bool ShouldCompilePermutation(const RenderCore::ShaderPermutationParameters& parameters)
        {
            return true;
        }

        static void ModifyShaderCompilerEnvironment(const RenderCore::ShaderPermutationParameters& parameters, RenderCore::ShaderCompilerEnvironment& outEnvironment)
        {
            outEnvironment.SetDefine("TAA_GL_STORAGE", parameters.Platform == static_cast<uint32_t>(RHI::ERHIShaderPlatform::OpenGL));
        }

        static const RenderCore::ShaderParametersMetadata* GetShaderParameterMetadata()
        {
            return TAAParameters::GetMetaData();
        }
    };

    BEGIN_SHADER_PARAMETER_STRUCT(FxaaParameters)
        SHADER_PARAMETER(Core::Int2, Resolution)
        SHADER_PARAMETER_RHI_SRV(Texture2D<float4>, InputColor)
        SHADER_PARAMETER_RHI_UAV(RWTexture2D<float4>, OutputColor)
    END_SHADER_PARAMETER_STRUCT(FxaaParameters)

    class FxaaCS : public RenderCore::GlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER_TYPE(FxaaCS);

        static bool ShouldCompilePermutation(const RenderCore::ShaderPermutationParameters& parameters)
        {
            return true;
        }

        static void ModifyShaderCompilerEnvironment(const RenderCore::ShaderPermutationParameters& parameters, RenderCore::ShaderCompilerEnvironment& outEnvironment)
        {
        }

        static const RenderCore::ShaderParametersMetadata* GetShaderParameterMetadata()
        {
            return FxaaParameters::GetMetaData();
        }
    };

} // namespace Renderer
