#pragma once
#include "LayoutMgr.h"
#include <Window.h>
#include "SlateRenderer.h"

namespace ImGUISlate
{
    class ImSlateRenderer;

    class IMGUISLATE_API ImMainWindow final : public SlateCore::Window
    {
    public:
        ImMainWindow(
            int width,
            int height,
            const std::string& title, SlateCore::SlateRenderer* renderer);

        ~ImMainWindow() override;
        void Draw();
        bool Initialize() override;
    public:
        ImSlateRenderer* GetRenderer() const;
        void AddWidget(SlateCore::Widget* widget, const LayoutParams& params);
        void RemoveWidget(SlateCore::Widget* widget) override;
        // Window registrations must stay synchronized with the layout.
        void SetRootWidget(SlateCore::Widget* widget) = delete;
        SlateCore::Widget* GetRootWidgets() const = delete;
    public:
        bool OnFocusReceived() override;
        bool OnFocusLost() override;
        bool OnResize(uint32_t width, uint32_t height) override;


        bool OnMouseMove(
            const SlateCore::MouseMoveEvent& event) override;

        bool OnMouseButton(
            const SlateCore::MouseButtonEvent& event) override;

        bool OnMouseWheel(
            const SlateCore::MouseWheelEvent& event) override;

        bool OnKeyDown(
            const SlateCore::KeyEvent& event) override;

        bool OnKeyUp(
            const SlateCore::KeyEvent& event) override;

    private:
        SlateCore::SlateRenderer* m_WindowRenderer = nullptr;
        std::unique_ptr<LayoutManager> m_Layout;
        std::unique_ptr<ImSlateRenderer> m_Renderer;
    };
}