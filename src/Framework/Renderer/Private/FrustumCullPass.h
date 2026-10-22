#pragma once

#include "GlobalShader.h"
#include "ShaderParameter.h"
#include "RHICommandList.h"
#include "ShaderCore.h"
#include "RenderResource.h"
#include <vector>

namespace Renderer {

    BEGIN_SHADER_PARAMETER_STRUCT(AABBParameters)
        SHADER_PARAMETER(Core::Float3, Min)
        SHADER_PARAMETER(Core::Float3, Max)
    END_SHADER_PARAMETER_STRUCT(AABBParameters)

    BEGIN_SHADER_PARAMETER_STRUCT(FrustumCullParameters)
        SHADER_PARAMETER(Core::Float4x4, ViewProjection)
        SHADER_PARAMETER(uint32_t, PrimitiveCount)
        SHADER_PARAMETER_RHI_STRUCTURED_BUFFER(AABBParameters, AABBs)
        SHADER_PARAMETER_RHI_UAV(RWStructuredBuffer<uint>, VisibilityFlags)
    END_SHADER_PARAMETER_STRUCT(FrustumCullParameters)

    class FrustumCullCS : public RenderCore::GlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER_TYPE(FrustumCullCS);

        static bool ShouldCompilePermutation(const RenderCore::ShaderPermutationParameters& parameters)
        {
            return true;
        }

        static void ModifyShaderCompilerEnvironment(const RenderCore::ShaderPermutationParameters& parameters, RenderCore::ShaderCompilerEnvironment& outEnvironment)
        {
        }

        static const RenderCore::ShaderParametersMetadata* GetShaderParameterMetadata()
        {
            return FrustumCullParameters::GetMetaData();
        }
    };

    struct FrustumCullPassInput
    {
        Core::Float4x4 ViewProjection;
        uint32_t PrimitiveCount = 0;
        RenderCore::RenderBuffer* PrimitiveBoundsBuffer = nullptr;
        RenderCore::RenderBuffer* VisibilityFlagsBuffer = nullptr;
    };

    // Runs frustum culling on GPU and reads visibility flags back to CPU.
    RENDERER_API bool ExecuteFrustumCullPass(
        const FrustumCullPassInput& input,
        std::vector<uint32_t>& outVisibilityFlags);

} // namespace Renderer
