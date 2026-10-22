#include "ImMainWindow.h"

#include "ImSlateRenderer.h"
#include "ImWidget.h"

namespace ImGUISlate
{

    ImMainWindow::ImMainWindow(
        int width,
        int height,
        const std::string& title)
        : Window(width, height, title)
    {

    }

    ImMainWindow::~ImMainWindow() = default;

    void ImMainWindow::Draw()
    {
        Renderer->Render();
    }

    bool ImMainWindow::Initialize()
    {
        bool res = Window::Initialize();
        Renderer = std::make_unique<ImSlateRenderer>(this);
        Layout = std::make_unique<HorizontalLayout>();
        return res;
    }

    ImSlateRenderer* ImMainWindow::GetRenderer() const
    {
        return Renderer.get();
    }

    void ImMainWindow::AddWidget(SlateCore::Widget* widget,const LayoutParams& params)
    {
        Layout->AddWidget(widget, params);
        Layout->Layout(_width,_height);
        if (widget->IsA<ImWidget>()) {
            ImWidget* imWidget = widget->Cast<ImWidget>();
            Renderer->RegisterWidget(imWidget);
        }
    }

    bool ImMainWindow::OnFocusReceived()
    {
        bool handled = false;

        if (Renderer)
        {
            handled |= Renderer->OnFocusReceived();
        }

        handled |= Window::OnFocusReceived();

        return handled;
    }

    bool ImMainWindow::OnFocusLost()
    {
        bool handled = false;

        if (Renderer)
        {
            handled |= Renderer->OnFocusLost();
        }

        handled |= Window::OnFocusLost();

        return handled;
    }
    bool ImMainWindow::OnResize(
        uint32_t width,
        uint32_t height)
    {
        _width = static_cast<int>(width);
        _height = static_cast<int>(height);
        Layout->Layout(_width, _height);

        return true;
    }
    bool ImMainWindow::OnMouseMove(
        const SlateCore::MouseMoveEvent& event)
    {
        bool handled = false;

        if (Renderer)
        {
            handled |= Renderer->OnMouseMove(event);
        }

        if (!handled)
        {
            handled |= Window::OnMouseMove(event);
        }

        return handled;
    }

    bool ImMainWindow::OnMouseButton(
        const SlateCore::MouseButtonEvent& event)
    {
        bool handled = false;

        if (Renderer)
        {
            handled |= Renderer->OnMouseButton(event);
        }

        if (!handled)
        {
            handled |= Window::OnMouseButton(event);
        }

        return handled;
    }

    bool ImMainWindow::OnMouseWheel(
        const SlateCore::MouseWheelEvent& event)
    {
        bool handled = false;

        if (Renderer)
        {
            handled |= Renderer->OnMouseWheel(event);
        }

        if (!handled)
        {
            handled |= Window::OnMouseWheel(event);
        }

        return handled;
    }

    bool ImMainWindow::OnKeyDown(
        const SlateCore::KeyEvent& event)
    {
        bool handled = false;

        if (Renderer)
        {
            handled |= Renderer->OnKeyDown(event);
        }

        if (!handled)
        {
            handled |= Window::OnKeyDown(event);
        }

        return handled;
    }

    bool ImMainWindow::OnKeyUp(
        const SlateCore::KeyEvent& event)
    {
        bool handled = false;

        if (Renderer)
        {
            handled |= Renderer->OnKeyUp(event);
        }

        if (!handled)
        {
            handled |= Window::OnKeyUp(event);
        }

        return handled;
    }

} // namespace ImGUISlate