#pragma once

#include "GlobalShader.h"
#include "ShaderParameter.h"
#include "RHICommandList.h"
#include "ShaderCore.h"
#include "RenderResource.h"
#include "RenderGraphBuilder.h"

namespace Renderer {

    BEGIN_SHADER_PARAMETER_STRUCT(DepthPyramidGenerateParameters)
        SHADER_PARAMETER(Core::Int2, InputExtent)
        SHADER_PARAMETER(Core::Int2, OutputExtent)
        SHADER_PARAMETER(uint32_t, InputMipLevel)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, InputDepthTexture)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, InDepthMipTexture)
        SHADER_PARAMETER_RHI_UAV(RWTexture2D<float>, SrcDepthMipTexture)
        SHADER_PARAMETER_RHI_UAV(RWTexture2D<float>, OutDepthMipTexture)
        SHADER_PARAMETER_SAMPLER(DepthSampler)
    END_SHADER_PARAMETER_STRUCT(DepthPyramidGenerateParameters)

    class DepthPyramidGenerateCS : public RenderCore::GlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER_TYPE(DepthPyramidGenerateCS);

        static bool ShouldCompilePermutation(const RenderCore::ShaderPermutationParameters& parameters)
        {
            return true;
        }

        static void ModifyShaderCompilerEnvironment(const RenderCore::ShaderPermutationParameters& parameters, RenderCore::ShaderCompilerEnvironment& outEnvironment)
        {
        }

        static const RenderCore::ShaderParametersMetadata* GetShaderParameterMetadata()
        {
            return DepthPyramidGenerateParameters::GetMetaData();
        }
    };

    struct BuildDepthPyramidPassInput
    {
        RenderCore::RenderGraphTextureRef SceneDepthTexture = nullptr;
        RenderCore::RenderGraphTextureRef DepthPyramidTexture = nullptr;
    };

    RENDERER_API void AddBuildDepthPyramidPass(RenderCore::RenderGraphBuilder& graphBuilder, const BuildDepthPyramidPassInput& input);

} // namespace Renderer
