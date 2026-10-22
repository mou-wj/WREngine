#pragma once

#include "TypeIDCast.h"
#include "EventHandler.h"
#include "PlatformSurfaceOwner.h"
#include <memory>

namespace SlateCore
{
    struct WidgetGeometry
    {
        float X = 0;
        float Y = 0;

        float Width = 100;
        float Height = 100;
    };

    enum class EVisibility
    {
        Visible,
        Hidden,
        Collapsed
    };

    class Widget : public EventHandler
    {
        DECLARE_TYPE_ID_BASE_TYPE(Widget)

    public:
        virtual ~Widget() = default;

    public:
        virtual void Tick(float dt)
        {
        }

        virtual void Draw() = 0;

        virtual void Resize(
            float width,
            float height)
        {
            SetGeometry(
                _geometry.X,
                _geometry.Y,
                width,
                height);
        }

        virtual bool HitTest(
            float x,
            float y) const
        {
            if (_visibility !=
                EVisibility::Visible)
            {
                return false;
            }

            const auto& g =
                _geometry;

            return
                x >= g.X &&
                x <= g.X + g.Width &&
                y >= g.Y &&
                y <= g.Y + g.Height;
        }

    public:
        void SetGeometry(
            float x,
            float y,
            float width,
            float height)
        {
            bool posChanged = false;
            if (x != _geometry.X || y != _geometry.Y) {
                posChanged = true;
            }
            _geometry.X = x;
            _geometry.Y = y;
            if (posChanged) {
                OnSetPosition(x, y);
            }


			bool sizeChanged = false;
            if (width != _geometry.Width || height != _geometry.Height) {
                sizeChanged = true;
            }
            _geometry.Width = width;
            _geometry.Height = height;
            if (sizeChanged) {
                OnSetSize(width, height);
            }

        }

        const WidgetGeometry&
            GetGeometry() const
        {
            return _geometry;
        }

        void SetVisibility(
            EVisibility visibility)
        {
            if (_visibility == visibility)
                return;
            _visibility =
                visibility;
            OnVisibilityChanged();
        }

        EVisibility
            GetVisibility() const
        {
            return _visibility;
        }

    protected:
        WidgetGeometry _geometry;

        EVisibility _visibility =
            EVisibility::Visible;
        virtual void OnVisibilityChanged()
        {
        }
        virtual void OnSetSize(float width, float height)
        {
        }
        virtual void OnSetPosition(float x, float y)
        {
        }
    };
    class PlatformSurface;
    class SLATECORE_API NativeWidget : public Widget, public PlatformSurfaceOwner
    {
    public:
        explicit NativeWidget(PlatformSurfaceOwner* parentOwner = nullptr);
        virtual ~NativeWidget() override;

        void SetParentOwnerSource(PlatformSurfaceOwner* parentOwner);
        PlatformSurfaceOwner* GetParentOwnerSource() const;

        void SetNativeHandle(void* nativeHandle);

        bool HasParentNativeRelationship() const;
        void* GetParentNativeHandle() const;
        void* GetNativeHandle() const override;
    protected:
        bool InitializeOwnedSurface();
        PlatformSurface* GetOwnedSurface() const override;
        void* _nativeHandle = nullptr;
        std::unique_ptr<PlatformSurface> _ownedSurface;
    };
}