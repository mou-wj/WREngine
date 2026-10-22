#pragma once
#include <memory>
#include <string>
#include <functional>
#include <Math.hpp>
#include <SlateViewport.h>
#include "PlatformSurfaceOwner.h"
#include "PlatformSurface.h"

namespace SlateCore {

    class SLATECORE_API Window : public PlatformSurfaceOwner
    {
    public:
        Window(int width, int height, const std::string& title);
        virtual ~Window() override = default;

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

        void SetRootWidget(Widget* widget)
        {
            _rootWidget = widget;
        }

        Widget* GetRootWidgets() const
        {
            return _rootWidget;
        }

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
        Widget* _rootWidget = nullptr;
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