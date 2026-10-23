#include "Window.h"
#include <algorithm>

#ifdef _WIN32
#include "Win32Surface.h"
#endif

namespace SlateCore {

    Window::Window(
        int width,
        int height,
        const std::string& title)
        : _width(width)
        , _height(height)
        , _title(title)
    {
    }

    Window::~Window()
    {
        Shutdown();
    }

    bool Window::Initialize()
    {
        if (_surface)
        {
            return true;
        }

#ifdef _WIN32
        _surface = std::make_unique<Win32Surface>(
            _width,
            _height,
            _title,
            this);
        return _surface->Initialize();
#else
        return false;
#endif
    }

    void Window::PollEvents()
    {
        if (_surface)
        {
            _surface->PollEvents();
        }
    }

    void Window::Shutdown()
    {
        if (_surface)
        {
            _surface->Shutdown();
        }
    }

    void Window::Show()
    {
        if (_surface)
        {
            _surface->Show();
        }
    }

    void Window::Hide()
    {
        if (_surface)
        {
            _surface->Hide();
        }
    }

    void Window::Close()
    {
        if (_surface)
        {
            _surface->Close();
        }
    }

    Core::Int2 Window::GetFramebufferSize() const
    {
        if (!_surface)
        {
            return { 0, 0 };
        }

        return _surface->GetFramebufferSize();
    }

    void* Window::GetNativeHandle() const
    {
        if (!_surface)
        {
            return nullptr;
        }

        return _surface->GetNativeHandle();
    }

    PlatformSurface* Window::GetOwnedSurface() const
    {
        return _surface.get();
    }

    void Window::OnSurfaceCloseRequested()
    {
        if (_closeCallback)
        {
            _closeCallback();
        }
    }

    void Window::AddWidget(Widget* widget)
    {
        if (widget && std::find(_widgets.begin(), _widgets.end(), widget) == _widgets.end())
            _widgets.push_back(widget);
    }
    void Window::RemoveWidget(Widget* widget)
    {
        if (!widget) return;
        if (_capturedWidget == widget) Window::OnFocusLost();
        else if (_focusedWidget == widget) widget->OnFocusLost();
        if (_focusedWidget == widget) _focusedWidget = nullptr;
        _widgets.erase(std::remove(_widgets.begin(), _widgets.end(), widget), _widgets.end());
    }
    void Window::SetRootWidget(Widget* widget)
    {
        Window::OnFocusLost();
        _focusedWidget = nullptr;
        _widgets.clear();
        AddWidget(widget);
    }
    Widget* Window::FindWidget(int x, int y) const
    {
        for (auto it = _widgets.rbegin(); it != _widgets.rend(); ++it)
            if ((*it)->HitTest(static_cast<float>(x), static_cast<float>(y)))
                return *it;
        return nullptr;
    }
    bool Window::OnResize(uint32_t width, uint32_t height)
    {
        _width = static_cast<int>(width);
        _height = static_cast<int>(height);
        if (_resizeCallback) _resizeCallback(_width, _height);
        return true;
    }
    bool Window::OnFocusReceived()
    {
        return _focusedWidget && _focusedWidget->OnFocusReceived();
    }
    bool Window::OnFocusLost()
    {
        // Cancel held buttons as well as capture, so viewport interaction cannot remain active.
        auto* captured = _capturedWidget;
        const auto buttons = _capturedButtons;
        _capturedWidget = nullptr;
        _capturedButtons = 0;
        if (captured)
        {
            for (uint32_t button = 0; button < 5; ++button)
                if (buttons & (1u << button))
                {
                    MouseButtonEvent release;
                    release.Button = static_cast<EMouseButton>(button);
                    release.Event = EInputEvent::Released;
                    captured->OnMouseButton(release);
                }
        }
        return _focusedWidget && _focusedWidget->OnFocusLost();
    }
    bool Window::OnMouseMove(const MouseMoveEvent& event)
    {
        auto* widget = _capturedWidget ? _capturedWidget : FindWidget(event.X, event.Y);
        if (!widget) return false;
        auto local = event;
        local.X -= static_cast<int>(widget->GetGeometry().X);
        local.Y -= static_cast<int>(widget->GetGeometry().Y);
        return widget->OnMouseMove(local);
    }
    bool Window::OnMouseButton(const MouseButtonEvent& event)
    {
        auto* widget = _capturedWidget ? _capturedWidget : FindWidget(event.X, event.Y);
        if (!widget) return false;
        if (event.Event == EInputEvent::Pressed && widget != _focusedWidget)
        {
            if (_focusedWidget) _focusedWidget->OnFocusLost();
            _focusedWidget = widget;
            widget->OnFocusReceived();
        }
        auto local = event;
        local.X -= static_cast<int>(widget->GetGeometry().X);
        local.Y -= static_cast<int>(widget->GetGeometry().Y);
        const bool handled = widget->OnMouseButton(local);
        const uint32_t mask = 1u << static_cast<uint32_t>(event.Button);
        if (event.Event == EInputEvent::Pressed && handled)
        { _capturedWidget = widget; _capturedButtons |= mask; }
        if (event.Event == EInputEvent::Released)
        { _capturedButtons &= ~mask; if (!_capturedButtons) _capturedWidget = nullptr; }
        return handled;
    }
    bool Window::OnMouseWheel(const MouseWheelEvent& event)
    {
        auto* widget = _capturedWidget ? _capturedWidget : FindWidget(event.X, event.Y);
        if (!widget) return false;
        auto local = event;
        local.X -= static_cast<int>(widget->GetGeometry().X);
        local.Y -= static_cast<int>(widget->GetGeometry().Y);
        return widget->OnMouseWheel(local);
    }
    bool Window::OnKeyDown(const KeyEvent& event) { return _focusedWidget && _focusedWidget->OnKeyDown(event); }
    bool Window::OnKeyUp(const KeyEvent& event) { return _focusedWidget && _focusedWidget->OnKeyUp(event); }

    WindowSP WindowFactory::CreateWindowSP(int w, int h, const std::string& title) {
        return m_Creator(w, h, title);
    }


    WindowFactory::CreatorFunc WindowFactory::m_Creator = [](int w, int h, const std::string& t) -> WindowSP {
        auto win = std::make_shared<Window>(w, h, t);
        win->Initialize();
        return win;
    };


}
