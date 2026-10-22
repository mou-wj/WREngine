#include "VulkanTransientResource.h"
#include "VulkanResource.h"
#include "VulkanDevice.h"
#include "VulkanQueue.h"
#include "VulkanRHIUtils.h"
#include "VulkanFuncWrapper.h"
#include <stdexcept>

namespace RHIVulkan
{
    namespace
    {
        constexpr uint64_t AddressClass = 1ull << 32;
        constexpr uint64_t ImageClass = 1ull << 33;
        void WaitForGPU(VulkanDevice* device)
        {
            for (auto* queue : {device->GetGraphicsQueue(), device->GetComputeQueue(), device->GetTransferQueue()})
                if (queue)
                    queue->WaitIdle();
        }
        template <class Resource> struct DeleteTransientResource
        {
            VulkanDevice* Device;
            void operator()(Resource* resource) const
            {
                // Existing resource destructors enqueue native deletion. Keep the
                // allocation alive until those handles are actually destroyed.
                auto allocation = resource->GetTransientAllocation().Owner;
                WaitForGPU(Device);
                delete resource;
                Device->ReleaseDeferredResources(0);
            }
        };
        VulkanTransientAllocation NativeAllocation(const RHI::RHITransientAllocationSP& allocation)
        {
            return {static_cast<VulkanTransientHeap*>(allocation->Heap.get()), allocation->Offset, allocation->Size,
                    allocation};
        }
    } // namespace
    VulkanTransientHeap::VulkanTransientHeap(VulkanDevice* device, uint64_t size, uint64_t memoryClass)
        : RHITransientHeap(size, memoryClass), Device(device->GetHandle())
    {
        VkMemoryAllocateInfo info{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        info.allocationSize = size;
        info.memoryTypeIndex = static_cast<uint32_t>(memoryClass);
        VkMemoryAllocateFlagsInfo flags{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};
        if (memoryClass & AddressClass)
        {
            flags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
            info.pNext = &flags;
        }
        if (!VKFunc::AllocateMemory(Device, &info, &Memory))
            throw std::runtime_error("Failed to allocate Vulkan transient heap");
    }
    VulkanTransientHeap::~VulkanTransientHeap()
    {
        if (Memory)
            VKFunc::FreeMemory(Device, Memory);
    }
    VulkanTransientResourceManager::VulkanTransientResourceManager(VulkanDevice* device) : m_Device(device)
    {
    }
    VulkanTransientResourceManager::~VulkanTransientResourceManager()
    {
        WaitForGPU(m_Device);
        ClearResources();
    }
    std::shared_ptr<RHI::RHITransientHeap> VulkanTransientResourceManager::CreateHeap(uint64_t size,
                                                                                      uint64_t memoryClass)
    {
        return std::make_shared<VulkanTransientHeap>(m_Device, size, memoryClass);
    }
    RHI::RHITextureSP VulkanTransientResourceManager::CreateTextureInternal(const RHI::RHITextureDesc& desc)
    {
        std::shared_ptr<VulkanTexture> texture(new VulkanTexture(m_Device, desc, true),
                                               DeleteTransientResource<VulkanTexture>{m_Device});
        if (!texture->GetImage())
            throw std::runtime_error("Failed to create transient Vulkan image");
        VkMemoryRequirements requirements{};
        VKFunc::GetImageMemoryRequirements(m_Device->GetHandle(), texture->GetImage(), &requirements);
        const uint64_t memoryClass =
            ImageClass | m_Device->FindMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        const auto allocation =
            NativeAllocation(AllocateMemory(requirements.size, requirements.alignment, memoryClass));
        texture->SetTransientAllocation(allocation);
        if (!VKFunc::BindImageMemory(m_Device->GetHandle(), texture->GetImage(), allocation.Heap->Memory,
                                     allocation.Offset))
            throw std::runtime_error("Failed to bind transient Vulkan image");
        texture->CreateDefaultView();
        if (!texture->GetImageView())
            throw std::runtime_error("Failed to create transient Vulkan image view");
        return texture;
    }
    RHI::RHIBufferSP VulkanTransientResourceManager::CreateBufferInternal(const RHI::RHIBufferDesc& desc)
    {
        std::shared_ptr<VulkanBuffer> buffer(new VulkanBuffer(m_Device, desc, true),
                                             DeleteTransientResource<VulkanBuffer>{m_Device});
        if (!buffer->GetHandle())
            throw std::runtime_error("Failed to create transient Vulkan buffer");
        VkMemoryRequirements requirements{};
        VKFunc::GetBufferMemoryRequirements(m_Device->GetHandle(), buffer->GetHandle(), &requirements);
        const bool host = desc.bCPUAccessible || EnumHasAnyFlags(desc.Usage, RHI::ERHIBufferUsageFlag::Staging);
        const auto properties = host ? VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
                                     : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        uint64_t memoryClass = m_Device->FindMemoryType(requirements.memoryTypeBits, properties);
        if (TransformBufferUsageFlagsFrom(desc.Usage) & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
            memoryClass |= AddressClass;
        const auto allocation =
            NativeAllocation(AllocateMemory(requirements.size, requirements.alignment, memoryClass));
        buffer->SetTransientAllocation(allocation);
        if (!VKFunc::BindBufferMemory(m_Device->GetHandle(), buffer->GetHandle(), allocation.Heap->Memory,
                                      allocation.Offset))
            throw std::runtime_error("Failed to bind transient Vulkan buffer");
        return buffer;
    }
    bool VulkanTransientResourceManager::CanRecycle(RHI::RHIViewableResource&)
    {
        WaitForGPU(m_Device);
        return true;
    }
} // namespace RHIVulkan
