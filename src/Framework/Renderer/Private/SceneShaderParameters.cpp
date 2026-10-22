#include "SceneShaderParameters.h"
#include "RenderGraphBuilder.h"
#include "Scene.h"
#include "IBLPrecomputeShader.h"
namespace Renderer {


    void BuildShaderParameters(
        Scene*
        scene,
        RenderCore::RenderGraphBuilder& builder,
        SceneShaderParameters&
        out) {
        const auto& sceneLightResourceInfo = scene->GetGPUResourceInfo().LightResourceInfo;
        out.LightParameters.PointLightCount = sceneLightResourceInfo.PointLightCount;
        out.LightParameters.SpotLightCount = sceneLightResourceInfo.SpotLightCount;
        out.LightParameters.DirectionalLightCount = sceneLightResourceInfo.DirectionalLightCount;
        if (sceneLightResourceInfo.PointLightBuffer) {
            auto rdgB = builder.RegisterExternalBuffer(
                "PointLights",
                sceneLightResourceInfo.PointLightBuffer.get());
            RenderCore::RenderGraphBufferSRVDesc srvDesc;
            srvDesc.NumElements = 1;
            srvDesc.Stride = sceneLightResourceInfo.PointLightBuffer->GetRHI()->GetDesc().Size;
            srvDesc.Buffer = rdgB;
            out.LightParameters.PointLights = builder.CreateBufferSRV("PointLightsSRV", srvDesc);
        }
        if (sceneLightResourceInfo.SpotLightBuffer) {
            auto rdgB = builder.RegisterExternalBuffer(
                "SpotLights",
                sceneLightResourceInfo.SpotLightBuffer.get());
            RenderCore::RenderGraphBufferSRVDesc srvDesc;
            srvDesc.NumElements = 1;
            srvDesc.Stride = sceneLightResourceInfo.SpotLightBuffer->GetRHI()->GetDesc().Size;
            srvDesc.Buffer = rdgB;
            out.LightParameters.SpotLights = builder.CreateBufferSRV("SpotLightsSRV", srvDesc);
        }
        if (sceneLightResourceInfo.DirectionalLightBuffer) {
            auto rdgB = builder.RegisterExternalBuffer(
                "DirectionalLights",
                sceneLightResourceInfo.DirectionalLightBuffer.get());
            RenderCore::RenderGraphBufferSRVDesc srvDesc;
            srvDesc.NumElements = 1;
            srvDesc.Stride = sceneLightResourceInfo.DirectionalLightBuffer->GetRHI()->GetDesc().Size;
            srvDesc.Buffer = rdgB;
            out.LightParameters.DirectionalLights = builder.CreateBufferSRV("DirectionalLightsSRV", srvDesc);
        }
        out.LightParameters.LinearClampSampler = RenderCore::GlobalSampler.get();
        //填充阴影参数
        out.LightShadowParameters.NearestSampler = RenderCore::GlobalNearestSampler.get();
        auto altasTexture = scene->GetShadowMapAllocator().GetShadowAtlas();
        auto pointLightShadowTextures = scene->GetShadowMapAllocator().GetDedicatedPointLightShadowTextures();
        auto parallelLightShadowTextures = scene->GetShadowMapAllocator().GetDedicatedParallelLightShadowTextures();
        int i = 0;
        for (auto& t : pointLightShadowTextures) {
            if (i > 7) break;
            auto rdgT = builder.RegisterExternalTexture("PointLightShadowTexture", t);
            out.LightShadowParameters.PointLightShadows[i] = rdgT;
            i++;
        }
        auto unused2D = builder.RegisterExternalTexture("Unused2DTexture", RenderCore::GlobalTestTexture.get());
        auto unusedCube = builder.RegisterExternalTexture("UnusedCubeTexture", RenderCore::GlobalEmptyCubeTexture.get());
        auto unused2DArray = builder.RegisterExternalTexture("UnusedTexture2dArray", RenderCore::GlobalEmptyTexture2DArray.get());
        for (; i < 8; i++) {
            out.LightShadowParameters.PointLightShadows[i] = unusedCube;
        }
        i = 0;
        for (auto& t : parallelLightShadowTextures) {
            if (i > 3) break;
            auto rdgT = builder.RegisterExternalTexture("ParallelLightShadowTexture", t);
            out.LightShadowParameters.ParrallelLightShadows[i] = rdgT;
            i++;
        }
        for (; i < 4; i++) {
            out.LightShadowParameters.ParrallelLightShadows[i] = unused2DArray;
        }
        auto rdgT = builder.RegisterExternalTexture("ShadowAtlasTexture", altasTexture);
        out.LightShadowParameters.LightShadowAtlas = rdgT;
        auto rdgB = builder.RegisterExternalBuffer("AtlasAccessTextureInfos", scene->GetGPUResourceInfo().ShadowResourceInfo.AtlasAccessInfoBuffer.get());
        RenderCore::RenderGraphBufferSRVDesc srvDesc;
        srvDesc.NumElements = 1;
        srvDesc.Stride = scene->GetGPUResourceInfo().ShadowResourceInfo.AtlasAccessInfoBuffer->GetRHI()->GetDesc().Size;
        srvDesc.Buffer = rdgB;
        out.LightShadowParameters.AtlasTextureInfos = builder.CreateBufferSRV("AtlasAccessTextureInfosSRV", srvDesc);
        rdgB = builder.RegisterExternalBuffer("LightShadowAccessInfos", scene->GetGPUResourceInfo().ShadowResourceInfo.LightShadowInfoBuffer.get());
        srvDesc.NumElements = 1;
        srvDesc.Stride = scene->GetGPUResourceInfo().ShadowResourceInfo.LightShadowInfoBuffer->GetRHI()->GetDesc().Size;
        srvDesc.Buffer = rdgB;
        out.LightShadowParameters.LightShadowInfos = builder.CreateBufferSRV("LightShadowAccessInfosSRV", srvDesc);
        rdgB = builder.RegisterExternalBuffer("DirectionalLightShadowViewInfos", scene->GetGPUResourceInfo().ShadowResourceInfo.DirectionalLightShadowViewInfoBuffer.get());
        srvDesc.NumElements = 1;
        srvDesc.Stride = scene->GetGPUResourceInfo().ShadowResourceInfo.DirectionalLightShadowViewInfoBuffer->GetRHI()->GetDesc().Size;
        srvDesc.Buffer = rdgB;
        out.LightShadowParameters.DirectionalLightViewInfos = builder.CreateBufferSRV("DirectionalLightShadowViewInfosSRV", srvDesc);
        
        auto& sceneSplitBuffer = scene->GetGPUResourceInfo().ShadowResourceInfo.SplitBuffer;
        auto splitBuffer = builder.RegisterExternalBuffer("SplitBuffer", sceneSplitBuffer.get());
        
        RenderCore::RenderGraphBufferSRVDesc desc;
        desc.Buffer = splitBuffer;
        desc.Format = ERHIFormat::R32_Float;
        desc.NumElements = splitBuffer->GetRHIBuffer()->GetDesc().Size / sizeof(float);
        desc.Stride = sizeof(float);
        auto splitBufferSRV = builder.CreateBufferSRV("SplitBufferSRV", desc);
		out.LightShadowParameters.SplitBuffer = splitBufferSRV;

        //填充距离场参数
		

    }

    void BuildGlobalDistanceFieldParameters(Scene* scene, RenderCore::RenderGraphBuilder& builder, SceneGlobalDistanceFieldParameters& out)
    {
        auto& distanceFieldResourceInfo = scene->GetGPUResourceInfo().DistanceFieldResourceInfo;
        out.BlockSize = Core::Float3(5, 5, 5);
        out.DistanceFieldVoxelSize = 5 / 64.0;
        out.DistanceSampler = RenderCore::GlobalSampler.get();
        out.GlobalDistanceFieldAtlasResolution = Core::UInt3(256, 256, 256);
        out.GlobalDistanceFieldGridSize = distanceFieldResourceInfo.StaticDistanceField.GetGridSize();
        out.GlobalDistanceFieldOrigin = distanceFieldResourceInfo.StaticDistanceField.GetOrigin();
        auto rdgGlobalDistenctAtlas = builder.RegisterExternalTexture("GlobalDistanceFieldAtlas", distanceFieldResourceInfo.StaticDistanceField.GetAtlas().GetAtlasTexture());
        out.GlobalDistanceFieldAtlas = rdgGlobalDistenctAtlas;
        out.AllocateBlockIndexInfos = distanceFieldResourceInfo.StaticDistanceField.GetBlockIndexBufferSRV();
    }

    void BuildEvnIBLLightParameters(Scene* scene, RenderCore::RenderGraphBuilder& builder, SceneEvnIBLLightParameters& out)
    {
        const auto& sceneLightResourceInfo = scene->GetGPUResourceInfo().LightResourceInfo;
        if (sceneLightResourceInfo.IBLDiffuseTexture != nullptr && sceneLightResourceInfo.IBLSpecularTexture != nullptr) {


            auto rdgT = builder.RegisterExternalTexture("IBLDiffuseTexture", sceneLightResourceInfo.IBLDiffuseTexture);
            out.IBLDiffuseMap = rdgT;
            rdgT = builder.RegisterExternalTexture("IBLSpecularTexture", sceneLightResourceInfo.IBLSpecularTexture);
            out.IBLSpecularMap = rdgT;
            rdgT = builder.RegisterExternalTexture("IBLLutTexture", GlobalIBLLutTexture.get());
            out.LinearClampIBLSampler = RenderCore::GlobalSampler.get();
            out.IBLLut = rdgT;
        }
        else {
            RenderCore::RenderGraphTextureDesc emptyCubeMapDesc;
            emptyCubeMapDesc.Type = RHI::ERHITextureType::TextureCube;
            emptyCubeMapDesc.Width = 1;
            emptyCubeMapDesc.Height = 1;
            emptyCubeMapDesc.ArraySize = 6;
            emptyCubeMapDesc.MipLevels = 1;
            emptyCubeMapDesc.Usage = RHI::ERHITextureCreateFlag::ShaderResource;
            emptyCubeMapDesc.Format = RHI::ERHIFormat::R8G8B8A8_UNorm;
            out.LinearClampIBLSampler = RenderCore::GlobalSampler.get();
            auto rdgT = builder.CreateTexture("EmptyCubeMap", emptyCubeMapDesc);
            out.IBLDiffuseMap = rdgT;
            out.IBLSpecularMap = rdgT;
            rdgT = builder.RegisterExternalTexture("IBLLutTexture", GlobalIBLLutTexture.get());
            out.IBLLut = rdgT;

        }
    }









}
