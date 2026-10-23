#pragma once
#include <memory>
#include <string>
#include <functional>
#include <vector>
#include <Math.hpp>
#include <SlateViewport.h>
#include "PlatformSurfaceOwner.h"
#include "PlatformSurface.h"

namespace SlateCore {

    class SLATECORE_API Window : public PlatformSurfaceOwner
    {
    public:
        Window(int width, int height, const std::string& title);
        ~Window() override;

        // 生命周期
        virtual bool Initialize();
        void PollEvents();
        void Shutdown();

        // 窗口操作
        void Show();
        void Hide();
        void Close();

        // 窗口属性
        int GetWidth() const { return _width; }
        int GetHeight() const { return _height; }
        Core::Int2 GetFramebufferSize() const;
        const std::string& GetTitle() const { return _title; }
        void* GetNativeHandle() const override;

        // Widgets are non-owning logical regions; remove them before destroying their owners.
        void AddWidget(Widget* widget);
        virtual void RemoveWidget(Widget* widget);
        const std::vector<Widget*>& GetWidgets() const { return _widgets; }
        void SetRootWidget(Widget* widget);
        Widget* GetRootWidgets() const { return _widgets.empty() ? nullptr : _widgets.front(); }
        bool HasMouseCapture() const { return _capturedWidget != nullptr; }

    public:
        void OnSurfaceCloseRequested() override;
        bool OnResize(uint32_t width, uint32_t height) override;

        bool OnFocusReceived() override;
        bool OnFocusLost() override;

        bool OnMouseMove(const MouseMoveEvent& event) override;
        bool OnMouseButton(const MouseButtonEvent& event) override;
        bool OnMouseWheel(const MouseWheelEvent& event) override;
        bool OnKeyDown(const KeyEvent& event) override;
        bool OnKeyUp(const KeyEvent& event) override;

    protected:
        PlatformSurface* GetOwnedSurface() const override;

    protected:
        int _width = 0;
        int _height = 0;
        std::string _title;
        std::vector<Widget*> _widgets;
        Widget* _focusedWidget = nullptr;
        Widget* _capturedWidget = nullptr;
        uint32_t _capturedButtons = 0;
        Widget* FindWidget(int x, int y) const;
        std::unique_ptr<PlatformSurface> _surface;
    };
	using WindowSP = std::shared_ptr<Window>;


    class SLATECORE_API WindowFactory {
    public:
        WindowFactory() = delete;
        static WindowSP CreateWindowSP(int w, int h, const std::string& title);

        using CreatorFunc = std::function<WindowSP(int, int, const std::string&)>;

    private:
        static CreatorFunc m_Creator;
    };

#define REGISTER_WINDOW_PLATFORM(PlatformClass) \
    CreatorFunc WindowFactory::m_Creator = [](int w, int h, const std::string& t) -> WindowSP{ \
        auto win = std::make_shared<PlatformClass>(w, h, t); \
        win->Initialize(); \
        return win; \
    };


}
