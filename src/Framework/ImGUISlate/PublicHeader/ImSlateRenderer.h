#pragma once

#include "Window.h"
#include "EventHandler.h"
#include <memory>
#include <unordered_map>
#include <list>

struct ImGuiContext;

namespace ImGUISlate {

    class ImWidget;

    class ImSlateRenderer : public SlateCore::EventHandler
    {
    public:
        ImSlateRenderer(SlateCore::Window* window);
        ~ImSlateRenderer();

        void Render();
        void RegisterWidget(ImWidget* widget);

        virtual bool OnMouseMove(
            const SlateCore::MouseMoveEvent& event)override;

        virtual bool OnMouseButton(
            const SlateCore::MouseButtonEvent& event)override;

        virtual bool OnMouseWheel(
            const SlateCore::MouseWheelEvent& event)override;

        virtual bool OnKeyDown(
            const SlateCore::KeyEvent& event)override;

        virtual bool OnKeyUp(
            const SlateCore::KeyEvent& event)override;

        virtual bool OnFocusReceived() override;

        virtual bool OnFocusLost()override;

        virtual bool OnResize(
            uint32_t width,
            uint32_t height) override;
    private:

        bool EnsureOpenGLContext();
        void DestroyOpenGLContexts();
        void UpdateDisplaySize();

        ImGuiContext* m_Context = nullptr;
        bool m_bOpenGLBackendInitialized = false;
        SlateCore::Window* m_Window = nullptr;
        void* m_DeviceContext = nullptr;
        void* m_GLContext = nullptr;
        std::vector<ImWidget*> m_Widgets;
    };

    IMGUISLATE_API ImSlateRenderer* CreateSlateRenderer(SlateCore::Window* window);
}