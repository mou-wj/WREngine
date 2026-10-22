#include "VulkanSync.h"
#include "VulkanFuncWrapper.h"
#include <stdexcept>

namespace RHIVulkan{

    VulkanEvent::VulkanEvent(VulkanDevice* device)
        : device_(device)
    {
        VkEventCreateInfo info{ VK_STRUCTURE_TYPE_EVENT_CREATE_INFO };
        info.flags = 0;
        VKFunc::CreateEvent_(device_->GetHandle(), &info, &event_);
    }

    VulkanEvent::~VulkanEvent()
    {
        if (event_ != VK_NULL_HANDLE) {
            VKFunc::DestroyEvent(device_->GetHandle(), event_);
            event_ = VK_NULL_HANDLE;
        }
    }

    VkEvent VulkanEvent::GetHandle() const { return event_; }

    void VulkanEvent::Set() const { VKFunc::SetEvent(device_->GetHandle(), event_); }
    void VulkanEvent::Reset() const { VKFunc::ResetEvent(device_->GetHandle(), event_); }
    VkResult VulkanEvent::GetStatus() const { return VKFunc::GetEventStatus(device_->GetHandle(), event_) ? VK_SUCCESS : VK_ERROR_UNKNOWN; }

    VulkanEventManager::VulkanEventManager(VulkanDevice* device)
        : device_(device) {
    }

    VulkanEventManager::~VulkanEventManager() {
        std::lock_guard<std::mutex> lock(mutex_);
        pool_ = {};
        managedObjects_ = {}; // shared_ptrԶ
    }

    std::shared_ptr<VulkanEvent> VulkanEventManager::Acquire() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!pool_.empty()) {
            auto evt = pool_.front();
            pool_.pop();
            evt->Reset();
            return evt;
        }

        //ؿʱ¶󣬲 managedObjects
        auto evt = std::make_shared<VulkanEvent>(device_);
        managedObjects_.push_back(evt);
        return evt;
    }

    void VulkanEventManager::Release(std::shared_ptr<VulkanEvent> evt) {
        if (!evt) return;
        std::lock_guard<std::mutex> lock(mutex_);
        pool_.push(evt);
    }


    VulkanSemaphore::VulkanSemaphore(VulkanDevice* device, bool isBinary, uint64_t initialValue)
        : device_(device),isBinary_(isBinary), value_(initialValue)
    {
        VkSemaphoreTypeCreateInfo typeCreateInfo{};
        typeCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
        if (!isBinary) {
            typeCreateInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
            typeCreateInfo.initialValue = initialValue;
            value_ = initialValue;
        }
        else {
            typeCreateInfo.semaphoreType = VK_SEMAPHORE_TYPE_BINARY;
            typeCreateInfo.initialValue = 0;
            value_ = 0;
        }
        
        VkSemaphoreCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        createInfo.pNext = &typeCreateInfo;
        createInfo.flags = 0;
        VKFunc::CreateSemaphore_(device_->GetHandle(), &createInfo, &semaphore_);
#ifdef DEBUG_INFO
		static uint64_t counter = 0;
		std::string debugNname = "Semaphore_" + std::to_string(counter++);
        		VKFunc::SetDebugName(device_->GetHandle(), VK_OBJECT_TYPE_SEMAPHORE, (uint64_t)semaphore_, debugNname.c_str());
#endif // DEBUG_INFO

    }

    VulkanSemaphore::~VulkanSemaphore()
    {
        if (semaphore_ != VK_NULL_HANDLE) {
            device_->EnqueueSemaphoreForDeletion(semaphore_);
            semaphore_ = VK_NULL_HANDLE;
        }
    }

    VkSemaphore VulkanSemaphore::GetHandle() const { return semaphore_; }



    uint64_t VulkanSemaphore::GetCurrentValue() {
        uint64_t value = 0;
        VKFunc::GetSemaphoreCounterValue(device_->GetHandle(), semaphore_, &value);
        return value;
    }

    void VulkanSemaphore::Wait(uint64_t Value, uint64_t TimeoutNS) {
        VkSemaphoreWaitInfo waitInfo{};
        waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
        waitInfo.flags = 0;
        waitInfo.semaphoreCount = 1;
        waitInfo.pSemaphores = &semaphore_;
        waitInfo.pValues = &Value;
        if (!VKFunc::WaitSemaphores(device_->GetHandle(), &waitInfo, TimeoutNS)) {
            throw std::runtime_error("Failed to wait for timeline semaphore");
        }
        value_ = Value;
    }

    VulkanSemaphoreManager::VulkanSemaphoreManager(VulkanDevice* device)
        : device_(device) {
        
    }

    VulkanSemaphoreManager::~VulkanSemaphoreManager() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto sem : allAllocateds_) {
            delete sem;
        }

    }

    void VulkanSemaphoreManager::BatchCreateSemaphores(size_t count) {
        for (size_t i = 0; i < count; ++i) {
            VulkanSemaphore* sem = new VulkanSemaphore(device_, true);
            freePool_.insert(sem);
            allAllocateds_.insert(sem);
        }
    }

    VulkanSemaphore* VulkanSemaphoreManager::AcquireBinary() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (freePool_.empty()) {
            BatchCreateSemaphores(BatchSize);
        }
        
        VulkanSemaphore* sem = *freePool_.begin();
        freePool_.erase(freePool_.begin());
        return sem;
    }

    void VulkanSemaphoreManager::ReleaseBinary(VulkanSemaphore* sem) {
        if (!sem) return;
        std::lock_guard<std::mutex> lock(mutex_);
        freePool_.insert(sem);
    }

// -------------------------------------------------------------------------------------------------
// VulkanFence Implementation
// -------------------------------------------------------------------------------------------------

VulkanFence::VulkanFence(VulkanDevice* device)
{
    Device = device;
    VkDevice vkDevice = Device->GetHandle();
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = 0;
    if (!VKFunc::CreateFence(vkDevice, &fenceInfo, &Fence)) {
        // Handle error
    }
}

VulkanFence::~VulkanFence()
{
    if (Fence) {
        VKFunc::DestroyFence(Device->GetHandle(), Fence);
        Fence = VK_NULL_HANDLE;
    }
}

bool VulkanFence::IsSignaled() const
{
    if (!Fence || !Device) return false;
    return VKFunc::GetFenceStatus(Device->GetHandle(), Fence);
}

void VulkanFence::Reset()
{
    if (!Fence || !Device) return;
    VKFunc::ResetFences(Device->GetHandle(), 1, &Fence);
}

void VulkanFence::Wait()
{
    if (!Fence || !Device) return;
    VKFunc::WaitForFences(Device->GetHandle(), 1, &Fence, VK_TRUE, UINT64_MAX);
}

// -------------------------------------------------------------------------------------------------
// VulkanFenceManager Implementation
// -------------------------------------------------------------------------------------------------

VulkanFenceManager::VulkanFenceManager(VulkanDevice* deviceIn)
    : device(deviceIn)
{
}

VulkanFenceManager::~VulkanFenceManager()
{
    allFences.clear();
    availableFences.clear();
    pendingFences.clear();
}

VulkanFence* VulkanFenceManager::AcquireFence()
{
    if (!availableFences.empty())
    {
        VulkanFence* fence = availableFences.front();
        availableFences.pop_front();
        return fence;
    }

    auto newFence = std::make_unique<VulkanFence>(device);
    VulkanFence* ptr = newFence.get();
    allFences.push_back(std::move(newFence));
    return ptr;
}

void VulkanFenceManager::ReleaseFence(VulkanFence* fence)
{
    if (fence)
    {
        pendingFences.push_back(fence);
    }
}

void VulkanFenceManager::GarbageCollect()
{
    size_t count = pendingFences.size();
    for (size_t i = 0; i < count; ++i)
    {
        VulkanFence* fence = pendingFences.front();
        pendingFences.pop_front();

        if (fence->IsSignaled())
        {
            availableFences.push_back(fence);
        }
        else
        {
            pendingFences.push_back(fence);
        }
    }
}

// -------------------------------------------------------------------------------------------------
// VulkanRHISyncPoint (Timeline Semaphore) Implementation
// -------------------------------------------------------------------------------------------------

VulkanRHISyncPoint::VulkanRHISyncPoint(VulkanDevice* device, RHI::EQueueType queueType, uint64_t initialValue, bool isBinary)
	: device_(device)
{
    semaphore_ = new VulkanSemaphore(device_, isBinary,initialValue);
    ownSemaphore_ = true;
    _type = queueType;
}

VulkanRHISyncPoint::VulkanRHISyncPoint(VulkanDevice* device, RHI::EQueueType queueType, VulkanSemaphore* semaphore)
{
    ownSemaphore_ = false;
    semaphore_ = semaphore;
    _type = queueType;
}

VulkanRHISyncPoint::~VulkanRHISyncPoint() {
    if (ownSemaphore_) {
        delete semaphore_;
    }
    
}

uint64_t VulkanRHISyncPoint::GetCurrentValue() {
    return semaphore_->GetCurrentValue();
}

void VulkanRHISyncPoint::Wait(uint64_t Value, uint64_t TimeoutNS) {
    semaphore_->Wait(Value, TimeoutNS);
}

}
