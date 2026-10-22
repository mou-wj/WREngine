#include "ImSlateRenderer.h"

#include "ImWidget.h"

#include "imgui.h"
#include "backends/imgui_impl_opengl3.h"

#include <memory>

#ifdef _WIN32
#include <Windows.h>
#include <gl/GL.h>
#endif

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

namespace ImGUISlate {

    namespace {
        std::unique_ptr<ImSlateRenderer> GImSlateRenderer;
    }

    ImSlateRenderer::ImSlateRenderer(SlateCore::Window* window)
    {
        IMGUI_CHECKVERSION();
        Context = ImGui::CreateContext();
        ImGui::SetCurrentContext(Context);
        ImGui::StyleColorsDark();
        Window = window;
        //ImGuiIO& io = ImGui::GetIO();
        //unsigned char* fontPixels = nullptr;
        //int fontWidth = 0;
        //int fontHeight = 0;
        //io.Fonts->GetTexDataAsRGBA32(&fontPixels, &fontWidth, &fontHeight);
        EnsureOpenGLContext();

    }

    ImSlateRenderer::~ImSlateRenderer()
    {
        if (bOpenGLBackendInitialized)
        {
            ImGui::SetCurrentContext(Context);
            ImGui_ImplOpenGL3_Shutdown();
            bOpenGLBackendInitialized = false;
        }

        DestroyOpenGLContexts();

        if (Context)
        {
            ImGui::SetCurrentContext(Context);
            ImGui::DestroyContext(Context);
            Context = nullptr;
        }
    }

    bool ImSlateRenderer::EnsureOpenGLContext()
    {
        SlateCore::Window* window = Window;
        if (!window)
        {
            return false;
        }

#ifndef _WIN32
        return false;
#else

        HWND hwnd = static_cast<HWND>(window->GetNativeHandle());
        if (!hwnd)
        {
            return false;
        }

        HDC hdc = GetDC(hwnd);
        if (!hdc)
        {
            return false;
        }

        PIXELFORMATDESCRIPTOR pfd = {};
        pfd.nSize = sizeof(PIXELFORMATDESCRIPTOR);
        pfd.nVersion = 1;
        pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
        pfd.iPixelType = PFD_TYPE_RGBA;
        pfd.cColorBits = 32;
        pfd.cDepthBits = 24;
        pfd.cStencilBits = 8;
        pfd.iLayerType = PFD_MAIN_PLANE;

        int pixelFormat = ChoosePixelFormat(hdc, &pfd);
        if (pixelFormat == 0)
        {
            ReleaseDC(hwnd, hdc);
            return false;
        }

        if (!SetPixelFormat(hdc, pixelFormat, &pfd))
        {
            ReleaseDC(hwnd, hdc);
            return false;
        }

        HGLRC glrc = wglCreateContext(hdc);
        if (!glrc)
        {
            ReleaseDC(hwnd, hdc);
            return false;
        }

        if (!wglMakeCurrent(hdc, glrc))
        {
            wglDeleteContext(glrc);
            ReleaseDC(hwnd, hdc);
            return false;
        }

        DeviceContext = hdc;
        GLContext = glrc;

        if (!bOpenGLBackendInitialized)
        {
            ImGui::SetCurrentContext(Context);
            bOpenGLBackendInitialized = ImGui_ImplOpenGL3_Init("#version 330");
        }

        return bOpenGLBackendInitialized;
#endif
    }

    void ImSlateRenderer::DestroyOpenGLContexts()
    {
        if (GLContext)
        {
            wglMakeCurrent(nullptr, nullptr);

            wglDeleteContext(
                (HGLRC)GLContext);

            GLContext = nullptr;
        }

        if (DeviceContext)
        {
            ReleaseDC(
                (HWND)Window->GetNativeHandle(),
                (HDC)DeviceContext);

            DeviceContext = nullptr;
        }
    }

    void ImSlateRenderer::Render()
    {
        if (!Window || !Context)
        {
            return;
        }
        ImGuiIO& io = ImGui::GetIO();
        UpdateDisplaySize();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui::NewFrame();

        //-------------------------------------
        // Draw Widgets
        //-------------------------------------

        for (ImWidget* widget : Widgets)
        {
            if (widget)
            {
                widget->Draw();
            }
        }

        

        //-------------------------------------
        // Render
        //-------------------------------------

        ImGui::Render();

#ifdef _WIN32

        

        glViewport(
            0,
            0,
            static_cast<GLsizei>(io.DisplaySize.x),
            static_cast<GLsizei>(io.DisplaySize.y));

        glClearColor(
            0.08f,
            0.08f,
            0.10f,
            1.0f);

        glClear(GL_COLOR_BUFFER_BIT);

        ImGui_ImplOpenGL3_RenderDrawData(
            ImGui::GetDrawData());

        SwapBuffers(
            static_cast<HDC>(DeviceContext));

#endif
    }

    void ImSlateRenderer::RegisterWidget(ImWidget* widget)
    {
        
        if (!Window)
        {
            return;
        }
        Widgets.push_back(widget);
    }

    void ImSlateRenderer::UpdateDisplaySize()
    {
        ImGuiIO& io =
            ImGui::GetIO();

        const auto& geometry =
            Window
            ->GetFramebufferSize();

        io.DisplaySize =
            ImVec2(
                geometry.X,
                geometry.Y);
    }

    ImSlateRenderer* CreateSlateRenderer(SlateCore::Window* window)
    {
        if (!GImSlateRenderer)
        {
            GImSlateRenderer = std::make_unique<ImSlateRenderer>(window);
        }

        return GImSlateRenderer.get();
    }

    bool ImSlateRenderer::OnMouseMove(
        const SlateCore::MouseMoveEvent& event)
    {
        ImGui::SetCurrentContext(Context);

        ImGuiIO& io = ImGui::GetIO();

        io.AddMousePosEvent(
            static_cast<float>(event.X),
            static_cast<float>(event.Y));

        return io.WantCaptureMouse;
    }
    bool ImSlateRenderer::OnMouseButton(
        const SlateCore::MouseButtonEvent& event)
    {
        ImGui::SetCurrentContext(Context);

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
        ImGui::SetCurrentContext(Context);

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
        ImGui::SetCurrentContext(Context);

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
        ImGui::SetCurrentContext(Context);

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
        ImGui::SetCurrentContext(Context);

        ImGui::GetIO().AddFocusEvent(true);

        return false;
    }
    bool ImSlateRenderer::OnFocusLost()
    {
        ImGui::SetCurrentContext(Context);

        ImGui::GetIO().AddFocusEvent(false);

        return false;
    }
    bool ImSlateRenderer::OnResize(
        uint32_t width,
        uint32_t height)
    {
        ImGui::SetCurrentContext(Context);

        ImGuiIO& io = ImGui::GetIO();

        io.DisplaySize = ImVec2(
            static_cast<float>(width),
            static_cast<float>(height));

        return false;
    }
}