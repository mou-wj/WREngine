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
        //������Ⱦ�߳�
        RenderCore::StartRenderThread();
		InitGlobalRenderResource();
        GTransientResourceAllocator.InitRHI();
        GMeshMaterialShaderMap.Initialize();
        GMaterialShaderMap.Initialize();
        GShaderMap.Initialize();
        //初始化ibl lut
        InitIBLLut();
        Engine::InitializeShapeStaticMeshes();
        bLoaded = true;
    }

    void RenderModule::ShutdownModule()
    {
        bLoaded = false;
        GRenderTargetPool.Clear();
        GTransientResourceAllocator.ReleaseRHI();
		GMeshMaterialShaderMap.Clear();
        GMaterialShaderMap.Clear();
		GShaderMap.Clear();
        ReleaseGlobalRenderResource();
        GlobalIBLLutTexture.reset();
        Engine::ReleaseShapeStaticMeshes();
        // �ر���Ⱦ�߳�
        RenderCore::StopRenderThread();
		
    }

    bool RenderModule::IsLoaded() const
    {
        return bLoaded;
    }


    void RenderModule::BeginRender(
        Engine::SceneViewFamily* Views)
    {

        RenderCore::RenderGraphBuilder builder;
        auto ColorTex = Views->RenderTarget->GetRenderTarget();
        auto sceneColorTexture = builder.RegisterExternalTexture("RenderTarget", ColorTex);
        PoolRenderTargetDesc depthDesc;
        depthDesc.Format = RHI::ERHIFormat::D32_Float;
        depthDesc.Width = ColorTex->GetRHI()->GetDesc().Width;
		depthDesc.Height = ColorTex->GetRHI()->GetDesc().Height;
        depthDesc.Usage = RHI::ERHITextureCreateFlag::DepthStencil | RHI::ERHITextureCreateFlag::ShaderResource;
        auto PoolDepthTarget = GRenderTargetPool.GetFreeRenderTarget(depthDesc);
		PoolDepthTarget->MarkUsed(true);
		auto depthText = builder.RegisterExternalTexture("DepthTarget", PoolDepthTarget.get());
        uint32_t maxMipLevel = std::max<uint32_t>(1, (uint32_t)std::log2(std::max<uint32_t>(1, depthDesc.Width)));
        depthDesc.MipLevels = maxMipLevel;
        depthDesc.Usage |= RHI::ERHITextureCreateFlag::UAV;
        auto PoolDepthPyramidTarget = GRenderTargetPool.GetFreeRenderTarget(depthDesc);
        PoolDepthPyramidTarget->MarkUsed(true);
        auto preDepthText = builder.RegisterExternalTexture("DepthTargetPyramid", PoolDepthPyramidTarget.get());
        //构建Gbuffer
        auto gbufferInfo = CreateGBufferInfo({});
        std::vector<RenderCore::RenderGraphTextureRef> gbuffers;
        std::vector<PooledRenderTarget*> GbufferTargets;
        for (int i = 0; i < gbufferInfo.NumTargets; i++) {
            PoolRenderTargetDesc gbufferDesc;
            gbufferDesc.Format = gbufferInfo.Targets[i].TargetFormat;
            gbufferDesc.Width = ColorTex->GetRHI()->GetDesc().Width;
            gbufferDesc.Height = ColorTex->GetRHI()->GetDesc().Height;
            gbufferDesc.Usage = RHI::ERHITextureCreateFlag::RenderTarget | RHI::ERHITextureCreateFlag::ShaderResource;
            auto PoolGbufferTarget = GRenderTargetPool.GetFreeRenderTarget(gbufferDesc);
            PoolGbufferTarget->MarkUsed(true);
            GbufferTargets.push_back(PoolGbufferTarget.get());
            auto gbufferText = builder.RegisterExternalTexture(std::string("GBufferTarget") + std::to_string(i), PoolGbufferTarget.get());
            gbuffers.push_back(gbufferText);
        }
		LocalVertexFactoryInstanceManager::Get().BeginUpdateGPUResources();
        auto sceneRenderer = CreateSceneRenderer();
        sceneRenderer->Scene =  dynamic_cast<Scene*>(Views->Scene);
        sceneRenderer->Views = Views;
        sceneRenderer->SceneTextures.SceneColor = sceneColorTexture;
        sceneRenderer->SceneTextures.SceneDepth = depthText;
        sceneRenderer->SceneTextures.SceneDepthPyramid = preDepthText;
        sceneRenderer->SceneTextures.GBufferA = gbuffers[0];
        sceneRenderer->SceneTextures.GBufferB = gbuffers[1];
        sceneRenderer->SceneTextures.GBufferC = gbuffers[2];
		sceneRenderer->BuildSceneLightShadowMap(sceneRenderer->Scene,builder);
        sceneRenderer->UploadShadowMapInfo(sceneRenderer->Scene,builder);
        sceneRenderer->Build(builder);
        LocalVertexFactoryInstanceManager::Get().UpdateGPUResources();
        sceneRenderer->Scene->UpdateGlobalDistanceFieldIfNeeded();
        builder.Execute();
        PoolDepthTarget->MarkUsed(false);
        PoolDepthPyramidTarget->MarkUsed(false);
        for (auto gTarget : GbufferTargets) {
            gTarget->MarkUsed(false);
        }
    }
    Engine::SceneInterface* RenderModule::AllocateScene() {
        return new Scene();
    }
    void RenderModule::PreComputeIBL(RenderCore::RenderTexture* InHDRTexture, RenderCore::RenderTexture* OutDiffuseIBL, RenderCore::RenderTexture* OutSpecularIBL) {
        if (!InHDRTexture || !OutDiffuseIBL || !OutSpecularIBL) return;
        IBLPrecomputeParameters params;
        uint32_t roughnessCount = OutDiffuseIBL->GetRHI()->GetDesc().MipLevels;
        Core::Int2 outsize;
        outsize.X = OutDiffuseIBL->GetRHI()->GetDesc().Width;
        outsize.Y = OutDiffuseIBL->GetRHI()->GetDesc().Height;
        
        params.EnvironmentMapParameter.SampleCount = 4;


        TransitionTextureImmediate(RHI::GRHIApi, InHDRTexture,  RHI::ERHIResourceAccess::SRV, RHI::EQueueType::Compute);
        TransitionTextureImmediate(RHI::GRHIApi, OutDiffuseIBL, RHI::ERHIResourceAccess::UAV, RHI::EQueueType::Compute);
        TransitionTextureImmediate(RHI::GRHIApi, OutSpecularIBL, RHI::ERHIResourceAccess::UAV, RHI::EQueueType::Compute);
        params.EnvironmentMapParameter.EnvSampler = RenderCore::GlobalSampler.get();
        params.EnvironmentMapParameter.EnvironmentMap = InHDRTexture->GetRHI();


        // 获取 shader 实例（按变体 id）
        auto& GShaderMap = RenderCore::GShaderMap;
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
            RenderCore::Shader* shader = GShaderMap.GetShader(shaderType, 0);
            if (!shader) return;
            RHI::RHIComputePipelineStateDesc computeDesc;
            computeDesc.ComputeShader = dynamic_cast<RHI::RHIComputeShader*>(shader->GetRHIShader());
            auto pipelineState = RHI::RHIPipelineStateCache::GetOrCreateComputePipelineState(computeDesc);
            cmdlist.SetComputePipelineState(pipelineState);
            RHI::RHITexUAVCreateInfo SpecEnvUAVDesc;
            SpecEnvUAVDesc.Format = OutSpecularIBL->GetRHI()->GetDesc().Format;
            SpecEnvUAVDesc.ArraySize = OutSpecularIBL->GetRHI()->GetDesc().ArraySize;
			SpecEnvUAVDesc.FirstMipSlice = mip;
            SpecEnvUAVDesc.ViewType = RHI::ERHITextureViewType::TextureView2DArray;
            auto specuav = OutSpecularIBL->GetViewCache().GetOrCreateUAV(OutSpecularIBL->GetRHI(), SpecEnvUAVDesc);
            
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
        RHI::RHITexUAVCreateInfo DifEnvUAVDesc;
        DifEnvUAVDesc.Format = OutDiffuseIBL->GetRHI()->GetDesc().Format;
        DifEnvUAVDesc.ArraySize = OutDiffuseIBL->GetRHI()->GetDesc().ArraySize;
        DifEnvUAVDesc.ViewType = RHI::ERHITextureViewType::TextureView2DArray;
        auto difuav = OutDiffuseIBL->GetViewCache().GetOrCreateUAV(OutDiffuseIBL->GetRHI(), DifEnvUAVDesc);
        params.EnvironmentMapParameter.OutputDiffuseParam.OutputDiffuseTexture = difuav;
        RenderCore::Shader* shader = GShaderMap.GetShader(shaderType, 1);
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
        InHDRTexture->GetTracker().UpdateLastAccessFence(fence);
        OutDiffuseIBL->GetTracker().UpdateLastAccessFence(fence);
        OutSpecularIBL->GetTracker().UpdateLastAccessFence(fence);
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
        auto& GShaderMap = RenderCore::GShaderMap;
        auto shaderType = ShaderType::GetRegisterMap()[ShaderType::EShaderTypeFlag::Global]["IBLPrecomputeCS"];

        // 创建或获取 compute pipeline state
        auto computeContex = GRHIApi->GetQueue(RHI::EQueueType::Compute)->AcquireCastedCommandContext<RHI::RHIComputeContex>();
        RHI::RHIComputeCommandList cmdlist(computeContex);
        cmdlist.SetImmediate(true);

        //计算lut预计算贴图
        RenderCore::Shader* shader = GShaderMap.GetShader(shaderType, 2);
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