#include "OpenGLPlatformSurport.h"
#include "glad/gl.h"
#include <iostream>
namespace RHIOpenGL {
	OpenGLPlatformContext::OpenGLPlatformContext(void* windowHandle,RHI::ERHIFormat pixelFormat) : _mWindowHandle(windowHandle),_mPixelFormat(pixelFormat)
	{
	}

	OpenGLPlatformContext::~OpenGLPlatformContext()
	{
	}


#if defined(_WIN32)

    static LRESULT CALLBACK OpenGLBootstrapWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        return DefWindowProcA(window, message, wParam, lParam);
    }
    static GLADapiproc LoadOpenGLProcAddress(const char* name)
    {
        PROC proc = wglGetProcAddress(name);
        const auto procValue = reinterpret_cast<uintptr_t>(proc);
        if (proc == nullptr || procValue == 1 || procValue == 2 || procValue == 3 || procValue == static_cast<uintptr_t>(-1))
        {
            static HMODULE openGLModule = GetModuleHandleA("opengl32.dll");
            if (!openGLModule)
            {
                openGLModule = LoadLibraryA("opengl32.dll");
            }
            proc = openGLModule ? GetProcAddress(openGLModule, name) : nullptr;
        }

        return reinterpret_cast<GLADapiproc>(proc);
    }
    using WGLCreateContextAttribsProc = HGLRC(WINAPI*)(HDC, HGLRC, const int*);
    static HGLRC TryCreateOpenGL43Context(HDC deviceContext, HGLRC shareContext)
    {
        auto createContextAttribs = reinterpret_cast<WGLCreateContextAttribsProc>(
            wglGetProcAddress("wglCreateContextAttribsARB"));
        if (!createContextAttribs)
        {
            return nullptr;
        }

        const int attributes[] = {
            0x2091, 4, // WGL_CONTEXT_MAJOR_VERSION_ARB
            0x2092, 3, // WGL_CONTEXT_MINOR_VERSION_ARB
            0x9126, 0x00000001, // WGL_CONTEXT_PROFILE_MASK_ARB: core profile
            0
        };
        return createContextAttribs(deviceContext, shareContext, attributes);
    }
    static bool RegisterOpenGLBootstrapWindowClass(HINSTANCE instance)
    {
        WNDCLASSA windowClass{};
        windowClass.style = CS_OWNDC;
        windowClass.lpfnWndProc = OpenGLBootstrapWindowProc;
        windowClass.hInstance = instance;
        windowClass.lpszClassName = "RenderingLearnOpenGLBootstrap";

        if (RegisterClassA(&windowClass) != 0)
        {
            return true;
        }

        return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }

    static HGLRC CreateOpenGLBootstrapContext(HDC deviceContext)
    {
        PIXELFORMATDESCRIPTOR pixelFormatDescriptor{};
        pixelFormatDescriptor.nSize = sizeof(PIXELFORMATDESCRIPTOR);
        pixelFormatDescriptor.nVersion = 1;
        pixelFormatDescriptor.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
        pixelFormatDescriptor.iPixelType = PFD_TYPE_RGBA;
        pixelFormatDescriptor.cColorBits = 32;
        pixelFormatDescriptor.cDepthBits = 24;
        pixelFormatDescriptor.cStencilBits = 8;
        pixelFormatDescriptor.iLayerType = PFD_MAIN_PLANE;

        const int pixelFormat = ChoosePixelFormat(deviceContext, &pixelFormatDescriptor);
        if (pixelFormat == 0 || !SetPixelFormat(deviceContext, pixelFormat, &pixelFormatDescriptor))
        {
            return nullptr;
        }
        return wglCreateContext(deviceContext);
    }



    void* BootstrapWindow = nullptr;
    void* BootstrapDeviceContext = nullptr;
    void* BootstrapDefaultContext = nullptr;
    void* BootstrapGLContext = nullptr;
    OpenGLPlatformContextWin32::OpenGLPlatformContextWin32(void* windowHandle,RHI::ERHIFormat pixelFormat) : OpenGLPlatformContext(windowHandle, pixelFormat), m_MWindow(static_cast<HWND>(windowHandle)) {}

    OpenGLPlatformContextWin32::~OpenGLPlatformContextWin32() { Shutdown(); }

    static PIXELFORMATDESCRIPTOR GetPixelFormatDescriptor(const RHI::ERHIFormat& pixelFormat) {
        PIXELFORMATDESCRIPTOR pfd{};
        pfd.nSize = sizeof(PIXELFORMATDESCRIPTOR);
        pfd.nVersion = 1;
        pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
        pfd.iPixelType = PFD_TYPE_RGBA;
        pfd.iLayerType = PFD_MAIN_PLANE;

        switch (pixelFormat)
        {
        case RHI::ERHIFormat::R8G8B8_UNorm:
        case RHI::ERHIFormat::R8G8B8_SRGB:
        case RHI::ERHIFormat::B8G8R8_UNorm:
        case RHI::ERHIFormat::B8G8R8_SRGB:
            pfd.cColorBits = 24;
            pfd.cRedBits = 8;
            pfd.cGreenBits = 8;
            pfd.cBlueBits = 8;
            pfd.cAlphaBits = 0;
            break;

        case RHI::ERHIFormat::R8G8B8A8_UNorm:
        case RHI::ERHIFormat::R8G8B8A8_SRGB:
        case RHI::ERHIFormat::B8G8R8A8_UNorm:
        case RHI::ERHIFormat::B8G8R8A8_SRGB:
            pfd.cColorBits = 32;
            pfd.cRedBits = 8;
            pfd.cGreenBits = 8;
            pfd.cBlueBits = 8;
            pfd.cAlphaBits = 8;
            break;

        case RHI::ERHIFormat::D24_UNorm_S8_UInt:
            pfd.cDepthBits = 24;
            pfd.cStencilBits = 8;
            break;

        case RHI::ERHIFormat::D32_Float:
            pfd.cDepthBits = 32;
            break;

        default:
            return {};
        }

        return pfd;
    }

    bool OpenGLPlatformContextWin32::Initialize()
    {
        if (_bMInitialized) return true;
        if (!m_MWindow) return false;

        m_MDeviceContext = GetDC(m_MWindow);
        if (!m_MDeviceContext) return false;

        PIXELFORMATDESCRIPTOR pfd = GetPixelFormatDescriptor(_mPixelFormat);
        const int pixelFormat = ChoosePixelFormat(m_MDeviceContext, &pfd);
        if (pixelFormat == 0) { ReleaseDC(m_MWindow, m_MDeviceContext); m_MDeviceContext = nullptr; return false; }

        if (!SetPixelFormat(m_MDeviceContext, pixelFormat, &pfd)) { ReleaseDC(m_MWindow, m_MDeviceContext); m_MDeviceContext = nullptr; return false; }
        
        m_MContext = TryCreateOpenGL43Context(m_MDeviceContext, (HGLRC)BootstrapGLContext);
        if (!m_MContext) { ReleaseDC(m_MWindow, m_MDeviceContext); m_MDeviceContext = nullptr; return false; }

        

        RECT rect{};
        if (GetClientRect(m_MWindow, &rect)) { _mWidth = static_cast<uint32_t>(rect.right - rect.left); _mHeight = static_cast<uint32_t>(rect.bottom - rect.top); }

        _bMInitialized = true;
        return true;
    }

    void OpenGLPlatformContextWin32::Shutdown()
    {
        if (m_MContext && wglGetCurrentContext() == m_MContext) wglMakeCurrent(nullptr, nullptr);
        if (m_MContext) { wglDeleteContext(m_MContext); m_MContext = nullptr; }
        if (m_MDeviceContext && m_MWindow) { ReleaseDC(m_MWindow, m_MDeviceContext); m_MDeviceContext = nullptr; }
        _bMInitialized = false;
    }

    bool OpenGLPlatformContextWin32::MakeCurrent()
    {
        if (!m_MDeviceContext || !m_MContext) return false;
        return wglMakeCurrent(m_MDeviceContext, m_MContext) == TRUE;
    }

    void OpenGLPlatformContextWin32::DoneCurrent()
    {
        wglMakeCurrent(nullptr, nullptr);
    }

    void OpenGLPlatformContextWin32::SwapBuffers()
    {
        if (m_MDeviceContext)
        {
            ::SwapBuffers(m_MDeviceContext);
        }
            
    }

    void OpenGLPlatformContextWin32::Resize(uint32_t width, uint32_t height)
    {
        _mWidth = width;
        _mHeight = height;
    }

    

    OpenGLPlatformContext* CreateOpenGLPlatformContext(void* windowHandle, RHI::ERHIFormat pixelFormat)
    {

        return new OpenGLPlatformContextWin32(windowHandle,pixelFormat);

    }

    

    bool InitializePlatformSurport()
    {

        const HINSTANCE instance = GetModuleHandleA(nullptr);
        if (!RegisterOpenGLBootstrapWindowClass(instance))
        {
            std::cerr << "[OpenGLRHI] Failed to register bootstrap window class" << std::endl;
            return false;
        }

        HWND bootstrapWindow = CreateWindowExA(
            0,
            "RenderingLearnOpenGLBootstrap",
            "RenderingLearn OpenGL Bootstrap",
            WS_POPUP,
            0,
            0,
            1,
            1,
            nullptr,
            nullptr,
            instance,
            nullptr);
        if (!bootstrapWindow)
        {
            std::cerr << "[OpenGLRHI] Failed to create bootstrap window" << std::endl;
            return false;
        }
		BootstrapWindow = bootstrapWindow;
        HDC deviceContext = GetDC(bootstrapWindow);
        if (!deviceContext)
        {
            DestroyWindow(bootstrapWindow);
            std::cerr << "[OpenGLRHI] Failed to get bootstrap device context" << std::endl;
            return false;
        }
		BootstrapDeviceContext = deviceContext;



        HGLRC legacyContext = CreateOpenGLBootstrapContext(deviceContext);
        if (!legacyContext || !wglMakeCurrent(deviceContext, legacyContext))
        {
            if (legacyContext)
            {
                wglDeleteContext(legacyContext);
            }
            ReleaseDC(bootstrapWindow, deviceContext);
            DestroyWindow(bootstrapWindow);
            std::cerr << "[OpenGLRHI] Failed to create bootstrap WGL context" << std::endl;
            return false;
        }
        BootstrapDefaultContext = legacyContext;
        const int bootstrapVersion = gladLoadGL(LoadOpenGLProcAddress);
        if (bootstrapVersion == 0)
        {
            wglMakeCurrent(nullptr, nullptr);
            wglDeleteContext(legacyContext);
            ReleaseDC(bootstrapWindow, deviceContext);
            DestroyWindow(bootstrapWindow);
            std::cerr << "[OpenGLRHI] gladLoadGL failed for bootstrap WGL context" << std::endl;
            return false;
        }
        
        HGLRC modernContext = TryCreateOpenGL43Context(deviceContext, nullptr);
        if (modernContext)
        {
            wglMakeCurrent(nullptr, nullptr);
            
            if (!wglMakeCurrent(deviceContext, modernContext))
            {
                wglDeleteContext(modernContext);
                ReleaseDC(bootstrapWindow, deviceContext);
                DestroyWindow(bootstrapWindow);
                std::cerr << "[OpenGLRHI] Failed to activate OpenGL 4.3 context" << std::endl;
                return false;
            }
        }

        const int version = gladLoadGL(LoadOpenGLProcAddress);
        if (version == 0)
        {
            wglMakeCurrent(nullptr, nullptr);
            wglDeleteContext(legacyContext);
            ReleaseDC(bootstrapWindow, deviceContext);
            DestroyWindow(bootstrapWindow);
            std::cerr << "[OpenGLRHI] gladLoadGL failed" << std::endl;
            return false;
        }
        
        std::cout << "[OpenGLRHI] OpenGL environment initialized. GL version: "
            << GLAD_VERSION_MAJOR(version) << "." << GLAD_VERSION_MINOR(version)
            << std::endl;
        BootstrapGLContext = modernContext;

        // 主线程的初始化职责到此结束。BootstrapGLContext 后续由 worker 线程接管
        // （InitializeWorkerPlatformContext），主线程不再持有任何 current context，
        // 避免同 share group 跨线程持锁导致的 wglMakeCurrent 卡死。
        wglMakeCurrent(nullptr, nullptr);

        return true;
    }

    void ShutdownPlatformSurport()
    {
        if (BootstrapDefaultContext) 
        {
            wglDeleteContext(static_cast<HGLRC>(BootstrapDefaultContext));
            BootstrapDefaultContext = nullptr;
        }

        if (BootstrapGLContext)
		{
			wglMakeCurrent(nullptr, nullptr);
			wglDeleteContext(static_cast<HGLRC>(BootstrapGLContext));
			BootstrapGLContext = nullptr;
		}
        if (BootstrapDeviceContext)
        {
            ReleaseDC(static_cast<HWND>(BootstrapWindow), static_cast<HDC>(BootstrapDeviceContext));
            BootstrapDeviceContext = nullptr;
        }
		if (BootstrapWindow)
		{
			DestroyWindow(static_cast<HWND>(BootstrapWindow));
			BootstrapWindow = nullptr;
		}
    }
    bool InitializeWorkerPlatformContext()
    {
        // worker 线程接管默认（bootstrap）上下文作为常驻 current context。
        // 前置条件：InitializePlatformSurport 末尾已把主线程的 current 释放，
        // 否则同一个 HGLRC 不可能同时绑在两个线程上（wglMakeCurrent 会失败）。
        auto deviceContext = static_cast<HDC>(BootstrapDeviceContext);
        auto glContext = static_cast<HGLRC>(BootstrapGLContext);
        if (!deviceContext || !glContext)
        {
            return false;
        }
        return wglMakeCurrent(deviceContext, glContext) == TRUE;
    }

    void MakeDefaultContextCurrent()
    {
        // worker 在临时切走（如 Present 切到 swapchain 上下文并 DoneCurrent）之后，
        // 用这个恢复自己的常驻默认上下文。
        if (wglMakeCurrent(static_cast<HDC>(BootstrapDeviceContext),
            static_cast<HGLRC>(BootstrapGLContext)) != TRUE) {
            std::cerr << "[OpenGLRHI] Failed to make default context current" << std::endl;
        }
    }

    bool HasCurrentContext()
    {
        return wglGetCurrentContext() != nullptr;
    }
    
#endif

}
