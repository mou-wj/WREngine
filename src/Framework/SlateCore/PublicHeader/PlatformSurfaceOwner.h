#pragma once

#include <functional>
#include <utility>
#include "EventHandler.h"

namespace SlateCore
{
    class PlatformSurface;

    using WindowCloseCallback = std::function<void()>;
    using WindowResizeCallback = std::function<void(int width, int height)>;

    class SLATECORE_API PlatformSurfaceOwner : public EventHandler
    {
    public:
        virtual ~PlatformSurfaceOwner() = default;

    public:
        PlatformSurfaceOwner* GetParent() const {
            return _parent;
        }

        void SetParent(PlatformSurfaceOwner* parent)
        {
            _parent = parent;
        }

        virtual void* GetNativeHandle() const
        {
            return nullptr;
        }

        void SetCloseCallback(WindowCloseCallback cb)
        {
            _closeCallback = std::move(cb);
        }

        void SetResizeCallback(WindowResizeCallback cb)
        {
            _resizeCallback = std::move(cb);
        }

        void SetPosition(int x, int y);
        void SetSize(int width, int height);

        virtual void OnSurfaceCloseRequested();
        bool OnResize(uint32_t width, uint32_t height) override;

        bool OnFocusReceived() override;
        bool OnFocusLost() override;

        bool OnMouseMove(const MouseMoveEvent& event) override;
        bool OnMouseButton(const MouseButtonEvent& event) override;
        bool OnMouseWheel(const MouseWheelEvent& event) override;
        bool OnKeyDown(const KeyEvent& event) override;
        bool OnKeyUp(const KeyEvent& event) override;

    protected:
        virtual PlatformSurface* GetOwnedSurface() const;

        WindowCloseCallback _closeCallback;
        WindowResizeCallback _resizeCallback;
        PlatformSurfaceOwner* _parent = nullptr;
    };
}
