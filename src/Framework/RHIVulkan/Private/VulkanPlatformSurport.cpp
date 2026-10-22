#include "VulkanPlatformSurport.h"
#ifdef _WIN32
#include <windows.h>
#include <vulkan/vulkan_win32.h>
#elif defined(__APPLE__)
#include <vulkan/vulkan_metal.h>
#include <QuartzCore/CAMetalLayer.h>
#elif defined(__linux__)
#include <vulkan/vulkan_xlib.h>
#include <X11/Xlib.h>
#endif
#include "VulkanFuncWrapper.h"

namespace RHIVulkan{
    
    bool VulkanPlatformSupport::CreateVulkanSurface(VkInstance instance, void* windowHandle, VkSurfaceKHR* surface)
    {
#ifdef _WIN32
        VkWin32SurfaceCreateInfoKHR createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
        createInfo.hwnd = (HWND)windowHandle;
        createInfo.hinstance = nullptr;

        return VKFunc::CreateWin32SurfaceKHR(instance, &createInfo, surface);
#else
        Log::Error("Unsupported platform!");
        return false;
#endif
    }


    bool VulkanPlatformSupport::GetFramebufferSize(void* windowHandle, int& width, int& height)
    {
#ifdef _WIN32
        HWND hwnd = static_cast<HWND>(windowHandle);
        RECT rect;
        if (!GetClientRect(hwnd, &rect))
            return false;
        width = rect.right - rect.left;
        height = rect.bottom - rect.top;
        return true;

#elif defined(__APPLE__)
        NSWindow* nsWindow = (__bridge NSWindow*)windowHandle;
        NSView* contentView = [nsWindow contentView];
        if (!contentView)
            return false;

        // 获取 backing layer 尺寸
        NSRect bounds = [contentView bounds];
        NSScreen* screen = [nsWindow screen];
        CGFloat scale = [screen backingScaleFactor]; // Retina 屏幕需要缩放
        width = bounds.size.width * scale;
        height = bounds.size.height * scale;
        return true;

#elif defined(__linux__)
        // 假设 X11
        Window win = reinterpret_cast<Window>(windowHandle);
        Display* dpy = XOpenDisplay(nullptr);
        if (!dpy)
            return false;

        XWindowAttributes attr;
        if (!XGetWindowAttributes(dpy, win, &attr))
        {
            XCloseDisplay(dpy);
            return false;
        }

        width = attr.width;
        height = attr.height;
        XCloseDisplay(dpy);
        return true;

#else
        return false; // Unsupported platform
#endif
    }

    std::vector<const char*> VulkanPlatformSupport::GetPlatformWantedLayers() 
    {
        std::vector<const char*> layers;
#ifdef DEBUG_INFO
        layers.push_back("VK_LAYER_KHRONOS_validation");
#endif
       // 平台层
#ifdef _WIN32
    // Windows 没有特殊 layer
#elif defined(__linux__)
    // Linux 没有特殊 layer
#elif defined(__APPLE__)
    // MoltenVK / macOS 没有特殊 layer
#endif
       return layers;
    }

    std::vector<const char*> VulkanPlatformSupport::GetPlatformWantedExtentions()
    {
        std::vector<const char*> extensions;
#if defined DEBUG_INFO
        // Debug Utils 用于 debug / marker
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif
        extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
#ifdef _WIN32
        extensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#elif defined(__linux__)
        extensions.push_back(VK_KHR_XLIB_SURFACE_EXTENSION_NAME);
        // 或者 VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME
#elif defined(__APPLE__)
        extensions.push_back(VK_EXT_METAL_SURFACE_EXTENSION_NAME);
#endif
        return extensions;
    }

    std::vector<const char*> VulkanPlatformSupport::GetPlatformWantedDeviceExtentions()
    {
        std::vector<const char*> res;
        //
        res.push_back("VK_KHR_maintenance1");
        return res;
    }

}