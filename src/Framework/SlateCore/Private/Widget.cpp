#include "Widget.h"
#include "PlatformSurface.h"
#include "Win32Surface.h"
namespace SlateCore {

    NativeWidget::NativeWidget(PlatformSurfaceOwner* parentOwner)
    {
        SetParent(parentOwner);
        InitializeOwnedSurface();
    }

    NativeWidget::~NativeWidget()
    {
        if (_ownedSurface)
        {
            _ownedSurface->Shutdown();
            _ownedSurface.reset();
        }
    }

    PlatformSurface* NativeWidget::GetOwnedSurface() const
    {
        return _ownedSurface.get();
    }

    void NativeWidget::SetParentOwnerSource(PlatformSurfaceOwner* parentOwner)
    {
        SetParent(parentOwner);
    }

    PlatformSurfaceOwner* NativeWidget::GetParentOwnerSource() const
    {
        return GetParent();
    }

    void NativeWidget::SetNativeHandle(void* nativeHandle)
    {
        _nativeHandle = nativeHandle;
    }

    bool NativeWidget::HasParentNativeRelationship() const
    {
        return GetParent() != nullptr;
    }

    void* NativeWidget::GetParentNativeHandle() const
    {
        auto* parentOwner = GetParent();
        if (!parentOwner)
        {
            return nullptr;
        }

        return parentOwner->GetNativeHandle();
    }
    void* NativeWidget::GetNativeHandle() const
    {
        if (_ownedSurface)
        {
            return _ownedSurface->GetNativeHandle();
        }

        return _nativeHandle;
    }


    bool NativeWidget::InitializeOwnedSurface()
    {
        if (_ownedSurface)
        {
            return true;
        }

#ifdef _WIN32
        _ownedSurface = std::make_unique<Win32Surface>(
            _geometry.Width,
            _geometry.Height,
            "SlateWidgetSurface",
            this);
#else
        return false;
#endif

        if (!_ownedSurface->Initialize())
        {
            _ownedSurface.reset();
            return false;
        }

        _nativeHandle = _ownedSurface->GetNativeHandle();
        OnVisibilityChanged();
        return true;
    }

}