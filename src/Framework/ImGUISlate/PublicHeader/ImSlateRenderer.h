#pragma once

#include "Window.h"
#include "SlateRenderer.h"
#include "EventHandler.h"
#include <memory>
#include <unordered_map>
#include <list>
#include <chrono>

struct ImGuiContext;

namespace ImGUISlate {

    class ImGuiRHIBackend;

    class ImSlateRenderer : public SlateCore::EventHandler
    {
    public:
        ImSlateRenderer(SlateCore::Window* window, SlateCore::SlateRenderer* renderer);
        ~ImSlateRenderer();

        void Render();


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

        void UpdateDisplaySize();
        ImGuiContext* m_Context = nullptr;
        SlateCore::Window* m_Window = nullptr;
        SlateCore::SlateRenderer* m_WindowRenderer = nullptr;
        std::unique_ptr<ImGuiRHIBackend> m_Backend;
        std::chrono::steady_clock::time_point m_LastFrame = std::chrono::steady_clock::now();
    };
}
