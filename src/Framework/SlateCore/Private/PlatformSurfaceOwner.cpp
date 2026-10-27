#include "PlatformSurfaceOwner.h"
#include "PlatformSurface.h"

namespace SlateCore
{
    PlatformSurface::PlatformSurface(
        int width,
        int height,
        std::string title,
        PlatformSurfaceOwner* owner)
        : _width(width)
        , _height(height)
        , _title(std::move(title))
        , _owner(owner)
    {
    }

    void PlatformSurfaceOwner::SetPosition(
        int x,
        int y)
    {
        auto* surface = GetOwnedSurface();
        if (!surface)
        {
            return;
        }

        surface->SetPosition(x, y);
    }

    void PlatformSurfaceOwner::SetSize(
        int width,
        int height)
    {
        auto* surface = GetOwnedSurface();
        if (!surface)
        {
            return;
        }

        surface->SetSize(width, height);
    }

    PlatformSurface* PlatformSurfaceOwner::GetOwnedSurface() const
    {
        return nullptr;
    }

    void PlatformSurfaceOwner::SetMouseCursor(EMouseCursor cursor)
    {
        if (auto* surface = GetOwnedSurface()) surface->SetMouseCursor(cursor);
    }

    void PlatformSurfaceOwner::OnSurfaceCloseRequested()
    {
        if (_closeCallback)
        {
            _closeCallback();
        }
    }

    bool PlatformSurfaceOwner::OnResize(
        uint32_t,
        uint32_t)
    {
        return false;
    }

    bool PlatformSurfaceOwner::OnFocusReceived()
    {
        return false;
    }

    bool PlatformSurfaceOwner::OnFocusLost()
    {
        return false;
    }

    bool PlatformSurfaceOwner::OnMouseMove(
        const MouseMoveEvent&)
    {
        return false;
    }

    bool PlatformSurfaceOwner::OnMouseButton(
        const MouseButtonEvent&)
    {
        return false;
    }

    bool PlatformSurfaceOwner::OnMouseWheel(
        const MouseWheelEvent&)
    {
        return false;
    }

    bool PlatformSurfaceOwner::OnKeyDown(
        const KeyEvent&)
    {
        return false;
    }

    bool PlatformSurfaceOwner::OnKeyUp(
        const KeyEvent&)
    {
        return false;
    }
}
