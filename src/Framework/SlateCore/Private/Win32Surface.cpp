#include "Win32Surface.h"

#ifdef _WIN32

#include "PlatformSurfaceOwner.h"
#include <Windowsx.h>

namespace SlateCore
{
    Win32Surface::Win32Surface(
        int width,
        int height,
        const std::string& title,
        PlatformSurfaceOwner* owner)
        : PlatformSurface(width, height, title, owner)
    {
    }

    bool Win32Surface::Initialize()
    {
        if (m_HWnd)
        {
            return true;
        }

        HWND parentHwnd = nullptr;
        if (_owner)
        {
            auto* parentOwner = _owner->GetParent();
            if (parentOwner)
            {
                parentHwnd = static_cast<HWND>(parentOwner->GetNativeHandle());
            }
        }

        WNDCLASS wc = {};
        wc.lpfnWndProc = Win32Surface::WndProcSetup;
        wc.hInstance = GetModuleHandle(nullptr);
        wc.lpszClassName = "SlateCoreWin32Surface";

        if (!RegisterClass(&wc) &&
            GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        {
            return false;
        }

        const DWORD windowStyle =
            parentHwnd
            ? (WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS)
            : WS_OVERLAPPEDWINDOW;

        const int posX = parentHwnd ? 0 : CW_USEDEFAULT;
        const int posY = parentHwnd ? 0 : CW_USEDEFAULT;

        m_HWnd = CreateWindowEx(
            0,
            wc.lpszClassName,
            _title.c_str(),
            windowStyle,
            posX,
            posY,
            _width,
            _height,
            parentHwnd,
            nullptr,
            wc.hInstance,
            this);

        return m_HWnd != nullptr;
    }

    void Win32Surface::PollEvents()
    {
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
        }
    }

    void Win32Surface::Shutdown()
    {
        if (m_HWnd)
        {
            ::DestroyWindow(m_HWnd);
            m_HWnd = nullptr;
        }
    }

    void Win32Surface::Show()
    {
        ::ShowWindow(m_HWnd, SW_SHOW);
    }

    void Win32Surface::Hide()
    {
        ::ShowWindow(m_HWnd, SW_HIDE);
    }

    void Win32Surface::Close()
    {
        if (_owner)
        {
            _owner->OnSurfaceCloseRequested();
        }

        if (m_HWnd)
        {
            ::DestroyWindow(m_HWnd);
            m_HWnd = nullptr;
        }
    }

    void Win32Surface::SetPosition(int x, int y)
    {
        if (!m_HWnd)
        {
            return;
        }

        ::SetWindowPos(
            m_HWnd,
            nullptr,
            x,
            y,
            0,
            0,
            SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    void Win32Surface::SetSize(int width, int height)
    {
        _width = width;
        _height = height;

        if (!m_HWnd)
        {
            return;
        }

        ::SetWindowPos(
            m_HWnd,
            nullptr,
            0,
            0,
            width,
            height,
            SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    void* Win32Surface::GetNativeHandle() const
    {
        return m_HWnd;
    }

    Core::Int2 Win32Surface::GetFramebufferSize() const
    {
        HWND hwnd = static_cast<HWND>(GetNativeHandle());
        if (!hwnd)
        {
            return { 0, 0 };
        }

        RECT rect;
        GetClientRect(hwnd, &rect);
        int logicalWidth = rect.right - rect.left;
        int logicalHeight = rect.bottom - rect.top;

        UINT dpiX = 96;
        HMODULE user32 = LoadLibraryA("user32.dll");
        if (user32)
        {
            using GetDpiForWindowFunc = UINT(WINAPI*)(HWND);
            auto pGetDpiForWindow = reinterpret_cast<GetDpiForWindowFunc>(
                GetProcAddress(user32, "GetDpiForWindow"));
            if (pGetDpiForWindow)
            {
                dpiX = pGetDpiForWindow(hwnd);
            }
            FreeLibrary(user32);
        }

        int fbWidth = static_cast<int>(logicalWidth * dpiX / 96.0f);
        int fbHeight = static_cast<int>(logicalHeight * dpiX / 96.0f);
        return { fbWidth, fbHeight };
    }

    LRESULT CALLBACK Win32Surface::WndProcSetup(
        HWND hwnd,
        UINT msg,
        WPARAM wParam,
        LPARAM lParam)
    {
        Win32Surface* surface = nullptr;

        if (msg == WM_NCCREATE)
        {
            CREATESTRUCT* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
            surface = reinterpret_cast<Win32Surface*>(cs->lpCreateParams);
            ::SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(surface));
        }
        else
        {
            surface = reinterpret_cast<Win32Surface*>(::GetWindowLongPtr(hwnd, GWLP_USERDATA));
        }

        if (surface)
        {
            return surface->WndProc(hwnd, msg, wParam, lParam);
        }

        return ::DefWindowProc(hwnd, msg, wParam, lParam);
    }

    LRESULT Win32Surface::WndProc(
        HWND hwnd,
        UINT msg,
        WPARAM wParam,
        LPARAM lParam)
    {
        switch (msg)
        {
        case WM_CLOSE:
            if (_owner)
            {
                _owner->OnSurfaceCloseRequested();
            }
            return 0;

        case WM_SIZE:
            _width = LOWORD(lParam);
            _height = HIWORD(lParam);
            if (_owner)
            {
                _owner->OnResize(static_cast<uint32_t>(_width), static_cast<uint32_t>(_height));
            }
            return 0;

        case WM_SETFOCUS:
            if (_owner)
            {
                _owner->OnFocusReceived();
            }
            return 0;

        case WM_KILLFOCUS:
            if (_owner)
            {
                _owner->OnFocusLost();
            }
            return 0;
        }

        if (RouteInputMessage(msg, wParam, lParam))
        {
            return 0;
        }

        return ::DefWindowProc(hwnd, msg, wParam, lParam);
    }

    EKey Win32Surface::TranslateKey(WPARAM key) const
    {
        switch (key)
        {
        case 'W': return EKey::W;
        case 'A': return EKey::A;
        case 'S': return EKey::S;
        case 'D': return EKey::D;
        case 'Q': return EKey::Q;
        case 'E': return EKey::E;

        case VK_SHIFT: return EKey::Shift;
        case VK_CONTROL: return EKey::Ctrl;
        case VK_MENU: return EKey::Alt;

        case VK_SPACE: return EKey::Space;
        case VK_ESCAPE: return EKey::Escape;
        default: return EKey::Unknown;
        }
    }

    bool Win32Surface::RouteInputMessage(
        UINT msg,
        WPARAM wParam,
        LPARAM lParam)
    {
        if (!_owner)
        {
            return false;
        }

        switch (msg)
        {
        case WM_MOUSEMOVE:
        {
            MouseMoveEvent event;
            event.X = GET_X_LPARAM(lParam);
            event.Y = GET_Y_LPARAM(lParam);

            static int32_t lastX = event.X;
            static int32_t lastY = event.Y;

            event.DeltaX = event.X - lastX;
            event.DeltaY = event.Y - lastY;

            lastX = event.X;
            lastY = event.Y;

            event.Modifiers.bCtrl = (::GetKeyState(VK_CONTROL) & 0x8000) != 0;
            event.Modifiers.bShift = (::GetKeyState(VK_SHIFT) & 0x8000) != 0;
            event.Modifiers.bAlt = (::GetKeyState(VK_MENU) & 0x8000) != 0;

            return _owner->OnMouseMove(event);
        }

        case WM_MOUSEWHEEL:
        {
            MouseWheelEvent event;
            event.Delta = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / WHEEL_DELTA;

            POINT pt;
            pt.x = GET_X_LPARAM(lParam);
            pt.y = GET_Y_LPARAM(lParam);
            ::ScreenToClient(m_HWnd, &pt);

            event.X = pt.x;
            event.Y = pt.y;

            return _owner->OnMouseWheel(event);
        }

        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
        case WM_MBUTTONUP:
        {
            MouseButtonEvent event;

            event.X = GET_X_LPARAM(lParam);
            event.Y = GET_Y_LPARAM(lParam);

            event.Event =
                (msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN || msg == WM_MBUTTONDOWN)
                ? EInputEvent::Pressed
                : EInputEvent::Released;

            switch (msg)
            {
            case WM_LBUTTONDOWN:
            case WM_LBUTTONUP:
                event.Button = EMouseButton::Left;
                break;

            case WM_RBUTTONDOWN:
            case WM_RBUTTONUP:
                event.Button = EMouseButton::Right;
                break;

            case WM_MBUTTONDOWN:
            case WM_MBUTTONUP:
                event.Button = EMouseButton::Middle;
                break;
            }

            return _owner->OnMouseButton(event);
        }

        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
        {
            KeyEvent event;
            event.Key = TranslateKey(wParam);
            event.Event = EInputEvent::Pressed;
            return _owner->OnKeyDown(event);
        }

        case WM_KEYUP:
        case WM_SYSKEYUP:
        {
            KeyEvent event;
            event.Key = TranslateKey(wParam);
            event.Event = EInputEvent::Released;
            return _owner->OnKeyUp(event);
        }
        }

        return false;
    }
}

#endif
