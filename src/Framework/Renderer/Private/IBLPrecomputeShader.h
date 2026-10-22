#pragma once

#include "GlobalShader.h"
#include "ShaderParameter.h"
#include "RHICommandList.h"
#include "ShaderCore.h"
#include "RenderResource.h"

namespace Renderer {
    extern RENDERER_API RenderCore::RenderTextureSP GlobalIBLLutTexture;

    BEGIN_SHADER_PARAMETER_STRUCT(IBLPrecomputeSpecEvnParameters)
        SHADER_PARAMETER_RHI_UAV(RWTexture2DArray<float4>, OutputSpecularTexture)
    END_SHADER_PARAMETER_STRUCT(IBLPrecomputeSpecEvnParameters)
    BEGIN_SHADER_PARAMETER_STRUCT(IBLPrecomputeDiffuseEvnParameters)
        SHADER_PARAMETER_RHI_UAV(RWTexture2DArray<float4>, OutputDiffuseTexture)
    END_SHADER_PARAMETER_STRUCT(IBLPrecomputeDiffuseEvnParameters)
    BEGIN_SHADER_PARAMETER_STRUCT(IBLPrecomputeEvnParameters)
        SHADER_PARAMETER(int, SampleCount)
        SHADER_PARAMETER(float, Roughness)
        SHADER_PARAMETER_RHI_TEXTURE(Texture2D, EnvironmentMap)
        SHADER_PARAMETER_SAMPLER(EnvSampler)
        SHADER_PARAMETER_STRUCT_REFERENCE(IBLPrecomputeSpecEvnParameters, OutputSpecularParam)
        SHADER_PARAMETER_STRUCT_REFERENCE(IBLPrecomputeDiffuseEvnParameters, OutputDiffuseParam)
    END_SHADER_PARAMETER_STRUCT(IBLPrecomputeEvnParameters)
    BEGIN_SHADER_PARAMETER_STRUCT(IBLPrecomputeBRDFParameters)
        SHADER_PARAMETER_RHI_UAV(RWTexture2D<float2>, OutputBRDFLUT)
    END_SHADER_PARAMETER_STRUCT(IBLPrecomputeBRDFParameters)

    // IBL 预计算所需参数，包含环境贴图与 BRDF LUT 两种输出（由变体选择）
    BEGIN_SHADER_PARAMETER_STRUCT(IBLPrecomputeParameters)
        SHADER_PARAMETER(Core::Int2, OutputSize)
        SHADER_PARAMETER_STRUCT_REFERENCE(IBLPrecomputeEvnParameters, EnvironmentMapParameter)
        SHADER_PARAMETER_STRUCT_REFERENCE(IBLPrecomputeBRDFParameters,BRDFParameter)
    END_SHADER_PARAMETER_STRUCT(IBLPrecomputeParameters)

    // 变体宏：选择计算类型（0 = EnvMap, 1 = BRDF_LUT）
    static constexpr char Macro_IBLMode[] = "IBL_PRECOMPUTE_MODE";
    using IBLModeDim = RenderCore::FPermutationDimensionEnum<Macro_IBLMode, 3>;
    

    class IBLPrecomputeCS : public RenderCore::GlobalShader
    {
    public:
        using PermutationDomain = RenderCore::ShaderPermutationDomain<IBLModeDim>;
        DECLARE_GLOBAL_SHADER_TYPE(IBLPrecomputeCS);

        static bool ShouldCompilePermutation(const RenderCore::ShaderPermutationParameters& parameters)
        {
            return true;
        }

        static void ModifyShaderCompilerEnvironment(const RenderCore::ShaderPermutationParameters& parameters, RenderCore::ShaderCompilerEnvironment& outEnvironment)
        {
            PermutationDomain domain;
            domain.SetFromId(parameters.PermutationId);
            domain.ModifyCompilationEnvironment(outEnvironment);
        }

        static const RenderCore::ShaderParametersMetadata* GetShaderParameterMetadata()
        {
            return IBLPrecomputeParameters::GetMetaData();
        }
    };

} // namespace Renderer

