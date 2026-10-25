#pragma once
#include "ShaderParameter.h"
#include "RenderGraphResource.h"
#include "EngineExport.h"
namespace RenderCore {
	class RenderGraphBuilder;
}

namespace Engine
{

    // ============================================================================
    // Material Uniform Data
    // Runtime scalar/vector parameters
    // Constant Buffer
    // ============================================================================

    BEGIN_SHADER_PARAMETER_STRUCT(MaterialUniformData)

        // ---------- Surface ----------

        SHADER_PARAMETER(
            Core::Float4,
            BaseColor)

        SHADER_PARAMETER(
            Core::Float3,
            EmissiveColor)

        SHADER_PARAMETER(
            float,
            Opacity)

        SHADER_PARAMETER(
            float,
            OpacityMask)

        // ---------- PBR ----------

        SHADER_PARAMETER(
            float,
            Metallic)

        SHADER_PARAMETER(
            float,
            Roughness)

        SHADER_PARAMETER(
            float,
            Specular)

        SHADER_PARAMETER(
            float,
            AmbientOcclusion)

        // ---------- Normal ----------

        SHADER_PARAMETER(
            float,
            NormalIntensity)

        // ---------- UV ----------

        SHADER_PARAMETER(
            Core::Float2,
            UVTiling)

        SHADER_PARAMETER(
            Core::Float2,
            UVOffset)

        // ---------- Misc ----------

        SHADER_PARAMETER(
            float,
            AlphaCutoff)

    END_SHADER_PARAMETER_STRUCT(
    MaterialUniformData)



        // ============================================================================
        // Material Textures
        // Descriptor bindings
        // ============================================================================

    BEGIN_SHADER_PARAMETER_STRUCT(
        MaterialTextureParameters)
        // 激活纹理mask
		SHADER_PARAMETER(
			uint32_t,
			TextureMask)


        // ---------- Surface ----------

        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, BaseColorTexture)

        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, OpacityTexture)

        // ---------- Normal ----------

        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, NormalTexture)

        // ---------- Packed PBR ----------

        // R = AO
        // G = Roughness
        // B = Metallic
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ORMTexture)

        // ---------- Emissive ----------

        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, EmissiveTexture)

    END_SHADER_PARAMETER_STRUCT(
    MaterialTextureParameters)



        // ============================================================================
        // Material Samplers
        // ============================================================================

    BEGIN_SHADER_PARAMETER_STRUCT(
        MaterialSamplerParameters)

        SHADER_PARAMETER_SAMPLER(
            LinearWrapSampler)

    END_SHADER_PARAMETER_STRUCT(
    MaterialSamplerParameters)



        // ============================================================================
        // Material Shader Parameters
        // Final material binding
        // ============================================================================

    BEGIN_SHADER_PARAMETER_STRUCT(
        MaterialShaderParameters)

        SHADER_PARAMETER_STRUCT_INCLUDE(
            MaterialUniformData,
            UniformData)

        SHADER_PARAMETER_STRUCT_INCLUDE(
            MaterialTextureParameters,
            TextureData)

        SHADER_PARAMETER_STRUCT_INCLUDE(
            MaterialSamplerParameters,
            SamplerData)

    END_SHADER_PARAMETER_STRUCT(
    MaterialShaderParameters)

   class MaterialRenderProxy;
    
   ENGINE_API void BuildShaderParameters(
            const MaterialRenderProxy*
            proxy,
            RenderCore::RenderGraphBuilder& builder,
            MaterialShaderParameters&
            outParams);
}
