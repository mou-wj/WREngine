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
            return _mWindowHandle;
        }

        uint32_t GetWidth() const
        {
            return _mWidth;
        }

        uint32_t GetHeight() const
        {
            return _mHeight;
        }

        bool IsInitialized() const
        {
            return _bMInitialized;
        }

    protected:
        void* _mWindowHandle = nullptr;
        RHI::ERHIFormat _mPixelFormat;

        bool _bMInitialized = false;
        uint32_t _mWidth = 0;
        uint32_t _mHeight = 0;
    };
    OpenGLPlatformContext* CreateOpenGLPlatformContext(void* windowHandle,RHI::ERHIFormat pixelFormat);

    bool InitializePlatformSurport();
	void ShutdownPlatformSurport();
    // worker 线程接管默认（bootstrap）上下文，作为其常驻 current context
    bool InitializeWorkerPlatformContext();
    // 把默认（bootstrap）上下文重新绑到调用线程（worker 在 Present 等临时切换后恢复用）
    void MakeDefaultContextCurrent();
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
        HWND  m_MWindow = nullptr;
        HDC   m_MDeviceContext = nullptr;
        HGLRC m_MContext = nullptr;
    };

#endif

}
