#pragma once
#include "Module.h"
#include <functional>
#include <memory>
#include <vector>
#include <cstdint>
namespace RHI { class RHIGraphicCommandList; class RHITexture; struct RHIWaitInfo; }
namespace RenderCore { class RenderTexture; class RenderBuffer; }
namespace SlateCore
{
    class Window;
    class SLATECORE_API SlateRenderer
    {
    public:
        struct DrawResources
        {
            std::vector<RenderCore::RenderTexture*> SampledTextures;
            std::vector<RenderCore::RenderBuffer*> VertexOrIndexBuffers;
        };
        using DrawCallback = std::function<void(RHI::RHIGraphicCommandList&, RHI::RHITexture*, std::vector<RHI::RHIWaitInfo>&, DrawResources&)>;
        virtual ~SlateRenderer() = default;
        // Returns the graphics queue completion value for this window frame.
        virtual uint64_t Render(Window* window, const DrawCallback& draw) = 0;
        virtual void ReleaseWindow(Window* window) = 0;
    };
    using SlateRendererSP = std::shared_ptr<SlateRenderer>;
    class SLATECORE_API SlateRendererModule : public Core::Module
    {
    public:
        virtual SlateRenderer* CreateSlateRenderer() = 0;
    };
    SLATECORE_API SlateRendererModule* GetSlateRendererModule();
}
