#pragma once

#include "Widget.h"
#include <functional>

namespace ImGUISlate {

    class IMGUISLATE_API ImWidgetBase : public SlateCore::EventHandler
    {

    public:
        using DrawCallback = std::function<void(int x, int y, int w,int h)>;

        ImWidgetBase() = default;
        explicit ImWidgetBase(DrawCallback callback);

        void SetDrawCallback(DrawCallback callback);

        
        virtual bool OnMouseMove(const SlateCore::MouseMoveEvent& event) override;
        virtual bool OnMouseButton(const SlateCore::MouseButtonEvent& event) override;
        virtual bool OnMouseWheel(const SlateCore::MouseWheelEvent& event) override;
        virtual bool OnKeyDown(const SlateCore::KeyEvent& event) override;
        virtual bool OnKeyUp(const SlateCore::KeyEvent& event) override;
        virtual bool OnFocusReceived() override;
        virtual bool OnFocusLost() override;


    protected:
        DrawCallback _drawHandler;
    };

    class IMGUISLATE_API ImWidget :  public SlateCore::Widget, public ImWidgetBase {
    public:
        ImWidget() = default;
        explicit ImWidget(DrawCallback callback);
        virtual void Draw() override;
        virtual bool OnResize(uint32_t width, uint32_t height) override;
    };
    class IMGUISLATE_API PopupImWidget : public SlateCore::NativeWidget, public ImWidgetBase {
    public:
        explicit PopupImWidget(SlateCore::PlatformSurfaceOwner* parentOwner = nullptr);
        virtual void Draw() override;
        virtual bool OnResize(uint32_t width, uint32_t height) override;
    };
}