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
            uint32_t selectedCardIndex, Renderer::ELumenViewerCardView cardView);
        void Reset();
        void ResetCache();
        RenderCore::RenderTexture* GetTexture() const { return m_Color.get(); }
        const std::vector<Renderer::LumenViewerCardInfo>& GetSurfaceCards() const { return m_Renderer.GetSurfaceCards(); }

    private:
        void WaitForLastDraw();
        std::unique_ptr<RenderCore::RenderTexture> m_Color;
        Renderer::LumenViewerRenderer m_Renderer;
    };
}
