#include "StaticMeshMaterialShader.h"
#include "VertexFactory.h"
#include "ShaderCore.h"
#include "MaterialCore.h"
#include "LocalVertexFactory.h"
#include "RHIPipelineStateCache.h"
using namespace Engine;
namespace Renderer
{

    /*
    ===============================================================================
        StaticMeshMaterialShaderVS
    ===============================================================================
    */

    bool StaticMeshMaterialShaderVS::ShouldCompilePermutation(
        const RenderCore::ShaderPermutationParameters& Parameters)
    {
        const MeshMaterialShaderPermutationParameters& MeshParams =
            static_cast<const MeshMaterialShaderPermutationParameters&>(Parameters);

        // 这里只做最基础的过滤
        if (MeshParams.VFType == nullptr)
        {
            return false;
        }
        return true;
    }

    void StaticMeshMaterialShaderVS::ModifyShaderCompilerEnvironment(
        const RenderCore::ShaderPermutationParameters& Parameters,
        RenderCore::ShaderCompilerEnvironment& OutEnvironment)
    {
        const MeshMaterialShaderPermutationParameters& MeshParams =
            static_cast<const MeshMaterialShaderPermutationParameters&>(Parameters);

        // VS define
        OutEnvironment.SetDefine("VERTEX_SHADER", 1);



    }

    const RenderCore::ShaderParametersMetadata*
        StaticMeshMaterialShaderVS::GetShaderParameterMetadata()
    {
        return LocalVertexFactoryParameters::GetMetaData();
    }

    IMPLEMENT_MESH_MATERIAL_SHADER_TYPE(
        StaticMeshMaterialShaderVS,
        "StaticMeshMaterialShaderVS",
        "/material/StaticMeshMaterialShaderVS.sf",
        "MainVS",
        RHI::ERHIShaderFrequency::Vertex
    );




        /*
        ===============================================================================
            StaticMeshMaterialShaderPS
        ===============================================================================
        */

        bool StaticMeshMaterialShaderPS::ShouldCompilePermutation(
            const RenderCore::ShaderPermutationParameters& Parameters)
    {
        const MeshMaterialShaderPermutationParameters& MeshParams =
            static_cast<const MeshMaterialShaderPermutationParameters&>(Parameters);

        if (MeshParams.VFType == nullptr)
        {
            return false;
        }

        return true;
    }

    void StaticMeshMaterialShaderPS::ModifyShaderCompilerEnvironment(
        const RenderCore::ShaderPermutationParameters& Parameters,
        RenderCore::ShaderCompilerEnvironment& OutEnvironment)
    {
        const MeshMaterialShaderPermutationParameters& MeshParams =
            static_cast<const MeshMaterialShaderPermutationParameters&>(Parameters);

        OutEnvironment.SetDefine("PIXEL_SHADER", 1);
        Engine::ModifyShaderCompilerEnvironment(MeshParams.MaterialParams, OutEnvironment);
    }



    const RenderCore::ShaderParametersMetadata*
        StaticMeshMaterialShaderPS::GetShaderParameterMetadata()
    {
        return StaticMeshMaterialShaderPSParameters::GetMetaData();
    }

    IMPLEMENT_MESH_MATERIAL_SHADER_TYPE(
        StaticMeshMaterialShaderPS,
        "StaticMeshMaterialShaderPS",
        "/material/StaticMeshMaterialShaderPS.sf",
        "MainPS",
        ERHIShaderFrequency::Fragment
    )

        /*
           ===============================================================================
               StaticMeshMaterialShaderPS
           ===============================================================================
           */

        bool StaticMeshMaterialGBufferShaderPS::ShouldCompilePermutation(
            const RenderCore::ShaderPermutationParameters& Parameters)
    {
        const MeshMaterialShaderPermutationParameters& MeshParams =
            static_cast<const MeshMaterialShaderPermutationParameters&>(Parameters);

        if (MeshParams.VFType == nullptr)
        {
            return false;
        }

        return true;
    }

    void StaticMeshMaterialGBufferShaderPS::ModifyShaderCompilerEnvironment(
        const RenderCore::ShaderPermutationParameters& Parameters,
        RenderCore::ShaderCompilerEnvironment& OutEnvironment)
    {
        const MeshMaterialShaderPermutationParameters& MeshParams =
            static_cast<const MeshMaterialShaderPermutationParameters&>(Parameters);

        OutEnvironment.SetDefine("PIXEL_SHADER", 1);
        Engine::ModifyShaderCompilerEnvironment(MeshParams.MaterialParams, OutEnvironment);

    }



    const RenderCore::ShaderParametersMetadata*
        StaticMeshMaterialGBufferShaderPS::GetShaderParameterMetadata()
    {
        return StaticMeshMaterialGBufferShaderPSParameters::GetMetaData();
    }

    IMPLEMENT_MESH_MATERIAL_SHADER_TYPE(
        StaticMeshMaterialGBufferShaderPS,
        "StaticMeshMaterialGBufferShaderPS",
        "/material/StaticMeshMaterialGBufferShaderPS.sf",
        "MainPS",
        ERHIShaderFrequency::Fragment
    )
    void StaticMeshMaterialDefferedShadingCS::ModifyShaderCompilerEnvironment(const RenderCore::ShaderPermutationParameters& Parameters, RenderCore::ShaderCompilerEnvironment& OutEnvironment)
    {
        const MaterialShaderPermutationParameters& MeshParams =
            static_cast<const MaterialShaderPermutationParameters&>(Parameters);
        Engine::ModifyShaderCompilerEnvironment(MeshParams.MaterialParams, OutEnvironment);
        PermutationDomain domain;
        domain.SetFromId(Parameters.PermutationId);
        domain.ModifyCompilationEnvironment(OutEnvironment);
    }

    IMPLEMENT_MATERIAL_SHADER_TYPE(
        StaticMeshMaterialDefferedShadingCS,
        "StaticMeshMaterialDefferedShadingCS",
        "/material/StaticMeshMaterialDefferedShadingCS.sf",
        "MainCS",
        RHI::ERHIShaderFrequency::Compute
    );
    

    /*
     ===============================================================================
         PositionOnlyVS
     ===============================================================================
     */

    bool PositionOnlyVS::ShouldCompilePermutation(
        const RenderCore::ShaderPermutationParameters& Parameters)
    {
        return true;
    }

    void PositionOnlyVS::ModifyShaderCompilerEnvironment(
        const RenderCore::ShaderPermutationParameters& Parameters,
        RenderCore::ShaderCompilerEnvironment& OutEnvironment)
    {
        PermutationDomain domain;
        domain.SetFromId(Parameters.PermutationId);
        domain.ModifyCompilationEnvironment(OutEnvironment);
    }



    const RenderCore::ShaderParametersMetadata*
        PositionOnlyVS::GetShaderParameterMetadata()
    {
        return PositionOnlyVSParameters::GetMetaData();
    }

    IMPLEMENT_GLOBAL_SHADER_TYPE(
        PositionOnlyVS,
        "PositionOnlyVS",
        "/tools/PositionOnlyVS.sf",
        "MainVS",
        ERHIShaderFrequency::Vertex
    )

    
    /*
     ===============================================================================
         DepthShadowPassPS
     ===============================================================================
     */
    
    bool DepthShadowPassPS::ShouldCompilePermutation(
        const RenderCore::ShaderPermutationParameters& Parameters)
    {
        const MeshMaterialShaderPermutationParameters& MeshParams =
            static_cast<const MeshMaterialShaderPermutationParameters&>(Parameters);
    
        if (MeshParams.VFType == nullptr)
        {
            return false;
        }
    
        return true;
    }
    
    void DepthShadowPassPS::ModifyShaderCompilerEnvironment(
        const RenderCore::ShaderPermutationParameters& Parameters,
        RenderCore::ShaderCompilerEnvironment& OutEnvironment)
    {
        PermutationDomain Domain;
        Domain.SetFromId(Parameters.PermutationId);
        Domain.ModifyCompilationEnvironment(OutEnvironment);
    }
    
    
    
    const RenderCore::ShaderParametersMetadata*
        DepthShadowPassPS::GetShaderParameterMetadata()
    {
        return DepthShadowPassPSParameters::GetMetaData();
    }
    
    IMPLEMENT_GLOBAL_SHADER_TYPE(
        DepthShadowPassPS,
        "DepthShadowPassPS",
        "/tools/DepthShadowPassPS.sf",
        "MainPS",
        ERHIShaderFrequency::Fragment
    )


    RHI::RHIVertexDescState* GetVertexOnlyState(bool useIntance) {
        RHI::RHIVertexDescStateDesc Desc;

        // 一个 Vertex Buffer
        Desc.Bindings.push_back({
            .Binding = 0,
            .Stride = sizeof(float) * 3,
            .InputRate = ERHIInputRate::PerVertex
            });

        // Position
        Desc.Attributes.push_back({
            .Location = 0,
            .Binding = 0,
            .Offset = 0,
            .Format = ERHIFormat::R32G32B32_Float
            });
        if (useIntance) {
            // 一个 Vertex Buffer
            Desc.Bindings.push_back({
                .Binding = 1,
                .Stride = sizeof(uint32_t),
                .InputRate = ERHIInputRate::PerInstance
                });

            // Position
            Desc.Attributes.push_back({
                .Location = 1,
                .Binding = 1,
                .Offset = 0,
                .Format = ERHIFormat::R32_UInt
                });


        }
        return RHI::RHIPipelineStateCache::GetOrCreateVertexDescState(Desc);
    }
}