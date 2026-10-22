#pragma once
#include "VulkanDevice.h"
#include <vector>
#include <cstdint>
#include <memory>
#include <deque>
#include "RHIResource.h"
namespace RHIVulkan{


class VulkanAllocation
{
public:
    VulkanAllocation(VkDeviceMemory memory = VK_NULL_HANDLE, VkDeviceSize offset = 0, VkDeviceSize size = 0, void* mapped = nullptr, VkBuffer buffer = VK_NULL_HANDLE)
        : m_Memory(memory), m_Offset(offset), m_Size(size), m_Mapped(mapped), m_Buffer(buffer) {}

    VkDeviceMemory GetMemory() const { return m_Memory; }
    VkDeviceSize GetOffset() const { return m_Offset; }
    VkDeviceSize GetSize() const { return m_Size; }
    void* GetMappedPointer() const { return m_Mapped; }
    VkBuffer GetBufferHandle() const { return m_Buffer; }

    void SetBufferHandle(VkBuffer buffer) { m_Buffer = buffer; }

private:
    VkDeviceMemory m_Memory;
    VkDeviceSize m_Offset;
    VkDeviceSize m_Size;
    void* m_Mapped; // 可选，仅对HOST_VISIBLE内存使用
    VkBuffer m_Buffer; // 关联的缓冲区句柄
};

class VulkanMemoryBlock
{
public:
    VulkanMemoryBlock(VulkanDevice* device, uint32_t memoryTypeIndex, VkDeviceSize blockSize, bool mappable);
    ~VulkanMemoryBlock();

    bool Allocate(VkDeviceSize size, VkDeviceSize alignment, VulkanAllocation& outAlloc);
    void Reset(); // 可选：用于帧结束时重置分配
    bool Free(VulkanAllocation& alloc); // 可选：用于释放分配的内存

    uint32_t GetMemoryTypeIndex() const { return m_MemoryTypeIndex; }

private:
    void MergeFreeList();
    struct FreeRegion
    {
        VkDeviceSize Offset;
        VkDeviceSize Size;
    };
    std::vector<FreeRegion> m_FreeList;
    VulkanDevice* m_Device;
    VkDeviceMemory m_Memory;
    VkDeviceSize m_Size;
    VkDeviceSize m_Offset;
    void* m_Mapped;
    uint32_t m_MemoryTypeIndex;
    bool m_bMappable;
};

class VulkanMemoryManager
{
public:
    VulkanMemoryManager(VulkanDevice* device);
    ~VulkanMemoryManager();

    bool Allocate(const VkMemoryRequirements& memReqs, VkMemoryPropertyFlags props, VulkanAllocation& outAlloc);
    void Reset(); // 重置所有块，释放已分配空间（适用于线性分配）
    bool Free(VulkanAllocation& alloc); // 释放分配的内存
private:
    VulkanDevice* m_Device;
    std::vector<std::unique_ptr<VulkanMemoryBlock>> m_Blocks;

    uint32_t FindMemoryType(uint32_t typeBits, VkMemoryPropertyFlags properties) const;
};

class VulkanStagingBuffer : public RHI::RHIStagingBuffer
{
public:
    VulkanStagingBuffer(VulkanDevice* device, VulkanMemoryManager* memManager, VkDeviceSize size);
    ~VulkanStagingBuffer();

    VkBuffer GetHandle() const { return m_Buffer; }
    VkDeviceSize GetSize() const { return m_Size; }
    VulkanAllocation& GetAllocation() { return m_Allocation; }
    void* Map(uint32_t offset, uint32_t numBytes) override;
    void Unmap() override;

private:
    VulkanDevice* m_Device;
    VulkanMemoryManager* m_MemManager;
    VkBuffer m_Buffer = VK_NULL_HANDLE;
    VulkanAllocation m_Allocation;
    void* m_Mapped = nullptr;
    VkDeviceSize m_Size = 0;
};

class VulkanCommandBuffer;
class VulkanFence;
class VulkanStagingManager
{
public:
    VulkanStagingManager(VulkanDevice* device, VulkanMemoryManager* memManager);
    ~VulkanStagingManager();

    std::shared_ptr<VulkanStagingBuffer> Acquire(VkDeviceSize size);

    // CPU 用完，但 GPU 可能还在用
    void ReleaseToCmdBuffer(VulkanCommandBuffer* cmd,
        std::shared_ptr<VulkanStagingBuffer> buffer);

    void MarkMappedBuffersUsed(std::shared_ptr<VulkanStagingBuffer> buffer,bool used);


    // 每帧调用，检查 fence 并回收 staging
    void GarbageCollect();

private:


    struct PendingEntry
    {
        std::vector<std::shared_ptr<VulkanStagingBuffer>> PendingBuffers;
    };

    VulkanDevice* m_Device;
    VulkanMemoryManager* m_MemManager;

    std::vector<std::shared_ptr<VulkanStagingBuffer>> m_FreeBuffers;
    std::vector<std::shared_ptr<VulkanStagingBuffer>> m_MapedToFreeBuffers;
    std::unordered_map<std::shared_ptr<VulkanStagingBuffer>, bool> m_bMapedToFreeBufferUsed;
    // 记录某个 cmdBuffer 本次录制用了哪些 staging
    std::unordered_map<VulkanCommandBuffer*, PendingEntry> m_PendingBufferMap;

    std::mutex m_Mutex;
};

}