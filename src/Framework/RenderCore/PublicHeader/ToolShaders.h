#pragma once

#include "GlobalShader.h"
#include "ShaderParameter.h"
#include "RHICommandList.h"
#include "ShaderCore.h"
namespace RenderCore {



    // Shader parameter structure for texture blitting.
    BEGIN_SHADER_PARAMETER_STRUCT(BlitTextureParameters)
        SHADER_PARAMETER(Core::Float2, SrcSize)
        SHADER_PARAMETER(Core::Float2, SrcInvSize)
        SHADER_PARAMETER(Core::Float2, DstSize)
        SHADER_PARAMETER(Core::Float2, DstInvSize)
        SHADER_PARAMETER(float, SrcMipLevel)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D,SrcTexture)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>,DstTexture)
		SHADER_PARAMETER_SAMPLER(SrcSampler)
    END_SHADER_PARAMETER_STRUCT(BlitTextureParameters)

    class BlitTextureCS : public GlobalShader
    {
        
    public:
        static constexpr char Macro_ColorMode[] = "COLOR_CONVERSION_MODE";

        // 2. 定义变体维度（例如 3 种颜色转换模式）
        using ColorConversionDim = FPermutationDimensionEnum<Macro_ColorMode, 3>;

        // 3. 定义变体域（Domain），可以包含多个维度
        using PermutationDomain = ShaderPermutationDomain<ColorConversionDim>;

        
        DECLARE_GLOBAL_SHADER_TYPE(BlitTextureCS);

        static bool ShouldCompilePermutation(const ShaderPermutationParameters& parameters)
        {
            return true;
        }

        static void ModifyShaderCompilerEnvironment(const ShaderPermutationParameters& parameters,ShaderCompilerEnvironment& outEnvironment)
        {
            PermutationDomain domain;
            domain.SetFromId(parameters.PermutationId);
            domain.ModifyCompilationEnvironment(outEnvironment);
        }

        static const ShaderParametersMetadata* GetShaderParameterMetadata()
        {
            return BlitTextureParameters::GetMetaData();
        }
    };

	

} // namespace RenderCore