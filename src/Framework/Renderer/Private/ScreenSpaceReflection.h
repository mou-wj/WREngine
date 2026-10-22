#pragma once

#include "GlobalShader.h"
#include "ShaderParameter.h"
#include "RHICommandList.h"
#include "ShaderCore.h"
#include "RenderResource.h"
#include "RenderGraphBuilder.h"

namespace Renderer {

    BEGIN_SHADER_PARAMETER_STRUCT(ScreenSpaceReflectionDepthPyramidParameters)
        SHADER_PARAMETER(Core::Int2, InputExtent)
        SHADER_PARAMETER(Core::Int2, OutputExtent)
        SHADER_PARAMETER(uint32_t, InputMipLevel)
        SHADER_PARAMETER(uint32_t, IsFirstMip)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, InputDepthTexture)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputDepthMipTexture)
        SHADER_PARAMETER_SAMPLER(DepthSampler)
    END_SHADER_PARAMETER_STRUCT(ScreenSpaceReflectionDepthPyramidParameters)

    class ScreenSpaceReflectionDepthPyramidCS : public RenderCore::GlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER_TYPE(ScreenSpaceReflectionDepthPyramidCS);

        static bool ShouldCompilePermutation(const RenderCore::ShaderPermutationParameters& parameters)
        {
            return true;
        }

        static void ModifyShaderCompilerEnvironment(const RenderCore::ShaderPermutationParameters& parameters, RenderCore::ShaderCompilerEnvironment& outEnvironment)
        {
        }

        static const RenderCore::ShaderParametersMetadata* GetShaderParameterMetadata()
        {
            return ScreenSpaceReflectionDepthPyramidParameters::GetMetaData();
        }
    };

    struct BuildSSRDepthPyramidPassInput
    {
        RenderCore::RenderGraphTextureRef SceneDepthTexture = nullptr;
        RenderCore::RenderGraphTextureRef DepthPyramidTexture = nullptr;
        uint32_t MipCount = 0;
        bool bUseSceneDepthForFirstMip = true;
    };

    // Add a graph pass that builds depth mip levels used by screen-space ray tracing.
    RENDERER_API void AddBuildSSRDepthPyramidPass(RenderCore::RenderGraphBuilder& graphBuilder, const BuildSSRDepthPyramidPassInput& input);

    BEGIN_SHADER_PARAMETER_STRUCT(ScreenSpaceReflectionTextureParameters)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, GBufferA)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, GBufferB)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, GBufferC)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, SceneColorTexture)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, DepthPyramidTexture)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputReflectionColor)
        SHADER_PARAMETER_SAMPLER(PointSampler)
    END_SHADER_PARAMETER_STRUCT(ScreenSpaceReflectionTextureParameters)

    BEGIN_SHADER_PARAMETER_STRUCT(ScreenSpaceReflectionParameters)
        SHADER_PARAMETER(Core::Float4x4, ViewProj)
        SHADER_PARAMETER(Core::Float4x4, InvViewProj)
        SHADER_PARAMETER(Core::Float3, CameraPos)
        SHADER_PARAMETER(Core::Float2, ScreenSize)
        SHADER_PARAMETER(uint32_t, MaxTraceSteps)
        SHADER_PARAMETER(float, MaxTraceDistance)
        SHADER_PARAMETER(float, TraceThickness)
        SHADER_PARAMETER(float, RoughnessThreshold)
        SHADER_PARAMETER_STRUCT_INCLUDE(ScreenSpaceReflectionTextureParameters, Textures)
    END_SHADER_PARAMETER_STRUCT(ScreenSpaceReflectionParameters)

    class ScreenSpaceReflectionCS : public RenderCore::GlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER_TYPE(ScreenSpaceReflectionCS);

        static bool ShouldCompilePermutation(const RenderCore::ShaderPermutationParameters& parameters)
        {
            return true;
        }

        static void ModifyShaderCompilerEnvironment(const RenderCore::ShaderPermutationParameters& parameters, RenderCore::ShaderCompilerEnvironment& outEnvironment)
        {
        }

        static const RenderCore::ShaderParametersMetadata* GetShaderParameterMetadata()
        {
            return ScreenSpaceReflectionParameters::GetMetaData();
        }
    };

    struct ScreenSpaceReflectionPassInput
    {
        RenderCore::RenderGraphTextureRef GBufferA = nullptr;
        RenderCore::RenderGraphTextureRef GBufferB = nullptr;
        RenderCore::RenderGraphTextureRef GBufferC = nullptr;
        RenderCore::RenderGraphTextureRef SceneColor = nullptr;
        RenderCore::RenderGraphTextureRef DepthPyramid = nullptr;
        RenderCore::RenderGraphTextureRef OutputReflection = nullptr;

        Core::Float4x4 ViewProj = Core::Float4x4(1.0f);
        Core::Float4x4 InvViewProj = Core::Float4x4(1.0f);
        Core::Float3 CameraPos = Core::Float3(0.0f, 0.0f, 0.0f);

        uint32_t MaxTraceSteps = 32;
        float MaxTraceDistance = 50.0f;
        float TraceThickness = 0.0025f;
        float RoughnessThreshold = 0.85f;
    };

    // Add a graph pass that computes screen-space reflection from gbuffer.
    RENDERER_API void AddScreenSpaceReflectionPass(RenderCore::RenderGraphBuilder& graphBuilder, const ScreenSpaceReflectionPassInput& input);

} // namespace Renderer
