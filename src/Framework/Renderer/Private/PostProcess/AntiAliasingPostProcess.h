#pragma once

#include "GlobalShader.h"
#include "ShaderParameter.h"
#include "RHICommandList.h"
#include "ShaderCore.h"
#include "RenderResource.h"

namespace Renderer {

    BEGIN_SHADER_PARAMETER_STRUCT(TAAParameters)
        SHADER_PARAMETER(Core::Int2, Resolution)
        SHADER_PARAMETER(float, BlendFactor)
        SHADER_PARAMETER_RHI_SRV(Texture2D<float4>, CurrentColor)
        SHADER_PARAMETER_RHI_SRV(Texture2D<float4>, HistoryColor)
        SHADER_PARAMETER_RHI_SRV(Texture2D<float2>, Velocity)
        SHADER_PARAMETER_RHI_UAV(RWTexture2D<float4>, OutputColor)
    END_SHADER_PARAMETER_STRUCT(TAAParameters)

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
