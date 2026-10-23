#pragma once
#include "SlateRenderer.h"
#include "RHIResource.h"
#include "RHICommandContex.h"
#include <map>
namespace SlateRHIRenderer
{
    class SlateRHIRenderer : public SlateCore::SlateRenderer
    {
    public:
        ~SlateRHIRenderer() override;
        uint64_t Render(SlateCore::Window* window, const DrawCallback& draw) override;
        void ReleaseWindow(SlateCore::Window* window) override;
    private:
        struct WindowViewportInfo
        {
            RHI::RHISwapchainSP Swapchain;
            uint32_t Width = 0, Height = 0;
            uint64_t LastSubmission = 0;
            std::map<RHI::RHITexture*, RHI::ERHIResourceAccess> Access;
        };
        std::map<SlateCore::Window*, WindowViewportInfo> m_Windows;
    };
}
