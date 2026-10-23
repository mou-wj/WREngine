#include "SlateRHIRenderer.h"
#include "Window.h"
#include "RHIApi.h"
#include "RenderResource.h"
#include "RenderResourceTracker.h"
#include <stdexcept>
namespace SlateRHIRenderer
{
    using namespace RHI;
    SlateRHIRenderer::~SlateRHIRenderer()
    {
        while (!m_Windows.empty()) ReleaseWindow(m_Windows.begin()->first);
    }
    void SlateRHIRenderer::ReleaseWindow(SlateCore::Window* window)
    {
        auto it = m_Windows.find(window);
        if (it == m_Windows.end()) return;
        if (it->second.LastSubmission)
            GRHIApi->GetQueue(EQueueType::Graphics)->WaitValue(it->second.LastSubmission);
        m_Windows.erase(it);
    }
    uint64_t SlateRHIRenderer::Render(SlateCore::Window* window, const DrawCallback& draw)
    {
        const auto size = window->GetFramebufferSize();
        if (size.X <= 0 || size.Y <= 0) return 0;
        auto* queue = GRHIApi->GetQueue(EQueueType::Graphics);
        auto& state = m_Windows[window];
        if (!state.Swapchain)
            state.Swapchain = GRHIApi->CreateSwapchain(window->GetNativeHandle(), size.X, size.Y, ERHIFormat::R8G8B8A8_UNorm);
        else if (state.Width != static_cast<uint32_t>(size.X) || state.Height != static_cast<uint32_t>(size.Y))
        {
            if (state.LastSubmission) queue->WaitValue(state.LastSubmission);
            state.Swapchain->Resize(size.X, size.Y);
            state.Access.clear();
        }
        state.Width = size.X; state.Height = size.Y;
        auto slot = state.Swapchain->AcquireNextSlot();
        if (!slot.Texture) return 0;
        auto* context = dynamic_cast<RHIGraphicContex*>(queue->AcquireCommandContext());
        if (!context) throw std::runtime_error("Window renderer requires a graphics context");
        RHIGraphicCommandList cmd(context);
        cmd.SetImmediate(true);
        cmd.Begin();
        auto it = state.Access.find(slot.Texture);
        const auto before = it == state.Access.end() ? ERHIResourceAccess::Unknown : it->second;
        RenderCore::TransitionResource(GRHIApi, cmd, slot.Texture, before, ERHIResourceAccess::RenderTargetView, EQueueType::Graphics, EQueueType::Graphics);
        std::vector<RHIWaitInfo> waits;
        if (slot.ReadySync) waits.push_back({slot.ReadySync, EQueueType::Graphics, 0, ERHIPipelineStage::ColorAttachmentOutput});
        DrawResources resources;
        draw(cmd, slot.Texture, waits, resources);
        RenderCore::TransitionResource(GRHIApi, cmd, slot.Texture, ERHIResourceAccess::RenderTargetView, ERHIResourceAccess::Present, EQueueType::Graphics, EQueueType::Graphics);
        cmd.End();
        const auto drawSubmission = queue->ExecuteContext({context}, waits);
        const RHIFence accessFence{ EQueueType::Graphics, drawSubmission };
        for (auto* texture : resources.SampledTextures)
        {
            texture->GetTracker().UpdateSubresourceAccess({}, ERHIResourceAccess::SRV);
            texture->GetTracker().UpdateLastAccessFence(accessFence);
        }
        for (auto* buffer : resources.VertexOrIndexBuffers)
        {
            buffer->GetTracker().UpdateAccess(ERHIResourceAccess::VertexOrIndexBuffer);
            buffer->GetTracker().UpdateLastAccessFence(accessFence);
        }
        state.Access[slot.Texture] = ERHIResourceAccess::Present;
        RHIWaitInfo ready;
        ready.QueueType = EQueueType::Graphics;
        ready.Value = drawSubmission;
        GRHIApi->GetPresentExecutor()->Present(state.Swapchain.get(), ready);
        // A completion marker also covers backends whose Present is queued on a worker.
        // Signal a window-specific fence without blocking the CPU or draining unrelated submissions.
        auto* completionContext = queue->AcquireCommandContext();
        RHICommandListBase completion(completionContext);
        completion.SetImmediate(true);
        completion.Begin();
        completion.End();
        state.LastSubmission = queue->ExecuteContext(completionContext);
        return state.LastSubmission;
    }
}
