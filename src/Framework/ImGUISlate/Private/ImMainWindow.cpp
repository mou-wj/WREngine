#include "ImMainWindow.h"

#include "ImSlateRenderer.h"
#include "ImWidget.h"
#include <algorithm>
#include <stdexcept>

namespace ImGUISlate
{

    ImMainWindow::ImMainWindow(
        int width,
        int height,
        const std::string& title, SlateCore::SlateRenderer* renderer)
        : Window(width, height, title), m_WindowRenderer(renderer)
    {

    }

    ImMainWindow::~ImMainWindow() = default;

    bool ImMainWindow::OnTextInput(uint16_t character)
    {
        return m_Renderer && m_Renderer->OnTextInput(character);
    }

    void ImMainWindow::Draw()
    {
        m_Renderer->Render();
    }

    bool ImMainWindow::Initialize()
    {
        if (!m_WindowRenderer || !Window::Initialize()) return false;
        m_Renderer = std::make_unique<ImSlateRenderer>(this, m_WindowRenderer);
        m_Layout = std::make_unique<HorizontalLayout>();
        return true;
    }

    ImSlateRenderer* ImMainWindow::GetRenderer() const
    {
        return m_Renderer.get();
    }

    void ImMainWindow::SetLayout(std::unique_ptr<LayoutManager> layout)
    {
        if (!layout) throw std::invalid_argument("Window layout cannot be null");
        if (!m_Renderer || !_widgets.empty())
            throw std::logic_error("Select the layout after Initialize and before AddWidget");
        m_Layout = std::move(layout);
    }

    void ImMainWindow::AddWidget(SlateCore::Widget* widget,const LayoutParams& params)
    {
        if (!widget || std::find(_widgets.begin(), _widgets.end(), widget) != _widgets.end()) return;
        Window::AddWidget(widget);
        m_Layout->AddWidget(widget, params);
        const auto size = GetFramebufferSize();
        m_Layout->Layout(static_cast<float>(size.X), static_cast<float>(size.Y));

    }

    void ImMainWindow::UpdateLayout()
    {
        const auto size = GetFramebufferSize();
        if (!m_Layout || size.X <= 0 || size.Y <= 0) return;
        if (m_Renderer) m_Renderer->WaitForLastDraw();
        m_Layout->Layout(static_cast<float>(size.X), static_cast<float>(size.Y));
    }
    void ImMainWindow::WaitForLastDraw()
    {
        if (m_Renderer) m_Renderer->WaitForLastDraw();
    }

    void ImMainWindow::RemoveWidget(SlateCore::Widget* widget)
    {
        if (!widget) return;
        // The caller may destroy the widget and its textures after removal.
        m_WindowRenderer->ReleaseWindow(this);
        Window::RemoveWidget(widget);
        m_Layout->RemoveWidget(widget);
        const auto size = GetFramebufferSize();
        if (size.X > 0 && size.Y > 0)
            m_Layout->Layout(static_cast<float>(size.X), static_cast<float>(size.Y));
    }

    bool ImMainWindow::OnFocusReceived()
    {
        bool handled = false;

        if (m_Renderer)
        {
            handled |= m_Renderer->OnFocusReceived();
        }

        handled |= Window::OnFocusReceived();

        return handled;
    }

    bool ImMainWindow::OnFocusLost()
    {
        bool handled = false;

        if (m_Renderer)
        {
            handled |= m_Renderer->OnFocusLost();
        }

        handled |= Window::OnFocusLost();

        return handled;
    }
    bool ImMainWindow::OnResize(
        uint32_t width,
        uint32_t height)
    {
        // Layout can recreate viewport textures; finish previous users before resizing them.
        if (m_Renderer) m_WindowRenderer->ReleaseWindow(this);
        Window::OnResize(width, height);
        const auto size = GetFramebufferSize();
        if (m_Layout && size.X > 0 && size.Y > 0)
            m_Layout->Layout(static_cast<float>(size.X), static_cast<float>(size.Y));

        return true;
    }
    bool ImMainWindow::OnMouseMove(
        const SlateCore::MouseMoveEvent& event)
    {
        bool handled = false;

        if (m_Renderer)
        {
            handled |= m_Renderer->OnMouseMove(event);
        }

        if (!handled || HasMouseCapture())
        {
            handled |= Window::OnMouseMove(event);
        }

        return handled;
    }

    bool ImMainWindow::OnMouseButton(
        const SlateCore::MouseButtonEvent& event)
    {
        bool handled = false;

        if (m_Renderer)
        {
            handled |= m_Renderer->OnMouseButton(event);
        }

        if (!handled || HasMouseCapture())
        {
            handled |= Window::OnMouseButton(event);
        }

        return handled;
    }

    bool ImMainWindow::OnMouseWheel(
        const SlateCore::MouseWheelEvent& event)
    {
        bool handled = false;

        if (m_Renderer)
        {
            handled |= m_Renderer->OnMouseWheel(event);
        }

        if (!handled || HasMouseCapture())
        {
            handled |= Window::OnMouseWheel(event);
        }

        return handled;
    }

    bool ImMainWindow::OnKeyDown(
        const SlateCore::KeyEvent& event)
    {
        bool handled = false;

        if (m_Renderer)
        {
            handled |= m_Renderer->OnKeyDown(event);
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

        if (m_Renderer)
        {
            handled |= m_Renderer->OnKeyUp(event);
        }

        if (!handled)
        {
            handled |= Window::OnKeyUp(event);
        }

        return handled;
    }

} // namespace ImGUISlate
