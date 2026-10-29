#include "ImSlateRenderer.h"
#include "imgui_impl_rhi.h"
#include "SlateWidget.h"
#include "SlateViewport.h"
#include "RHIApi.h"
#include "imgui_internal.h"
#include <chrono>

namespace
{
    bool WantsMouseAtPosition(int x, int y)
    {
        const auto& context = *ImGui::GetCurrentContext();
        const auto& io = context.IO;
        if (context.WantCaptureMouseNextFrame != -1)
            return context.WantCaptureMouseNextFrame != 0;
        if (!context.OpenPopupStack.empty()) return true;

        // Preserve ImGui's press ownership. Re-running its frame update here
        // would reassign a still-latched MouseClicked at the new mouse position.
        int earliestButton = -1;
        bool anyDown = false;
        for (int button = 0; button < IM_ARRAYSIZE(io.MouseDown); ++button)
        {
            anyDown |= io.MouseDown[button];
            if ((io.MouseDown[button] || io.MouseReleased[button]) &&
                (earliestButton < 0 || io.MouseClickedTime[button] < io.MouseClickedTime[earliestButton]))
                earliestButton = button;
        }
        if (earliestButton >= 0 && !io.MouseDownOwned[earliestButton]) return false;

        ImGuiWindow* hovered = nullptr;
        ImGuiWindow* underMovingWindow = nullptr;
        if (!(io.ConfigFlags & ImGuiConfigFlags_NoMouse))
            ImGui::FindHoveredWindowEx({static_cast<float>(x),static_cast<float>(y)}, false, &hovered, &underMovingWindow);
        return anyDown || hovered != nullptr;
    }
}

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
    case SlateCore::EKey::Backspace: return ImGuiKey_Backspace;
    case SlateCore::EKey::Delete: return ImGuiKey_Delete;
    case SlateCore::EKey::Enter: return ImGuiKey_Enter;
    case SlateCore::EKey::Tab: return ImGuiKey_Tab;
    case SlateCore::EKey::Left: return ImGuiKey_LeftArrow;
    case SlateCore::EKey::Right: return ImGuiKey_RightArrow;
    case SlateCore::EKey::Up: return ImGuiKey_UpArrow;
    case SlateCore::EKey::Down: return ImGuiKey_DownArrow;
    case SlateCore::EKey::Home: return ImGuiKey_Home;
    case SlateCore::EKey::End: return ImGuiKey_End;
    case SlateCore::EKey::C: return ImGuiKey_C;
    case SlateCore::EKey::V: return ImGuiKey_V;
    case SlateCore::EKey::X: return ImGuiKey_X;
    case SlateCore::EKey::Y: return ImGuiKey_Y;
    case SlateCore::EKey::Z: return ImGuiKey_Z;

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
    bool ImSlateRenderer::OnTextInput(uint16_t character)
    {
        ImGui::SetCurrentContext(m_Context);
        ImGui::GetIO().AddInputCharacterUTF16(character);
        return ImGui::GetIO().WantTextInput;
    }
    ImSlateRenderer::ImSlateRenderer(SlateCore::Window* window, SlateCore::SlateRenderer* renderer)
        : m_Window(window), m_WindowRenderer(renderer)
    {
        IMGUI_CHECKVERSION();
        m_Context = ImGui::CreateContext();
        ImGui::SetCurrentContext(m_Context);
        ImGui::StyleColorsDark();
        ImGui::GetIO().BackendFlags |= ImGuiBackendFlags_HasMouseCursors;
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
    void ImSlateRenderer::WaitForLastDraw()
    {
        // Render returns the existing window completion marker, covering UI
        // sampling and Present. Preserve the swapchain during internal relayout.
        if (m_LastSubmission)
            RHI::GRHIApi->GetQueue(RHI::EQueueType::Graphics)->WaitValue(m_LastSubmission);
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
        if (!(ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_NoMouseCursorChange))
        {
            SlateCore::EMouseCursor cursor = SlateCore::EMouseCursor::Arrow;
            switch (ImGui::GetMouseCursor())
            {
            case ImGuiMouseCursor_TextInput: cursor = SlateCore::EMouseCursor::TextInput; break;
            case ImGuiMouseCursor_ResizeAll: cursor = SlateCore::EMouseCursor::ResizeAll; break;
            case ImGuiMouseCursor_ResizeNS: cursor = SlateCore::EMouseCursor::ResizeNS; break;
            case ImGuiMouseCursor_ResizeEW: cursor = SlateCore::EMouseCursor::ResizeEW; break;
            case ImGuiMouseCursor_ResizeNESW: cursor = SlateCore::EMouseCursor::ResizeNESW; break;
            case ImGuiMouseCursor_ResizeNWSE: cursor = SlateCore::EMouseCursor::ResizeNWSE; break;
            case ImGuiMouseCursor_Hand: cursor = SlateCore::EMouseCursor::Hand; break;
            case ImGuiMouseCursor_Wait: cursor = SlateCore::EMouseCursor::Wait; break;
            case ImGuiMouseCursor_Progress: cursor = SlateCore::EMouseCursor::Progress; break;
            case ImGuiMouseCursor_NotAllowed: cursor = SlateCore::EMouseCursor::NotAllowed; break;
            case ImGuiMouseCursor_None: cursor = SlateCore::EMouseCursor::None; break;
            default: break;
            }
            if (ImGui::GetIO().MouseDrawCursor) cursor = SlateCore::EMouseCursor::None;
            m_Window->SetMouseCursor(cursor);
        }
        // CPU draw data is translated now; queued backends only retain RHI commands and owned uploads.
        const auto fence = m_WindowRenderer->Render(m_Window, [this](RHI::RHIGraphicCommandList& cmd,
            RHI::RHITexture* target, std::vector<RHI::RHIWaitInfo>& waits, SlateCore::SlateRenderer::DrawResources& resources)
        { m_Backend->RenderDrawData(ImGui::GetDrawData(), cmd, target, waits, resources); });
        if (fence)
        {
            m_LastSubmission = fence;
            m_Backend->EndFrame(fence);
        }
    }
    bool ImSlateRenderer::OnMouseMove(
        const SlateCore::MouseMoveEvent& event)
    {
        ImGui::SetCurrentContext(m_Context);

        ImGuiIO& io = ImGui::GetIO();

        io.AddMousePosEvent(
            static_cast<float>(event.X),
            static_cast<float>(event.Y));

        return WantsMouseAtPosition(event.X,event.Y);
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

        return WantsMouseAtPosition(event.X,event.Y);
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

        return WantsMouseAtPosition(event.X,event.Y);
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
