#pragma once

#include "GlobalShader.h"
#include "ShaderParameter.h"
#include "RHICommandList.h"
#include "ShaderCore.h"
#include "RenderResource.h"
#include "RenderGraphBuilder.h"

#include <vector>

namespace Renderer {

    struct HZBInstanceAABB
    {
        Core::Float3 Min = Core::Float3(0.0f, 0.0f, 0.0f);
        Core::Float3 Max = Core::Float3(0.0f, 0.0f, 0.0f);
        Core::Float4x4 Transform = Core::Float4x4(1.0f);
    };

    BEGIN_SHADER_PARAMETER_STRUCT(HZBInstanceParameters)
        SHADER_PARAMETER(Core::Float3, Min)
        SHADER_PARAMETER(Core::Float3, Max)
        SHADER_PARAMETER(Core::Float4x4, Transform)
    END_SHADER_PARAMETER_STRUCT(HZBInstanceParameters)

    BEGIN_SHADER_PARAMETER_STRUCT(HZBOcclusionTestParameters)
        SHADER_PARAMETER(Core::Float4x4, ViewProjection)
        SHADER_PARAMETER(Core::Float2, ScreenSize)
        SHADER_PARAMETER(uint32_t, PrimitiveCount)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, DepthPyramidTexture)
        SHADER_PARAMETER_RHI_STRUCTURED_BUFFER(HZBInstanceParameters, Instances)
        SHADER_PARAMETER_RHI_UAV(RWStructuredBuffer<uint>, VisibilityFlags)
    END_SHADER_PARAMETER_STRUCT(HZBOcclusionTestParameters)

    class HZBOcclusionTestCS : public RenderCore::GlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER_TYPE(HZBOcclusionTestCS);

        static bool ShouldCompilePermutation(const RenderCore::ShaderPermutationParameters& parameters)
        {
            return true;
        }

        static void ModifyShaderCompilerEnvironment(const RenderCore::ShaderPermutationParameters& parameters, RenderCore::ShaderCompilerEnvironment& outEnvironment)
        {
        }

        static const RenderCore::ShaderParametersMetadata* GetShaderParameterMetadata()
        {
            return HZBOcclusionTestParameters::GetMetaData();
        }
    };

    struct HZBOcclusionTestInput
    {
        Core::Float4x4 ViewProjection = Core::Float4x4(1.0f);
        Core::Float2 ScreenSize = Core::Float2(1920.0f, 1080.0f);
        uint32_t PrimitiveCount = 0;

        const HZBInstanceAABB* InstanceData = nullptr;
        RenderCore::RenderBuffer* InstanceBoundsBuffer = nullptr;
        RenderCore::RenderGraphTextureRef DepthPyramidTexture = nullptr;
        RenderCore::RenderBuffer* VisibilityFlagsBuffer = nullptr;
    };

    struct HZBProjectedBox
    {
        float MinX = 0.0f;
        float MinY = 0.0f;
        float MaxX = 0.0f;
        float MaxY = 0.0f;
        bool bIsValid = false;
    };

    RENDERER_API HZBProjectedBox ProjectAABBToScreenSpace(const Core::Float4x4& viewProjection, const Core::Float2& screenSize, const HZBInstanceAABB& instance);

    RENDERER_API bool ExecuteHZBOcclusionTestPass(
        const HZBOcclusionTestInput& input,
        std::vector<uint32_t>& outVisibilityFlags);

} // namespace Renderer
