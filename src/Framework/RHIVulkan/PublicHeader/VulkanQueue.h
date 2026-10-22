#pragma once

#include "RHICommandContex.h"
#include "VulkanDevice.h"
#include "VulkanBarriers.h"
#include "VulkanCommandBuffer.h"
#include "VulkanSync.h"
#include <vector>
#include <memory>
#include <mutex>

namespace RHIVulkan{

class VulkanDevice;
class VulkanSemaphore;
class VulkanCommandContext;

class RHIVULKAN_API VulkanQueue : public RHI::RHIQueue
{
public:
    VulkanQueue() = default;
    VulkanQueue(VulkanDevice* device, VkQueue queue, uint32_t familyIndex, EQueueType type
    );
    ~VulkanQueue() override;

    // Initialize the context pool for this queue. poolSize contexts will be pre-created.
    void InitContextPool(uint32_t poolSize = 1);

    VkQueue GetHandle() const { return m_Queue; }
    uint32_t GetFamilyIndex() const { return m_FamilyIndex; }
    VulkanDevice* GetDevice() const { return m_Device; }
    VulkanImageLayoutManager* GetImageLayoutManager() { return &m_ImageLayoutManager; }
    void UpdatedCommandBufferImageLayoutManager(VulkanCommandBuffer* commandBuffer) {
        //if (commandBuffer) {
        //    auto layoutManager = commandBuffer->GetImageLayoutManager();
        //    imageLayoutManager_.TransferTo(*layoutManager);
        //}
    }

    // ---- RHI::RHIQueue overrides ----
    RHI::EQueueType GetType() const override;
    RHI::RHIContextBase* AcquireCommandContext() override;
    RHI::RHIContextBase* ReleaseCommandContext(RHI::RHIContextBase* context) override;
    uint64_t ExecuteContext(RHI::RHIContextBase* context) override;
    uint64_t ExecuteContext(const std::vector<RHI::RHIContextBase*>& cmds, const std::vector<RHI::RHIWaitInfo>& waitInfos) override;
    VulkanSemaphore* SubmitEmptyWithDependency(VkSemaphore timelineWait, uint64_t waitValue);
    void WaitValue(uint64_t fence) override;

    void WaitIdle() override;

	uint64_t GetCurrentTimelineValue() override;

	RHISyncPoint* GetSyncPoint() override { return m_SubmitSyncPoint; }

    void GarbageCollect();

private:
    struct PendingInfo {
        std::vector<VulkanCommandBuffer*> Cmds;
        uint64_t FinishedTimelineValue;
        std::vector<VulkanCommandContext*> Contexts;
        std::vector<VulkanSemaphore*> PendingBinarySemaphores;
    };
    std::queue<PendingInfo> m_PendingInfos;

    VulkanDevice* m_Device = nullptr;
    VkQueue m_Queue = VK_NULL_HANDLE;
    uint32_t m_FamilyIndex = UINT32_MAX;
    VulkanImageLayoutManager m_ImageLayoutManager;
    RHI::EQueueType m_QueueType = RHI::EQueueType::Graphics;
    std::vector<VulkanCommandContext*> m_AllContexts;  // owns all pooled contexts
    std::vector<VulkanCommandContext*> m_FreeContexts; // contexts available for acquire
    VulkanRHISyncPoint* m_SubmitSyncPoint = nullptr; // 用于跨提交等待的时间线信号量
    uint64_t m_CurrentTimelineValue = 0;
    std::mutex m_ContextPoolMutex;
    std::mutex m_FlushContextMutex;
};

// -------------------------------------------------------------------------------------------------
// Present executor backed by a VulkanQueue (graphics queue)
// -------------------------------------------------------------------------------------------------
class RHIVULKAN_API VulkanPresentExecutor final : public RHI::RHIPresentExecutor
{
public:
    explicit VulkanPresentExecutor(VulkanQueue* queue);
    void Present(RHI::RHISwapchain* swapchain, const RHI::RHIWaitInfo& waitinfo) override;
private:
    VulkanQueue* m_Queue = nullptr;
};

}