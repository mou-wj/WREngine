#include "SlateWidget.h"
namespace SlateCore
{
    SlateWidget::~SlateWidget() = default;
    void SlateWidget::OnSetSize(float width, float height)
    {
        if (m_ViewportChild && width > 0 && height > 0)
            m_ViewportChild->Resize(static_cast<int>(width), static_cast<int>(height));
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
        OnSetSize(_geometry.Width, _geometry.Height);
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
