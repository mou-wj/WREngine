#include "RenderModule.h"
#include "DefferedSceneRenderer.h"
#include "ForwardSceneRenderer.h"
#include "Scene.h"
#include "RenderThread.h"
#include "ScreenPass.h"
#include "SceneInterface.h"
#include "Viewport.h"
#include "RenderResource.h"
#include "MeshMaterialShader.h"
#include "MateiralShader.h"
#include "GlobalShader.h"
#include "ShaderCompiler.h"
#include "RHIPipelineStateCache.h"
#include "StaticMeshComponent.h"
#include "StaticMesh.h"
#include "StaticMeshResources.h"
#include "PrimitiveComponent.h"
#include "Shape.h"
#include "GBufferInfo.h"
#include "Common.h"
#include "HintRender.h"

#include <algorithm>
#include <vector>

using namespace RenderCore;
using namespace Engine;

namespace Renderer {



    SceneRendererSP CreateSceneRenderer() {
        return std::make_shared<DefferedSceneRenderer>();
    }

    RenderModule::RenderModule() = default;
    RenderModule::~RenderModule() = default;

    void RenderModule::StartupModule()
    {
        // Start the render thread.
        RenderCore::StartRenderThread();
		InitGlobalRenderResource();
        GTransientResourceAllocator.InitRHI();
        GMeshMaterialShaderMap.Initialize();
        GMaterialShaderMap.Initialize();
        GShaderMap.Initialize();
        //初始化ibl lut
        InitIBLLut();
        Engine::InitializeShapeStaticMeshes();
        m_bLoaded = true;
    }

    void RenderModule::ShutdownModule()
    {
        m_bLoaded = false;
        GRenderTargetPool.Clear();
        GTransientResourceAllocator.ReleaseRHI();
		GMeshMaterialShaderMap.Clear();
        GMaterialShaderMap.Clear();
		GShaderMap.Clear();
        ReleaseGlobalRenderResource();
        GlobalIBLLutTexture.reset();
        Engine::ReleaseShapeStaticMeshes();
        // Stop the render thread.
        RenderCore::StopRenderThread();
		
    }

    bool RenderModule::IsLoaded() const
    {
        return m_bLoaded;
    }


    void RenderModule::BeginRender(
        Engine::SceneViewFamily* views)
    {

        const auto sceneSetting = views->Scene->GetSceneSetting();
        RenderCore::RenderGraphBuilder builder;
        auto colorTex = views->RenderTarget->GetRenderTarget();
        auto sceneColorTexture = builder.RegisterExternalTexture("RenderTarget", colorTex);
        PoolRenderTargetDesc depthDesc;
        depthDesc.Format = RHI::ERHIFormat::D32_Float;
        depthDesc.Width = colorTex->GetRHI()->GetDesc().Width;
		depthDesc.Height = colorTex->GetRHI()->GetDesc().Height;
        depthDesc.Usage = RHI::ERHITextureCreateFlag::DepthStencil | RHI::ERHITextureCreateFlag::ShaderResource;
        auto poolDepthTarget = GRenderTargetPool.GetFreeRenderTarget(depthDesc);
		poolDepthTarget->MarkUsed(true);
		auto depthText = builder.RegisterExternalTexture("DepthTarget", poolDepthTarget.get());
        uint32_t maxMipLevel = std::max<uint32_t>(1, (uint32_t)std::log2(std::max<uint32_t>(1, depthDesc.Width)));
        depthDesc.MipLevels = maxMipLevel;
        depthDesc.Usage |= RHI::ERHITextureCreateFlag::UAV;
        auto poolDepthPyramidTarget = GRenderTargetPool.GetFreeRenderTarget(depthDesc);
        poolDepthPyramidTarget->MarkUsed(true);
        auto preDepthText = builder.RegisterExternalTexture("DepthTargetPyramid", poolDepthPyramidTarget.get());
        //构建Gbuffer
        auto gbufferInfo = CreateGBufferInfo({});
        std::vector<RenderCore::RenderGraphTextureRef> gbuffers;
        std::vector<PooledRenderTarget*> gbufferTargets;
        for (int i = 0; i < gbufferInfo.NumTargets; i++) {
            PoolRenderTargetDesc gbufferDesc;
            gbufferDesc.Format = gbufferInfo.Targets[i].TargetFormat;
            gbufferDesc.Width = colorTex->GetRHI()->GetDesc().Width;
            gbufferDesc.Height = colorTex->GetRHI()->GetDesc().Height;
            gbufferDesc.Usage = RHI::ERHITextureCreateFlag::RenderTarget | RHI::ERHITextureCreateFlag::ShaderResource;
            auto poolGbufferTarget = GRenderTargetPool.GetFreeRenderTarget(gbufferDesc);
            poolGbufferTarget->MarkUsed(true);
            gbufferTargets.push_back(poolGbufferTarget.get());
            auto gbufferText = builder.RegisterExternalTexture(std::string("GBufferTarget") + std::to_string(i), poolGbufferTarget.get());
            gbuffers.push_back(gbufferText);
        }
		LocalVertexFactoryInstanceManager::Get().BeginUpdateGPUResources();
        auto sceneRenderer = CreateSceneRenderer();
        sceneRenderer->Scene =  dynamic_cast<Scene*>(views->Scene);
        sceneRenderer->Views = views;
        sceneRenderer->SceneTextures.SceneColor = sceneColorTexture;
        sceneRenderer->SceneTextures.SceneDepth = depthText;
        sceneRenderer->SceneTextures.SceneDepthPyramid = preDepthText;
        sceneRenderer->SceneTextures.GBufferA = gbuffers[0];
        sceneRenderer->SceneTextures.GBufferB = gbuffers[1];
        sceneRenderer->SceneTextures.GBufferC = gbuffers[2];
		sceneRenderer->BuildSceneLightShadowMap(sceneRenderer->Scene,builder);
        sceneRenderer->UploadShadowMapInfo(sceneRenderer->Scene,builder);
        sceneRenderer->Build(builder);
        // Append view-local overlays to the same graph after all scene passes.
        // Each extension must declare its texture access in its pass parameters.
        for (const auto& view : views->GetViews()) {
            if (sceneSetting.bDrawLightHints && sceneSetting.LightHintTarget) {
                HintRender::AddLightPass(builder, view,
                    sceneRenderer->SceneTextures.SceneColor,
                    sceneRenderer->SceneTextures.SceneDepth, *sceneSetting.LightHintTarget);
            }
            for (const auto& extension : view.DrawExtensions) {
                if (extension.Callback) {
                    extension.Callback(builder, view,
                        sceneRenderer->SceneTextures.SceneColor,
                        sceneRenderer->SceneTextures.SceneDepth);
                }
            }
        }
        // The grid blends over the completed scene, including view extensions.
        // Attachment declarations keep it after their color/depth accesses in RDG.
        if (sceneSetting.bDrawGrid)
            for (const auto& view : views->GetViews())
                HintRender::AddGridPass(builder,view,
                    sceneRenderer->SceneTextures.SceneColor,
                    sceneRenderer->SceneTextures.SceneDepth);
        LocalVertexFactoryInstanceManager::Get().UpdateGPUResources();
        sceneRenderer->Scene->UpdateGlobalDistanceFieldIfNeeded();
        builder.Execute();
        poolDepthTarget->MarkUsed(false);
        poolDepthPyramidTarget->MarkUsed(false);
        for (auto gTarget : gbufferTargets) {
            gTarget->MarkUsed(false);
        }
    }
    Engine::SceneInterface* RenderModule::AllocateScene() {
        return new Scene();
    }
    void RenderModule::PreComputeIBL(RenderCore::RenderTexture* inHdrTexture, RenderCore::RenderTexture* outDiffuseIbl, RenderCore::RenderTexture* outSpecularIbl) {
        if (!inHdrTexture || !outDiffuseIbl || !outSpecularIbl) return;
        IBLPrecomputeParameters params;
        uint32_t roughnessCount = outDiffuseIbl->GetRHI()->GetDesc().MipLevels;
        Core::Int2 outsize;
        outsize.X = outDiffuseIbl->GetRHI()->GetDesc().Width;
        outsize.Y = outDiffuseIbl->GetRHI()->GetDesc().Height;
        
        params.EnvironmentMapParameter.SampleCount = 4;


        TransitionTextureImmediate(RHI::GRHIApi, inHdrTexture,  RHI::ERHIResourceAccess::SRV, RHI::EQueueType::Compute);
        TransitionTextureImmediate(RHI::GRHIApi, outDiffuseIbl, RHI::ERHIResourceAccess::UAV, RHI::EQueueType::Compute);
        TransitionTextureImmediate(RHI::GRHIApi, outSpecularIbl, RHI::ERHIResourceAccess::UAV, RHI::EQueueType::Compute);
        params.EnvironmentMapParameter.EnvSampler = RenderCore::GlobalSampler.get();
        params.EnvironmentMapParameter.EnvironmentMap = inHdrTexture->GetRHI();


        // 获取 shader 实例（按变体 id）
        auto& gShaderMap = RenderCore::GShaderMap;
        auto shaderType = ShaderType::GetRegisterMap()[ShaderType::EShaderTypeFlag::Global]["IBLPrecomputeCS"];
        
        

        // 创建或获取 compute pipeline state

        
        auto computeContex = GRHIApi->GetQueue(RHI::EQueueType::Compute)->AcquireCastedCommandContext<RHI::RHIComputeContex>();
        RHI::RHIComputeCommandList cmdlist(computeContex);
        cmdlist.SetImmediate(true);
        cmdlist.Begin();
        float roughnessStep = 1 / 5.0;
        //计算镜面反射预计算贴图
		for (uint32_t mip = 0; mip < 6; mip++) {

            params.EnvironmentMapParameter.Roughness = mip * roughnessStep;
            RenderCore::Shader* shader = gShaderMap.GetShader(shaderType, 0);
            if (!shader) return;
            RHI::RHIComputePipelineStateDesc computeDesc;
            computeDesc.ComputeShader = dynamic_cast<RHI::RHIComputeShader*>(shader->GetRHIShader());
            auto pipelineState = RHI::RHIPipelineStateCache::GetOrCreateComputePipelineState(computeDesc);
            cmdlist.SetComputePipelineState(pipelineState);
            RHI::RHITexUAVCreateInfo specEnvUavDesc;
            specEnvUavDesc.Format = outSpecularIbl->GetRHI()->GetDesc().Format;
            specEnvUavDesc.ArraySize = outSpecularIbl->GetRHI()->GetDesc().ArraySize;
			specEnvUavDesc.FirstMipSlice = mip;
            specEnvUavDesc.ViewType = RHI::ERHITextureViewType::TextureView2DArray;
            auto specuav = outSpecularIbl->GetViewCache().GetOrCreateUAV(outSpecularIbl->GetRHI(), specEnvUavDesc);
            
            params.EnvironmentMapParameter.OutputSpecularParam.OutputSpecularTexture = specuav;

            params.OutputSize = outsize / pow(2, mip);
            // 设置 shader 参数
            SetShaderParameters(cmdlist, shader, IBLPrecomputeParameters::GetMetaData(), &params);
            // Dispatch
            

            int width = params.OutputSize.X;
            int height = params.OutputSize.Y;
            uint32_t groupX = (width + 15) / 16;
            uint32_t groupY = (height + 15) / 16;
            cmdlist.Dispatch(groupX, groupY, 6);
            
        }
		//计算漫反射预计算贴图
        RHI::RHITexUAVCreateInfo difEnvUavDesc;
        difEnvUavDesc.Format = outDiffuseIbl->GetRHI()->GetDesc().Format;
        difEnvUavDesc.ArraySize = outDiffuseIbl->GetRHI()->GetDesc().ArraySize;
        difEnvUavDesc.ViewType = RHI::ERHITextureViewType::TextureView2DArray;
        auto difuav = outDiffuseIbl->GetViewCache().GetOrCreateUAV(outDiffuseIbl->GetRHI(), difEnvUavDesc);
        params.EnvironmentMapParameter.OutputDiffuseParam.OutputDiffuseTexture = difuav;
        RenderCore::Shader* shader = gShaderMap.GetShader(shaderType, 1);
        if (!shader) return;
        RHI::RHIComputePipelineStateDesc computeDesc;
        computeDesc.ComputeShader = dynamic_cast<RHI::RHIComputeShader*>(shader->GetRHIShader());
        auto pipelineState = RHI::RHIPipelineStateCache::GetOrCreateComputePipelineState(computeDesc);
        cmdlist.SetComputePipelineState(pipelineState);
        params.OutputSize = outsize;
        SetShaderParameters(cmdlist, shader, IBLPrecomputeParameters::GetMetaData(), &params);
        // Dispatch
        int width = params.OutputSize.X;
        int height = params.OutputSize.Y;
        uint32_t groupX = (width + 15) / 16;
		uint32_t groupY = (height + 15) / 16;
        cmdlist.Dispatch(groupX, groupY, 6);
        //生成diffuse的mipmap

        cmdlist.End();
        RHI::RHIFence fence;
		fence.QueueType = RHI::EQueueType::Compute;
        fence.Value = GRHIApi->GetQueue(RHI::EQueueType::Compute)->ExecuteContext(computeContex);
        inHdrTexture->GetTracker().UpdateLastAccessFence(fence);
        outDiffuseIbl->GetTracker().UpdateLastAccessFence(fence);
        outSpecularIbl->GetTracker().UpdateLastAccessFence(fence);
    }

    void RenderModule::InitIBLLut() {
        RHI::RHITextureDesc desc;
        desc.Width = 512;
        desc.Height = 512;
        desc.Format = RHI::ERHIFormat::R32G32_Float;
        desc.Usage = RHI::ERHITextureCreateFlag::ShaderResource | RHI::ERHITextureCreateFlag::UAV | RHI::ERHITextureCreateFlag::TransferSrc;
        GlobalIBLLutTexture = std::make_shared<RenderTexture>(desc);
        GlobalIBLLutTexture->InitRHIResource();

        IBLPrecomputeParameters params;
        Core::Int2 outsize;
        outsize.X = GlobalIBLLutTexture->GetRHI()->GetDesc().Width;
        outsize.Y = GlobalIBLLutTexture->GetRHI()->GetDesc().Height;

        params.EnvironmentMapParameter.SampleCount = 4;


        TransitionTextureImmediate(RHI::GRHIApi, GlobalIBLLutTexture.get(),  RHI::ERHIResourceAccess::UAV, RHI::EQueueType::Compute);
        RHI::RHITexUAVCreateInfo lutUAVDesc;
        lutUAVDesc.Format = GlobalIBLLutTexture->GetRHI()->GetDesc().Format;
        lutUAVDesc.ArraySize = GlobalIBLLutTexture->GetRHI()->GetDesc().ArraySize;
        auto uav = GlobalIBLLutTexture->GetViewCache().GetOrCreateUAV(GlobalIBLLutTexture->GetRHI(), lutUAVDesc);
        params.BRDFParameter.OutputBRDFLUT = uav;
        // 获取 shader 实例（按变体 id）
        auto& gShaderMap = RenderCore::GShaderMap;
        auto shaderType = ShaderType::GetRegisterMap()[ShaderType::EShaderTypeFlag::Global]["IBLPrecomputeCS"];

        // 创建或获取 compute pipeline state
        auto computeContex = GRHIApi->GetQueue(RHI::EQueueType::Compute)->AcquireCastedCommandContext<RHI::RHIComputeContex>();
        RHI::RHIComputeCommandList cmdlist(computeContex);
        cmdlist.SetImmediate(true);

        //计算lut预计算贴图
        RenderCore::Shader* shader = gShaderMap.GetShader(shaderType, 2);
        if (!shader) return;
        RHI::RHIComputePipelineStateDesc computeDesc;
        computeDesc.ComputeShader = dynamic_cast<RHI::RHIComputeShader*>(shader->GetRHIShader());
        auto pipelineState = RHI::RHIPipelineStateCache::GetOrCreateComputePipelineState(computeDesc);
        cmdlist.SetComputePipelineState(pipelineState);
        params.OutputSize = outsize;
        SetShaderParameters(cmdlist, shader, IBLPrecomputeParameters::GetMetaData(), &params);
        // Dispatch
        
        int width = params.OutputSize.X;
        int height = params.OutputSize.Y;
        uint32_t groupX = (width + 15) / 16;
        uint32_t groupY = (height + 15) / 16;
        cmdlist.Dispatch(groupX, groupY, 1);
        //生成diffuse的mipmap
        cmdlist.End();
        RHI::RHIFence fence;
        fence.QueueType = RHI::EQueueType::Compute;
        fence.Value = GRHIApi->GetQueue(RHI::EQueueType::Compute)->ExecuteContext(computeContex);
        GlobalIBLLutTexture->GetTracker().UpdateLastAccessFence(fence);
        //写出测试
        //SaveTexture(GlobalIBLLutTexture.get(), "IBLLut.png",0,0);
    }


	IMPLEMENT_SIMPLE_MODULE(RenderModule, "Renderer");


}
