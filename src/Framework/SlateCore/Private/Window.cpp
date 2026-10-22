#include "Window.h"

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

    bool Window::OnResize(
        uint32_t width,
        uint32_t height)
    {
        _width = static_cast<int>(width);
        _height = static_cast<int>(height);

        if (_resizeCallback)
        {
            _resizeCallback(_width, _height);
        }

        if (_rootWidget)
        {
            _rootWidget->OnResize(width, height);
        }

        return _rootWidget != nullptr;
    }

    bool Window::OnFocusReceived()
    {
        if (!_rootWidget)
        {
            return false;
        }

        return _rootWidget->OnFocusReceived();
    }

    bool Window::OnFocusLost()
    {
        if (!_rootWidget)
        {
            return false;
        }

        return _rootWidget->OnFocusLost();
    }

    bool Window::OnMouseMove(
        const MouseMoveEvent& event)
    {
        if (!_rootWidget)
        {
            return false;
        }

        return _rootWidget->OnMouseMove(event);
    }

    bool Window::OnMouseButton(
        const MouseButtonEvent& event)
    {
        if (!_rootWidget)
        {
            return false;
        }

        return _rootWidget->OnMouseButton(event);
    }

    bool Window::OnMouseWheel(
        const MouseWheelEvent& event)
    {
        if (!_rootWidget)
        {
            return false;
        }

        return _rootWidget->OnMouseWheel(event);
    }

    bool Window::OnKeyDown(
        const KeyEvent& event)
    {
        if (!_rootWidget)
        {
            return false;
        }

        return _rootWidget->OnKeyDown(event);
    }

    bool Window::OnKeyUp(
        const KeyEvent& event)
    {
        if (!_rootWidget)
        {
            return false;
        }

        return _rootWidget->OnKeyUp(event);
    }


    WindowSP WindowFactory::CreateWindowSP(int w, int h, const std::string& title) {
        return m_Creator(w, h, title);
    }


    WindowFactory::CreatorFunc WindowFactory::m_Creator = [](int w, int h, const std::string& t) -> WindowSP {
        auto win = std::make_shared<Window>(w, h, t);
        win->Initialize();
        return win;
    };


}