// StaticMeshMaterialShader.h
#pragma once

#include "MeshMaterialShader.h"
#include "ShaderParameter.h"
#include "LocalVertexFactory.h"
#include "SceneShaderParameters.h"
#include "MaterialShaderParameter.h"
#include "MateiralShader.h"
#include "GlobalShader.h"
#include "GBufferInfo.h"

namespace Renderer
{
    BEGIN_SHADER_PARAMETER_STRUCT(StaticMeshMaterialShaderVSParameters)
        SHADER_PARAMETER_STRUCT_REFERENCE(Engine::LocalVertexFactoryParameters, vertexFactoryParameters)
    END_SHADER_PARAMETER_STRUCT(StaticMeshMaterialShaderVSParameters)

    /*
    ===============================================================================
        StaticMeshMaterialShaderVS
    ===============================================================================
    */
    class StaticMeshMaterialShaderVS : public MeshMaterialShader
    {
    public:
        DECLARE_MESH_MATERIAL_SHADER_TYPE(StaticMeshMaterialShaderVS)


        static bool ShouldCompilePermutation(
            const RenderCore::ShaderPermutationParameters& parameters);

        static void ModifyShaderCompilerEnvironment(
            const RenderCore::ShaderPermutationParameters& parameters,
            RenderCore::ShaderCompilerEnvironment& outEnvironment);

        static const RenderCore::ShaderParametersMetadata*
            GetShaderParameterMetadata();
    };

    BEGIN_SHADER_PARAMETER_STRUCT(StaticMeshMaterialShaderPSParameters)
        SHADER_PARAMETER_STRUCT_REFERENCE(Engine::LocalVertexFactoryParameters, vertexFactoryParameters)
        SHADER_PARAMETER_STRUCT_REFERENCE(SceneShaderParameters, Scene)
        SHADER_PARAMETER_STRUCT_REFERENCE(Engine::MaterialShaderParameters, Material)
        SHADER_PARAMETER_RENDER_TARGET_BINDING_SLOTS(renderTargetSlots)
    END_SHADER_PARAMETER_STRUCT(StaticMeshMaterialShaderPSParameters)
    /*
    ===============================================================================
        StaticMeshMaterialShaderPS
    ===============================================================================
    */
    class StaticMeshMaterialShaderPS : public MeshMaterialShader
    {
    public:
        DECLARE_MESH_MATERIAL_SHADER_TYPE(StaticMeshMaterialShaderPS)


        static bool ShouldCompilePermutation(
            const RenderCore::ShaderPermutationParameters& parameters);

        static void ModifyShaderCompilerEnvironment(
            const RenderCore::ShaderPermutationParameters& parameters,
            RenderCore::ShaderCompilerEnvironment& outEnvironment);

        static const RenderCore::ShaderParametersMetadata*
            GetShaderParameterMetadata();
    };


    BEGIN_SHADER_PARAMETER_STRUCT(StaticMeshMaterialGBufferShaderPSParameters)
        SHADER_PARAMETER_STRUCT_REFERENCE(Engine::LocalVertexFactoryParameters, vertexFactoryParameters)
        SHADER_PARAMETER_STRUCT_REFERENCE(Engine::MaterialShaderParameters, Material)
        SHADER_PARAMETER_RENDER_TARGET_BINDING_SLOTS(renderTargetSlots)
    END_SHADER_PARAMETER_STRUCT(StaticMeshMaterialGBufferShaderPSParameters)
    /*
    ===============================================================================
        StaticMeshMaterialGBufferShaderPS
    ===============================================================================
    */
    class StaticMeshMaterialGBufferShaderPS : public MeshMaterialShader
    {
    public:
        DECLARE_MESH_MATERIAL_SHADER_TYPE(StaticMeshMaterialGBufferShaderPS)


        static bool ShouldCompilePermutation(
            const RenderCore::ShaderPermutationParameters& parameters);

        static void ModifyShaderCompilerEnvironment(
            const RenderCore::ShaderPermutationParameters& parameters,
            RenderCore::ShaderCompilerEnvironment& outEnvironment);

        static const RenderCore::ShaderParametersMetadata*
            GetShaderParameterMetadata();
    };

    BEGIN_SHADER_PARAMETER_STRUCT(GBufferTextureParameters)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputColor)
        SHADER_PARAMETER_STRUCT_REFERENCE(GBufferInputParameters, GBufferInput)
    END_SHADER_PARAMETER_STRUCT(GBufferTextureParameters)


    BEGIN_SHADER_PARAMETER_STRUCT(StaticMeshMaterialDefferedShadingCSParameters)
        SHADER_PARAMETER(Core::Float4x4, InvViewProj)
        SHADER_PARAMETER(Core::Float3, CameraPos)
        SHADER_PARAMETER(Core::Float2, ScreenSize)
        SHADER_PARAMETER_STRUCT_REFERENCE(SceneShaderParameters, Scene)
        SHADER_PARAMETER_STRUCT_REFERENCE(GBufferTextureParameters, GBuffer)
        SHADER_PARAMETER_STRUCT_REFERENCE(SceneEvnIBLLightParameters, EnvIBLParameters)
    END_SHADER_PARAMETER_STRUCT(StaticMeshMaterialDefferedShadingCSParameters)

    class StaticMeshMaterialDefferedShadingCS : public MaterialShader
    {

    public:
        static constexpr char IS_IBL[] = "IS_IBL";

        // Boolean IBL permutation dimension.
        using IsIBLPermutation = RenderCore::FPermutationDimensionBool<IS_IBL>;

        // Define the IBL permutation domain.
        using PermutationDomain = RenderCore::ShaderPermutationDomain<IsIBLPermutation>;

        DECLARE_MATERIAL_SHADER_TYPE(StaticMeshMaterialDefferedShadingCS);

        static bool ShouldCompilePermutation(const RenderCore::ShaderPermutationParameters& parameters)
        {
            return true;
        }
        static void ModifyShaderCompilerEnvironment(const RenderCore::ShaderPermutationParameters& parameters, RenderCore::ShaderCompilerEnvironment& outEnvironment);

        static const RenderCore::ShaderParametersMetadata* GetShaderParameterMetadata()
        {
            return StaticMeshMaterialDefferedShadingCSParameters::GetMetaData();
        }
    };

    BEGIN_SHADER_PARAMETER_STRUCT(PositionOnlyVSParameters)
        SHADER_PARAMETER(Core::Mat4, ViewProjection)
        SHADER_PARAMETER(Core::Mat4, Model)
        SHADER_PARAMETER_STRUCT_REFERENCE(Engine::LocalVertexFactoryInstanceParameters, LocalVFInstanceInfo)
    END_SHADER_PARAMETER_STRUCT(PositionOnlyVSParameters)

    /*
   ===============================================================================
       PositionOnlyVS
   ===============================================================================
   */
    class PositionOnlyVS : public RenderCore::GlobalShader
    {
    public:
        static constexpr char USE_INSTANCE_MODEL[] = "USE_INSTANCE";

        // Boolean instancing permutation dimension.
        using InstancePermutation = RenderCore::FPermutationDimensionBool<USE_INSTANCE_MODEL>;

        // Define the instancing permutation domain.
        using PermutationDomain = RenderCore::ShaderPermutationDomain<InstancePermutation>;
        DECLARE_GLOBAL_SHADER_TYPE(PositionOnlyVS)
        static bool ShouldCompilePermutation(
            const RenderCore::ShaderPermutationParameters& parameters);

        static void ModifyShaderCompilerEnvironment(
            const RenderCore::ShaderPermutationParameters& parameters,
            RenderCore::ShaderCompilerEnvironment& outEnvironment);

        static const RenderCore::ShaderParametersMetadata*
            GetShaderParameterMetadata();
    };


    BEGIN_SHADER_PARAMETER_STRUCT(DepthShadowPassPSParameters)
        SHADER_PARAMETER(Core::Float3, CameraWorldPosition)
        SHADER_PARAMETER_RENDER_TARGET_BINDING_SLOTS(renderTargetSlots)
    END_SHADER_PARAMETER_STRUCT(DepthShadowPassPSParameters)
   
    /*
   ===============================================================================
       DepthShadowPassPS
   ===============================================================================
   */
    class DepthShadowPassPS : public RenderCore::GlobalShader
    {
    public:
        static constexpr char Macro_DepthStoreMode[] = "DEPTH_STORE_MODE";

        // Depth storage mode permutation dimension.
        using DepthStoreModeDim = RenderCore::FPermutationDimensionEnum<Macro_DepthStoreMode, 2>;

        // Define the depth storage permutation domain.
        using PermutationDomain = RenderCore::ShaderPermutationDomain<DepthStoreModeDim>;
        DECLARE_GLOBAL_SHADER_TYPE(DepthShadowPassPS)
        static bool ShouldCompilePermutation(
            const RenderCore::ShaderPermutationParameters& parameters);

        static void ModifyShaderCompilerEnvironment(
            const RenderCore::ShaderPermutationParameters& parameters,
            RenderCore::ShaderCompilerEnvironment& outEnvironment);

        static const RenderCore::ShaderParametersMetadata* GetShaderParameterMetadata();
    };

    RHI::RHIVertexDescState* GetVertexOnlyState(bool useIntance = false);
}