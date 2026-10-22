#pragma once
#include "RHIResource.h"

#include "VulkanMemory.h"
#include "VulkanDevice.h"
#include "VulkanFuncWrapper.h"
#include "VulkanTransientResource.h"
#include <vector>
#include <memory>
#include <deque>
using namespace RHI;

namespace RHIVulkan{
class VulkanCommandContext;

struct VulkanTextureView
{
    VulkanTextureView()
        : View(VK_NULL_HANDLE)
        , Image(VK_NULL_HANDLE)
        , ViewId(0)
    {
    }

    // 创建 ImageView
    bool Create(VulkanDevice* device,
        VkImage inImage,
        VkImageViewType viewType,
        VkImageAspectFlags aspectFlags,
        VkFormat format,
        uint32_t firstMip,
        uint32_t numMips,
        uint32_t arraySliceIndex,
        uint32_t numArraySlices,
        bool bUseIdentitySwizzle = false,
        VkImageUsageFlags imageUsageFlags = 0)
    {
        assert(inImage != VK_NULL_HANDLE);

        Image = inImage;

        // VkImageViewCreateInfo
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = Image;
        viewInfo.viewType = viewType;
        viewInfo.format = format;

        viewInfo.subresourceRange.aspectMask = aspectFlags;
        viewInfo.subresourceRange.baseMipLevel = firstMip;
        viewInfo.subresourceRange.levelCount = numMips;
        viewInfo.subresourceRange.baseArrayLayer = arraySliceIndex;
        viewInfo.subresourceRange.layerCount = numArraySlices;

        // UE 支持 Identity Swizzle
        if (bUseIdentitySwizzle)
        {
            viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
            viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
            viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
            viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        }
        else
        {
            viewInfo.components.r = VK_COMPONENT_SWIZZLE_R;
            viewInfo.components.g = VK_COMPONENT_SWIZZLE_G;
            viewInfo.components.b = VK_COMPONENT_SWIZZLE_B;
            viewInfo.components.a = VK_COMPONENT_SWIZZLE_A;
        }

        bool result = VKFunc::CreateImageView(device->GetHandle(), &viewInfo, &View);
        assert(result == true && "Failed to create VkImageView");

        // 简单 ID 用于 debug / hash
        static uint32_t nextViewId = 1;
        ViewId = nextViewId++;
        return result;
    }

    // 销毁 ImageView
    void Destroy(VulkanDevice* device)
    {
        if (View != VK_NULL_HANDLE)
        {
            device->EnqueueImageViewForDeletion(View);
            View = VK_NULL_HANDLE;
        }

        Image = VK_NULL_HANDLE;
        ViewId = 0;
    }

public:
    VkImageView View;
    VkImage Image;
    uint32_t ViewId;
};

struct VulkanBufferView
{
    VulkanBufferView()
        : View(VK_NULL_HANDLE)
        , Buffer(VK_NULL_HANDLE)
        , ViewId(0)
        , Format(VK_FORMAT_UNDEFINED)
        , Size(0)
        , Offset(0)
    {
    }

    // 创建 BufferView
    bool Create(VulkanDevice* device,
        VkBuffer inBuffer,
        VkFormat inFormat,
        VkDeviceSize offset,
        VkDeviceSize size)
    {
        assert(inBuffer != VK_NULL_HANDLE);

        Buffer = inBuffer;
        Format = inFormat;

        // VkBufferViewCreateInfo
        VkBufferViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO;
        viewInfo.buffer = Buffer;
        viewInfo.format = Format;
        viewInfo.offset = offset;
        viewInfo.range = size;
        this->Size = size;
        this->Offset = offset;

        bool result = VKFunc::CreateBufferView(device->GetHandle(), &viewInfo, &View);
        assert(result == true && "Failed to create VkBufferView");

        // 简单 ID 用于 debug / hash
        static uint32_t nextViewId = 1;
        ViewId = nextViewId++;
        return result == VK_SUCCESS;
    }

    // 销毁 BufferView
    void Destroy(VulkanDevice* device)
    {
        if (View != VK_NULL_HANDLE)
        {
            device->EnqueueBufferViewForDeletion(View);
            View = VK_NULL_HANDLE;
        }

        Buffer = VK_NULL_HANDLE;
        Format = VK_FORMAT_UNDEFINED;
        ViewId = 0;
    }

public:
    VkBufferView View;
    VkBuffer Buffer;
    uint32_t ViewId;
    VkFormat Format;
    VkDeviceSize Size;
    VkDeviceSize Offset;
};

class VulkanViewBase{
public:
    virtual void Invalidate() = 0;
    virtual ~VulkanViewBase() = default;
protected:
    bool _bIsValid = true;
};

class VulkanTransientAllocation;

// Vulkan Texture
class VulkanTexture : public RHITexture {
public:
    VulkanTexture(VulkanDevice* device, const RHITextureDesc& desc, bool externalAllocated = false);
    VulkanTexture(VulkanDevice* device, const RHITextureDesc& desc, VkImage image);
    ~VulkanTexture() override;

    VkImage GetImage() const { return m_Image; }
    VkImageView GetImageView() const { return m_DefaltView.View; }
    VkFormat GetFormat() const { return m_Format; }
	VkImageAspectFlags GetAspectFlags() const { return m_ImageAspectFlags; }
	VulkanAllocation& GetAllocation() { return m_Allocation; }

    void SetTransientAllocation(VulkanTransientAllocation transientAllocation) {
        m_TransientAllocation = transientAllocation;
    }
    VulkanTransientAllocation GetTransientAllocation() const { return m_TransientAllocation; }
    void CreateDefaultView();

    // View management
    void AttachView(VulkanViewBase* view);
    void DetachView(VulkanViewBase* view);
private:
    VulkanTransientAllocation m_TransientAllocation;
    void DetermineDefaultLayout(ERHITextureCreateFlags flags, VkImageLayout &layout,ERHIResourceAccess &access);

    VkImage m_Image = VK_NULL_HANDLE;
    VulkanTextureView m_DefaltView;
    VkFormat m_Format = VK_FORMAT_UNDEFINED;
    VulkanDevice* m_Device = nullptr;
    VulkanAllocation m_Allocation;
    bool m_bOwner = true;
	VkImageAspectFlags m_ImageAspectFlags = 0;
    bool m_bExternalAllocated = false;
    std::vector<VulkanViewBase*> m_Views;
};
using VulkanTextureSP = std::shared_ptr<VulkanTexture>;

// Vulkan Buffer
class VulkanBuffer : public RHIBuffer {
public:
    VulkanBuffer(VulkanDevice* device, const RHIBufferDesc& desc,bool externalAllocated = false);
    ~VulkanBuffer() override;

    const VkBuffer& GetHandle() const { return m_Buffer; }
    VkDeviceSize GetSize() const { return m_Size; }

    void SetTransientAllocation(VulkanTransientAllocation transientAllocation) {
        m_TransientAllocation = transientAllocation;
    }
    VulkanTransientAllocation GetTransientAllocation() const { return m_TransientAllocation; }

    // View management
    void AttachView(VulkanViewBase* view);
    void DetachView(VulkanViewBase* view);

private:
    VkBuffer m_Buffer = VK_NULL_HANDLE;
    VkDeviceSize m_Size = 0;
    VulkanDevice* m_Device = nullptr;
    VulkanAllocation m_Allocation;
    bool m_bExternalAllocated = false;
    VulkanTransientAllocation m_TransientAllocation;

    std::vector<VulkanViewBase*> m_Views;
};

class VulkanRayTracingGeometry : public RHIRayTracingGeometry
{
public:
    explicit VulkanRayTracingGeometry(VulkanDevice* device, const RHIRayTracingGeometryDesc& desc);
    ~VulkanRayTracingGeometry() override;

    void SetGeometryDesc(const RHIRayTracingGeometryDesc& desc) override;

    VkAccelerationStructureKHR GetHandle() const { return m_AccelerationStructure; }
    VkDeviceAddress GetDeviceAddress() const { return m_DeviceAddress; }
    const VkAccelerationStructureGeometryKHR& GetGeometryInfo() const { return m_GeometryInfo; }
    const VkAccelerationStructureBuildRangeInfoKHR& GetBuildRangeInfo() const { return m_BuildRangeInfo; }
    const VkBuildAccelerationStructureFlagsKHR& GetBuildFlags() const { return m_BuildFlags; }
private:
    void DestroyAccelerationStructure();
    void DestroyStorageBuffer();

private:
    VulkanDevice* m_Device = nullptr;
    VkBuffer m_StorageBuffer = VK_NULL_HANDLE;
    VulkanAllocation m_StorageAllocation;
    VkAccelerationStructureKHR m_AccelerationStructure = VK_NULL_HANDLE;
    VkDeviceAddress m_DeviceAddress = 0;
    VkAccelerationStructureGeometryKHR m_GeometryInfo{};
    VkAccelerationStructureBuildRangeInfoKHR m_BuildRangeInfo{};
    VkBuildAccelerationStructureFlagsKHR m_BuildFlags;
};

class VulkanRayTracingInstance : public RHIRayTracingInstance
{
public:
    explicit VulkanRayTracingInstance(VulkanDevice* device, const RHIRayTracingInstancesDesc& instances);
    ~VulkanRayTracingInstance() override;

    void SetInstancesDesc(const RHIRayTracingInstancesDesc& instances) override;

    VkAccelerationStructureKHR GetHandle() const { return m_AccelerationStructure; }
    VkDeviceAddress GetDeviceAddress() const { return m_DeviceAddress; }
    const VkAccelerationStructureGeometryKHR& GetGeometryInfo() const { return m_GeometryInfo; }
    const VkAccelerationStructureBuildRangeInfoKHR& GetBuildRangeInfo() const { return m_BuildRangeInfo; }

private:
    void DestroyAccelerationStructure();
    void DestroyStorageBuffer();
    void DestroyInstanceBuffer();

private:
    VulkanDevice* m_Device = nullptr;
    VkBuffer m_StorageBuffer = VK_NULL_HANDLE;
    VulkanAllocation m_StorageAllocation;
    VkBuffer m_InstanceBuffer = VK_NULL_HANDLE;
    VulkanAllocation m_InstanceAllocation;
    VkAccelerationStructureKHR m_AccelerationStructure = VK_NULL_HANDLE;
    VkDeviceAddress m_DeviceAddress = 0;
    VkAccelerationStructureGeometryKHR m_GeometryInfo{};
    VkAccelerationStructureBuildRangeInfoKHR m_BuildRangeInfo{};
};

// --------------------------------------------------
// Vulkan Shader Resource View
// --------------------------------------------------
class VulkanShaderResourceView : public RHIShaderResourceView, public VulkanViewBase
{
public:
    // Texture SRV 构造
    VulkanShaderResourceView(VulkanDevice* device, RHIViewableResource* resource, const RHITexSRVCreateInfo& srvInfo);
    // Buffer SRV 构造
    VulkanShaderResourceView(VulkanDevice* device, RHIViewableResource* resource, const RHIBufferSRVCreateInfo& srvInfo);

    ~VulkanShaderResourceView() override;

    // Vulkan 句柄访问
    const VulkanTextureView& GetTextureView() const;
    const VulkanBufferView& GetBufferView() const;

    // 元信息查询
    bool IsTexture() const { return m_ResourceType == EResourceType::Texture; }
    bool IsBuffer() const { return m_ResourceType == EResourceType::Buffer; }
    VkFormat GetFormat() const { return m_Format; }
    uint32_t GetBaseMipLevel() const { return m_BaseMipLevel; }
    uint32_t GetMipLevelCount() const { return m_MipLevelCount; }
    uint32_t GetBaseArrayLayer() const { return m_BaseArrayLayer; }
    uint32_t GetLayerCount() const { return m_LayerCount; }
    VkDescriptorType GetDescriptorType() const { return m_DescriptorType; }
    const std::string& GetDebugName() const { return m_DebugName; }

    // VulkanViewBase override
    void Invalidate() override;

private:
    void CreateTextureView(const RHITexSRVCreateInfo& srvInfo);
    void CreateBufferView(const RHIBufferSRVCreateInfo& srvInfo);
    void DestroyView();

private:
    VulkanDevice* m_Device = nullptr;
    RHIViewableResource* m_ResourcePtr = nullptr;

    // 存储 view 对象（而非原生句柄）
    VulkanTextureView m_TextureView;
    VulkanBufferView m_BufferView;

    // 类型信息
    enum class EResourceType { Texture, Buffer } m_ResourceType;
    VkFormat m_Format = VK_FORMAT_UNDEFINED;
    uint32_t m_BaseMipLevel = 0;
    uint32_t m_MipLevelCount = 1;
    uint32_t m_BaseArrayLayer = 0;
    uint32_t m_LayerCount = 1;

    // Descriptor 信息
    VkDescriptorType m_DescriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;

    // 可选调试名称
    std::string m_DebugName;
};



// --------------------------------------------------
// Vulkan Unordered Access View
// --------------------------------------------------
class VulkanUnorderedAccessView : public RHIUnorderedAccessView, public VulkanViewBase
{
public:
    VulkanUnorderedAccessView(VulkanDevice* device, RHIViewableResource* resource, const RHITexUAVCreateInfo& uavInfo);
    VulkanUnorderedAccessView(VulkanDevice* device, RHIViewableResource* resource, const RHIBufferUAVCreateInfo& uavInfo);

    ~VulkanUnorderedAccessView() override;

    // Vulkan 句柄访问
    const VulkanTextureView& GetTextureView() const;
    const VulkanBufferView& GetBufferView() const;

    bool IsTexture() const { return m_ResourceType == EResourceType::Texture; }
    bool IsBuffer() const { return m_ResourceType == EResourceType::Buffer; }
    VkFormat GetFormat() const { return m_Format; }
    uint32_t GetBaseMipLevel() const { return m_BaseMipLevel; }
    uint32_t GetMipLevelCount() const { return m_MipLevelCount; }
    uint32_t GetBaseArrayLayer() const { return m_BaseArrayLayer; }
    uint32_t GetLayerCount() const { return m_LayerCount; }
    VkDescriptorType GetDescriptorType() const { return m_DescriptorType; }
    const std::string& GetDebugName() const { return m_DebugName; }

    // VulkanViewBase override
    void Invalidate() override;

private:
    void CreateTextureView(const RHITexUAVCreateInfo& uavInfo);
    void CreateBufferView(const RHIBufferUAVCreateInfo& uavInfo);
    void DestroyView();

private:
    VulkanDevice* m_Device = nullptr;
    RHIViewableResource* m_ResourcePtr = nullptr;

    // 存储 view 对象（而非原生句柄）
    VulkanTextureView m_TextureView;
    VulkanBufferView m_BufferView;

    enum class EResourceType { Texture, Buffer } m_ResourceType;
    VkFormat m_Format = VK_FORMAT_UNDEFINED;
    uint32_t m_BaseMipLevel = 0;
    uint32_t m_MipLevelCount = 1;
    uint32_t m_BaseArrayLayer = 0;
    uint32_t m_LayerCount = 1;

    VkDescriptorType m_DescriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;

    std::string m_DebugName;
};


// Vulkan Ring Buffer - 用于动态分配小块内存的环形缓冲区
class VulkanRingBuffer
{
public:
    VulkanRingBuffer(VulkanDevice* device, uint64_t totalSize, VkBufferUsageFlags usage, VkMemoryPropertyFlags memPropertyFlags);
    ~VulkanRingBuffer();

    // 分配内存空间
    uint64_t AllocateMemory(uint64_t size, uint32_t alignment);

    // 获取缓冲区信息
    uint32_t GetBufferOffset() const { return m_Allocation.GetOffset(); }
    VkDeviceAddress GetBufferAddress() const { return m_BufferAddress; }
    VkBuffer GetHandle() const { return m_Allocation.GetBufferHandle(); }
    void* GetMappedPointer() { return m_Allocation.GetMappedPointer(); }
    VulkanAllocation& GetAllocation() { return m_Allocation; }
    const VulkanAllocation& GetAllocation() const { return m_Allocation; }

private:
    uint64_t m_BufferSize;
    uint64_t m_BufferOffset;
    VkDeviceAddress m_BufferAddress;
    uint32_t m_MinAlignment;
    VulkanAllocation m_Allocation;
    VulkanDevice* m_Device;


    uint64_t WrapAroundAllocateMemory(uint64_t size, uint32_t alignment);
};

// Vulkan Loose Uniform Buffer Uploader - 用于上传uniform buffer数据的工具
class VulkanLooseUniformDataUploader
{
public:
    VulkanLooseUniformDataUploader(VulkanDevice* device);
    ~VulkanLooseUniformDataUploader();

    // 获取CPU映射指针
    uint8_t* GetCPUMappedPointer() { return static_cast<uint8_t*>(m_CPUBuffer->GetMappedPointer()); }

    // 分配内存
    uint64_t AllocateMemory(uint64_t size, uint32_t alignment)
    {
        return m_CPUBuffer->AllocateMemory(size, alignment);
    }

    // 获取缓冲区信息
    const VulkanAllocation& GetCPUBufferAllocation() const { return m_CPUBuffer->GetAllocation(); }
    VkBuffer GetCPUBufferHandle() const { return m_CPUBuffer->GetHandle(); }
    uint32_t GetCPUBufferOffset() const { return m_CPUBuffer->GetBufferOffset(); }
    VkDeviceAddress GetCPUBufferAddress() const { return m_CPUBuffer->GetBufferAddress(); }

private:
    VulkanRingBuffer* m_CPUBuffer;
    friend class VulkanCommandContext; // 允许命令上下文访问
};



// Vulkan Sampler
class VulkanSampler : public RHISampler {
public:
    VulkanSampler(VulkanDevice* device, const RHISamplerDesc& desc);
    ~VulkanSampler();

    VkSampler GetSampler() const { return m_Sampler; }

private:
    VkSampler m_Sampler = VK_NULL_HANDLE;
    VulkanDevice* m_Device = nullptr;
};

class VulkanSwapchain;
class VulkanSemaphore;
class VulkanRHISyncPoint;
class VulkanCommandContext;
class VulkanCommandBuffer;
class VulkanQueue;
// Vulkan RHISwapchain implementation
class VulkanRHISwapchain : public RHISwapchain {
public:
    VulkanRHISwapchain(VulkanDevice* device, uint32_t width, uint32_t height, void* windowHandle, ERHIFormat format);
    ~VulkanRHISwapchain() override;

    VulkanSwapchain* GetSwapchain() const { return m_Swapchain; }
    RHISwapchainSlot AcquireNextSlot() override;
    void Resize(uint32_t width, uint32_t height) override;
    void Present(VulkanQueue* presentQueue, const RHI::RHIWaitInfo& waitInfo);
    VulkanTextureSP GetBackTexture();

private:
    void CreateSwapchain();
    void DestroySwapchain();

    VulkanSwapchain* m_Swapchain = nullptr;
    VulkanDevice* m_Device = nullptr;
    void* m_WindowHandle;
    uint32_t m_Width;
    uint32_t m_Height;
    ERHIFormat m_Format;

    std::vector<VulkanTextureSP> m_BackBufferTextures; // 所有 backbuffers
    std::vector<VulkanRHISyncPoint*> m_AcquireSemaphores;
    std::queue<VulkanSemaphore*> m_PresentSemaphores;
    int m_CurrentBackBufferIndex = -1;  // 渲染用
    int m_CurrentIndex = 0; // Present用
    std::vector<VkImage> m_SwapchainImages;
};


class VulkanVertexDescState : public RHIVertexDescState
{
public:
    VulkanVertexDescState(VulkanDevice* device, const RHIVertexDescStateDesc& desc);
    virtual ~VulkanVertexDescState();

    VkPipelineVertexInputStateCreateInfo VertexInputInfo{};
    std::vector<VkVertexInputBindingDescription> BindingDescriptions;
    std::vector<VkVertexInputAttributeDescription> AttributeDescriptions;

private:
    VulkanDevice* m_Device = nullptr;
};



// ----------------------------
// 光栅化状态 Vulkan派生
// ----------------------------
class VulkanRasterizerState : public RHIRasterizerState
{
public:
    VulkanRasterizerState(VulkanDevice* device, const RHIRasterizerStateDesc& desc);
    virtual ~VulkanRasterizerState();

    VkPipelineRasterizationStateCreateInfo RasterizerInfo{};
    void Initialize();

private:
    VulkanDevice* m_Device = nullptr;
};

// ----------------------------
// 颜色混合状态 Vulkan派生
// ----------------------------
class VulkanColorBlendState : public RHIColorBlendState
{
public:
    VulkanColorBlendState(VulkanDevice* device, const RHIColorBlendStateDesc& desc);
    virtual ~VulkanColorBlendState();

    VkPipelineColorBlendStateCreateInfo ColorBlendInfo{};
    std::vector<VkPipelineColorBlendAttachmentState> AttachmentStates;

    void Initialize();

private:
    VulkanDevice* m_Device = nullptr;
};

// ----------------------------
// 深度模板状态 Vulkan派生
// ----------------------------
class VulkanDepthStencilState : public RHIDepthStencilState
{
public:
    VulkanDepthStencilState(VulkanDevice* device, const RHIDepthStencilStateDesc& desc);
    virtual ~VulkanDepthStencilState();

    VkPipelineDepthStencilStateCreateInfo DepthStencilInfo{};
    void Initialize();

private:
    VulkanDevice* m_Device = nullptr;
};

}