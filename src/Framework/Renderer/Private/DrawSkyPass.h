#pragma once

#include "GlobalShader.h"
#include "ShaderParameter.h"
#include "RHICommandList.h"
#include "ShaderCore.h"
#include "RenderResource.h"
#include "SceneView.h"
#include "StaticMeshMaterialShader.h"

namespace Renderer {
    BEGIN_SHADER_PARAMETER_STRUCT(DrawSkyVertexParameters)
        SHADER_PARAMETER(Core::Float4x4,View)
        SHADER_PARAMETER(Core::Float4x4, Projection)
    END_SHADER_PARAMETER_STRUCT(DrawSkyVertexParameters)

    class DrawSkyVS : public RenderCore::GlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER_TYPE(DrawSkyVS)

        static bool ShouldCompilePermutation(const RenderCore::ShaderPermutationParameters& parameters)
        {
            return true;
        }

        static void ModifyShaderCompilerEnvironment(const RenderCore::ShaderPermutationParameters& parameters, RenderCore::ShaderCompilerEnvironment& outEnvironment)
        {
            outEnvironment.SetDefine("VERTEX_SHADER", 1);
        }


        static const RenderCore::ShaderParametersMetadata* GetShaderParameterMetadata()
        {
            return DrawSkyVertexParameters::GetMetaData();
        }
    };

    BEGIN_SHADER_PARAMETER_STRUCT(DrawSkyPixelParameters)
        SHADER_PARAMETER_RDG_TEXTURE(TextureCube, SkyCubemap)
        SHADER_PARAMETER_SAMPLER(SkySampler)
        SHADER_PARAMETER_RENDER_TARGET_BINDING_SLOTS(renderTargetSlots)
    END_SHADER_PARAMETER_STRUCT(DrawSkyPixelParameters)

    class DrawSkyPS : public RenderCore::GlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER_TYPE(DrawSkyPS)

        static bool ShouldCompilePermutation(const RenderCore::ShaderPermutationParameters& parameters)
        {
            return true;
        }

        static void ModifyShaderCompilerEnvironment(const RenderCore::ShaderPermutationParameters& parameters, RenderCore::ShaderCompilerEnvironment& outEnvironment)
        {
            outEnvironment.SetDefine("PIXEL_SHADER", 1);
        }

        static const RenderCore::ShaderParametersMetadata* GetShaderParameterMetadata()
        {
            return DrawSkyPixelParameters::GetMetaData();
        }
    };

    void AddDrawSkyBoxPass(RenderCore::RenderGraphBuilder& builder, RenderCore::RenderTexture* envMap, RenderCore::RenderGraphTexture* colorAttachment, RenderCore::RenderGraphTexture* depthAttachment,Engine::SceneView& view);
}
