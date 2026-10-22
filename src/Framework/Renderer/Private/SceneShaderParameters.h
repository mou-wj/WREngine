#pragma once
#include "ShaderParameter.h"
#include "Math.hpp"
#include "GlobalDistanceField.h"
#include "EngineExport.h"
namespace RenderCore {
	class RenderGraphBuilder;
}

namespace Renderer {

    BEGIN_SHADER_PARAMETER_STRUCT(LightCommonData)
        SHADER_PARAMETER(Core::Float3,Color)
        SHADER_PARAMETER(float,Intensity)
        SHADER_PARAMETER(uint32_t, LightId)
    END_SHADER_PARAMETER_STRUCT(LightCommonData)

    BEGIN_SHADER_PARAMETER_STRUCT(DirectionalLightData)

        SHADER_PARAMETER_STRUCT_INCLUDE( LightCommonData, Common )
        SHADER_PARAMETER(Core::Float3,Direction)
        
    END_SHADER_PARAMETER_STRUCT(DirectionalLightData)


    BEGIN_SHADER_PARAMETER_STRUCT(PointLightData)
        SHADER_PARAMETER_STRUCT_INCLUDE(LightCommonData,Common)
        SHADER_PARAMETER(Core::Float3,Position)
        SHADER_PARAMETER(float,Radius)
    END_SHADER_PARAMETER_STRUCT(PointLightData)

    BEGIN_SHADER_PARAMETER_STRUCT(SpotLightData)
        SHADER_PARAMETER_STRUCT_INCLUDE(LightCommonData,Common)
        SHADER_PARAMETER(Core::Float3,Position)
        SHADER_PARAMETER(float,Radius)
        SHADER_PARAMETER(Core::Float3,Direction)
        SHADER_PARAMETER(float,InnerConeCos)
        SHADER_PARAMETER(float,OuterConeCos)
    END_SHADER_PARAMETER_STRUCT(SpotLightData)

    BEGIN_SHADER_PARAMETER_STRUCT(SceneLightParameters)
        SHADER_PARAMETER(uint32_t,DirectionalLightCount)
        SHADER_PARAMETER(uint32_t,PointLightCount)
        SHADER_PARAMETER(uint32_t,SpotLightCount)
        SHADER_PARAMETER_RDG_STRUCTURED_BUFFER(DirectionalLightData,DirectionalLights)
        SHADER_PARAMETER_RDG_STRUCTURED_BUFFER(PointLightData,PointLights)
        SHADER_PARAMETER_RDG_STRUCTURED_BUFFER(SpotLightData,SpotLights)
        SHADER_PARAMETER_SAMPLER(LinearClampSampler)
    END_SHADER_PARAMETER_STRUCT(SceneLightParameters)

    BEGIN_SHADER_PARAMETER_STRUCT(SceneEvnIBLLightParameters)
        SHADER_PARAMETER_SAMPLER(LinearClampIBLSampler)
        SHADER_PARAMETER_RDG_TEXTURE(TextureCube, IBLSpecularMap)
        SHADER_PARAMETER_RDG_TEXTURE(TextureCube, IBLDiffuseMap)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, IBLLut)
    END_SHADER_PARAMETER_STRUCT(SceneEvnIBLLightParameters)


    // Spot light shadow atlas access parameters.
    BEGIN_SHADER_PARAMETER_STRUCT(AtlasShadowTextureAccessInfo)
        SHADER_PARAMETER(uint32_t, Layer)
        SHADER_PARAMETER(uint32_t, mip)
        SHADER_PARAMETER(Core::Float2, UVScale)
        SHADER_PARAMETER(Core::Float2, UVBias)
        SHADER_PARAMETER(Core::Float2, Padding)
        SHADER_PARAMETER(Core::Mat4, ViewProj)
    END_SHADER_PARAMETER_STRUCT(AtlasShadowTextureAccessInfo)

    BEGIN_SHADER_PARAMETER_STRUCT(LightShadowAccessInfo)
        SHADER_PARAMETER(uint32_t, ShadowType)
        SHADER_PARAMETER(uint32_t, ShadowTextureIndex)
        SHADER_PARAMETER(uint32_t, ShadowInfoIndex)
        SHADER_PARAMETER(uint32_t, CascadeCount)
    END_SHADER_PARAMETER_STRUCT(LightShadowAccessInfo)

    BEGIN_SHADER_PARAMETER_STRUCT(DirectionalLightCascadeShadowViewInfo)
        SHADER_PARAMETER(Core::Mat4, ViewProj)
    END_SHADER_PARAMETER_STRUCT(DirectionalLightCascadeShadowViewInfo)

    BEGIN_SHADER_PARAMETER_STRUCT(SceneLightShadowParameters)
        SHADER_PARAMETER_SAMPLER(NearestSampler)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, SplitBuffer)
        SHADER_PARAMETER_RDG_STRUCTURED_BUFFER(AtlasShadowTextureAccessInfo, AtlasTextureInfos)
        SHADER_PARAMETER_RDG_STRUCTURED_BUFFER(LightShadowAccessInfo, LightShadowInfos)
        SHADER_PARAMETER_RDG_STRUCTURED_BUFFER(DirectionalLightCascadeShadowViewInfo, DirectionalLightViewInfos)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, LightShadowAtlas)
		SHADER_PARAMETER_RDG_TEXTURE_ARRAY(TextureCube, PointLightShadows, 8)// Fixed capacity for point-light shadow maps.
        SHADER_PARAMETER_RDG_TEXTURE_ARRAY(Texture2DArray, ParrallelLightShadows, 4)// Fixed capacity for directional-light shadow maps.
    END_SHADER_PARAMETER_STRUCT(SceneLightShadowParameters)

    //距离场参数
    BEGIN_SHADER_PARAMETER_STRUCT(SceneGlobalDistanceFieldParameters)
        SHADER_PARAMETER_SAMPLER(DistanceSampler)
        SHADER_PARAMETER_RDG_TEXTURE(Texture3D, GlobalDistanceFieldAtlas)
        SHADER_PARAMETER_RHI_STRUCTURED_BUFFER(GlobalDistanceFieldBlockIndex, AllocateBlockIndexInfos)
        SHADER_PARAMETER(Core::Float3, GlobalDistanceFieldOrigin)
        SHADER_PARAMETER(Core::Float3, BlockSize)
        SHADER_PARAMETER(Core::UInt3, GlobalDistanceFieldGridSize)
        SHADER_PARAMETER(float, DistanceFieldVoxelSize)
        // Atlas尺寸
        SHADER_PARAMETER(Core::UInt3, GlobalDistanceFieldAtlasResolution)
    END_SHADER_PARAMETER_STRUCT(SceneGlobalDistanceFieldParameters)


	BEGIN_SHADER_PARAMETER_STRUCT(SceneShaderParameters)
        SHADER_PARAMETER_STRUCT_REFERENCE(SceneLightParameters, LightParameters)
        SHADER_PARAMETER_STRUCT_REFERENCE(SceneLightShadowParameters, LightShadowParameters)
    END_SHADER_PARAMETER_STRUCT(SceneShaderParameters)

    class Scene;
    ENGINE_API void BuildShaderParameters(
        Scene*  scene,
        RenderCore::RenderGraphBuilder& builder,
        SceneShaderParameters&
        out);

    ENGINE_API void BuildGlobalDistanceFieldParameters(
        Scene* scene,
        RenderCore::RenderGraphBuilder& builder,
        SceneGlobalDistanceFieldParameters& out);

    ENGINE_API void BuildEvnIBLLightParameters(
		Scene* scene,
		RenderCore::RenderGraphBuilder& builder,
		SceneEvnIBLLightParameters& out);

}
