#include "ImSlateRenderer.h"
#include "imgui_impl_rhi.h"
#include "SlateWidget.h"
#include "SlateViewport.h"
#include "RHIApi.h"
#include <chrono>

ImGuiKey ConvertKey(SlateCore::EKey key)
{
    switch (key)
    {
    case SlateCore::EKey::W:       return ImGuiKey_W;
    case SlateCore::EKey::A:       return ImGuiKey_A;
    case SlateCore::EKey::S:       return ImGuiKey_S;
    case SlateCore::EKey::D:       return ImGuiKey_D;
    case SlateCore::EKey::Q:       return ImGuiKey_Q;
    case SlateCore::EKey::E:       return ImGuiKey_E;

    case SlateCore::EKey::Space:   return ImGuiKey_Space;
    case SlateCore::EKey::Escape:  return ImGuiKey_Escape;

    case SlateCore::EKey::Shift:   return ImGuiKey_LeftShift;
    case SlateCore::EKey::Ctrl:    return ImGuiKey_LeftCtrl;
    case SlateCore::EKey::Alt:     return ImGuiKey_LeftAlt;

    default:
        return ImGuiKey_None;
    }
}

int ConvertMouseButton(SlateCore::EMouseButton button)
{
    switch (button)
    {
    case SlateCore::EMouseButton::Left:    return 0;
    case SlateCore::EMouseButton::Right:   return 1;
    case SlateCore::EMouseButton::Middle:  return 2;
    case SlateCore::EMouseButton::Thumb01: return 3;
    case SlateCore::EMouseButton::Thumb02: return 4;
    }

    return 0;
}

namespace ImGUISlate
{
    ImSlateRenderer::ImSlateRenderer(SlateCore::Window* window, SlateCore::SlateRenderer* renderer)
        : m_Window(window), m_WindowRenderer(renderer)
    {
        IMGUI_CHECKVERSION();
        m_Context = ImGui::CreateContext();
        ImGui::SetCurrentContext(m_Context);
        ImGui::StyleColorsDark();
        try
        {
            m_Backend = std::make_unique<ImGuiRHIBackend>();
        }
        catch (...)
        {
            ImGui::DestroyContext(m_Context);
            throw;
        }
    }
    ImSlateRenderer::~ImSlateRenderer()
    {
        ImGui::SetCurrentContext(m_Context);
        m_WindowRenderer->ReleaseWindow(m_Window);
        m_Backend.reset();
        ImGui::DestroyContext(m_Context);
    }
    void ImSlateRenderer::UpdateDisplaySize()
    {
        auto& io = ImGui::GetIO();
        auto size = m_Window->GetFramebufferSize();
        io.DisplaySize = ImVec2(static_cast<float>(size.X), static_cast<float>(size.Y));
        io.DisplayFramebufferScale = ImVec2(1, 1);
    }
    void ImSlateRenderer::Render()
    {
        ImGui::SetCurrentContext(m_Context);
        UpdateDisplaySize();
        if (ImGui::GetIO().DisplaySize.x <= 0 || ImGui::GetIO().DisplaySize.y <= 0) return;
        const auto now = std::chrono::steady_clock::now();
        ImGui::GetIO().DeltaTime = std::chrono::duration<float>(now - m_LastFrame).count();
        m_LastFrame = now;
        ImGui::NewFrame();
        // All registered widgets share this GUI context and the window's final RHI target.
        for (auto* widget : m_Window->GetWidgets())
        {
            if (widget->GetVisibility() != SlateCore::EVisibility::Visible) continue;
            widget->Draw();
            SlateCore::SlateViewport* viewport = nullptr;
            if (widget->IsA<SlateCore::SlateViewport>()) viewport = widget->Cast<SlateCore::SlateViewport>();
            else if (widget->IsA<SlateCore::SlateWidget>()) viewport = widget->Cast<SlateCore::SlateWidget>()->GetViewportChild();
            if (viewport)
            {
                auto* texture = static_cast<RenderCore::RenderTexture*>(viewport->GetViewportRenderTargetTexture());
                const auto& g = widget->GetGeometry();
                ImGui::GetBackgroundDrawList()->AddImage(ImTextureRef(ImGuiRHIBackend::GetTextureID(texture)),
                    ImVec2(g.X, g.Y), ImVec2(g.X + g.Width, g.Y + g.Height));
            }
        }
        ImGui::Render();
        // CPU draw data is translated now; queued backends only retain RHI commands and owned uploads.
        const auto fence = m_WindowRenderer->Render(m_Window, [this](RHI::RHIGraphicCommandList& cmd,
            RHI::RHITexture* target, std::vector<RHI::RHIWaitInfo>& waits, SlateCore::SlateRenderer::DrawResources& resources)
        { m_Backend->RenderDrawData(ImGui::GetDrawData(), cmd, target, waits, resources); });
        if (fence) m_Backend->EndFrame(fence);
    }
    bool ImSlateRenderer::OnMouseMove(
        const SlateCore::MouseMoveEvent& event)
    {
        ImGui::SetCurrentContext(m_Context);

        ImGuiIO& io = ImGui::GetIO();

        io.AddMousePosEvent(
            static_cast<float>(event.X),
            static_cast<float>(event.Y));

        return io.WantCaptureMouse;
    }
    bool ImSlateRenderer::OnMouseButton(
        const SlateCore::MouseButtonEvent& event)
    {
        ImGui::SetCurrentContext(m_Context);

        ImGuiIO& io = ImGui::GetIO();

        io.AddMousePosEvent(
            static_cast<float>(event.X),
            static_cast<float>(event.Y));

        io.AddMouseButtonEvent(
            ConvertMouseButton(event.Button),
            event.Event != SlateCore::EInputEvent::Released);

        return io.WantCaptureMouse;
    }
    bool ImSlateRenderer::OnMouseWheel(
        const SlateCore::MouseWheelEvent& event)
    {
        ImGui::SetCurrentContext(m_Context);

        ImGuiIO& io = ImGui::GetIO();

        io.AddMousePosEvent(
            static_cast<float>(event.X),
            static_cast<float>(event.Y));

        io.AddMouseWheelEvent(
            0.0f,
            event.Delta);

        return io.WantCaptureMouse;
    }
    bool ImSlateRenderer::OnKeyDown(
        const SlateCore::KeyEvent& event)
    {
        ImGui::SetCurrentContext(m_Context);

        ImGuiIO& io = ImGui::GetIO();

        io.AddKeyEvent(
            ImGuiMod_Ctrl,
            event.Modifiers.bCtrl);

        io.AddKeyEvent(
            ImGuiMod_Shift,
            event.Modifiers.bShift);

        io.AddKeyEvent(
            ImGuiMod_Alt,
            event.Modifiers.bAlt);

        ImGuiKey key = ConvertKey(event.Key);

        if (key != ImGuiKey_None)
        {
            io.AddKeyEvent(key, true);
        }

        return io.WantCaptureKeyboard;
    }
    bool ImSlateRenderer::OnKeyUp(
        const SlateCore::KeyEvent& event)
    {
        ImGui::SetCurrentContext(m_Context);

        ImGuiIO& io = ImGui::GetIO();

        io.AddKeyEvent(
            ImGuiMod_Ctrl,
            event.Modifiers.bCtrl);

        io.AddKeyEvent(
            ImGuiMod_Shift,
            event.Modifiers.bShift);

        io.AddKeyEvent(
            ImGuiMod_Alt,
            event.Modifiers.bAlt);

        ImGuiKey key = ConvertKey(event.Key);

        if (key != ImGuiKey_None)
        {
            io.AddKeyEvent(key, false);
        }

        return io.WantCaptureKeyboard;
    }
    bool ImSlateRenderer::OnFocusReceived()
    {
        ImGui::SetCurrentContext(m_Context);

        ImGui::GetIO().AddFocusEvent(true);

        return false;
    }
    bool ImSlateRenderer::OnFocusLost()
    {
        ImGui::SetCurrentContext(m_Context);

        ImGui::GetIO().AddFocusEvent(false);

        return false;
    }
    bool ImSlateRenderer::OnResize(
        uint32_t width,
        uint32_t height)
    {
        ImGui::SetCurrentContext(m_Context);

        ImGuiIO& io = ImGui::GetIO();

        io.DisplaySize = ImVec2(
            static_cast<float>(width),
            static_cast<float>(height));

        return false;
    }
}
