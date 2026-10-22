#pragma once

#include "VulkanDevice.h"
#include "RHIDefine.h"
#include "VulkanSync.h"
#include <algorithm>
#include <vector>
namespace RHIVulkan{

struct SwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR Capabilities;
    std::vector<VkSurfaceFormatKHR> Formats;
    std::vector<VkPresentModeKHR> PresentModes;
};

class VulkanSwapchain
{
public:
    struct SwapchainDesc
    {
        void* WindowHandle = nullptr; // Add window handle
        uint32_t Width = 0;
        uint32_t Height = 0;
        ERHIFormat PreferredFormat = ERHIFormat::B8G8R8A8_UNorm;
    };

    VulkanSwapchain() = default;
    VulkanSwapchain(VulkanDevice* device, const SwapchainDesc& desc,std::vector<VkImage>& outImages);
    ~VulkanSwapchain();



    VkSwapchainKHR GetHandle() const { return m_Swapchain; }
    VkFormat GetFormat() const { return m_ImageFormat; }
    VkExtent2D GetExtent() const { return m_Extent; }
    VkSurfaceKHR GetSurface() const { return m_Surface; } // Get the surface

    bool Present(VulkanQueue* presentQueue, VulkanSemaphore* drawDoneSemaphore);
    void AcquireNextImage(VulkanSemaphore* signalSemaphore, int* outImageIndex);

private:
    SwapChainSupportDetails QuerySwapChainSupport(VkPhysicalDevice device);
    VkSurfaceFormatKHR ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats,VkFormat wantForamt);
    VkPresentModeKHR ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
    VkExtent2D ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities, uint32_t width, uint32_t height, void* windowHandle);
    void Cleanup();

    VulkanDevice* m_Device = nullptr;
    VkSwapchainKHR m_Swapchain = VK_NULL_HANDLE;
    VkFormat m_ImageFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D m_Extent{};
    VkSurfaceKHR m_Surface = VK_NULL_HANDLE; // Store the surface
    uint32_t m_CurrentImageIndex = 0;
};


}
