#include "VulkanSync.h"
#include "VulkanFuncWrapper.h"
#include <stdexcept>

namespace RHIVulkan{

    VulkanEvent::VulkanEvent(VulkanDevice* device)
        : m_Device(device)
    {
        VkEventCreateInfo info{ VK_STRUCTURE_TYPE_EVENT_CREATE_INFO };
        info.flags = 0;
        VKFunc::CreateEvent_(m_Device->GetHandle(), &info, &m_Event);
    }

    VulkanEvent::~VulkanEvent()
    {
        if (m_Event != VK_NULL_HANDLE) {
            VKFunc::DestroyEvent(m_Device->GetHandle(), m_Event);
            m_Event = VK_NULL_HANDLE;
        }
    }

    VkEvent VulkanEvent::GetHandle() const { return m_Event; }

    void VulkanEvent::Set() const { VKFunc::SetEvent(m_Device->GetHandle(), m_Event); }
    void VulkanEvent::Reset() const { VKFunc::ResetEvent(m_Device->GetHandle(), m_Event); }
    VkResult VulkanEvent::GetStatus() const { return VKFunc::GetEventStatus(m_Device->GetHandle(), m_Event) ? VK_SUCCESS : VK_ERROR_UNKNOWN; }

    VulkanEventManager::VulkanEventManager(VulkanDevice* device)
        : m_Device(device) {
    }

    VulkanEventManager::~VulkanEventManager() {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Pool = {};
        m_ManagedObjects = {}; // shared_ptrԶ
    }

    std::shared_ptr<VulkanEvent> VulkanEventManager::Acquire() {
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (!m_Pool.empty()) {
            auto evt = m_Pool.front();
            m_Pool.pop();
            evt->Reset();
            return evt;
        }

        //ؿʱ¶󣬲 managedObjects
        auto evt = std::make_shared<VulkanEvent>(m_Device);
        m_ManagedObjects.push_back(evt);
        return evt;
    }

    void VulkanEventManager::Release(std::shared_ptr<VulkanEvent> evt) {
        if (!evt) return;
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Pool.push(evt);
    }


    VulkanSemaphore::VulkanSemaphore(VulkanDevice* device, bool isBinary, uint64_t initialValue)
        : m_Device(device),m_bIsBinary(isBinary), m_Value(initialValue)
    {
        VkSemaphoreTypeCreateInfo typeCreateInfo{};
        typeCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
        if (!isBinary) {
            typeCreateInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
            typeCreateInfo.initialValue = initialValue;
            m_Value = initialValue;
        }
        else {
            typeCreateInfo.semaphoreType = VK_SEMAPHORE_TYPE_BINARY;
            typeCreateInfo.initialValue = 0;
            m_Value = 0;
        }
        
        VkSemaphoreCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        createInfo.pNext = &typeCreateInfo;
        createInfo.flags = 0;
        VKFunc::CreateSemaphore_(m_Device->GetHandle(), &createInfo, &m_Semaphore);
#ifdef DEBUG_INFO
		static uint64_t counter = 0;
		std::string debugNname = "Semaphore_" + std::to_string(counter++);
                VKFunc::SetDebugName(m_Device->GetHandle(), VK_OBJECT_TYPE_SEMAPHORE, (uint64_t)m_Semaphore, debugNname.c_str());
#endif // DEBUG_INFO

    }

    VulkanSemaphore::~VulkanSemaphore()
    {
        if (m_Semaphore != VK_NULL_HANDLE) {
            m_Device->EnqueueSemaphoreForDeletion(m_Semaphore);
            m_Semaphore = VK_NULL_HANDLE;
        }
    }

    VkSemaphore VulkanSemaphore::GetHandle() const { return m_Semaphore; }



    uint64_t VulkanSemaphore::GetCurrentValue() {
        uint64_t value = 0;
        VKFunc::GetSemaphoreCounterValue(m_Device->GetHandle(), m_Semaphore, &value);
        return value;
    }

    void VulkanSemaphore::Wait(uint64_t value, uint64_t timeoutNs) {
        VkSemaphoreWaitInfo waitInfo{};
        waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
        waitInfo.flags = 0;
        waitInfo.semaphoreCount = 1;
        waitInfo.pSemaphores = &m_Semaphore;
        waitInfo.pValues = &value;
        if (!VKFunc::WaitSemaphores(m_Device->GetHandle(), &waitInfo, timeoutNs)) {
            throw std::runtime_error("Failed to wait for timeline semaphore");
        }
        m_Value = value;
    }

    VulkanSemaphoreManager::VulkanSemaphoreManager(VulkanDevice* device)
        : m_Device(device) {
        
    }

    VulkanSemaphoreManager::~VulkanSemaphoreManager() {
        std::lock_guard<std::mutex> lock(m_Mutex);
        for (auto sem : m_AllAllocateds) {
            delete sem;
        }

    }

    void VulkanSemaphoreManager::BatchCreateSemaphores(size_t count) {
        for (size_t i = 0; i < count; ++i) {
            VulkanSemaphore* sem = new VulkanSemaphore(m_Device, true);
            m_FreePool.insert(sem);
            m_AllAllocateds.insert(sem);
        }
    }

    VulkanSemaphore* VulkanSemaphoreManager::AcquireBinary() {
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (m_FreePool.empty()) {
            BatchCreateSemaphores(m_BatchSize);
        }
        
        VulkanSemaphore* sem = *m_FreePool.begin();
        m_FreePool.erase(m_FreePool.begin());
        return sem;
    }

    void VulkanSemaphoreManager::ReleaseBinary(VulkanSemaphore* sem) {
        if (!sem) return;
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_FreePool.insert(sem);
    }

// -------------------------------------------------------------------------------------------------
// VulkanFence Implementation
// -------------------------------------------------------------------------------------------------

VulkanFence::VulkanFence(VulkanDevice* device)
{
    m_Device = device;
    VkDevice vkDevice = m_Device->GetHandle();
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = 0;
    if (!VKFunc::CreateFence(vkDevice, &fenceInfo, &m_Fence)) {
        // Handle error
    }
}

VulkanFence::~VulkanFence()
{
    if (m_Fence) {
        VKFunc::DestroyFence(m_Device->GetHandle(), m_Fence);
        m_Fence = VK_NULL_HANDLE;
    }
}

bool VulkanFence::IsSignaled() const
{
    if (!m_Fence || !m_Device) return false;
    return VKFunc::GetFenceStatus(m_Device->GetHandle(), m_Fence);
}

void VulkanFence::Reset()
{
    if (!m_Fence || !m_Device) return;
    VKFunc::ResetFences(m_Device->GetHandle(), 1, &m_Fence);
}

void VulkanFence::Wait()
{
    if (!m_Fence || !m_Device) return;
    VKFunc::WaitForFences(m_Device->GetHandle(), 1, &m_Fence, VK_TRUE, UINT64_MAX);
}

// -------------------------------------------------------------------------------------------------
// VulkanFenceManager Implementation
// -------------------------------------------------------------------------------------------------

VulkanFenceManager::VulkanFenceManager(VulkanDevice* deviceIn)
    : m_Device(deviceIn)
{
}

VulkanFenceManager::~VulkanFenceManager()
{
    m_AllFences.clear();
    m_AvailableFences.clear();
    m_PendingFences.clear();
}

VulkanFence* VulkanFenceManager::AcquireFence()
{
    if (!m_AvailableFences.empty())
    {
        VulkanFence* fence = m_AvailableFences.front();
        m_AvailableFences.pop_front();
        return fence;
    }

    auto newFence = std::make_unique<VulkanFence>(m_Device);
    VulkanFence* ptr = newFence.get();
    m_AllFences.push_back(std::move(newFence));
    return ptr;
}

void VulkanFenceManager::ReleaseFence(VulkanFence* fence)
{
    if (fence)
    {
        m_PendingFences.push_back(fence);
    }
}

void VulkanFenceManager::GarbageCollect()
{
    size_t count = m_PendingFences.size();
    for (size_t i = 0; i < count; ++i)
    {
        VulkanFence* fence = m_PendingFences.front();
        m_PendingFences.pop_front();

        if (fence->IsSignaled())
        {
            m_AvailableFences.push_back(fence);
        }
        else
        {
            m_PendingFences.push_back(fence);
        }
    }
}

// -------------------------------------------------------------------------------------------------
// VulkanRHISyncPoint (Timeline Semaphore) Implementation
// -------------------------------------------------------------------------------------------------

VulkanRHISyncPoint::VulkanRHISyncPoint(VulkanDevice* device, RHI::EQueueType queueType, uint64_t initialValue, bool isBinary)
	: m_Device(device)
{
    m_Semaphore = new VulkanSemaphore(m_Device, isBinary,initialValue);
    m_bOwnSemaphore = true;
    _type = queueType;
}

VulkanRHISyncPoint::VulkanRHISyncPoint(VulkanDevice* device, RHI::EQueueType queueType, VulkanSemaphore* semaphore)
{
    m_bOwnSemaphore = false;
    m_Semaphore = semaphore;
    _type = queueType;
}

VulkanRHISyncPoint::~VulkanRHISyncPoint() {
    if (m_bOwnSemaphore) {
        delete m_Semaphore;
    }
    
}

uint64_t VulkanRHISyncPoint::GetCurrentValue() {
    return m_Semaphore->GetCurrentValue();
}

void VulkanRHISyncPoint::Wait(uint64_t value, uint64_t timeoutNs) {
    m_Semaphore->Wait(value, timeoutNs);
}

}
