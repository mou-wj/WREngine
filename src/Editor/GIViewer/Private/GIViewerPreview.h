#pragma once

#include "EditorGIViewer.h"
#include "RenderResource.h"
#include <memory>
#include <vector>

namespace Editor
{
    class GIViewerPreview
    {
    public:
        ~GIViewerPreview();
        bool Render(const GIViewerSource& source, Core::Int2 size, Renderer::ELumenViewerMode mode,
            uint32_t selectedCardIndex, Renderer::ELumenViewerCardView cardView,
            const Renderer::LumenViewerRadiositySettings& radiosity, const Renderer::LumenViewerWorldCacheSettings& worldCache,
            bool bUseHardwareTracing);
        void Reset();
        void ResetCache();
        RenderCore::RenderTexture* GetTexture() const { return m_Color.get(); }
        const std::vector<Renderer::LumenViewerCardInfo>& GetSurfaceCards() const { return m_Renderer.GetSurfaceCards(); }
        Renderer::LumenViewerRayTracingInfo GetRayTracingInfo() const { return m_Renderer.GetRayTracingInfo(); }
        uint32_t GetRadiosityIteration() const { return m_Renderer.GetRadiosityIteration(); }

    private:
        void WaitForLastDraw();
        std::unique_ptr<RenderCore::RenderTexture> m_Color;
        Renderer::GIViewerRenderer m_Renderer;
    };
}
