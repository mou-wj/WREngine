#pragma once

#include "GlobalShader.h"
#include "ShaderParameter.h"
#include "RHICommandList.h"
#include "ShaderCore.h"
#include "RenderResource.h"
#include "RenderGraphBuilder.h"
#include "DepthPyramidGenerate.h"
#include "GBufferInfo.h"

namespace Renderer {

    BEGIN_SHADER_PARAMETER_STRUCT(ScreenSpaceGITextureParameters)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, SceneColorTexture)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputGIColor)
        SHADER_PARAMETER_STRUCT_REFERENCE(GBufferInputParameters,GBufferInput)
    END_SHADER_PARAMETER_STRUCT(ScreenSpaceGITextureParameters)

    BEGIN_SHADER_PARAMETER_STRUCT(ScreenSpaceGIParameters)
        SHADER_PARAMETER(Core::Float4x4, ViewProj)
        SHADER_PARAMETER(Core::Float4x4, InvViewProj)
        SHADER_PARAMETER(Core::Float3, CameraPos)
        SHADER_PARAMETER(Core::Float2, ScreenSize)
        SHADER_PARAMETER(uint32_t, MaxTraceSteps)
        SHADER_PARAMETER(float, MaxTraceDistance)
        SHADER_PARAMETER(float, TraceThickness)
        SHADER_PARAMETER_STRUCT_INCLUDE(ScreenSpaceGITextureParameters, Textures)
    END_SHADER_PARAMETER_STRUCT(ScreenSpaceGIParameters)

    class ScreenSpaceGICS : public RenderCore::GlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER_TYPE(ScreenSpaceGICS);

        static bool ShouldCompilePermutation(const RenderCore::ShaderPermutationParameters& parameters)
        {
            return true;
        }

        static void ModifyShaderCompilerEnvironment(const RenderCore::ShaderPermutationParameters& parameters, RenderCore::ShaderCompilerEnvironment& outEnvironment)
        {
        }

        static const RenderCore::ShaderParametersMetadata* GetShaderParameterMetadata()
        {
            return ScreenSpaceGIParameters::GetMetaData();
        }
    };

    struct ScreenSpaceGIPassInput
    {
        RenderCore::RenderGraphTextureRef GBufferA = nullptr;
        RenderCore::RenderGraphTextureRef GBufferB = nullptr;
        RenderCore::RenderGraphTextureRef GBufferC = nullptr;
        RenderCore::RenderGraphTextureRef SceneColor = nullptr;
        RenderCore::RenderGraphTextureRef DepthPyramid = nullptr;
        RenderCore::RenderGraphTextureRef OutputGI = nullptr;

        Core::Float4x4 ViewProj = Core::Float4x4(1.0f);
        Core::Float4x4 InvViewProj = Core::Float4x4(1.0f);
        Core::Float3 CameraPos = Core::Float3(0.0f, 0.0f, 0.0f);

        uint32_t MaxTraceSteps = 32;
        float MaxTraceDistance = 50.0f;
        float TraceThickness = 0.0025f;
        float RoughnessThreshold = 0.85f;
    };

    RENDERER_API void AddScreenSpaceGIPass(RenderCore::RenderGraphBuilder& graphBuilder, const ScreenSpaceGIPassInput& input);

} // namespace Renderer
