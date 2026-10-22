#pragma once

#include <vulkan/vulkan.h>
#include "VulkanRHIApi.h"


#include <vector>
#include <string>
#include <memory>
#include <queue>
#include <mutex>

namespace RHIVulkan{

class VulkanMemoryManager; // 前向声明
class VulkanQueue; // 前向声明
class VulkanSemaphoreManager; // 前向声明
class VulkanEventManager; // 前向声明
class VulkanStagingManager;// 前向声明
class VulkanCommandContext;
class VulkanFenceManager;
class VulkanRenderPassManager;
class VulkanDescriptorSetLayoutManager;
class VulkanDescriptorSetManager;
class VulkanShaderManager;
class VulkanPipelineLayoutCache;
class VulkanRHISyncPointManager;
class VulkanPresentExecutor;

// 延迟删除队列 - 用于延迟删除Vulkan对象
class VulkanDeferredDeleteQueue
{
public:
    enum class EResourceType
    {
        RenderPass,
        Buffer,
        BufferView,
        Image,
        ImageView,
        Pipeline,
        Framebuffer,
        Sampler,
        ShaderModule,
        Semaphore,
        MAX_TYPE
    };

    struct FDeferredDelete
    {
        uint64_t Handle;
        uint32_t FrameNumber;
    };

    VulkanDeferredDeleteQueue(class VulkanDevice* inDevice);
    ~VulkanDeferredDeleteQueue();

    // 入队资源
    template <typename T>
    void EnqueueResource(EResourceType type, T handle)
    {
        static_assert(sizeof(T) <= sizeof(uint64_t), "Vulkan resource handle size too large.");
        EnqueueGenericResource(type, reinterpret_cast<uint64_t>(handle));
    }

    // 释放资源
    void ReleaseResources(uint32_t frameDelay = 2);

    // 立即清空所有资源
    void Clear();

private:
    void EnqueueGenericResource(EResourceType type, uint64_t handle);
    void ReleaseResource(EResourceType type, const FDeferredDelete& entry);

    class VulkanDevice* m_Device;
    std::array<std::queue<FDeferredDelete>, static_cast<size_t>(EResourceType::MAX_TYPE)> m_Queues;
    std::mutex m_QueueMutex;
    uint32_t m_CurrentFrameNumber;

    // 删除顺序：从最不依赖的开始
    static constexpr std::array<EResourceType, 15> DeletionOrder = {
        EResourceType::Pipeline,        // Pipeline 依赖于 PipelineLayout 和 ShaderModule
        EResourceType::Framebuffer,     // Framebuffer 依赖于 RenderPass 和 ImageView
        EResourceType::ImageView,       // ImageView 依赖于 Image
        EResourceType::BufferView,      // BufferView 依赖于 Buffer
        EResourceType::RenderPass,      // RenderPass 不依赖其他延迟删除对象
        EResourceType::ShaderModule,    // ShaderModule 不依赖其他延迟删除对象
        EResourceType::Image,           // Image 不依赖其他延迟删除对象
        EResourceType::Buffer,          // Buffer 不依赖其他延迟删除对象
        EResourceType::Sampler,         // Sampler 不依赖其他延迟删除对象
        EResourceType::Semaphore,       // Semaphore 不依赖其他延迟删除对象
    };
};

class VulkanDevice
{
public:
    VulkanDevice(VulkanRHIApi* rhiApi,VkPhysicalDevice physicalDevice);

    ~VulkanDevice();
    bool Init(const std::vector<const char*>& enabledLayers,const std::vector<const char*>& enabledExtensions);
    void Destroy();
    VulkanRHIApi* GetRhiApi() const { return m_RhiApi; }
    VkDevice GetHandle() const { return m_Device; }
    VkPhysicalDevice GetPhysicalDevice() const { return m_PhysicalDevice; }
    VulkanQueue* GetGraphicsQueue() const { return m_GraphicsQueue; }
    VulkanQueue* GetComputeQueue() const { return m_ComputeQueue; }
    VulkanQueue* GetTransferQueue() const { return m_TransferQueue; }
    VulkanQueue* GetPresentQueue() const { return m_PresentQueue; }

    uint32_t GetGraphicsQueueFamilyIndex() const { return m_GraphicsQueueFamilyIndex; }
    uint32_t GetComputeQueueFamilyIndex() const { return m_ComputeQueueFamilyIndex; }
    uint32_t GetTransferQueueFamilyIndex() const { return m_TransferQueueFamilyIndex; }
    uint32_t GetPresentQueueFamilyIndex() const { return m_PresentQueueFamilyIndex; }

    VulkanMemoryManager* GetMemoryManager() const { return m_MemoryManager; }
    VkInstance GetInstance() const { return m_RhiApi->GetInstance(); }
    bool InitPresentQueue(VkSurfaceKHR surface);
    VulkanSemaphoreManager* GetSemaphoreManager() const { return m_SemaphoreManager; }
	VulkanEventManager* GetEventManager() const { return m_EventManager; }
	VulkanStagingManager* GetStagingManager() const { return m_StagingManager; }
    VulkanCommandContext* GetGlobalCommandContext() const { return m_GlobalCommandContext; }
    VulkanFenceManager* GetFenceManager() const { return m_FenceManager; }
	VulkanRenderPassManager* GetRenderPassManager() const { return m_RenderPassManager; }
    VulkanDescriptorSetLayoutManager* GetDescriptorSetLayoutManager() const { return m_DescriptorSetLayoutManager; }
    VulkanDescriptorSetManager* GetDescriptorSetManager() const { return m_DescriptorSetManager; }
	VulkanShaderManager* GetShaderManager() const { return m_ShaderManager; }
    VulkanPipelineLayoutCache* GetPipelineLayoutCache() const { return m_PipelineLayoutCache; }
    VulkanPresentExecutor* GetPresentExecutor() const { return m_PresentExecutor; }

    // 延迟删除资源接口
    void EnqueueRenderPassForDeletion(VkRenderPass renderPass);
    void EnqueueBufferForDeletion(VkBuffer buffer);
    void EnqueueImageForDeletion(VkImage image);
    void EnqueueImageViewForDeletion(VkImageView imageView);
    void EnqueueBufferViewForDeletion(VkBufferView bufferView);
    void EnqueuePipelineForDeletion(VkPipeline pipeline);
    void EnqueueFramebufferForDeletion(VkFramebuffer framebuffer);
    void EnqueueSamplerForDeletion(VkSampler sampler);
    void EnqueueShaderModuleForDeletion(VkShaderModule shaderModule);
    void EnqueueSemaphoreForDeletion(VkSemaphore semaphore);

    // 释放延迟删除的资源
    void ReleaseDeferredResources(uint32_t frameDelay = 2);
    uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;
private:
    void SelectQueueFamilies(VkPhysicalDevice physicalDevice);
    void CreateLogicalDevice(VkPhysicalDevice physicalDevice,
                             const std::vector<const char*>& extensions,
                             const std::vector<const char*>& layers);

    VulkanRHIApi *m_RhiApi;

    VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
    VkDevice m_Device{VK_NULL_HANDLE};
	VkPhysicalDeviceFeatures2 m_SupportedFeatures2{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
	VkPhysicalDeviceTimelineSemaphoreFeatures m_TimelineSemaphoreFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES };

    uint32_t m_QueueFamilyCount = 0;
    uint32_t m_GraphicsQueueFamilyIndex = UINT32_MAX;
    uint32_t m_ComputeQueueFamilyIndex = UINT32_MAX;
    uint32_t m_TransferQueueFamilyIndex = UINT32_MAX;
    uint32_t m_PresentQueueFamilyIndex = UINT32_MAX;

    VulkanQueue *m_GraphicsQueue;
    VulkanQueue *m_ComputeQueue;
    VulkanQueue *m_TransferQueue;
    VulkanQueue *m_PresentQueue;

    VulkanMemoryManager* m_MemoryManager;
	VulkanSemaphoreManager* m_SemaphoreManager = nullptr;
	VulkanEventManager* m_EventManager = nullptr;
	VulkanStagingManager* m_StagingManager = nullptr;
	VulkanCommandContext* m_GlobalCommandContext = nullptr;
    VulkanFenceManager* m_FenceManager = nullptr;
    VulkanRenderPassManager* m_RenderPassManager = nullptr;
    VulkanDescriptorSetLayoutManager* m_DescriptorSetLayoutManager;
    VulkanDescriptorSetManager* m_DescriptorSetManager;
    VulkanShaderManager* m_ShaderManager;
    VulkanPipelineLayoutCache* m_PipelineLayoutCache;
    VulkanDeferredDeleteQueue* m_DeferredDeleteQueue = nullptr;
    VulkanPresentExecutor* m_PresentExecutor = nullptr;
};

}