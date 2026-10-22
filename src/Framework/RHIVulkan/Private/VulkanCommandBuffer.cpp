#include "VulkanCommandBuffer.h"
#include "VulkanDevice.h"
#include "VulkanCommandContex.h"
#include "VulkanQueue.h"
#include "VulkanFuncWrapper.h"
#include <stdexcept>

namespace RHIVulkan{

// VulkanCommandBuffer



VulkanCommandBuffer::VulkanCommandBuffer(VulkanDevice* device, VulkanCommandBufferPool* owner, VkCommandBufferLevel level) :m_Device(device), m_Owner(owner),m_Level(level)
{

}

VulkanCommandBuffer::~VulkanCommandBuffer()
{

}

void VulkanCommandBuffer::AllocateMemory()
{
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_Owner->GetHandle();
    allocInfo.level = m_Level;
    allocInfo.commandBufferCount = 1;

    if (!VKFunc::AllocateCommandBuffers(m_Device->GetHandle(), &allocInfo, &m_CommandBuffer)) {
        throw std::runtime_error("Failed to allocate command buffer");
    }
}

void VulkanCommandBuffer::Begin(VkCommandBufferUsageFlags usage)
{
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = usage;

    VKFunc::BeginCommandBuffer(m_CommandBuffer, &beginInfo);
}

void VulkanCommandBuffer::End()
{
    VKFunc::EndCommandBuffer(m_CommandBuffer);
}

void VulkanCommandBuffer::Reset()
{
    VKFunc::ResetCommandBuffer(m_CommandBuffer, 0);
    //imageLayoutManager.Clear();
}

// VulkanCommandBufferPool
VulkanCommandBufferPool::VulkanCommandBufferPool(VulkanDevice* device, uint32_t queueFamily)
    : m_Device(device), m_QueueFamilyIndex(queueFamily)
{
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = queueFamily;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; // 支持单个或整体 reset

    VKFunc::CreateCommandPool(device->GetHandle(), &poolInfo, &m_CommandPool);
}

VulkanCommandBufferPool::~VulkanCommandBufferPool()
{
    // 析构所有 CommandBuffer
    m_AllBuffers.clear();

    if (m_CommandPool != VK_NULL_HANDLE)
    {
        VKFunc::DestroyCommandPool(m_Device->GetHandle(), m_CommandPool);
        m_CommandPool = VK_NULL_HANDLE;
    }
}

VulkanCommandBuffer* VulkanCommandBufferPool::AllocateCommandBuffer(VkCommandBufferLevel level)
{
    // 先从可用池拿
    if (!m_AvailableBuffers.empty())
    {
        auto buf = m_AvailableBuffers.back();
        m_AvailableBuffers.pop_back();
        return buf;
    }
    auto cmdBuf = std::make_unique<VulkanCommandBuffer>(m_Device, this,level);
    cmdBuf->AllocateMemory();
    VulkanCommandBuffer* ptr = cmdBuf.get();

    m_AllBuffers.push_back(std::move(cmdBuf));
    return ptr;
}

void VulkanCommandBufferPool::Reset()
{
    // 重置 Vulkan CommandPool，所有 CommandBuffer 自动重置
    VKFunc::ResetCommandPool(m_Device->GetHandle(), m_CommandPool, 0); // 已用VKFunc包装

    // 所有 buffer 都回到可用池
    m_AvailableBuffers.clear();
    for (auto& buf : m_AllBuffers) // 注意 auto&
    {
        m_AvailableBuffers.push_back(buf.get());
        buf->MarkState(VulkanCommandBuffer::Free);
    }
}

// VulkanCommandBufferManager

VulkanCommandBufferManager::VulkanCommandBufferManager(VulkanDevice* device, VulkanCommandContext* commandContext)
    : m_Device(device), m_CommandContext(commandContext)
{
    Allocate(VK_COMMAND_BUFFER_LEVEL_PRIMARY);
}

VulkanCommandBufferManager::~VulkanCommandBufferManager()
{
    m_Pools.clear();
}

VulkanCommandBufferPool* VulkanCommandBufferManager::GetPool()
{
    // 假设每个Manager只管理一个队列族，由commandContext->GetQueue()获得
    uint32_t queueFamilyIndex = m_CommandContext->GetQueue()->GetFamilyIndex();
    auto it = m_Pools.find(queueFamilyIndex);
    if (it != m_Pools.end())
    {
        return it->second.get();
    }
    // 创建新的池
    auto pool = std::make_unique<VulkanCommandBufferPool>(m_Device, queueFamilyIndex);
    VulkanCommandBufferPool* poolPtr = pool.get();
    m_Pools[queueFamilyIndex] = std::move(pool);
    return poolPtr;
}

VulkanCommandBuffer* VulkanCommandBufferManager::Allocate(VkCommandBufferLevel level)
{
    VulkanCommandBufferPool* pool = GetPool();
    return pool->AllocateCommandBuffer(level);
}

VulkanCommandBuffer* VulkanCommandBufferManager::GetActiveCommandBuffer(VkCommandBufferLevel level)
{
    if (m_ActiveCommandBuffer) {
		return m_ActiveCommandBuffer;
    }

    // 查找可复用的命令缓冲区（Fence已完成）
    for (auto& buffer : m_ManagedBuffers)
    {
        
        if (buffer->GetState() == VulkanCommandBuffer::Free)
        {
            buffer->Reset();
            buffer->Begin();
            m_CommandContext->GetQueue()->UpdatedCommandBufferImageLayoutManager(buffer);
            m_ActiveCommandBuffer = buffer;
            return buffer;
        }
    }
    // 没有可用的，分配新的
    VulkanCommandBuffer* newBuffer = Allocate(level);
    
    m_ManagedBuffers.emplace_back(newBuffer);
    newBuffer->Begin();
    m_CommandContext->GetQueue()->UpdatedCommandBufferImageLayoutManager(newBuffer);
    m_ActiveCommandBuffer = newBuffer;
    return newBuffer;
}


VulkanCommandBuffer* VulkanCommandBufferManager::EndActiveCommandBuffer()
{
    if (!m_ActiveCommandBuffer)
    {
        return nullptr;
    }

    m_ActiveCommandBuffer->End();
    VulkanCommandBuffer* completedCommandBuffer = m_ActiveCommandBuffer;
    m_ActiveCommandBuffer = nullptr;
    return completedCommandBuffer;
}

void VulkanCommandBufferManager::Reset()
{
    for (auto& pair : m_Pools)
    {
        pair.second->Reset();
    }
}

void VulkanCommandBufferManager::GarbageCollect()
{
	for (auto it = m_ManagedBuffers.begin(); it != m_ManagedBuffers.end();)
	{
		if ((*it)->GetState() == VulkanCommandBuffer::ECommandBufferState::NeedRecycle)
		{
			// 回收资源
			(*it)->Reset();
            (*it)->MarkState(VulkanCommandBuffer::Free);
		}
		else
		{
			++it;
		}
	}
}

} // namespace WR::RHIVulkan