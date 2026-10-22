#include "VulkanSwapchain.h"
#include "VulkanDevice.h"
#include "VulkanPlatformSurport.h"
#include "VulkanRHIApi.h"
#include "VulkanQueue.h"
#include "VulkanResource.h"
#include "Log.h"
#include "VulkanRHIUtils.h"
#include "VulkanFuncWrapper.h"

namespace RHIVulkan {

    VulkanSwapchain::VulkanSwapchain(VulkanDevice* device, const SwapchainDesc& desc, std::vector<VkImage>& outImages)
        : m_Device(device)
    {
        VulkanRHIApi* api = m_Device->GetRhiApi();
        VkInstance instance = const_cast<VkInstance>(api->GetInstance());

        // Create surface from window handle
        if (!VulkanPlatformSupport::CreateVulkanSurface(instance, desc.WindowHandle, &m_Surface))
        {
            return;
        }


        VkPhysicalDevice physicalDevice = m_Device->GetPhysicalDevice();

        // 1. 获取表面支持细节
        SwapChainSupportDetails swapChainSupport = QuerySwapChainSupport(physicalDevice);

        auto preferredFormat = TransformFormatFrom(desc.PreferredFormat);

        // 2. 选择最佳表面格式和颜色空间
        VkSurfaceFormatKHR surfaceFormat = ChooseSwapSurfaceFormat(swapChainSupport.Formats, preferredFormat);

        // 3. 选择最佳呈现模式
        VkPresentModeKHR presentMode = ChooseSwapPresentMode(swapChainSupport.PresentModes);

        // 4. 选择交换链范围
        VkExtent2D extent = ChooseSwapExtent(swapChainSupport.Capabilities, desc.Width, desc.Height, desc.WindowHandle);

        // 5. 确定要使用的图像数量
        uint32_t imageCount = swapChainSupport.Capabilities.minImageCount + 1;
        if (swapChainSupport.Capabilities.maxImageCount > 0 && imageCount > swapChainSupport.Capabilities.maxImageCount) {
            imageCount = swapChainSupport.Capabilities.maxImageCount;
        }

        // 6. 填充交换链创建信息结构体
        VkSwapchainCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        createInfo.surface = m_Surface; // Use the created surface
        createInfo.minImageCount = imageCount;
        createInfo.imageFormat = surfaceFormat.format;
        createInfo.imageColorSpace = surfaceFormat.colorSpace;
        createInfo.imageExtent = extent;
        createInfo.imageArrayLayers = 1;
        createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
        m_Device->InitPresentQueue(m_Surface); // Initialize the present queue

        uint32_t presentQueueFamilyIndex = m_Device->GetPresentQueueFamilyIndex();
        uint32_t graphicQueueFamilyIndex = m_Device->GetGraphicsQueueFamilyIndex();
        if (presentQueueFamilyIndex != graphicQueueFamilyIndex) {
			uint32_t queueFamilyIndices[] = { presentQueueFamilyIndex, graphicQueueFamilyIndex };
			createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
			createInfo.queueFamilyIndexCount = 2;
			createInfo.pQueueFamilyIndices = queueFamilyIndices;
        }
        else {
            uint32_t queueFamilyIndices[] = { presentQueueFamilyIndex };
            createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
            createInfo.queueFamilyIndexCount = 1;
            createInfo.pQueueFamilyIndices = queueFamilyIndices;
        }



        createInfo.preTransform = swapChainSupport.Capabilities.currentTransform;
        createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        createInfo.presentMode = presentMode;
        createInfo.clipped = VK_TRUE;
        createInfo.oldSwapchain = VK_NULL_HANDLE;

        // 7. 创建交换链
        VkDevice deviceHandle = m_Device->GetHandle();
        if (!VKFunc::CreateSwapchainKHR(deviceHandle, &createInfo, &m_Swapchain)) {
            return;
        }

        // 8. 检索交换链图像句柄
        VKFunc::GetSwapchainImagesKHR(deviceHandle, m_Swapchain, &imageCount, nullptr);
        std::vector<VkImage>& images = outImages;
        images.resize(imageCount);
        VKFunc::GetSwapchainImagesKHR(deviceHandle, m_Swapchain, &imageCount, images.data());
        


        // 9. 保存图像格式和范围
        m_ImageFormat = surfaceFormat.format;
        m_Extent = extent;
    }

    VulkanSwapchain::~VulkanSwapchain()
    {
        Cleanup();
    }

    void VulkanSwapchain::Cleanup()
    {
        VkDevice deviceHandle = m_Device->GetHandle();

        if (m_Swapchain != VK_NULL_HANDLE) {
            VKFunc::DestroySwapchainKHR(deviceHandle, m_Swapchain);
            m_Swapchain = VK_NULL_HANDLE;
        }

        // Destroy the surface
        if (m_Surface != VK_NULL_HANDLE) {
            VKFunc::DestroySurfaceKHR(m_Device->GetInstance(), m_Surface);
            m_Surface = VK_NULL_HANDLE;
        }
    }


    bool VulkanSwapchain::Present(VulkanQueue* presentQueue, VulkanSemaphore* drawDoneSemaphore)
    {
        VulkanQueue* queue = m_Device->GetPresentQueue();
        if (queue == nullptr) {
            return false;
        }
        VkQueue queueHandle = presentQueue->GetHandle();
        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        VkSemaphore waitSemaphore = VK_NULL_HANDLE;
        if (drawDoneSemaphore)
        {
            waitSemaphore = drawDoneSemaphore->GetHandle();
            presentInfo.waitSemaphoreCount = 1;
            presentInfo.pWaitSemaphores = &waitSemaphore;
        }
        else
        {
            presentInfo.waitSemaphoreCount = 0;
            presentInfo.pWaitSemaphores = nullptr;
        }

        VkSwapchainKHR swapChains[] = { m_Swapchain };
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = swapChains;

        presentInfo.pImageIndices = &m_CurrentImageIndex;

        auto result = VKFunc::QueuePresentKHR(queueHandle, &presentInfo);

        if (result != true) {
            return false;
        }

        return true;
    }

    void VulkanSwapchain::AcquireNextImage(VulkanSemaphore* signalSemaphore, int* outImageIndex)
    {
        uint32_t imageIndex = 0;
        if (VKFunc::AcquireNextImageKHR(m_Device->GetHandle(), m_Swapchain, UINT64_MAX, signalSemaphore->GetHandle(), VK_NULL_HANDLE, &imageIndex)) {
            *outImageIndex = imageIndex;
            m_CurrentImageIndex = imageIndex;
        }
        else {
            *outImageIndex = -1;
        }


    }


    SwapChainSupportDetails VulkanSwapchain::QuerySwapChainSupport(VkPhysicalDevice device) {
        SwapChainSupportDetails details;

        // Surface capabilities
        VKFunc::GetPhysicalDeviceSurfaceCapabilitiesKHR(device, m_Surface, &details.Capabilities);

        // Surface formats
        uint32_t formatCount;
        VKFunc::GetPhysicalDeviceSurfaceFormatsKHR(device, m_Surface, &formatCount, nullptr);

        if (formatCount != 0) {
            details.Formats.resize(formatCount);
            VKFunc::GetPhysicalDeviceSurfaceFormatsKHR(device, m_Surface, &formatCount, details.Formats.data());
        }

        // Present modes
        uint32_t presentModeCount;
        VKFunc::GetPhysicalDeviceSurfacePresentModesKHR(device, m_Surface, &presentModeCount, nullptr);

        if (presentModeCount != 0) {
            details.PresentModes.resize(presentModeCount);
            VKFunc::GetPhysicalDeviceSurfacePresentModesKHR(device, m_Surface, &presentModeCount, details.PresentModes.data());
        }

        return details;
    }

    VkSurfaceFormatKHR VulkanSwapchain::ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats, VkFormat wantForamt) {
        //检查是否支持预想的格式
        for (const auto& availableFormat : availableFormats) {
            if (availableFormat.format == wantForamt && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                return availableFormat;
            }
        }
        
        return availableFormats[0];
    }

    VkPresentModeKHR VulkanSwapchain::ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) {
        for (const auto& availablePresentMode : availablePresentModes) {
            if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
                return availablePresentMode;
            }
        }

        return VK_PRESENT_MODE_FIFO_KHR;
    }

    VkExtent2D VulkanSwapchain::ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities, uint32_t width, uint32_t height, void* windowHandle) {
        if (capabilities.currentExtent.width != UINT32_MAX) {
            return capabilities.currentExtent;
        }
        else {
            int windowWidth = 0, windowHeight = 0;
            VulkanPlatformSupport::GetFramebufferSize(windowHandle, windowWidth, windowHeight);

            VkExtent2D actualExtent = {
                static_cast<uint32_t>(windowWidth),
                static_cast<uint32_t>(windowHeight)
            };

            actualExtent.width = ::std::clamp(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
            actualExtent.height = ::std::clamp(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

            return actualExtent;
        }
    }

}
