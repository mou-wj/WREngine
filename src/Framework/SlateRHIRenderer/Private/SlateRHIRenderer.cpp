#include "SlateRHIRenderer.h"
#include "RHIApi.h"
#include "RenderThread.h"
#include "RenderResource.h"
#include "SlateViewport.h"
#include "Log.h"
#include "Common.h"
#include "Timer.h"
using namespace RHI;
using namespace RenderCore;
namespace SlateRHIRenderer {
    std::unordered_map<RHI::RHITexture*, ERHIResourceAccess> SwapChainTextureLastAccess;

	SlateRHIRenderer::SlateRHIRenderer() {

	}
	SlateRHIRenderer::~SlateRHIRenderer() {

	}
    uint64_t frameCount = 0;
	void SlateRHIRenderer::Render(SlateCore::SlateWidget* slateWidget) {
        if (!slateWidget)
        {
            return;
        }
        
        CreateViewport(slateWidget);
        auto slateViewport = m_Viewports[slateWidget];
		auto rhiSwapchain = slateViewport.SwapchainRHI;
		auto slot = rhiSwapchain->AcquireNextSlot();
		auto backTexture = slot.Texture;
        if (backTexture == nullptr) { return; }
        //LOG_INFO("render frame: %u",frameCount);
        //LOG_INFO("duration time %f s", Core::Timer::GetGlobalInstance().GetDelta());
        frameCount++;
		// 将视口纹理复制到交换链图像后呈现。
		auto* windowWidget = static_cast<SlateCore::Widget*>(slateWidget->GetViewportChild());
        if (!windowWidget)
        {
            return;
        }
        std::vector<SlateCore::Widget*> widgets;
        widgets.push_back(windowWidget);
        auto computeTransitionContex = RHI::GRHIApi->GetQueue(EQueueType::Compute)->AcquireCommandContext();
        RHIComputeCommandList computeTransitionCmd(dynamic_cast<RHIComputeContex*>(computeTransitionContex));
        computeTransitionCmd.SetImmediate(true);
        
		auto graphicTransitionContex = RHI::GRHIApi->GetQueue(EQueueType::Graphics)->AcquireCommandContext();
        RHIGraphicCommandList graphicTransitionCmd(dynamic_cast<RHIGraphicContex*>(graphicTransitionContex));
        graphicTransitionCmd.SetImmediate(true);
        graphicTransitionCmd.Begin();
        std::vector<RHI::RHIWaitInfo> graphicWaitInfos;
        std::vector<RHI::RHIWaitInfo> computeWaitInfos;
        RHI::RHIWaitInfo graphicValidFinish;
        graphicValidFinish.QueueType = EQueueType::Graphics;
        graphicValidFinish.Value = 0;
        RHI::RHIWaitInfo computeValidFinish;
        computeValidFinish.QueueType = EQueueType::Compute;
        computeValidFinish.Value = 0;


        std::vector<RenderCore::RenderTexture*> renderTextures;
        bool hasCompute = false;
        for (auto& widget : widgets) {
            if (widget->IsA<SlateCore::SlateViewport>()) {
                auto slateW = widget->Cast<SlateCore::SlateViewport>();
                auto widgetTeture = static_cast<RenderCore::RenderTexture*>(slateW->GetViewportRenderTargetTexture());
                auto lastQueue = widgetTeture->GetTracker().GetLastAccessFence().QueueType;
				auto value = widgetTeture->GetTracker().GetLastAccessFence().Value;
                renderTextures.push_back(widgetTeture);
				if (lastQueue == EQueueType::Graphics) {
					TransitionResource(RHI::GRHIApi, graphicTransitionCmd, widgetTeture->GetRHI(), widgetTeture->GetTracker().GetSubresourceAccess(RHI::RHISubresourceRange{}), ERHIResourceAccess::TransferSrc, lastQueue, lastQueue);
					graphicValidFinish.Value = CORE_MAX(graphicValidFinish.Value, value);
                }
                else if (lastQueue == EQueueType::Compute) {
                    if (!hasCompute) {
                        computeTransitionCmd.Begin();
                    }
                    TransitionResource(RHI::GRHIApi, computeTransitionCmd, widgetTeture->GetRHI(), widgetTeture->GetTracker().GetSubresourceAccess(RHI::RHISubresourceRange{}), ERHIResourceAccess::TransferSrc, lastQueue, RHI::EQueueType::Graphics);
					hasCompute = true;
                    computeValidFinish.Value = CORE_MAX(computeValidFinish.Value, value);
                }

            }
        }
        //转换swapchain的access
        TransitionResource(RHI::GRHIApi, graphicTransitionCmd, backTexture, SwapChainTextureLastAccess[backTexture], ERHIResourceAccess::TransferDest, EQueueType::Graphics, EQueueType::Graphics);
        //blit texture

        for (auto& widget : widgets) {
            if (widget->IsA<SlateCore::SlateViewport>()) {
                auto slateW = widget->Cast<SlateCore::SlateViewport>();
                auto widgetTeture = static_cast<RenderCore::RenderTexture*>(slateW->GetViewportRenderTargetTexture());
                auto texDesc = widgetTeture->GetRHI()->GetDesc();
                auto backWidth = backTexture->GetDesc().Width;
                auto backHeight = backTexture->GetDesc().Height;
                RHI::RHIBlitTextureDesc blit{};
                blit.SrcRegion.Width = texDesc.Width;
                blit.SrcRegion.Height = texDesc.Height;
                blit.DstRegion.OffsetX = slateW->GetGeometry().X;
                blit.DstRegion.OffsetY = slateW->GetGeometry().Y;
                blit.DstRegion.Width = slateW->GetWidth();
                blit.DstRegion.Height = slateW->GetHeight();
                graphicTransitionCmd.BlitTexture(widgetTeture->GetRHI(), backTexture, blit);
            }
        }
        TransitionResource(RHI::GRHIApi, graphicTransitionCmd, backTexture, ERHIResourceAccess::TransferDest, ERHIResourceAccess::Present,EQueueType::Graphics,EQueueType::Graphics);
        if (slot.ReadySync)
        {
            graphicWaitInfos.push_back({ slot.ReadySync, EQueueType::Graphics, 0,RHI::ERHIPipelineStage::TopOfPipe });
        }

        if (hasCompute) {
			computeWaitInfos.push_back(computeValidFinish);
            
            computeTransitionCmd.End();
            computeTransitionCmd.ExecuteAll();
            auto transientComputeFenceVlue = RHI::GRHIApi->GetQueue(EQueueType::Compute)->ExecuteContext(computeTransitionContex);
            RHIWaitInfo waitInfoCompute;
            waitInfoCompute.QueueType = EQueueType::Compute;
			waitInfoCompute.Value = transientComputeFenceVlue;
            graphicWaitInfos.push_back(waitInfoCompute);
        }
        else {
            RHI::GRHIApi->GetQueue(EQueueType::Compute)->ReleaseCommandContext(computeTransitionContex);
        }

		graphicTransitionCmd.End();
		
        
        auto transientGraphicFenceVlue = RHI::GRHIApi->GetQueue(EQueueType::Graphics)->ExecuteContext({ graphicTransitionContex }, graphicWaitInfos);

        RHI::RHIWaitInfo presentWait;
        presentWait.QueueType = EQueueType::Graphics;
        presentWait.Value = transientGraphicFenceVlue;
        RHI::RHIFence transientGraphicFence;
		transientGraphicFence.QueueType = EQueueType::Graphics;
        transientGraphicFence.Value = transientGraphicFenceVlue;

        RHI::GRHIApi->GetPresentExecutor()->Present(rhiSwapchain.get(), { presentWait });

        for (auto& renderTexture : renderTextures) {
            renderTexture->GetTracker().UpdateLastAccessFence(transientGraphicFence);
            renderTexture->GetTracker().UpdateSubresourceAccess(RHI::RHISubresourceRange{}, ERHIResourceAccess::TransferSrc);
        }

		
		}
		

	

	void SlateRHIRenderer::CreateViewport(SlateCore::SlateWidget* slateWidget)
	{
        if (!slateWidget) {
            return;
        }

        const int width = slateWidget->GetGeometry().Width;
        const int height = slateWidget->GetGeometry().Height;

        if (m_Viewports.find(slateWidget) == m_Viewports.end()) {
            WindowViewportInfo viewportInfo;
            auto windowHandle = slateWidget->GetNativeHandle();
            viewportInfo.SwapchainRHI = GRHIApi->CreateSwapchain(windowHandle, width, height, ERHIFormat::R8G8B8A8_UNorm);
            m_Viewports[slateWidget] = viewportInfo;
            m_Viewports[slateWidget].Width = width;
            m_Viewports[slateWidget].Height = height;
        }
        else {
            auto& viewportInfo = m_Viewports[slateWidget];
            if (viewportInfo.Width != width || viewportInfo.Height != height) {
                viewportInfo.SwapchainRHI->Resize(width, height);
                viewportInfo.Width = width;
                viewportInfo.Height = height;
            }
        }

	}



}