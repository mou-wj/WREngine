#include "VulkanQueue.h"
#include "VulkanDevice.h"
#include "VulkanCommandBuffer.h"
#include "VulkanCommandContex.h"
#include "VulkanResource.h"
#include "VulkanSwapchain.h"
#include "VulkanFuncWrapper.h"
#include "Log.h"
#include "Timer.h"
#include <stdexcept>
#include <cassert>
#include <algorithm>
#include <format>

namespace RHIVulkan {

namespace {
VulkanCommandContext* CreateQueueContext(VulkanDevice* device, VulkanQueue* queue, RHI::EQueueType queueType)
{
    switch (queueType)
    {
    case RHI::EQueueType::Compute:
        return new VulkanComputeContext(device, queue);
    case RHI::EQueueType::Graphics:
    default:
        return new VulkanGraphicContext(device, queue);
    }
}

VkPipelineStageFlags ToVkPipelineStage(RHI::ERHIPipelineStage stage)
{
    switch (stage)
    {
    case RHI::ERHIPipelineStage::TopOfPipe:
        return VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    case RHI::ERHIPipelineStage::DrawIndirect:
        return VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT;
    case RHI::ERHIPipelineStage::VertexInput:
        return VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
    case RHI::ERHIPipelineStage::VertexShader:
        return VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
    case RHI::ERHIPipelineStage::FragmentShader:
        return VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    case RHI::ERHIPipelineStage::EarlyFragmentTests:
        return VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    case RHI::ERHIPipelineStage::LateFragmentTests:
        return VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    case RHI::ERHIPipelineStage::ColorAttachmentOutput:
        return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    case RHI::ERHIPipelineStage::ComputeShader:
        return VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    case RHI::ERHIPipelineStage::Transfer:
        return VK_PIPELINE_STAGE_TRANSFER_BIT;
    case RHI::ERHIPipelineStage::BottomOfPipe:
        return VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    case RHI::ERHIPipelineStage::AllCommands:
        return VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    case RHI::ERHIPipelineStage::None:
    default:
        return VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    }
}
}

// -------------------------------------------------------------------------------------------------
// VulkanQueue
// -------------------------------------------------------------------------------------------------

VulkanQueue::VulkanQueue(VulkanDevice* device, VkQueue queue, uint32_t familyIndex,EQueueType queueType)
	: m_Device(device), m_Queue(queue), m_FamilyIndex(familyIndex), m_QueueType(queueType)
{
    m_SubmitSyncPoint = new VulkanRHISyncPoint(device, queueType);
#ifdef DEBUG_INFO
	std::string queueTypeName;
	switch (queueType)
	{
	case RHI::EQueueType::Graphics:
		queueTypeName = "Graphics";
		break;
	case RHI::EQueueType::Compute:
		queueTypeName = "Compute";
		break;
	default:
		queueTypeName = "Unknown";
		break;
	}
    std::string str1 = std::format("{:#x}", uint64_t(m_Queue));
    
	VKFunc::SetDebugName(m_Device->GetHandle(), VK_OBJECT_TYPE_QUEUE, (uint64_t)m_Queue, ("VulkanQueue:" + queueTypeName + " " + str1).c_str());
    LOG_INFO("Create VulkanQueue: %s %s", queueTypeName.c_str(), str1.c_str());
#endif
}

VulkanQueue::~VulkanQueue()
{
    for (VulkanCommandContext* ctx : m_AllContexts)
    {
        delete ctx;
    }
    m_AllContexts.clear();
    m_FreeContexts.clear();
    delete m_SubmitSyncPoint;
}

void VulkanQueue::WaitIdle()
{
    VKFunc::QueueWaitIdle(m_Queue);

}

uint64_t VulkanQueue::GetCurrentTimelineValue()
{
	return m_SubmitSyncPoint->GetCurrentValue();
}

void VulkanQueue::GarbageCollect() 
{
    uint64_t currentTime = m_SubmitSyncPoint->GetCurrentValue();
    while (!m_PendingInfos.empty()) {
        auto& front = m_PendingInfos.front();
        if (front.FinishedTimelineValue <= currentTime) {
            for (auto cmd : front.Cmds) {
                cmd->MarkState(VulkanCommandBuffer::NeedRecycle);
            }
            for (auto contex : front.Contexts) {
				ReleaseCommandContext(contex);
            }
            for (auto sem : front.PendingBinarySemaphores) {
                m_Device->GetSemaphoreManager()->ReleaseBinary(sem);
            }
            m_PendingInfos.pop();
            continue;
        }
        break;
    }
}

void VulkanQueue::InitContextPool(uint32_t poolSize)
{
    std::lock_guard<std::mutex> lock(m_ContextPoolMutex);
    for (uint32_t i = 0; i < poolSize; ++i)
    {
        VulkanCommandContext* ctx = CreateQueueContext(m_Device, this, m_QueueType);
        m_AllContexts.push_back(ctx);
        m_FreeContexts.push_back(ctx);
    }
}

RHI::EQueueType VulkanQueue::GetType() const
{
    return m_QueueType;
}

RHI::RHIContextBase* VulkanQueue::AcquireCommandContext()
{
    std::lock_guard<std::mutex> lock(m_ContextPoolMutex);
    //LOG_INFO("duration time %f s", Core::Timer::GetGlobalInstance().GetDelta());
    if (!m_FreeContexts.empty())
    {
        VulkanCommandContext* ctx = m_FreeContexts.back();
        m_FreeContexts.pop_back();
        return ctx;
    }
    if (m_QueueType == EQueueType::Graphics) {
        LOG_INFO("%s", "create context");
    }
   
    VulkanCommandContext* ctx = CreateQueueContext(m_Device, this, m_QueueType);
    m_AllContexts.push_back(ctx);
    return ctx;
}

RHI::RHIContextBase* VulkanQueue::ReleaseCommandContext(RHI::RHIContextBase* context)
{
    if (!context)
    {
        return nullptr;
    }

    VulkanCommandContext* vulkanCtx = dynamic_cast<VulkanCommandContext*>(context);
    std::lock_guard<std::mutex> lock(m_ContextPoolMutex);
    m_FreeContexts.push_back(vulkanCtx);
    return nullptr;
}

uint64_t VulkanQueue::ExecuteContext(RHI::RHIContextBase* contextBase)
{
    return ExecuteContext({ contextBase }, {});
}

uint64_t VulkanQueue::ExecuteContext(const std::vector<RHI::RHIContextBase*>& contexts, const std::vector<RHI::RHIWaitInfo>& waitInfos)
{
    std::lock_guard<std::mutex> lock(m_FlushContextMutex);

    // 1. 准备所有的 Wait 信息 (Timeline Semaphore 模式)
    std::vector<VkSemaphore> waitHandles;
    std::vector<uint64_t> waitValues;
    std::vector<VkPipelineStageFlags> waitStages;
    std::vector<VulkanSemaphore*> pendingSeamphores;

    for (const auto& waitInfo : waitInfos)
    {
        RHISyncPoint* rhivkSyncPoint = nullptr;
        if (waitInfo.SyncPoint) {
			rhivkSyncPoint = waitInfo.SyncPoint;
        } else {
            // 优先判断QueueType是否和当前queue一致
            if (waitInfo.QueueType == m_QueueType) {
                rhivkSyncPoint = m_SubmitSyncPoint;
            } else {
                switch (waitInfo.QueueType)
                {
                case EQueueType::Graphics:
                    rhivkSyncPoint = m_Device->GetGraphicsQueue()->GetSyncPoint();
					break;
                case EQueueType::Compute:
                    rhivkSyncPoint = m_Device->GetComputeQueue()->GetSyncPoint();
					break;
                default:
                    break;
                }
            }
        }
        VulkanRHISyncPoint* vkSyncPoint = static_cast<VulkanRHISyncPoint*>(waitInfo.SyncPoint);
        if (!vkSyncPoint) {
            // 无法获取到SyncPoint，跳过
            continue;
        }
        bool isBinary = vkSyncPoint->IsBinary();
        if (isBinary)
        {
            // Binary Semaphore 直接等待即可，无需 Value
            waitHandles.push_back(vkSyncPoint->GetSemaphore()->GetHandle());
            waitValues.push_back(0);
            pendingSeamphores.push_back(vkSyncPoint->GetSemaphore());
        }
        else {
            waitHandles.push_back(vkSyncPoint->GetSemaphore()->GetHandle());
            waitValues.push_back(waitInfo.Value); // 关键：使用 WaitInfo 指定的 Value
        }
        // Acquired images may first be used by transfer, compute or ray tracing rather than raster output.
        waitStages.push_back(isBinary ? VK_PIPELINE_STAGE_ALL_COMMANDS_BIT : ToVkPipelineStage(waitInfo.WaitStage));
    }

    // 2. 确定本次提交的 Signal Value (自增)
    // 假设 SubmitSyncPoint_ 是当前 Queue 对应的时间轴信号量
    uint64_t signalValue = ++m_CurrentTimelineValue;
    // 3. 收集所有的 Command Buffers
    std::vector<VkCommandBuffer> cmdHandles;
    std::vector<VulkanCommandBuffer*> cmdBuffers;
    std::vector<VulkanCommandContext*> vkcontexts;
    for (auto* context : contexts)
    {
		
        VulkanCommandContext* vkCtx = dynamic_cast<VulkanCommandContext*>(context);
        vkcontexts.push_back(vkCtx);
        auto recorededCmdBuffer = vkCtx->GetRecordedCommandBuffer();
        if (recorededCmdBuffer) {
            recorededCmdBuffer->MarkState(VulkanCommandBuffer::ECommandBufferState::Pending);
            cmdHandles.push_back(recorededCmdBuffer->GetHandle());
            cmdBuffers.push_back(recorededCmdBuffer);
        }

    }

    // 4. 执行底层提交 (这里建议直接调用 vkQueueSubmit 或封装好的提交接口)
    // 注意：需要使用 VkTimelineSemaphoreSubmitInfo 链入 pNext
    VkTimelineSemaphoreSubmitInfo timelineInfo{ VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO };
    timelineInfo.waitSemaphoreValueCount = waitValues.size();
    timelineInfo.pWaitSemaphoreValues = waitValues.data();
    timelineInfo.signalSemaphoreValueCount = 1;
    timelineInfo.pSignalSemaphoreValues = &signalValue;

    VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submitInfo.pNext = &timelineInfo;
    submitInfo.waitSemaphoreCount = waitHandles.size();
    submitInfo.pWaitSemaphores = waitHandles.data();
    submitInfo.pWaitDstStageMask = waitStages.data();
    submitInfo.commandBufferCount = cmdHandles.size();
    submitInfo.pCommandBuffers = cmdHandles.data();
    submitInfo.signalSemaphoreCount = 1;
    VkSemaphore signalHandle = m_SubmitSyncPoint->GetSemaphore()->GetHandle();
    submitInfo.pSignalSemaphores = &signalHandle;

    bool suc =VKFunc::QueueSubmit(m_Queue, 1, &submitInfo, VK_NULL_HANDLE);
    
    VkDeviceFaultCountsEXT faultCounts{ VK_STRUCTURE_TYPE_DEVICE_FAULT_COUNTS_EXT };
    //if (!suc) {
    //    VKFunc::GetDeviceFaultInfoEXT(device_->GetHandle(), &faultCounts, nullptr);
    //
    //    if (faultCounts.addressInfoCount > 0 || faultCounts.vendorInfoCount > 0) {
    //        // 说明 GPU 真的挂了！
    //        VkDeviceFaultInfoEXT faultInfo{ VK_STRUCTURE_TYPE_DEVICE_FAULT_INFO_EXT };
    //        faultInfo.pAddressInfos = new VkDeviceFaultAddressInfoEXT[faultCounts.addressInfoCount];
    //
    //        VKFunc::GetDeviceFaultInfoEXT(device_->GetHandle(), &faultCounts, &faultInfo);
    //        // 这里能直接读到发生错误的 GPU 虚拟地址（VA）和故障类型（如页错误、越界等）
    //    }
    //}



    m_Device->GetStagingManager()->GarbageCollect();
    //for (auto cmdBuffer : cmdBuffers)
    //{
    //    cmdBuffer->GetImageLayoutManager()->TransferTo(imageLayoutManager_);
    //    cmdBuffer->GetImageLayoutManager()->Clear();
    //
    //}
    //imageLayoutManager_.PrintLayoutInfo();

    // 5. 资源回收与状态更新
    m_Device->ReleaseDeferredResources();
    GarbageCollect();
    PendingInfo pendingInfo{};
    pendingInfo.Cmds = cmdBuffers;
    pendingInfo.FinishedTimelineValue = signalValue;
    pendingInfo.Contexts = vkcontexts;
    pendingInfo.PendingBinarySemaphores = pendingSeamphores;
    m_PendingInfos.push(pendingInfo);
    // 6. 返回 Fence：它本质上是同步点和数值的组合
    RHI::RHIFence result{};
    result.QueueType = m_SubmitSyncPoint->GetQueueType();
    result.Value = signalValue;

    return result.Value;

}

VulkanSemaphore* VulkanQueue::SubmitEmptyWithDependency(VkSemaphore timelineWait, uint64_t waitValue)
{
    auto sigSemaphore = m_Device->GetSemaphoreManager()->AcquireBinary();
    uint64_t signalValue = ++m_CurrentTimelineValue;
    std::array<uint64_t, 2> signalValues = {1,signalValue };
    VkTimelineSemaphoreSubmitInfo timelineInfo{ VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO };
    timelineInfo.waitSemaphoreValueCount = 1;
    timelineInfo.pWaitSemaphoreValues = &waitValue;
    timelineInfo.signalSemaphoreValueCount = 2;
    timelineInfo.pSignalSemaphoreValues = signalValues.data();
    VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submitInfo.pNext = &timelineInfo;

    // 等待 Timeline
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &timelineWait;
    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    submitInfo.pWaitDstStageMask = &waitStage;

    // 触发 Binary
    submitInfo.signalSemaphoreCount = 1;
    VkSemaphore sigVKSemaphore = sigSemaphore->GetHandle();
    VkSemaphore curTimeLineSemaphore = m_SubmitSyncPoint->GetSemaphore()->GetHandle();
    std::array<VkSemaphore,2> sigSemaphores = { sigVKSemaphore,curTimeLineSemaphore };
    submitInfo.pSignalSemaphores = sigSemaphores.data();
    
    // 提交空命令
    VKFunc::QueueSubmit(m_Queue, 1, &submitInfo, VK_NULL_HANDLE);
    

    PendingInfo pendingInfo{};
    pendingInfo.Cmds = {};
    pendingInfo.FinishedTimelineValue = signalValue;
    pendingInfo.Contexts = {};
    pendingInfo.PendingBinarySemaphores = { sigSemaphore };
    m_PendingInfos.push(pendingInfo);
    return sigSemaphore;
}


void VulkanQueue::WaitValue(uint64_t fenceValue)
{
	if (fenceValue == 0)
	{
		return;
	}
    m_SubmitSyncPoint->Wait(fenceValue);
}

// -------------------------------------------------------------------------------------------------
// VulkanPresentExecutor
// -------------------------------------------------------------------------------------------------

VulkanPresentExecutor::VulkanPresentExecutor(VulkanQueue* queue)
    : m_Queue(queue)
{
}

void VulkanPresentExecutor::Present(RHI::RHISwapchain* swapchain, const RHI::RHIWaitInfo& waitDependency)
{
    auto* vulkanSwapchain = dynamic_cast<VulkanRHISwapchain*>(swapchain);
    if (!vulkanSwapchain || !m_Queue)
    {
        return;
    }
    
    vulkanSwapchain->Present(m_Queue, waitDependency);
}
}