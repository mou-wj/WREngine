#pragma once
#include "RHITransientResource.h"
#include <vulkan/vulkan.h>

namespace RHIVulkan
{
    class VulkanDevice;
    struct VulkanTransientHeap : RHI::RHITransientHeap
    {
        VulkanTransientHeap(VulkanDevice* device, uint64_t size, uint64_t memoryClass);
        ~VulkanTransientHeap() override;
        VkDevice Device;
        VkDeviceMemory Memory = VK_NULL_HANDLE;
    };
    // Preserve the type consumed by VulkanResource without changing that module.
    struct VulkanTransientAllocation
    {
        VulkanTransientHeap* Heap = nullptr;
        uint64_t Offset = 0, Size = 0;
        RHI::RHITransientAllocationSP Owner;
    };
    class RHIVULKAN_API VulkanTransientResourceManager final : public RHI::RHITransientResourceManager
    {
    public:
        explicit VulkanTransientResourceManager(VulkanDevice* device);
        ~VulkanTransientResourceManager() override;

    protected:
        RHI::RHITextureSP CreateTextureInternal(const RHI::RHITextureDesc& desc) override;
        RHI::RHIBufferSP CreateBufferInternal(const RHI::RHIBufferDesc& desc) override;
        std::shared_ptr<RHI::RHITransientHeap> CreateHeap(uint64_t size, uint64_t memoryClass) override;
        bool CanRecycle(RHI::RHIViewableResource& resource) override;

    private:
        VulkanDevice* Device;
    };
} // namespace RHIVulkan
