#include "GIViewerPreview.h"

#include "RenderGraphBuilder.h"
#include "RHIApi.h"

namespace Editor
{
    GIViewerPreview::~GIViewerPreview()
    {
        Reset();
    }

    void GIViewerPreview::WaitForLastDraw()
    {
        if (!m_Color || !RHI::GRHIApi)
        {
            return;
        }

        const auto fence = m_Color->GetTracker().GetLastAccessFence();
        if (fence.Value != 0)
        {
            RHI::GRHIApi->GetQueue(fence.QueueType)->WaitValue(fence.Value);
        }
    }

    void GIViewerPreview::Reset()
    {
        m_Renderer.Reset(m_Color.get());
        m_Color.reset();
    }

    void GIViewerPreview::ResetCache()
    {
        m_Renderer.Reset(m_Color.get());
    }

    bool GIViewerPreview::Render(const GIViewerSource& source, Core::Int2 size, Renderer::ELumenViewerMode mode,
        uint32_t selectedCardIndex, Renderer::ELumenViewerCardView cardView,
        const Renderer::LumenViewerRadiositySettings& radiosity, const Renderer::LumenViewerWorldCacheSettings& worldCache)
    {
        if (!source.Scene || !source.View || size.X <= 0 || size.Y <= 0)
        {
            return false;
        }

        if (!m_Color || m_Color->GetRHI()->GetDesc().Width != static_cast<uint32_t>(size.X) ||
            m_Color->GetRHI()->GetDesc().Height != static_cast<uint32_t>(size.Y))
        {
            WaitForLastDraw();
            RHI::RHITextureDesc desc;
            desc.Width = static_cast<uint32_t>(size.X);
            desc.Height = static_cast<uint32_t>(size.Y);
            desc.Format = RHI::ERHIFormat::R32G32B32A32_Float;
            desc.Usage = RHI::ERHITextureCreateFlag::RenderTarget | RHI::ERHITextureCreateFlag::ShaderResource |
                RHI::ERHITextureCreateFlag::UAV | RHI::ERHITextureCreateFlag::TransferSrc;
            m_Color = std::make_unique<RenderCore::RenderTexture>(desc);
            m_Color->InitRHIResource();
        }

        RenderCore::RenderGraphBuilder builder;
        const bool hasCache = m_Renderer.AddPasses(builder, source.Scene, *source.View, m_Color.get(), mode,
            selectedCardIndex, cardView, radiosity, worldCache);
        builder.Execute();
        return hasCache;
    }
}
