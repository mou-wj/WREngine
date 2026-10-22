// RenderGraphBuildTest.cpp
// 测试RenderGraph的构建和执行：实现一个简单的拷贝纹理pass
#include "RenderGraphBuilder.h"
#include "RenderGraphPass.h"
#include "RenderGraphResource.h"
#include "ShaderParameter.h"
#include "GlobalShader.h"
#include "RenderResource.h"
#include "ShaderCompiler.h"
#include "RHIApi.h"
#include "VulkanRHIApi.h"
#include "ToolShaders.h"
#include "Window.h"
#include <unordered_map>
using namespace RenderCore;
#include "TestBase.h"

namespace Test {

    namespace {
        std::unordered_map<const RHI::RHIViewableResource*, RHI::ERHIResourceAccess> GTrackedResourceAccess;
        std::unordered_map<const RHI::RHIViewableResource*, RHI::EQueueType> GTrackedResourceQueueTypes;

        void TransitionResource(
            RHI::RHIApi* api,
            RHI::RHICommandListBase& cmdList,
            RHI::RHIViewableResource* resource,
            RHI::ERHIResourceAccess currentAccess,
            RHI::ERHIResourceAccess targetAccess,
            EQueueType currentQueueType,
            EQueueType targetQueueType)
        {
            if (!api || !resource)
            {
                return;
            }

            if (currentAccess == targetAccess && currentQueueType == targetQueueType)
            {
                return;
            }


            std::vector<RHI::RHITransitionInfo> infos;
            if (auto* texture = dynamic_cast<RHI::RHITexture*>(resource))
            {
                infos.emplace_back(texture, currentAccess, targetAccess, currentQueueType,targetQueueType);
            }
            else if (auto* buffer = dynamic_cast<RHI::RHIBuffer*>(resource))
            {
                infos.emplace_back(buffer, currentAccess, targetAccess, currentQueueType, targetQueueType);
            }
            else
            {
                return;
            }

            char* transitionMem = new char[RHI::G_RHITransition_TotalSize];
            auto* transition = new(transitionMem) RHI::RHITransition();
            api->RHICreateTransition(transition, RHI::RHITransitionCreateInfo(RHI::ERHITransitionCreateFlags::None, std::move(infos)));

            cmdList.BeginTransitions({ transition });
            cmdList.EndTransitions({ transition });

            GTrackedResourceAccess[resource] = targetAccess;
            api->RHIReleaseTransition(transition);
            delete[] transitionMem;
        }
    

        void TransitionResource(
            RHI::RHIApi* api,
            RHI::RHICommandListBase& cmdList,
            RHI::RHIViewableResource* resource,
            RHI::ERHIResourceAccess targetAccess,
            EQueueType targetQueueType)
        {
            if (!api || !resource)
            {
                return;
            }

            const auto it = GTrackedResourceAccess.find(resource);
            const RHI::ERHIResourceAccess currentAccess =
                it != GTrackedResourceAccess.end() ? it->second : RHI::ERHIResourceAccess::Unknown;
			const auto queueIt = GTrackedResourceQueueTypes.find(resource);
			const EQueueType currentQueueType =
				queueIt != GTrackedResourceQueueTypes.end() ? queueIt->second : EQueueType::Graphics;
			TransitionResource(api, cmdList, resource, currentAccess, targetAccess, currentQueueType, targetQueueType
            );
        }
    }






class RenderGraphBuildTest : public TestBase {
public:
    int FrameWidth = 0;
    int FrameHeight = 0;
    int WindiwWidth = 512;
    int WindowHeight = 512;
    RHI::RHISamplerSP samplerSP;
    void Setup() override {
        RHI::GRHIApi = CreateTestRHIApi();
        RHI::GRHIApi->Init();
        auto* api = RHI::GRHIApi;
        if (!api)
        {
            return;
        }
        InitSwapchain();
        InitShaderMap();

        InitPipelines();
        InitGlobalResources();

        // 只保存静态描述
        texDesc.Width = FrameWidth;
        texDesc.Height = FrameHeight;
        texDesc.Format = ERHIFormat::R8G8B8A8_UNorm;
        texDesc.Usage = ERHITextureCreateFlag::ShaderResource | ERHITextureCreateFlag::TransferSrc | ERHITextureCreateFlag::TransferDest;
    }

    

    void InitShaderMap()
    {
        GShaderMap.Initialize();

    }

	void InitPipelines()
	{
		//获取第0好blittextureshader变体
        auto blitShaderType = ShaderType::GetRegisterMap()[ShaderType::EShaderTypeFlag::Global]["BlitTextureCS"];
        blitTextureShader0 = GShaderMap.GetShader(blitShaderType,0);
        if (!blitTextureShader0) {
            assert(0);
        }
       
        // 创建计算管线状态
        RHI::RHIComputePipelineStateDesc computeDesc;
        computeDesc.ComputeShader = dynamic_cast<RHIComputeShader*>(blitTextureShader0->GetRHIShader());
        ComputePipelineState = RHI::GRHIApi->CreateComputePipelineState(computeDesc);
	}

    void InitSwapchain()
    {
        // 
        Window = SlateCore::WindowFactory::CreateWindowSP(WindiwWidth, WindowHeight, "RenderGraphBuildTest");
        Window->Show();
        // 创建交换链
        void* windowHandle = Window->GetNativeHandle(); // 获取窗口句柄的函数
        uint32_t width = WindiwWidth;
        uint32_t height = WindowHeight;
        ERHIFormat format = ERHIFormat::B8G8R8A8_UNorm;
        Swapchain = RHI::GRHIApi->CreateSwapchain(windowHandle, width, height, format);
        auto frameSize = Window->GetFramebufferSize();
        FrameWidth = frameSize.X;
        FrameHeight = frameSize.Y;
    }

    void InitGlobalResources() 
    {
    }

    void Run() override {
        using namespace RHI;
        using namespace RenderCore;

        auto* api = GRHIApi;
        constexpr int kFrameCount = 1000;

        RHI::RHISamplerDesc samplerDesc{};
        samplerSP = api->CreateSampler(samplerDesc);

        // =========================================
        // 1️⃣ 创建随机源纹理（只做一次）
        // =========================================
        RHITextureDesc randomTexDesc = texDesc;
        texDesc.InitialQueueType = EQueueType::Compute;
        randomTexDesc.Usage |= ERHITextureCreateFlag::TransferDest | ERHITextureCreateFlag::ShaderResource;
        randomTex = std::make_shared<RenderTexture>(randomTexDesc);
        randomTex->InitRHIResource();
        std::vector<uint32_t> randomData(randomTexDesc.Width * randomTexDesc.Height);
        for (int i = 0; i < randomData.size(); i++) {
            randomData[i] = rand();
        }

        {
            auto* queue = api->GetQueue(EQueueType::Compute);
            auto* ctx = queue->AcquireCommandContext();
            RHIComputeCommandList cmd(dynamic_cast<RHIComputeContex*>(ctx));
            cmd.SetImmediate(true);
            cmd.Begin();

            TransitionResource(api, cmd, randomTex.get()->GetRHI(), ERHIResourceAccess::TransferDest,EQueueType::Compute);

            api->UpdateTexture(cmd, randomTex.get()->GetRHI(), randomData.data(),
                RHIUpdateTextureRegion::Create2DRegion(randomTexDesc.Width, randomTexDesc.Height));

            TransitionResource(api, cmd, randomTex.get()->GetRHI(), ERHIResourceAccess::SRV,EQueueType::Compute);

            cmd.End();

            queue->ExecuteContext({ ctx }, {});
            randomTex->GetTracker().UpdateSubresourceAccess(RHI::RHISubresourceRange{}, ERHIResourceAccess::SRV);
        }

        // =========================================
        // 2️⃣ 每帧执行 RDG
        // =========================================
        for (int frame = 0; frame < kFrameCount; ++frame)
        {
            // -------------------------------------
            // 从 pool 取 RT
            // -------------------------------------
            PoolRenderTargetDesc rtDesc{};
            rtDesc.Width = texDesc.Width;
            rtDesc.Height = texDesc.Height;
            rtDesc.Format = texDesc.Format;
            rtDesc.Usage = ERHITextureCreateFlag::UAV |
                ERHITextureCreateFlag::TransferSrc |
                ERHITextureCreateFlag::TransferDest;

            auto renderTarget = GRenderTargetPool.GetFreeRenderTarget(rtDesc);
            renderTarget->MarkUsed(true);
            // -------------------------------------
            // RDG 构建
            // -------------------------------------
            RenderGraphBuilder builder;

            // 👉 注册 external
            auto rdgSrc = builder.RegisterExternalTexture("SrcTex", randomTex.get());
            auto rdgDst = builder.RegisterExternalTexture("DstTex", renderTarget.get());

            // 👉 创建 SRV / UAV
            RenderGraphTextureUAVDesc dstUAVDesc{};
            dstUAVDesc.Texture = rdgDst;
            dstUAVDesc.Format = rtDesc.Format;
            auto dstUAV = builder.CreateTextureUAV("DstUAV", dstUAVDesc);

            // 👉 参数
            auto* params = builder.AllocateParameter<BlitTextureParameters>();
            BlitTextureParameters* paramsPtr = params;

            params->SrcTexture = rdgSrc;
            params->DstTexture = dstUAV;
            params->SrcSampler = samplerSP.get();
            params->SrcSize = { (float)texDesc.Width, (float)texDesc.Height };
            params->SrcInvSize = { 1.0f / texDesc.Width, 1.0f / texDesc.Height };
            params->DstSize = params->SrcSize;
            params->DstInvSize = params->SrcInvSize;
            params->SrcMipLevel = 0;

            // -------------------------------------
            // 添加 pass
            // -------------------------------------
            builder.AddPass<BlitTextureParameters>(
                "BlitPass",
                BlitTextureParameters::GetMetaData(),
                params,
                EPassFlag::Compute,
                [=](RHI::RHICommandListBase& RHICmdList)
                {
                    auto& cmd = static_cast<RHI::RHIComputeCommandList&>(RHICmdList);

                    cmd.SetComputePipelineState(ComputePipelineState.get());
                    SetShaderParameters(cmd, blitTextureShader0, params);
                    cmd.Dispatch(
                        texDesc.Width / 8,
                        texDesc.Height / 8,
                        1);
                }
            );

            // -------------------------------------
            // 执行 RDG
            // -------------------------------------
            builder.Execute();

            // =========================================
            // 3️⃣ Blit 到 swapchain
            // =========================================
            auto slot = Swapchain->AcquireNextSlot();
            auto* backTexture = slot.Texture;

            auto* computeQueue = api->GetQueue(EQueueType::Compute);
            auto* ctx = computeQueue->AcquireCommandContext();
            RHIComputeCommandList cmd(dynamic_cast<RHIComputeContex*>(ctx));
            cmd.SetImmediate(true);
            cmd.Begin();
            
            TransitionResource(api, cmd, renderTarget->GetRHI(), renderTarget->GetTracker().GetSubresourceAccess(RHI::RHISubresourceRange{}), ERHIResourceAccess::TransferSrc,EQueueType::Compute, EQueueType::Compute);


            TransitionResource(api, cmd, backTexture, ERHIResourceAccess::TransferDest, EQueueType::Compute);
            RHI::RHIBlitTextureDesc blit{};
            blit.SrcRegion.Width = texDesc.Width;
            blit.SrcRegion.Height = texDesc.Height;
            blit.DstRegion.Width = FrameWidth;
            blit.DstRegion.Height = FrameHeight;
            cmd.BlitTexture(renderTarget->GetRHI(), backTexture, blit);
            
            TransitionResource(api, cmd, backTexture, ERHIResourceAccess::Present, EQueueType::Compute);
            
            cmd.End();
            std::vector<RHI::RHIWaitInfo> waitInfos;

            RHI::RHIWaitInfo renderTargetFinish;
            renderTargetFinish.QueueType = renderTarget->GetTracker().GetLastAccessFence().QueueType;
            renderTargetFinish.Value = renderTarget->GetTracker().GetLastAccessFence().Value;
            waitInfos.push_back(renderTargetFinish);
            if (slot.ReadySync)
            {
                waitInfos.push_back({ slot.ReadySync, EQueueType::Graphics, 0,RHI::ERHIPipelineStage::ColorAttachmentOutput });
            }
            RHI::RHIFence fence;
            fence.QueueType = EQueueType::Compute;
            fence.Value = computeQueue->ExecuteContext({ ctx }, waitInfos);

            // 👉 更新 tracker（关键！）
            renderTarget->GetTracker().UpdateLastAccessFence(fence);
            renderTarget->GetTracker().UpdateSubresourceAccess(RHI::RHISubresourceRange{}, ERHIResourceAccess::TransferSrc);
            RHI::RHIWaitInfo presentWait;
            presentWait.QueueType = fence.QueueType;
            presentWait.Value = fence.Value;
            api->GetPresentExecutor()->Present(Swapchain.get(), { presentWait });
            renderTarget->MarkUsed(false);
        }

        std::cout << "RenderGraph CopyTexturePass test finished for " << kFrameCount << " frames." << std::endl;
    }

    void Teardown() override {
        // 资源清理如有需要可补充
        GRenderTargetPool.Clear();
        GShaderMap.Clear();
		ComputePipelineState.reset();
        Swapchain.reset();
        Window.reset();
		samplerSP.reset();
        randomTex.reset();
        
        delete RHI::GRHIApi;
    }

private:
    RenderGraphTextureDesc texDesc;
    RHI::RHIComputePipelineStateSP ComputePipelineState;
    RHI::RHISwapchainSP Swapchain;
    SlateCore::WindowSP Window;
    RenderCore::Shader* blitTextureShader0;
    RenderTextureSP randomTex;
};

REGISTER_RENDER_TEST("RenderGraphBuildTest", RenderGraphBuildTest);
}
