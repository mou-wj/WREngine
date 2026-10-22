#include "SlateWidget.h"

#include "PlatformSurface.h"

#ifdef _WIN32
#include "Win32Surface.h"
#endif

namespace SlateCore
{
    SlateWidget::SlateWidget(PlatformSurfaceOwner* parentOwner) : NativeWidget(parentOwner)
    {
        OnVisibilityChanged();
    }

    SlateWidget::~SlateWidget()
    {

    }

    void SlateWidget::OnVisibilityChanged()
    {
        if (!_ownedSurface)
        {
            return;
        }
        if (_visibility == EVisibility::Visible) {
            _ownedSurface->Show();
        }
        else if (_visibility == EVisibility::Hidden) {
            _ownedSurface->Hide();
        }
    }
    void SlateWidget::OnSetSize(float width, float height) {
        SetSize(static_cast<int>(width), static_cast<int>(height));
    }
    void SlateWidget::OnSetPosition(float x, float y) {
        OnSetPosition(static_cast<int>(x), static_cast<int>(y));
    }

    bool SlateWidget::AddChildWidget(SlateViewport* child)
    {
        if (!child)
        {
            m_ViewportChild = nullptr;
            return true;
        }

        m_ViewportChild = child;
        return true;
    }

    void SlateWidget::SetViewportChild(SlateViewport* viewport)
    {
        m_ViewportChild = viewport;
    }

    SlateViewport* SlateWidget::GetViewportChild() const
    {
        return m_ViewportChild;
    }



    void SlateWidget::Draw()
    {
        if (m_ViewportChild)
        {
            m_ViewportChild->Draw();
        }
    }

    bool SlateWidget::OnResize(
        uint32_t width,
        uint32_t height)
    {


        if (!m_ViewportChild)
        {
            return false;
        }
        
        return m_ViewportChild->OnResize(width, height);
    }

    bool SlateWidget::OnFocusReceived()
    {
        if (!m_ViewportChild)
        {
            return false;
        }

        return m_ViewportChild->OnFocusReceived();
    }

    bool SlateWidget::OnFocusLost()
    {
        if (!m_ViewportChild)
        {
            return false;
        }

        return m_ViewportChild->OnFocusLost();
    }

    bool SlateWidget::OnMouseMove(
        const MouseMoveEvent& event)
    {
        if (!m_ViewportChild)
        {
            return false;
        }

        return m_ViewportChild->OnMouseMove(event);
    }

    bool SlateWidget::OnMouseButton(
        const MouseButtonEvent& event)
    {
        if (!m_ViewportChild)
        {
            return false;
        }

        return m_ViewportChild->OnMouseButton(event);
    }

    bool SlateWidget::OnMouseWheel(
        const MouseWheelEvent& event)
    {
        if (!m_ViewportChild)
        {
            return false;
        }

        return m_ViewportChild->OnMouseWheel(event);
    }

    bool SlateWidget::OnKeyDown(
        const KeyEvent& event)
    {
        if (!m_ViewportChild)
        {
            return false;
        }

        return m_ViewportChild->OnKeyDown(event);
    }

    bool SlateWidget::OnKeyUp(
        const KeyEvent& event)
    {
        if (!m_ViewportChild)
        {
            return false;
        }

        return m_ViewportChild->OnKeyUp(event);
    }
}
