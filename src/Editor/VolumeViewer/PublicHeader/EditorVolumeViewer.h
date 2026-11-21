#pragma once

#include "ImWidget.h"
#include <memory>

namespace Editor
{
    class VolumeViewerPreview;

    class VOLUMEVIEWER_API EditorVolumeViewer final : public ImGUISlate::PopupImWidget
    {
    public:
        EditorVolumeViewer();
        ~EditorVolumeViewer() override;
        void Open();
        void Draw() override;
        bool HitTest(float x, float y) const override;

    private:
        std::unique_ptr<VolumeViewerPreview> m_Preview;
        SlateCore::WidgetGeometry m_WindowGeometry{};
        bool m_bOpen = false;
        bool m_bVisible = false;
    };
}
