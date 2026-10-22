#pragma once
#include "VulkanDevice.h"
#include "VulkanResource.h"
#include "VulkanSync.h"
#include "VulkanBarriers.h"
#include <vector>
#include <memory>
#include <unordered_map>


namespace RHIVulkan
{

class VulkanCommandContext; // 前向声明
class VulkanDevice;

class VulkanCommandBufferPool;

class VulkanCommandBuffer
{
public:
    VulkanCommandBuffer(VulkanDevice* device, VulkanCommandBufferPool* owner,VkCommandBufferLevel level);
    ~VulkanCommandBuffer();

    enum ECommandBufferState {
        Free,//空闲
        Pending,//已经提交
        NeedRecycle//需要回收
    };

    void MarkState(ECommandBufferState state) { this->m_State = state; }
    ECommandBufferState GetState() const { return m_State; }

    VkCommandBuffer GetHandle() const { return m_CommandBuffer; }
    void AllocateMemory();

    void Begin(VkCommandBufferUsageFlags usage = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    void End();

    void Reset();


	//VulkanImageLayoutManager* GetImageLayoutManager() { return &imageLayoutManager; }

private:
    VkCommandBufferLevel m_Level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    VulkanCommandBufferPool* m_Owner = nullptr;
    VulkanDevice* m_Device = nullptr;
    VkCommandBuffer m_CommandBuffer = VK_NULL_HANDLE;
    friend class VulkanQueue;
    //VulkanImageLayoutManager imageLayoutManager;
    ECommandBufferState m_State = ECommandBufferState::Free;
};

class VulkanCommandBufferPool
{
public:
    VulkanCommandBufferPool(VulkanDevice* device, uint32_t queueFamilyIndex);
    ~VulkanCommandBufferPool();

    VkCommandPool GetHandle() const { return m_CommandPool; }

    // 分配一个 CommandBuffer
    VulkanCommandBuffer* AllocateCommandBuffer(VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY);

    // Reset pool，所有 buffer 都会被重置
    void Reset();

private:
    VulkanDevice* m_Device = nullptr;
    VkCommandPool m_CommandPool = VK_NULL_HANDLE;

    // 已分配但未被使用的 buffer（空闲池）
    std::vector<VulkanCommandBuffer*> m_AvailableBuffers;

    // 所有 buffer 管理，方便析构统一释放
    std::vector<std::unique_ptr<VulkanCommandBuffer>> m_AllBuffers;

    uint32_t m_QueueFamilyIndex = 0;
};

// 新增：VulkanCommandBufferManager
class VulkanCommandBufferManager
{
public:
    VulkanCommandBufferManager(VulkanDevice* device, VulkanCommandContext* commandContext);
    ~VulkanCommandBufferManager();

    // 获取指定队列族的池
    VulkanCommandBufferPool* GetPool();



    // 获取一个可用的命令缓冲区（如有空闲则复用，否则新分配）
    VulkanCommandBuffer* GetActiveCommandBuffer(VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY);
	VulkanCommandBuffer* EndActiveCommandBuffer();


    // 重置所有池
    void Reset();

    void GarbageCollect();

private:
    // 分配命令缓冲区
    VulkanCommandBuffer* Allocate(VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY);

    VulkanCommandBuffer* m_ActiveCommandBuffer = nullptr;
    VulkanDevice* m_Device = nullptr;
    VulkanCommandContext* m_CommandContext = nullptr;
    std::unordered_map<uint32_t, std::unique_ptr<VulkanCommandBufferPool>> m_Pools;
    std::vector<VulkanCommandBuffer*> m_ManagedBuffers; // 管理所有分配的buffer
};



} // namespace WR::RHIVulkan

