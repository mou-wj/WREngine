#pragma once
#include <cstdint>
#include "RHIDefine.h"

#if defined(_WIN32)
#include <Windows.h>
#endif
namespace RHIOpenGL
{
    class OpenGLPlatformContext
    {
    public:
        explicit OpenGLPlatformContext(void* windowHandle,RHI::ERHIFormat pixelFormat);
        ~OpenGLPlatformContext();

        OpenGLPlatformContext(const OpenGLPlatformContext&) = delete;
        OpenGLPlatformContext& operator=(const OpenGLPlatformContext&) = delete;

        virtual bool Initialize() = 0;
        virtual void Shutdown() = 0;

        virtual bool MakeCurrent() = 0;
        virtual void DoneCurrent() = 0;

        virtual void SwapBuffers() = 0;

        virtual void Resize(uint32_t width, uint32_t height) = 0;

        void* GetWindowHandle() const
        {
            return mWindowHandle;
        }

        uint32_t GetWidth() const
        {
            return mWidth;
        }

        uint32_t GetHeight() const
        {
            return mHeight;
        }

        bool IsInitialized() const
        {
            return mInitialized;
        }

    protected:
        void* mWindowHandle = nullptr;
        RHI::ERHIFormat mPixelFormat;

        bool mInitialized = false;
        uint32_t mWidth = 0;
        uint32_t mHeight = 0;
    };
    OpenGLPlatformContext* CreateOpenGLPlatformContext(void* windowHandle,RHI::ERHIFormat pixelFormat);

    bool InitializePlatformSurport();
	void ShutdownPlatformSurport();
	bool InitializeBackPlatformSurport();
    void MakeBackCurrent(bool enable);
    bool HasCurrentContext();


#if defined(_WIN32)
    class OpenGLPlatformContextWin32 : public OpenGLPlatformContext {
    public:
        OpenGLPlatformContextWin32(void* windowHandle, RHI::ERHIFormat pixelFormat);
        ~OpenGLPlatformContextWin32();
        bool Initialize() override;
        void Shutdown() override;
        bool MakeCurrent() override;
		void DoneCurrent() override;
		void SwapBuffers() override;
		void Resize(uint32_t width, uint32_t height) override;

	private:
        HWND  mWindow = nullptr;
        HDC   mDeviceContext = nullptr;
        HGLRC mContext = nullptr;
    };

#endif

}
