#include "VulkanMemory.h"
#include "VulkanDevice.h"
#include <stdexcept>
#include <algorithm>
#include <memory>
#include <cstring>
#include "VulkanCommandBuffer.h"
#include "VulkanSync.h"
#include "VulkanFuncWrapper.h"
namespace RHIVulkan{
   
constexpr VkDeviceSize DEFAULT_BLOCK_SIZE = 64 * 1024 * 1024; // 64 MB

// VulkanMemoryBlock
VulkanMemoryBlock::VulkanMemoryBlock(VulkanDevice* device, uint32_t memoryTypeIndex, VkDeviceSize blockSize, bool mappable)
    : m_Device(device), m_MemoryTypeIndex(memoryTypeIndex), m_Size(blockSize), m_Offset(0), m_bMappable(mappable), m_Mapped(nullptr)
{
    VkMemoryAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    allocInfo.allocationSize = m_Size;
    allocInfo.memoryTypeIndex = memoryTypeIndex;
	VkMemoryAllocateFlagsInfo flagsInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO };
    flagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
	allocInfo.pNext = &flagsInfo;
    if (!VKFunc::AllocateMemory(m_Device->GetHandle(), &allocInfo, &m_Memory))
        throw std::runtime_error("Failed to allocate Vulkan memory block");

    if (m_bMappable)
        VKFunc::MapMemory(m_Device->GetHandle(), m_Memory, 0, VK_WHOLE_SIZE, 0, &m_Mapped);
    m_FreeList.push_back({ 0, m_Size });
}

VulkanMemoryBlock::~VulkanMemoryBlock()
{
    if (m_Mapped)
        VKFunc::UnmapMemory(m_Device->GetHandle(), m_Memory);
    VKFunc::FreeMemory(m_Device->GetHandle(), m_Memory);
}

bool VulkanMemoryBlock::Allocate(VkDeviceSize size, VkDeviceSize alignment, VulkanAllocation& outAlloc)
{
    // 1. 先在 free list 找合适的块
    for (auto it = m_FreeList.begin(); it != m_FreeList.end(); ++it)
    {
        // 命中 free block
        VkDeviceSize start = it->Offset;
        VkDeviceSize end = it->Offset + it->Size;
    
        VkDeviceSize alignedOffset =
            (start + alignment - 1) & ~(alignment - 1);
    
        VkDeviceSize allocEnd = alignedOffset + size;
    
        if (allocEnd > end)
            continue;
    
        // 切分前后两段
        VkDeviceSize beforeSize = alignedOffset - start;
        VkDeviceSize afterOffset = allocEnd;
        VkDeviceSize afterSize = end - allocEnd;
    
        // 先删掉当前 block
        it = m_FreeList.erase(it);
    
        // 插入 before
        if (beforeSize > 0)
            m_FreeList.push_back({ start, beforeSize });
    
        // 插入 after
        if (afterSize > 0)
            m_FreeList.push_back({ afterOffset, afterSize });
    
        // 返回分配
        outAlloc = VulkanAllocation(
            m_Memory,
            alignedOffset,
            size,
            m_Mapped ? static_cast<char*>(m_Mapped) + alignedOffset : nullptr
        );
    
        return true;
    }
    
    return false;

}

void VulkanMemoryBlock::Reset()
{
    m_Offset = 0;
    m_FreeList.clear();
    m_FreeList.push_back({ 0, m_Size });
}

bool VulkanMemoryBlock::Free(VulkanAllocation& alloc)
{
    if (alloc.GetMemory() != m_Memory)
        return false;
    
    m_FreeList.push_back({
        alloc.GetOffset(),
        alloc.GetSize()
        });
    
    alloc = VulkanAllocation();
    MergeFreeList();
    return true;

}
void VulkanMemoryBlock::MergeFreeList()
{
    std::sort(m_FreeList.begin(), m_FreeList.end(),
        [](auto& a, auto& b) { return a.Offset < b.Offset; });

    std::vector<FreeRegion> merged;

    for (auto& r : m_FreeList)
    {
        if (merged.empty())
        {
            merged.push_back(r);
            continue;
        }

        auto& last = merged.back();

        if (last.Offset + last.Size == r.Offset)
        {
            last.Size += r.Size;
        }
        else
        {
            merged.push_back(r);
        }
    }

    m_FreeList = std::move(merged);
}

// VulkanMemoryManager
VulkanMemoryManager::VulkanMemoryManager(VulkanDevice* device)
    : m_Device(device) {}

VulkanMemoryManager::~VulkanMemoryManager()
{
    m_Blocks.clear();
}

uint32_t VulkanMemoryManager::FindMemoryType(uint32_t typeBits, VkMemoryPropertyFlags properties) const
{
    VkPhysicalDeviceMemoryProperties memProps;
    VKFunc::GetPhysicalDeviceMemoryProperties(m_Device->GetPhysicalDevice(), &memProps);

    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
    {
        if ((typeBits & (1 << i)) && (memProps.memoryTypes[i].propertyFlags & properties) == properties)
            return i;
    }
    throw std::runtime_error("Failed to find suitable memory type");
}

bool VulkanMemoryManager::Allocate(const VkMemoryRequirements& memReqs, VkMemoryPropertyFlags props, VulkanAllocation& outAlloc)
{
    uint32_t typeIndex = FindMemoryType(memReqs.memoryTypeBits, props);
    for (auto& block : m_Blocks)
    {
        if (block->GetMemoryTypeIndex() == typeIndex)
        {
            if (block->Allocate(memReqs.size, memReqs.alignment, outAlloc))
                return true;
        }
    }

    // Allocate new block
    auto newBlock = std::make_unique<VulkanMemoryBlock>(m_Device, typeIndex, std::max(DEFAULT_BLOCK_SIZE, memReqs.size), (props & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0);
    bool success = newBlock->Allocate(memReqs.size, memReqs.alignment, outAlloc);
    if (!success)
        return false;

    m_Blocks.push_back(std::move(newBlock));
    return true;
}

void VulkanMemoryManager::Reset()
{
    for (auto& block : m_Blocks)
        block->Reset();
}

bool VulkanMemoryManager::Free( VulkanAllocation& alloc)
{
    if (alloc.GetMemory() == VK_NULL_HANDLE)
        return true;

    // 找到分配所在的内存块
    for (auto& block : m_Blocks)
    {
        if (block->Free(alloc))
        {
            alloc = VulkanAllocation();
            return true;
        }
    }
    return false;
}



VulkanStagingBuffer::VulkanStagingBuffer(VulkanDevice* device, VulkanMemoryManager* memManager, VkDeviceSize size)
    : RHI::RHIStagingBuffer(size), m_Device(device), m_MemManager(memManager), m_Size(size)
{
    // 创建 buffer
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = m_Size;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;


    VKFunc::CreateBuffer(m_Device->GetHandle(), &bufferInfo, &m_Buffer);
#ifdef DEBUG_INFO
    std::string debugName;
    char buf[64];
    snprintf(buf, sizeof(buf), "VulkanStagingBuffer:0x%llx", (unsigned long long)m_Buffer);
    debugName = buf;
	VKFunc::SetDebugName(m_Device->GetHandle(), VK_OBJECT_TYPE_BUFFER, (uint64_t)m_Buffer, debugName.c_str());
#endif

    // 查询 memory requirements
    VkMemoryRequirements memReq;
    VKFunc::GetBufferMemoryRequirements(m_Device->GetHandle(), m_Buffer, &memReq);


    // 分配 HostVisible 内存
    m_MemManager->Allocate(memReq, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_Allocation);


    VKFunc::BindBufferMemory(m_Device->GetHandle(), m_Buffer, m_Allocation.GetMemory(), m_Allocation.GetOffset());


    // 映射内存
    m_Mapped = m_Allocation.GetMappedPointer();
}


VulkanStagingBuffer::~VulkanStagingBuffer()
{
    if (m_Buffer)
    {
        m_Device->EnqueueBufferForDeletion(m_Buffer);
        m_Buffer = VK_NULL_HANDLE;
    }


    // VulkanMemoryManager 可以管理释放 allocation，或者这里不做事
}
void* VulkanStagingBuffer::Map(uint32_t offset, uint32_t numBytes)
{
    return m_Mapped;
}
void VulkanStagingBuffer::Unmap()
{

}
VulkanStagingManager::VulkanStagingManager(VulkanDevice* device, VulkanMemoryManager* memManager)
    : m_Device(device), m_MemManager(memManager)
{
}


VulkanStagingManager::~VulkanStagingManager()
{
    m_FreeBuffers.clear();
}


std::shared_ptr<VulkanStagingBuffer> VulkanStagingManager::Acquire(VkDeviceSize size)
{
    std::lock_guard<std::mutex> lock(m_Mutex);

    for (auto it = m_FreeBuffers.begin(); it != m_FreeBuffers.end(); ++it)
    {
        if ((*it)->GetSize() >= size)
        {
            auto buf = *it;
            m_FreeBuffers.erase(it);
            return buf;
        }
    }

    return std::make_shared<VulkanStagingBuffer>(m_Device, m_MemManager, size);
}

void VulkanStagingManager::ReleaseToCmdBuffer(
    VulkanCommandBuffer* cmd,
    std::shared_ptr<VulkanStagingBuffer> buffer)
{
    std::lock_guard<std::mutex> lock(m_Mutex);

    auto& entry = m_PendingBufferMap[cmd];
    entry.PendingBuffers.push_back(buffer);
}

void VulkanStagingManager::MarkMappedBuffersUsed(std::shared_ptr<VulkanStagingBuffer> buffer, bool used)
{
	std::lock_guard<std::mutex> lock(m_Mutex);
	m_bMapedToFreeBufferUsed[buffer] = used;
}

void VulkanStagingManager::GarbageCollect()
{
    std::lock_guard<std::mutex> lock(m_Mutex);
	std::vector<VulkanCommandBuffer*> completedCmds;
	std::vector<std::shared_ptr<VulkanStagingBuffer>> buffersToFree; 
	for (auto& entry : m_PendingBufferMap)
	{
		if (entry.first->GetState() == VulkanCommandBuffer::NeedRecycle)
		{
			completedCmds.push_back(entry.first);
			for (auto& buf : entry.second.PendingBuffers)
			{
                if (m_bMapedToFreeBufferUsed.find(buf) != m_bMapedToFreeBufferUsed.end()) {
                    if (m_bMapedToFreeBufferUsed[buf] = true) {
                        m_MapedToFreeBuffers.push_back(buf);
                    }
                    else {
                        m_FreeBuffers.push_back(buf);
                        m_bMapedToFreeBufferUsed.erase(buf);
                    }
                    continue;
                }
                m_FreeBuffers.push_back(buf);
			}
		}
	}
    for (auto& buf : m_MapedToFreeBuffers) {
        if (m_bMapedToFreeBufferUsed.find(buf) != m_bMapedToFreeBufferUsed.end()) {
            if (m_bMapedToFreeBufferUsed[buf] = false) {
                m_FreeBuffers.push_back(buf);
                m_bMapedToFreeBufferUsed.erase(buf);
            }
        }
    }
    for (auto& cmd : completedCmds)
	{
        m_PendingBufferMap.erase(cmd);
	}

}

}
