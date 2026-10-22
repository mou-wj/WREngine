
#pragma once

#include "Shader.h"
#include "Material.h"

namespace Renderer
{

    /*
    ===============================================================================

        MaterialShader

        Base class for all shaders that depend on a Material.

        Unlike GlobalShader:
            - MaterialShader participates in MaterialShaderMap
            - Can be specialized per-material
            - Can later derive into MeshMaterialShader

    ===============================================================================
    */
    class RENDERER_API MaterialShader : public RenderCore::Shader
    {
    public:
        using ShaderMetaType = Engine::MaterialShaderType;
        MaterialShader(const ShaderMetaType::ShaderCompiledInitializer& initializer)
            : Shader(initializer)
        {
        }

        virtual ~MaterialShader() = default;

    public:

        /*
        ===========================================================================
            Compilation Control
        ===========================================================================
        */

        /*
            Whether this shader should compile for the given material.
        */
        static bool ShouldCompilePermutation(
            const RenderCore::ShaderPermutationParameters& parameters)
        {
            return true;
        }

        /*
            Hook for material-specific compiler defines.
        */
        static void ModifyShaderCompilerEnvironment(
            const RenderCore::ShaderPermutationParameters& parameters,
            RenderCore::ShaderCompilerEnvironment& outEnvironment)
        {
        }

        /*
            Parameter metadata (optional override)
        */
        static const RenderCore::ShaderParametersMetadata* GetShaderParameterMetadata()
        {
            return nullptr;
        }
    };


   
} // namespace RenderCore