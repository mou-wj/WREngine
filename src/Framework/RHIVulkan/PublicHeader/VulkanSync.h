#pragma once
#include "VulkanDevice.h"
#include "RHICommandContex.h"
#include <queue>
#include <mutex>
#include <deque>
#include <vector>
#include <set>
#include <memory>

namespace RHIVulkan{

class VulkanDevice;
class VulkanCommandBuffer;
class VulkanEvent : public std::enable_shared_from_this<VulkanEvent> {
public:
    VulkanEvent(VulkanDevice* device);
    ~VulkanEvent();

    VkEvent GetHandle() const;

    void Set() const;
    void Reset() const;
    VkResult GetStatus() const;

private:
    VulkanDevice* m_Device;
    VkEvent m_Event;
};

class VulkanEventManager {
public:
    VulkanEventManager(VulkanDevice* device);
    ~VulkanEventManager();

    std::shared_ptr<VulkanEvent> Acquire();
    void Release(std::shared_ptr<VulkanEvent> evt);

private:
    VulkanDevice* m_Device;

    // Reusable event objects.
    std::queue<std::shared_ptr<VulkanEvent>> m_Pool;

    // Keep ownership of every event created by this manager.
    std::vector<std::shared_ptr<VulkanEvent>> m_ManagedObjects;

    std::mutex m_Mutex;
};



class VulkanSemaphore : public std::enable_shared_from_this<VulkanSemaphore> {
public:
    VulkanSemaphore(VulkanDevice* device,bool isBinary = false,uint64_t initialValue = 0);
    ~VulkanSemaphore();

    VkSemaphore GetHandle() const;
    bool IsBinary() const { return m_bIsBinary; }
    // 获取当前 GPU 已经执行到的数值
    uint64_t GetCurrentValue();
    // CPU 端阻塞等待直到达到某个值
    void Wait(uint64_t value, uint64_t timeoutNs = UINT64_MAX);

private:
    VulkanDevice* m_Device;
    VkSemaphore m_Semaphore;
    bool m_bIsBinary = false;
    uint64_t m_Value = 0;
};

class VulkanSemaphoreManager {
public:
    VulkanSemaphoreManager(VulkanDevice* device);
    ~VulkanSemaphoreManager();
    // binary semaphore manager
    // requireUnsignaled=true: always create a new unsignaled semaphore.
    // requireUnsignaled=false: reuse from pool when available.
    VulkanSemaphore* AcquireBinary();
    void ReleaseBinary(VulkanSemaphore* sem);

private:
    void BatchCreateSemaphores(size_t count);

    VulkanDevice* m_Device;
    std::mutex m_Mutex;

    static constexpr size_t m_BatchSize = 100;

    // 空闲池：存放全新 vkCreate 出来的干净信号量
    std::set<VulkanSemaphore*> m_FreePool;

    // 已用池：存放外部归还的、确定已空闲的信号量，攒满即砸碎
    std::set<VulkanSemaphore*> m_AllAllocateds;
};

// Vulkan Fence
class VulkanFence {
public:
    VulkanFence(VulkanDevice* device);
    ~VulkanFence();
    VkFence GetHandle() const { return m_Fence; }

    bool IsSignaled() const;
    void Reset();
    void Wait();
private:
    VkFence m_Fence = VK_NULL_HANDLE;
    VulkanDevice* m_Device = nullptr;
};

class VulkanFenceManager
{
public:
    VulkanFenceManager(VulkanDevice* device);
    ~VulkanFenceManager();

    VulkanFence* AcquireFence();
    void ReleaseFence(VulkanFence* fence);
    void GarbageCollect();

private:
    VulkanDevice* m_Device = nullptr;
    std::vector<std::unique_ptr<VulkanFence>> m_AllFences;
    std::deque<VulkanFence*> m_AvailableFences;
    std::deque<VulkanFence*> m_PendingFences;
};


// Vulkan RHI-level Sync Point (based on VkTimelineSemaphore)
class RHIVULKAN_API VulkanRHISyncPoint final : public RHI::RHISyncPoint {
public:
    VulkanRHISyncPoint(VulkanDevice* device, RHI::EQueueType queueType, uint64_t initialValue = 0,bool isBinary = false);
    VulkanRHISyncPoint(VulkanDevice* device, RHI::EQueueType queueType, VulkanSemaphore* semaphore);
    ~VulkanRHISyncPoint();
    void SetSemaphore(VulkanSemaphore* semaphore) { 
        if (!m_bOwnSemaphore) {
            m_Semaphore = semaphore;
            }
        }

    // 获取当前 GPU 已经执行到的数值
    uint64_t GetCurrentValue() override;
    // CPU 端阻塞等待直到达到某个值
    void Wait(uint64_t value, uint64_t timeoutNs = UINT64_MAX) override;

    // 获取VkSemaphore句柄（如有需要跨API用）
    VulkanSemaphore* GetSemaphore() const { return m_Semaphore; }

    bool IsBinary() const { return m_Semaphore->IsBinary(); }


private:
    bool m_bOwnSemaphore = true;
    VulkanDevice* m_Device = nullptr;
    VulkanSemaphore* m_Semaphore = nullptr;
};
}