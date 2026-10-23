#pragma once

#include "Widget.h"
#include "SlateViewport.h"

#include <memory>

namespace SlateCore
{

    class SLATECORE_API SlateWidget : public Widget
    {
        DECLARE_TYPE_ID_DERIVED_TYPE(SlateWidget, Widget)

    public:
        SlateWidget() = default;
        virtual ~SlateWidget() override;

    public:
        bool AddChildWidget(SlateViewport* child);
        void SetViewportChild(SlateViewport* viewport);
        SlateViewport* GetViewportChild() const;

        

    public:
        void Draw() override;

        bool OnResize(uint32_t width, uint32_t height) override;
        bool OnFocusReceived() override;
        bool OnFocusLost() override;
        bool OnMouseMove(const MouseMoveEvent& event) override;
        bool OnMouseButton(const MouseButtonEvent& event) override;
        bool OnMouseWheel(const MouseWheelEvent& event) override;
        bool OnKeyDown(const KeyEvent& event) override;
        bool OnKeyUp(const KeyEvent& event) override;

    private:
        
        virtual void OnSetSize(float width, float height) override;
    private:

        SlateViewport* m_ViewportChild = nullptr;
    };
}
