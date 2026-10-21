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
	: device_(device), queue_(queue), familyIndex_(familyIndex), QueueType_(queueType)
{
    SubmitSyncPoint_ = new VulkanRHISyncPoint(device, queueType);
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
    std::string str1 = std::format("{:#x}", uint64_t(queue_));
    
	VKFunc::SetDebugName(device_->GetHandle(), VK_OBJECT_TYPE_QUEUE, (uint64_t)queue_, ("VulkanQueue:" + queueTypeName + " " + str1).c_str());
    LOG_INFO("Create VulkanQueue: %s %s", queueTypeName.c_str(), str1.c_str());
#endif
}

VulkanQueue::~VulkanQueue()
{
    for (VulkanCommandContext* ctx : AllContexts_)
    {
        delete ctx;
    }
    AllContexts_.clear();
    FreeContexts_.clear();
    delete SubmitSyncPoint_;
}

void VulkanQueue::WaitIdle()
{
    VKFunc::QueueWaitIdle(queue_);

}

uint64_t VulkanQueue::GetCurrentTimelineValue()
{
	return SubmitSyncPoint_->GetCurrentValue();
}

void VulkanQueue::GarbageCollect() 
{
    uint64_t CurrentTime = SubmitSyncPoint_->GetCurrentValue();
    while (!PendingInfos.empty()) {
        auto& front = PendingInfos.front();
        if (front.FinishedTimelineValue <= CurrentTime) {
            for (auto cmd : front.Cmds) {
                cmd->MarkState(VulkanCommandBuffer::NeedRecycle);
            }
            for (auto contex : front.Contexts) {
				ReleaseCommandContext(contex);
            }
            for (auto sem : front.PendingBinarySemaphores) {
                device_->GetSemaphoreManager()->ReleaseBinary(sem);
            }
            PendingInfos.pop();
            continue;
        }
        break;
    }
}

void VulkanQueue::InitContextPool(uint32_t poolSize)
{
    std::lock_guard<std::mutex> lock(ContextPoolMutex_);
    for (uint32_t i = 0; i < poolSize; ++i)
    {
        VulkanCommandContext* ctx = CreateQueueContext(device_, this, QueueType_);
        AllContexts_.push_back(ctx);
        FreeContexts_.push_back(ctx);
    }
}

RHI::EQueueType VulkanQueue::GetType() const
{
    return QueueType_;
}

RHI::RHIContextBase* VulkanQueue::AcquireCommandContext()
{
    std::lock_guard<std::mutex> lock(ContextPoolMutex_);
    //LOG_INFO("duration time %f s", Core::Timer::GetGlobalInstance().GetDelta());
    if (!FreeContexts_.empty())
    {
        VulkanCommandContext* ctx = FreeContexts_.back();
        FreeContexts_.pop_back();
        return ctx;
    }
    if (QueueType_ == EQueueType::Graphics) {
        LOG_INFO("%s", "create context");
    }
   
    VulkanCommandContext* ctx = CreateQueueContext(device_, this, QueueType_);
    AllContexts_.push_back(ctx);
    return ctx;
}

RHI::RHIContextBase* VulkanQueue::ReleaseCommandContext(RHI::RHIContextBase* Context)
{
    if (!Context)
    {
        return nullptr;
    }

    VulkanCommandContext* vulkanCtx = dynamic_cast<VulkanCommandContext*>(Context);
    std::lock_guard<std::mutex> lock(ContextPoolMutex_);
    FreeContexts_.push_back(vulkanCtx);
    return nullptr;
}

uint64_t VulkanQueue::ExecuteContext(RHI::RHIContextBase* contextBase)
{
    return ExecuteContext({ contextBase }, {});
}

uint64_t VulkanQueue::ExecuteContext(const std::vector<RHI::RHIContextBase*>& Contexts, const std::vector<RHI::RHIWaitInfo>& WaitInfos)
{
    std::lock_guard<std::mutex> lock(FlushContextMutex_);

    // 1. 准备所有的 Wait 信息 (Timeline Semaphore 模式)
    std::vector<VkSemaphore> waitHandles;
    std::vector<uint64_t> waitValues;
    std::vector<VkPipelineStageFlags> waitStages;
    std::vector<VulkanSemaphore*> pendingSeamphores;

    for (const auto& waitInfo : WaitInfos)
    {
        RHISyncPoint* rhivkSyncPoint = nullptr;
        if (waitInfo.SyncPoint) {
			rhivkSyncPoint = waitInfo.SyncPoint;
        } else {
            // 优先判断QueueType是否和当前queue一致
            if (waitInfo.QueueType == QueueType_) {
                rhivkSyncPoint = SubmitSyncPoint_;
            } else {
                switch (waitInfo.QueueType)
                {
                case EQueueType::Graphics:
                    rhivkSyncPoint = device_->GetGraphicsQueue()->GetSyncPoint();
					break;
                case EQueueType::Compute:
                    rhivkSyncPoint = device_->GetComputeQueue()->GetSyncPoint();
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
    uint64_t signalValue = ++currentTimelineValue_;
    // 3. 收集所有的 Command Buffers
    std::vector<VkCommandBuffer> cmdHandles;
    std::vector<VulkanCommandBuffer*> cmdBuffers;
    std::vector<VulkanCommandContext*> vkcontexts;
    for (auto* context : Contexts)
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
    VkSemaphore signalHandle = SubmitSyncPoint_->GetSemaphore()->GetHandle();
    submitInfo.pSignalSemaphores = &signalHandle;

    bool suc =VKFunc::QueueSubmit(queue_, 1, &submitInfo, VK_NULL_HANDLE);
    
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



    device_->GetStagingManager()->GarbageCollect();
    //for (auto cmdBuffer : cmdBuffers)
    //{
    //    cmdBuffer->GetImageLayoutManager()->TransferTo(imageLayoutManager_);
    //    cmdBuffer->GetImageLayoutManager()->Clear();
    //
    //}
    //imageLayoutManager_.PrintLayoutInfo();

    // 5. 资源回收与状态更新
    device_->ReleaseDeferredResources();
    GarbageCollect();
    PendingInfo pendingInfo{};
    pendingInfo.Cmds = cmdBuffers;
    pendingInfo.FinishedTimelineValue = signalValue;
    pendingInfo.Contexts = vkcontexts;
    pendingInfo.PendingBinarySemaphores = pendingSeamphores;
    PendingInfos.push(pendingInfo);
    // 6. 返回 Fence：它本质上是同步点和数值的组合
    RHI::RHIFence result{};
    result.QueueType = SubmitSyncPoint_->GetQueueType();
    result.Value = signalValue;

    return result.Value;

}

VulkanSemaphore* VulkanQueue::SubmitEmptyWithDependency(VkSemaphore timelineWait, uint64_t waitValue)
{
    auto sigSemaphore = device_->GetSemaphoreManager()->AcquireBinary();
    uint64_t signalValue = ++currentTimelineValue_;
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
    VkSemaphore curTimeLineSemaphore = SubmitSyncPoint_->GetSemaphore()->GetHandle();
    std::array<VkSemaphore,2> sigSemaphores = { sigVKSemaphore,curTimeLineSemaphore };
    submitInfo.pSignalSemaphores = sigSemaphores.data();
    
    // 提交空命令
    VKFunc::QueueSubmit(queue_, 1, &submitInfo, VK_NULL_HANDLE);
    

    PendingInfo pendingInfo{};
    pendingInfo.Cmds = {};
    pendingInfo.FinishedTimelineValue = signalValue;
    pendingInfo.Contexts = {};
    pendingInfo.PendingBinarySemaphores = { sigSemaphore };
    PendingInfos.push(pendingInfo);
    return sigSemaphore;
}


void VulkanQueue::WaitValue(uint64_t FenceValue)
{
	if (FenceValue == 0)
	{
		return;
	}
    SubmitSyncPoint_->Wait(FenceValue);
}

// -------------------------------------------------------------------------------------------------
// VulkanPresentExecutor
// -------------------------------------------------------------------------------------------------

VulkanPresentExecutor::VulkanPresentExecutor(VulkanQueue* queue)
    : Queue(queue)
{
}

void VulkanPresentExecutor::Present(RHI::RHISwapchain* Swapchain, const RHI::RHIWaitInfo& WaitDependency)
{
    auto* vulkanSwapchain = dynamic_cast<VulkanRHISwapchain*>(Swapchain);
    if (!vulkanSwapchain || !Queue)
    {
        return;
    }
    
    vulkanSwapchain->Present(Queue, WaitDependency);
}
}