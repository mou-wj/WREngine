#pragma once

#ifdef _WIN32

#include "PlatformSurface.h"
#include "EventHandler.h"
#include <windows.h>

namespace SlateCore
{
    class SLATECORE_API Win32Surface : public PlatformSurface
    {
    public:
        Win32Surface(
            int width,
            int height,
            const std::string& title,
            PlatformSurfaceOwner* owner);

        ~Win32Surface() override = default;

    public:
        bool Initialize() override;
        void PollEvents() override;
        void Shutdown() override;

        void Show() override;
        void Hide() override;
        void Close() override;

        void SetPosition(int x, int y) override;
        void SetSize(int width, int height) override;

        void* GetNativeHandle() const override;
        Core::Int2 GetFramebufferSize() const override;

    private:
        static LRESULT CALLBACK WndProcSetup(
            HWND hwnd,
            UINT msg,
            WPARAM wParam,
            LPARAM lParam);

        LRESULT WndProc(
            HWND hwnd,
            UINT msg,
            WPARAM wParam,
            LPARAM lParam);

        bool RouteInputMessage(
            UINT msg,
            WPARAM wParam,
            LPARAM lParam);

        EKey TranslateKey(
            WPARAM key) const;

    private:
        HWND m_HWnd = nullptr;
    };
}

#endif
