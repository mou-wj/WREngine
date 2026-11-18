#pragma once

#include "ImWidget.h"
#include "Math.hpp"
#include "LumenViewerRenderer.h"
#include <functional>
#include <memory>

namespace Engine
{
    class SceneInterface;
    class SceneView;
}

namespace Editor
{
    struct GIViewerSource
    {
        Engine::SceneInterface* Scene = nullptr;
        const Engine::SceneView* View = nullptr;
    };

    class GIViewerPreview;

    class GIVIEWER_API EditorGIViewer final : public ImGUISlate::PopupImWidget
    {
    public:
        explicit EditorGIViewer(std::function<GIViewerSource()> getSource);
        ~EditorGIViewer() override;
        void Open();
        void Reset();
        void Draw() override;
        bool HitTest(float x, float y) const override;

    private:
        std::function<GIViewerSource()> m_GetSource;
        std::unique_ptr<GIViewerPreview> m_Preview;
        SlateCore::WidgetGeometry m_PopupGeometry;
        Core::Int2 m_PreviewSize{640, 360};
        Renderer::ELumenViewerMode m_Mode = Renderer::ELumenViewerMode::Scene;
        Renderer::ELumenViewerCardView m_CardView = Renderer::ELumenViewerCardView::BaseColor;
        uint32_t m_SelectedCardIndex = UINT32_MAX;
        bool m_bOpen = false;
        bool m_bVisible = false;
        bool m_bHasScene = false;
        bool m_bShowMeshCards = true;
        bool m_bSelectedCardOnly = false;
    };
}
