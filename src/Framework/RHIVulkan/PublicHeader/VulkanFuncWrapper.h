#pragma once
#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include "vulkan/vulkan.h"
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
namespace VKFunc {
    bool InitializeLoader();

    // =================================================================
    // 1. 全局实例与初始化 (Instance & Validation)
    // =================================================================
    bool CreateInstance(const VkInstanceCreateInfo* pCreateInfo, VkInstance* pInstance);
    void DestroyInstance(VkInstance instance);
    void EnumerateInstanceExtensionProperties(const char* pLayerName, uint32_t* pPropertyCount, VkExtensionProperties* pProperties);
    void EnumerateInstanceLayerProperties(uint32_t* pPropertyCount, VkLayerProperties* pProperties);

    void SetDebugName(VkDevice device, VkObjectType type, uint64_t handle, const char* name);
    bool CreateDebugUtilsMessengerEXT(VkInstance instance,
        const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
        VkDebugUtilsMessengerEXT* pDebugMessenger);
    void DestroyDebugUtilsMessengerEXT(VkInstance instance,
        VkDebugUtilsMessengerEXT debugMessenger);


    // =================================================================
    // 2. 物理设备探测 (Physical Device)
    // =================================================================
    bool EnumeratePhysicalDevices(VkInstance instance, uint32_t* pPhysicalDeviceCount, VkPhysicalDevice* pPhysicalDevices);
    void GetPhysicalDeviceProperties(VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties* pProperties);
    void GetPhysicalDeviceFeatures(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures* pFeatures);
    void GetPhysicalDeviceProperties2(VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties2* properties);
    void GetPhysicalDeviceFeatures2(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures2* pFeatures);
    void GetPhysicalDeviceMemoryProperties(VkPhysicalDevice physicalDevice, VkPhysicalDeviceMemoryProperties* pMemoryProperties);
    void GetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice physicalDevice, uint32_t* pQueueFamilyCount, VkQueueFamilyProperties* pQueueFamilies);
    void EnumerateDeviceExtensionProperties(VkPhysicalDevice physicalDevice, const char* pLayerName, uint32_t* pPropertyCount, VkExtensionProperties* pProperties);
    void EnumerateDeviceLayerProperties(VkPhysicalDevice physicalDevice, uint32_t* pPropertyCount, VkLayerProperties* pProperties);

    // =================================================================
    // 3. 逻辑设备与队列 (Device & Queue)
    // =================================================================
    bool CreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo* pCreateInfo, VkDevice* pDevice);
    void DestroyDevice(VkDevice device);
    void GetDeviceQueue(VkDevice device, uint32_t queueFamilyIndex, uint32_t queueIndex, VkQueue* pQueue);
    void DeviceWaitIdle(VkDevice device);
    void QueueWaitIdle(VkQueue queue);
    bool QueueSubmit(VkQueue queue, uint32_t submitCount, const VkSubmitInfo* pSubmits, VkFence fence);

    // =================================================================
    // 4. 内存管理与基础资源 (Memory, Buffer, Image)
    // =================================================================
    bool AllocateMemory(VkDevice device, const VkMemoryAllocateInfo* pAllocateInfo, VkDeviceMemory* pMemory);
    void FreeMemory(VkDevice device, VkDeviceMemory memory);
    bool MapMemory(VkDevice device, VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size, VkMemoryMapFlags flags, void** ppData);
    void UnmapMemory(VkDevice device, VkDeviceMemory memory);

    bool CreateBuffer(VkDevice device, const VkBufferCreateInfo* pCreateInfo, VkBuffer* pBuffer);
    void DestroyBuffer(VkDevice device, VkBuffer buffer);
    void GetBufferMemoryRequirements(VkDevice device, VkBuffer buffer, VkMemoryRequirements* pMemoryRequirements);
    bool BindBufferMemory(VkDevice device, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize offset);

    bool CreateBufferView(VkDevice device, const VkBufferViewCreateInfo* pCreateInfo, VkBufferView* pView);
    void DestroyBufferView(VkDevice device, VkBufferView bufferView);

    bool CreateImage(VkDevice device, const VkImageCreateInfo* pCreateInfo, VkImage* pImage);
    void DestroyImage(VkDevice device, VkImage image);
    void GetImageMemoryRequirements(VkDevice device, VkImage image, VkMemoryRequirements* pMemoryRequirements);
    bool BindImageMemory(VkDevice device, VkImage image, VkDeviceMemory memory, VkDeviceSize offset);
    bool CreateImageView(VkDevice device, const VkImageViewCreateInfo* pCreateInfo, VkImageView* pImageView);
    void DestroyImageView(VkDevice device, VkImageView imageView);
    bool CreateSampler(VkDevice device, const VkSamplerCreateInfo* pCreateInfo, VkSampler* pSampler);
    void DestroySampler(VkDevice device, VkSampler sampler);

    // =================================================================
    // 5. 描述符集 (Descriptors)
    // =================================================================
    bool CreateDescriptorSetLayout(VkDevice device, const VkDescriptorSetLayoutCreateInfo* pCreateInfo, VkDescriptorSetLayout* pSetLayout);
    void DestroyDescriptorSetLayout(VkDevice device, VkDescriptorSetLayout descriptorSetLayout);
    bool CreateDescriptorPool(VkDevice device, const VkDescriptorPoolCreateInfo* pCreateInfo, VkDescriptorPool* pDescriptorPool);
    void ResetDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool);
    void DestroyDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool);
    bool AllocateDescriptorSets(VkDevice device, const VkDescriptorSetAllocateInfo* pAllocateInfo, VkDescriptorSet* pDescriptorSets);
    void FreeDescriptorSets(VkDevice device, VkDescriptorPool descriptorPool, uint32_t descriptorSetCount, const VkDescriptorSet* pDescriptorSets);
    void UpdateDescriptorSets(VkDevice device, uint32_t descriptorWriteCount, const VkWriteDescriptorSet* pDescriptorWrites, uint32_t descriptorCopyCount, const VkCopyDescriptorSet* pDescriptorCopies);

    // =================================================================
    // 6. 管线与着色器 (Pipeline & Shaders)
    // =================================================================
    bool CreateShaderModule(VkDevice device, const VkShaderModuleCreateInfo* pCreateInfo, VkShaderModule* pShaderModule);
    void DestroyShaderModule(VkDevice device, VkShaderModule shaderModule);
    bool CreatePipelineLayout(VkDevice device, const VkPipelineLayoutCreateInfo* pCreateInfo, VkPipelineLayout* pPipelineLayout);
    void DestroyPipelineLayout(VkDevice device, VkPipelineLayout layout);
    bool CreateGraphicsPipelines(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkGraphicsPipelineCreateInfo* pCreateInfos, VkPipeline* pPipelines);
    bool CreateComputePipelines(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkComputePipelineCreateInfo* pCreateInfos, VkPipeline* pPipelines);
    bool CreateRayTracingPipelinesKHR(VkDevice device, VkDeferredOperationKHR deferredOperation, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkRayTracingPipelineCreateInfoKHR* pCreateInfos, VkPipeline* pPipelines);
    bool GetRayTracingShaderGroupHandlesKHR(VkDevice device, VkPipeline pipeline, uint32_t firstGroup, uint32_t groupCount, size_t dataSize, void* pData);
    void DestroyPipeline(VkDevice device, VkPipeline pipeline);

    bool CreateAccelerationStructureKHR(VkDevice device, const VkAccelerationStructureCreateInfoKHR* pCreateInfo, VkAccelerationStructureKHR* pAccelerationStructure);
    void DestroyAccelerationStructureKHR(VkDevice device, VkAccelerationStructureKHR accelerationStructure);
    void GetAccelerationStructureBuildSizesKHR(VkDevice device, VkAccelerationStructureBuildTypeKHR buildType, const VkAccelerationStructureBuildGeometryInfoKHR* pBuildInfo, const uint32_t* pMaxPrimitiveCounts, VkAccelerationStructureBuildSizesInfoKHR* pSizeInfo);
    void CmdBuildAccelerationStructuresKHR(VkCommandBuffer commandBuffer, uint32_t infoCount, const VkAccelerationStructureBuildGeometryInfoKHR* pInfos, const VkAccelerationStructureBuildRangeInfoKHR* const* ppBuildRangeInfos);
    VkDeviceAddress GetAccelerationStructureDeviceAddressKHR(VkDevice device, const VkAccelerationStructureDeviceAddressInfoKHR* pInfo);
    VkDeviceAddress GetBufferDeviceAddress(VkDevice device, const VkBufferDeviceAddressInfo* pInfo);

    // =================================================================
    // 7. 渲染目标 (RenderPass & Framebuffer)
    // =================================================================
    bool CreateRenderPass(VkDevice device, const VkRenderPassCreateInfo* pCreateInfo, VkRenderPass* pRenderPass);
    void DestroyRenderPass(VkDevice device, VkRenderPass renderPass);
    bool CreateFramebuffer(VkDevice device, const VkFramebufferCreateInfo* pCreateInfo, VkFramebuffer* pFramebuffer);
    void DestroyFramebuffer(VkDevice device, VkFramebuffer framebuffer);

    // =================================================================
    // 8. 指令录制 (Command Pools & Buffers)
    // =================================================================
    bool CreateCommandPool(VkDevice device, const VkCommandPoolCreateInfo* pCreateInfo, VkCommandPool* pCommandPool);
    void ResetCommandPool(VkDevice device, VkCommandPool commandPool, VkCommandPoolResetFlags flags);
    void DestroyCommandPool(VkDevice device, VkCommandPool commandPool);
    bool AllocateCommandBuffers(VkDevice device, const VkCommandBufferAllocateInfo* pAllocateInfo, VkCommandBuffer* pCommandBuffers);
    void FreeCommandBuffers(VkDevice device, VkCommandPool commandPool, uint32_t commandBufferCount, const VkCommandBuffer* pCommandBuffers);

    void BeginCommandBuffer(VkCommandBuffer commandBuffer, const VkCommandBufferBeginInfo* pBeginInfo);
    void EndCommandBuffer(VkCommandBuffer commandBuffer);
    void ResetCommandBuffer(VkCommandBuffer commandBuffer, VkCommandBufferResetFlags flags = 0);

    // 录制命令
    void CmdBindPipeline(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipeline pipeline);
    void CmdBindDescriptorSets(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout, uint32_t firstSet, uint32_t descriptorSetCount, const VkDescriptorSet* pDescriptorSets, uint32_t dynamicOffsetCount, const uint32_t* pDynamicOffsets);
    void CmdBindVertexBuffers(VkCommandBuffer commandBuffer, uint32_t firstBinding, uint32_t bindingCount, const VkBuffer* pBuffers, const VkDeviceSize* pOffsets);
    void CmdBindIndexBuffer(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkIndexType indexType);
    void CmdSetViewport(VkCommandBuffer commandBuffer, uint32_t firstViewport, uint32_t viewportCount, const VkViewport* pViewports);
    void CmdSetScissor(VkCommandBuffer commandBuffer, uint32_t firstScissor, uint32_t scissorCount, const VkRect2D* pScissors);
    void CmdDraw(VkCommandBuffer commandBuffer, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance);
    void CmdDrawIndexed(VkCommandBuffer commandBuffer, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance);
    void CmdDispatch(VkCommandBuffer commandBuffer, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ);
    void CmdCopyBuffer(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferCopy* pRegions);
    void CmdCopyBufferToImage(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkBufferImageCopy* pRegions);
	void CmdCopyImageToBuffer(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferImageCopy* pRegions);
    void CmdCopyImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageCopy* pRegions);
    void CmdBlitImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageBlit* pRegions, VkFilter filter);
    void CmdPipelineBarrier(VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask, VkDependencyFlags dependencyFlags, uint32_t memoryBarrierCount, const VkMemoryBarrier* pMemoryBarriers, uint32_t bufferMemoryBarrierCount, const VkBufferMemoryBarrier* pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount, const VkImageMemoryBarrier* pImageMemoryBarriers);
    void CmdBeginRenderPass(VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo* pRenderPassBegin, VkSubpassContents contents);
    void CmdEndRenderPass(VkCommandBuffer commandBuffer);
    void CmdCopyAccelerationStructureKHR(VkCommandBuffer commandBuffer, const VkCopyAccelerationStructureInfoKHR* pInfo);
    void CmdCopyAccelerationStructureToMemoryKHR(VkCommandBuffer commandBuffer, const VkCopyAccelerationStructureToMemoryInfoKHR* pInfo);
    void CmdCopyMemoryToAccelerationStructureKHR(VkCommandBuffer commandBuffer, const VkCopyMemoryToAccelerationStructureInfoKHR* pInfo);
    void CmdWriteAccelerationStructuresPropertiesKHR(VkCommandBuffer commandBuffer, uint32_t accelerationStructureCount, const VkAccelerationStructureKHR* pAccelerationStructures, VkQueryType queryType, VkQueryPool queryPool, uint32_t firstQuery);
    void CmdTraceRaysKHR(VkCommandBuffer commandBuffer, const VkStridedDeviceAddressRegionKHR* pRaygenShaderBindingTable, const VkStridedDeviceAddressRegionKHR* pMissShaderBindingTable, const VkStridedDeviceAddressRegionKHR* pHitShaderBindingTable, const VkStridedDeviceAddressRegionKHR* pCallableShaderBindingTable, uint32_t width, uint32_t height, uint32_t depth);
    void CmdTraceRaysIndirectKHR(VkCommandBuffer commandBuffer, const VkStridedDeviceAddressRegionKHR* pRaygenShaderBindingTable, const VkStridedDeviceAddressRegionKHR* pMissShaderBindingTable, const VkStridedDeviceAddressRegionKHR* pHitShaderBindingTable, const VkStridedDeviceAddressRegionKHR* pCallableShaderBindingTable, VkDeviceAddress indirectDeviceAddress);

    // =================================================================
    // 9. 同步机制 (Synchronization)
    // =================================================================
    bool CreateFence(VkDevice device, const VkFenceCreateInfo* pCreateInfo, VkFence* pFence);
    void DestroyFence(VkDevice device, VkFence fence);
    bool GetFenceStatus(VkDevice device, VkFence fence);
    bool WaitForFences(VkDevice device, uint32_t fenceCount, const VkFence* pFences, VkBool32 waitAll, uint64_t timeout);
    void ResetFences(VkDevice device, uint32_t fenceCount, const VkFence* pFences);

    bool CreateSemaphore_(VkDevice device, const VkSemaphoreCreateInfo* pCreateInfo, VkSemaphore* pSemaphore);
    void DestroySemaphore(VkDevice device, VkSemaphore semaphore);
    void GetSemaphoreCounterValue(VkDevice device, VkSemaphore semaphore, uint64_t* pValue);
    bool WaitSemaphores(VkDevice device, const VkSemaphoreWaitInfo* pWaitInfo, uint64_t timeout);

    bool CreateEvent_(VkDevice device, const VkEventCreateInfo* pCreateInfo, VkEvent* pEvent);
    void DestroyEvent(VkDevice device, VkEvent event);
    void SetEvent(VkDevice device, VkEvent event);
    void ResetEvent(VkDevice device, VkEvent event);
    bool GetEventStatus(VkDevice device, VkEvent event);

    // =================================================================
    // 10. WSI / 交换链 (Display)
    // =================================================================
    bool CreateWin32SurfaceKHR(VkInstance instance, const VkWin32SurfaceCreateInfoKHR* pCreateInfo, VkSurfaceKHR* pSurface);
    void DestroySurfaceKHR(VkInstance instance, VkSurfaceKHR surface);
    void GetPhysicalDeviceSurfaceSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, VkSurfaceKHR surface, VkBool32* pSupported);
    void GetPhysicalDeviceSurfaceCapabilitiesKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, VkSurfaceCapabilitiesKHR* pCapabilities);
    void GetPhysicalDeviceSurfaceFormatsKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t* pSurfaceFormatCount, VkSurfaceFormatKHR* pSurfaceFormats);
    void GetPhysicalDeviceSurfacePresentModesKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t* pPresentModeCount, VkPresentModeKHR* pPresentModes);

    bool CreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR* pCreateInfo, VkSwapchainKHR* pSwapchain);
    void DestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain);
    void GetSwapchainImagesKHR(VkDevice device, VkSwapchainKHR swapchain, uint32_t* pImageCount, VkImage* pImages);
    bool AcquireNextImageKHR(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout, VkSemaphore semaphore, VkFence fence, uint32_t* pImageIndex);
    bool QueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pPresentInfo);

    //
    void GetDeviceFaultInfoEXT(VkDevice device, VkDeviceFaultCountsEXT* pFaultCounts, VkDeviceFaultInfoEXT* pFaultInfo);
}