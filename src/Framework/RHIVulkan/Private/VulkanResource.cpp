#include "VulkanResource.h"
#include "VulkanDevice.h"
#include "VulkanSwapchain.h"
#include "VulkanPlatformSurport.h"
#include "VulkanRHIUtils.h"
#include "VulkanFuncWrapper.h"
#include "Log.h"
#include "VulkanSync.h"
#include "VulkanCommandContex.h"
#include "ThreadInfo.h"
#include "VulkanQueue.h"
#include <algorithm> // for std::max
#include <cstring>
using namespace Core;
namespace RHIVulkan{

static VulkanQueue* ResolveInitialQueue(VulkanDevice* device, EQueueType queueType)
{
    if (!device)
    {
        return nullptr;
    }

    switch (queueType)
    {
    case EQueueType::Graphics:
        return device->GetGraphicsQueue();
    case EQueueType::Compute:
        return device->GetComputeQueue() ? device->GetComputeQueue() : device->GetGraphicsQueue();
    default:
        return device->GetGraphicsQueue();
    }
}

   
// Vulkan Texture
VulkanTexture::VulkanTexture(VulkanDevice* device, const RHITextureDesc& desc, bool externalAllocated)
    : RHITexture(desc), m_Device(device),m_bExternalAllocated(externalAllocated) {
    VkDevice vkDevice = m_Device->GetHandle();
    
    m_Format = TransformFormatFrom(desc.Format);
	m_ImageAspectFlags = GetImageAspectFlags(m_Format);
    // Create VkImage
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = TransformImageTypeFrom(desc.Type); // TODO: Support other image types
    imageInfo.format = m_Format; // TODO: Support other formats
    imageInfo.extent.width = desc.Width;
    imageInfo.extent.height = desc.Height;
    imageInfo.extent.depth = desc.Depth;
    imageInfo.mipLevels = desc.MipLevels;
    imageInfo.arrayLayers = desc.ArraySize;
    imageInfo.samples = TransformSampleCountFrom(desc.SampleCount);
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = TransformTextureUsageFlagsFrom(desc.Usage); // TODO: Support other usages
    if (desc.Type == ERHITextureType::TextureCube) {
        imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    }
	std::vector<uint32_t> queueFamilyIndices;
    VulkanQueue* initialQueue = ResolveInitialQueue(m_Device, desc.InitialQueueType);
    queueFamilyIndices.push_back(initialQueue ? initialQueue->GetFamilyIndex() : m_Device->GetGraphicsQueue()->GetFamilyIndex());
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageInfo.queueFamilyIndexCount = static_cast<uint32_t>(queueFamilyIndices.size());
	imageInfo.pQueueFamilyIndices = queueFamilyIndices.data();
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VKFunc::CreateImage(vkDevice, &imageInfo, &m_Image);

    // Get memory requirements
    VkMemoryRequirements memRequirements;
    VKFunc::GetImageMemoryRequirements(vkDevice, m_Image, &memRequirements);
    VulkanMemoryManager* memoryManager = m_Device->GetMemoryManager();
    if (!externalAllocated)
    {
        // Allocate memory
        if (!memoryManager->Allocate(memRequirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_Allocation)) {
            VKFunc::DestroyImage(vkDevice, m_Image);
        }

        // Bind memory to image
        VKFunc::BindImageMemory(vkDevice, m_Image, m_Allocation.GetMemory(), m_Allocation.GetOffset());
        CreateDefaultView();
    }




#ifdef DEBUG_INFO
    std::string debugName;
    if (_desc.DebugName) {
        debugName = _desc.DebugName;
    }
    else {
        char buf[64];
        snprintf(buf, sizeof(buf), "VulkanTexture:0x%llx", (unsigned long long)m_Image);
        debugName = buf;
    }
    VKFunc::SetDebugName(vkDevice, VK_OBJECT_TYPE_IMAGE, (uint64_t)m_Image, debugName.c_str());
#endif
}

VulkanTexture::VulkanTexture(VulkanDevice* device, const RHITextureDesc& desc, VkImage image)
    : RHITexture(desc), m_Device(device), m_Image(image)
{
    m_bOwner = false;
    VkDevice vkDevice = m_Device->GetHandle();
    m_Format = TransformFormatFrom(desc.Format);
    m_ImageAspectFlags = GetImageAspectFlags(m_Format);
    auto usage = TransformTextureUsageFlagsFrom(desc.Usage);
    // Create VkImageView
    m_DefaltView.Create(device,                                // fvulkan_device& device
        m_Image,                                 // VkImage
        TransformViewTypeFrom(desc.Type),      // VkImageViewType
        GetImageAspectFlags(TransformFormatFrom(desc.Format)), // VkImageAspectFlags
        m_Format,       // VkFormat
        0,                                      // first mip level
        desc.MipLevels,                         // number of mips
        0,                                      // base array layer
        desc.ArraySize,                          // number of array layers
        true,                                    // use identity swizzle)
        usage
    );
#ifdef DEBUG_INFO
    std::string debugName;
    if (_desc.DebugName) {
        debugName = _desc.DebugName;
    }
    else {
        char buf[64];
        snprintf(buf, sizeof(buf), "VulkanTexture:0x%llx", (unsigned long long)m_Image);
        debugName = buf;
    }
    VKFunc::SetDebugName(vkDevice, VK_OBJECT_TYPE_IMAGE, (uint64_t)m_Image, debugName.c_str());
#endif
}

VulkanTexture::~VulkanTexture() {
    VkDevice device = m_Device->GetHandle();
    VulkanMemoryManager* memoryManager = m_Device->GetMemoryManager();

	m_DefaltView.Destroy(m_Device);

    if (m_Image != VK_NULL_HANDLE && m_bOwner) {
        m_Device->EnqueueImageForDeletion(m_Image);
    }
    for (auto view : m_Views) {
        view->Invalidate();
    }
    if (!m_bExternalAllocated)
    {
        memoryManager->Free(m_Allocation);
    }
}

void VulkanTexture::CreateDefaultView()
{
    auto usage = TransformTextureUsageFlagsFrom(_desc.Usage);
    // Create VkImageView
    bool viewSuccess = m_DefaltView.Create(
        m_Device,                                // fvulkan_device& device
        m_Image,                                 // VkImage
        TransformViewTypeFrom(_desc.Type),      // VkImageViewType
        GetImageAspectFlags(TransformFormatFrom(_desc.Format)), // VkImageAspectFlags
        TransformFormatFrom(_desc.Format),       // VkFormat
        0,                                      // first mip level
        _desc.MipLevels,                         // number of mips
        0,                                      // base array layer
        _desc.ArraySize,                          // number of array layers
        true,                                   // use identity swizzle
        usage
    );
    VulkanMemoryManager* memoryManager = m_Device->GetMemoryManager();
    if (!viewSuccess) {
        m_Image = VK_NULL_HANDLE;
        VKFunc::DestroyImage(m_Device->GetHandle(), m_Image);
        if (!m_bExternalAllocated)
        {
            memoryManager->Free(m_Allocation);
        }
    }
}

void VulkanTexture::AttachView(VulkanViewBase* view)
{
    m_Views.push_back(view);
}

void VulkanTexture::DetachView(VulkanViewBase* view)
{
    auto it = std::find(m_Views.begin(), m_Views.end(), view);
    if (it != m_Views.end())
    {
        m_Views.erase(it);
    }
}

void VulkanTexture::DetermineDefaultLayout(ERHITextureCreateFlags flags, VkImageLayout& outLayout, ERHIResourceAccess& outAccess)
{
    // --- 0. 初始默认值 ---
    outLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    outAccess = ERHIResourceAccess::Unknown;

    // --- 1. 优先级最高：呈现 (Swapchain) ---
    // Swapchain 图像在 Vulkan 中非常特殊，必须处于 PRESENT 布局才能交给窗口系统
    if (EnumHasAnyFlags(flags, ERHITextureCreateFlag::Presentable))
    {
        outLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        outAccess = ERHIResourceAccess::Present;
        return;
    }

    // --- 2. 手机端优化：Memoryless (Tile Memory Only) ---
    // Memoryless 通常只用于 Transient 的 Depth 或 Color Attachment
    // 它们不需要从显存 Load，也不需要 Store 到显存，因此初始状态必须是 Attachment
    if (EnumHasAnyFlags(flags, ERHITextureCreateFlag::Memoryless))
    {
        if (EnumHasAnyFlags(flags, ERHITextureCreateFlag::DepthStencil))
        {
            outLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            outAccess = ERHIResourceAccess::DSVWrite;
        }
        else
        {
            outLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            outAccess = ERHIResourceAccess::RenderTargetView;
        }
        return;
    }

    // --- 3. 读写冲突处理：UAV (Storage Image) ---
    // 如果一个资源被标记为 UAV，无论它是否也是 ShaderResource，
    // 在初始状态下为了通用性，通常映射到 GENERAL 布局
    if (EnumHasAnyFlags(flags, ERHITextureCreateFlag::UAV))
    {
        outLayout = VK_IMAGE_LAYOUT_GENERAL;
        outAccess = ERHIResourceAccess::UAV;
        return;
    }

    // --- 4. CPU 回读 (Readback) ---
    // CPU Readback 资源通常由底层 Buffer 支撑或者是线性布局的 Image
    // 在 Vulkan 中，这类需要映射内存读取的资源建议使用 GENERAL
    if (EnumHasAnyFlags(flags, ERHITextureCreateFlag::CPUReadback))
    {
        outLayout = VK_IMAGE_LAYOUT_GENERAL;
        outAccess = ERHIResourceAccess::CPURead;
        return;
    }

    // --- 5. 标准附件路径 (RenderTarget / DepthStencil) ---
    if (EnumHasAnyFlags(flags, ERHITextureCreateFlag::DepthStencil))
    {
        outLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        outAccess = ERHIResourceAccess::DSVWrite;
        return;
    }

    if (EnumHasAnyFlags(flags, ERHITextureCreateFlag::RenderTarget))
    {
        outLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        outAccess = ERHIResourceAccess::RenderTargetView;
        return;
    }

    // --- 6. 拷贝路径 ---
    // 如果资源的主要意图是作为拷贝目标（例如上传贴图数据）
    if (EnumHasAnyFlags(flags, ERHITextureCreateFlag::TransferDest))
    {
        outLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        outAccess = ERHIResourceAccess::TransferDest;
        return;
    }

    // --- 7. 只读贴图路径 (SRV) ---
    if (EnumHasAnyFlags(flags, ERHITextureCreateFlag::ShaderResource))
    {
        // 如果是 Dynamic 的，可能意味着它会频繁更新
        // 但初始状态下依然建议进入只读，等待第一次 RHIUpdate 触发转换
        outLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        outAccess = ERHIResourceAccess::SRV;
        return;
    }

    // --- 8. 拷贝源 ---
    if (EnumHasAnyFlags(flags, ERHITextureCreateFlag::TransferSrc))
    {
        outLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        outAccess = ERHIResourceAccess::TransferSrc;
        return;
    }

    // --- 9. 兜底：Undefined ---
    // 没有任何明确用途标志时，保持 Undefined
    outLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    outAccess = ERHIResourceAccess::Unknown;
}

// Vulkan Buffer
VulkanBuffer::VulkanBuffer(VulkanDevice* device, const RHIBufferDesc& desc, bool externalAllocated)
    : RHIBuffer(desc), m_Device(device),m_bExternalAllocated(externalAllocated) {
    VkDevice vkDevice = m_Device->GetHandle();


    // Create VkBuffer
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    m_Size = _desc.Size;
    bufferInfo.size = _desc.Size;
	auto usageFlags = TransformBufferUsageFlagsFrom(_desc.Usage);
    bufferInfo.usage = usageFlags; // TODO: Support other usages
    std::vector<uint32_t> queueFamilyIndices;
    VulkanQueue* initialQueue = ResolveInitialQueue(m_Device, desc.InitialQueueType);
    queueFamilyIndices.push_back(initialQueue ? initialQueue->GetFamilyIndex() : m_Device->GetGraphicsQueue()->GetFamilyIndex());
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    bufferInfo.queueFamilyIndexCount = static_cast<uint32_t>(queueFamilyIndices.size());
    bufferInfo.pQueueFamilyIndices = queueFamilyIndices.data();

    if (VKFunc::CreateBuffer(vkDevice, &bufferInfo, &m_Buffer) != true) {
    }

    VulkanMemoryManager* memoryManager = m_Device->GetMemoryManager();
    // Get memory requirements
    if (!externalAllocated) {
        VkMemoryRequirements memRequirements;
        VKFunc::GetBufferMemoryRequirements(vkDevice, m_Buffer, &memRequirements);
        // Allocate memory
        if (!memoryManager->Allocate(memRequirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_Allocation)) {
            VKFunc::DestroyBuffer(vkDevice, m_Buffer);
        }

        // Bind memory to buffer
        VKFunc::BindBufferMemory(vkDevice, m_Buffer, m_Allocation.GetMemory(), m_Allocation.GetOffset());

    }

#ifdef DEBUG_INFO
    std::string debugName;
    if (_desc.DebugName) {
        debugName = _desc.DebugName;
    } else {
        char buf[64];
        snprintf(buf, sizeof(buf), "VulkanBuffer:0x%llx", (unsigned long long)m_Buffer);
        debugName = buf;
    }
    VKFunc::SetDebugName(vkDevice, VK_OBJECT_TYPE_BUFFER, (uint64_t)m_Buffer, debugName.c_str());
#endif
}

VulkanBuffer::~VulkanBuffer() {
    VkDevice device = m_Device->GetHandle();
    VulkanMemoryManager* memoryManager = m_Device->GetMemoryManager();

    if (m_Buffer != VK_NULL_HANDLE) {
        m_Device->EnqueueBufferForDeletion(m_Buffer);
    }
    for (auto view : m_Views) {
        view->Invalidate();
    }
    if (!m_bExternalAllocated) {
        memoryManager->Free(m_Allocation);
    }

}

void VulkanBuffer::AttachView(VulkanViewBase* view)
{
    m_Views.push_back(view);
}

void VulkanBuffer::DetachView(VulkanViewBase* view)
{
    auto it = std::find(m_Views.begin(), m_Views.end(), view);
    if (it != m_Views.end())
    {
        m_Views.erase(it);
    }
}

static bool CreateRawBuffer(
    VulkanDevice* device,
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkMemoryPropertyFlags memoryProps,
    VkBuffer& outBuffer,
    VulkanAllocation& outAllocation)
{
    if (!device || size == 0)
    {
        return false;
    }

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkDevice vkDevice = device->GetHandle();
    if (!VKFunc::CreateBuffer(vkDevice, &bufferInfo, &outBuffer))
    {
        return false;
    }

    VkMemoryRequirements memRequirements{};
    VKFunc::GetBufferMemoryRequirements(vkDevice, outBuffer, &memRequirements);

    if (!device->GetMemoryManager()->Allocate(memRequirements, memoryProps, outAllocation))
    {
        VKFunc::DestroyBuffer(vkDevice, outBuffer);
        outBuffer = VK_NULL_HANDLE;
        return false;
    }

    outAllocation.SetBufferHandle(outBuffer);

    if (!VKFunc::BindBufferMemory(vkDevice, outBuffer, outAllocation.GetMemory(), outAllocation.GetOffset()))
    {
        device->GetMemoryManager()->Free(outAllocation);
        outAllocation = VulkanAllocation{};
        VKFunc::DestroyBuffer(vkDevice, outBuffer);
        outBuffer = VK_NULL_HANDLE;
        return false;
    }

    return true;
}

VulkanRayTracingGeometry::VulkanRayTracingGeometry(VulkanDevice* device, const RHIRayTracingGeometryDesc& desc) : m_Device(device)
{
    SetGeometryDesc(desc);
}

VulkanRayTracingGeometry::~VulkanRayTracingGeometry()
{
    DestroyAccelerationStructure();
}

void VulkanRayTracingGeometry::DestroyAccelerationStructure()
{
    if (m_Device && m_AccelerationStructure != VK_NULL_HANDLE)
    {
        VKFunc::DestroyAccelerationStructureKHR(m_Device->GetHandle(), m_AccelerationStructure);
        m_AccelerationStructure = VK_NULL_HANDLE;
        m_DeviceAddress = 0;
    }
    DestroyStorageBuffer();
}

void VulkanRayTracingGeometry::DestroyStorageBuffer()
{
    if (!m_Device)
    {
        return;
    }

    if (m_StorageBuffer != VK_NULL_HANDLE)
    {
        m_Device->EnqueueBufferForDeletion(m_StorageBuffer);
        m_StorageBuffer = VK_NULL_HANDLE;
    }

    if (m_StorageAllocation.GetMemory() != VK_NULL_HANDLE)
    {
        m_Device->GetMemoryManager()->Free(m_StorageAllocation);
        m_StorageAllocation = VulkanAllocation{};
    }
}

void VulkanRayTracingGeometry::SetGeometryDesc(const RHIRayTracingGeometryDesc& desc)
{
    _geometryDesc = desc;
    DestroyAccelerationStructure();

    if (!m_Device || !_geometryDesc.VertexBuffer || !_geometryDesc.IndexBuffer)
    {
        return;
    }

    VulkanBuffer* vertexBuffer = static_cast<VulkanBuffer*>(_geometryDesc.VertexBuffer);
    VulkanBuffer* indexBuffer = static_cast<VulkanBuffer*>(_geometryDesc.IndexBuffer);
    if (!vertexBuffer || !indexBuffer || _geometryDesc.IndexCount < 3 || _geometryDesc.VertexCount == 0)
    {
        return;
    }

    VkBufferDeviceAddressInfo vertexAddressInfo{};
    vertexAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    vertexAddressInfo.buffer = vertexBuffer->GetHandle();

    VkBufferDeviceAddressInfo indexAddressInfo{};
    indexAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    indexAddressInfo.buffer = indexBuffer->GetHandle();

    VkAccelerationStructureGeometryTrianglesDataKHR trianglesData{};
    trianglesData.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    trianglesData.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    trianglesData.vertexData.deviceAddress = VKFunc::GetBufferDeviceAddress(m_Device->GetHandle(), &vertexAddressInfo);
    trianglesData.vertexStride = _geometryDesc.VertexStride;
    trianglesData.maxVertex = _geometryDesc.VertexCount - 1;
    trianglesData.indexType = indexBuffer->GetDesc().Stride == 4 ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16;
    trianglesData.indexData.deviceAddress = VKFunc::GetBufferDeviceAddress(m_Device->GetHandle(), &indexAddressInfo);
    trianglesData.transformData.deviceAddress = 0;

    m_GeometryInfo = {};
    m_GeometryInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    m_GeometryInfo.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    m_GeometryInfo.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    m_GeometryInfo.geometry.triangles = trianglesData;

    const uint32_t primitiveCount = _geometryDesc.IndexCount / 3;
    m_BuildRangeInfo = {};
    m_BuildRangeInfo.primitiveCount = primitiveCount;
    m_BuildRangeInfo.primitiveOffset = 0;
    m_BuildRangeInfo.firstVertex = 0;
    m_BuildRangeInfo.transformOffset = 0;

    VkBuildAccelerationStructureFlagsKHR buildFlags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    if (_geometryDesc.bAllowUpdate)
    {
        buildFlags |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
    }
    if (_geometryDesc.bAllowCompaction)
    {
        buildFlags |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR;
    }

    VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
    buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    buildInfo.flags = buildFlags;
    m_BuildFlags = buildFlags;
    buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries = &m_GeometryInfo;

    VkAccelerationStructureBuildSizesInfoKHR sizeInfo{};
    sizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    VKFunc::GetAccelerationStructureBuildSizesKHR(
        m_Device->GetHandle(),
        VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &buildInfo,
        &primitiveCount,
        &sizeInfo);

    _sizeInfo.ResultSize = sizeInfo.accelerationStructureSize;
    _sizeInfo.BuildScratchSize = sizeInfo.buildScratchSize;
    _sizeInfo.UpdateScratchSize = sizeInfo.updateScratchSize;

    if (!CreateRawBuffer(
        m_Device,
        _sizeInfo.ResultSize,
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        m_StorageBuffer,
        m_StorageAllocation))
    {
        return;
    }

    VkAccelerationStructureCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    createInfo.buffer = m_StorageBuffer;
    createInfo.offset = 0;
    createInfo.size = _sizeInfo.ResultSize;
    createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;

    if (!VKFunc::CreateAccelerationStructureKHR(m_Device->GetHandle(), &createInfo, &m_AccelerationStructure))
    {
        DestroyStorageBuffer();
        return;
    }

    VkAccelerationStructureDeviceAddressInfoKHR addressInfo{};
    addressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    addressInfo.accelerationStructure = m_AccelerationStructure;
    m_DeviceAddress = VKFunc::GetAccelerationStructureDeviceAddressKHR(m_Device->GetHandle(), &addressInfo);
}


VulkanRayTracingInstance::VulkanRayTracingInstance(VulkanDevice* device, const RHIRayTracingInstancesDesc& instances) : m_Device(device)
{
    SetInstancesDesc(instances);
}

VulkanRayTracingInstance::~VulkanRayTracingInstance()
{
    DestroyAccelerationStructure();
}

void VulkanRayTracingInstance::DestroyAccelerationStructure()
{
    if (m_Device && m_AccelerationStructure != VK_NULL_HANDLE)
    {
        VKFunc::DestroyAccelerationStructureKHR(m_Device->GetHandle(), m_AccelerationStructure);
        m_AccelerationStructure = VK_NULL_HANDLE;
        m_DeviceAddress = 0;
    }
    DestroyInstanceBuffer();
    DestroyStorageBuffer();
}

void VulkanRayTracingInstance::DestroyStorageBuffer()
{
    if (!m_Device)
    {
        return;
    }

    if (m_StorageBuffer != VK_NULL_HANDLE)
    {
        m_Device->EnqueueBufferForDeletion(m_StorageBuffer);
        m_StorageBuffer = VK_NULL_HANDLE;
    }

    if (m_StorageAllocation.GetMemory() != VK_NULL_HANDLE)
    {
        m_Device->GetMemoryManager()->Free(m_StorageAllocation);
        m_StorageAllocation = VulkanAllocation{};
    }
}

void VulkanRayTracingInstance::DestroyInstanceBuffer()
{
    if (!m_Device)
    {
        return;
    }

    if (m_InstanceBuffer != VK_NULL_HANDLE)
    {
        m_Device->EnqueueBufferForDeletion(m_InstanceBuffer);
        m_InstanceBuffer = VK_NULL_HANDLE;
    }

    if (m_InstanceAllocation.GetMemory() != VK_NULL_HANDLE)
    {
        m_Device->GetMemoryManager()->Free(m_InstanceAllocation);
        m_InstanceAllocation = VulkanAllocation{};
    }
}

void VulkanRayTracingInstance::SetInstancesDesc(const RHIRayTracingInstancesDesc& desc)
{

    this->_instances = desc;
    const auto& instances = desc.Instances;
    DestroyAccelerationStructure();

    if (!m_Device || instances.empty())
    {
        return;
    }

    std::vector<VkAccelerationStructureInstanceKHR> vkInstances;
    vkInstances.reserve(instances.size());

    for (const RHIRayTracingInstanceDesc& instance : instances)
    {
        VkAccelerationStructureInstanceKHR vkInstance{};
        vkInstance.instanceCustomIndex = instance.InstanceID;
        vkInstance.mask = instance.Mask;
        vkInstance.instanceShaderBindingTableRecordOffset = 0;
        vkInstance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;

        for (uint32_t row = 0; row < 3; ++row)
        {
            for (uint32_t col = 0; col < 4; ++col)
            {
                vkInstance.transform.matrix[row][col] = instance.Transform(row, col);
            }
        }

        auto* vkGeometry = dynamic_cast<VulkanRayTracingGeometry*>(instance.Geometry);
        vkInstance.accelerationStructureReference = vkGeometry ? vkGeometry->GetDeviceAddress() : 0;
        vkInstances.push_back(vkInstance);
    }

    const VkDeviceSize instanceBufferSize = sizeof(VkAccelerationStructureInstanceKHR) * vkInstances.size();
    if (!CreateRawBuffer(
        m_Device,
        instanceBufferSize,
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        m_InstanceBuffer,
        m_InstanceAllocation))
    {
        return;
    }


    

    if (!m_InstanceAllocation.GetMappedPointer())
        throw std::runtime_error("The TLAS instance upload buffer is not mapped");
    memcpy(m_InstanceAllocation.GetMappedPointer(), vkInstances.data(), static_cast<size_t>(instanceBufferSize));

    VkBufferDeviceAddressInfo instanceAddressInfo{};
    instanceAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    instanceAddressInfo.buffer = m_InstanceBuffer;

    m_GeometryInfo = {};
    m_GeometryInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    m_GeometryInfo.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    m_GeometryInfo.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    m_GeometryInfo.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    m_GeometryInfo.geometry.instances.arrayOfPointers = VK_FALSE;
    m_GeometryInfo.geometry.instances.data.deviceAddress = VKFunc::GetBufferDeviceAddress(m_Device->GetHandle(), &instanceAddressInfo);

    const uint32_t primitiveCount = static_cast<uint32_t>(instances.size());
    m_BuildRangeInfo = {};
    m_BuildRangeInfo.primitiveCount = primitiveCount;
    m_BuildRangeInfo.primitiveOffset = 0;
    m_BuildRangeInfo.firstVertex = 0;
    m_BuildRangeInfo.transformOffset = 0;

    VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
    buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
    buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries = &m_GeometryInfo;

    VkAccelerationStructureBuildSizesInfoKHR sizeInfo{};
    sizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    VKFunc::GetAccelerationStructureBuildSizesKHR(
        m_Device->GetHandle(),
        VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &buildInfo,
        &primitiveCount,
        &sizeInfo);

    _sizeInfo.ResultSize = sizeInfo.accelerationStructureSize;
    _sizeInfo.BuildScratchSize = sizeInfo.buildScratchSize;
    _sizeInfo.UpdateScratchSize = sizeInfo.updateScratchSize;

    if (!CreateRawBuffer(
        m_Device,
        _sizeInfo.ResultSize,
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        m_StorageBuffer,
        m_StorageAllocation))
    {
        DestroyInstanceBuffer();
        return;
    }

    VkAccelerationStructureCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    createInfo.buffer = m_StorageBuffer;
    createInfo.offset = 0;
    createInfo.size = _sizeInfo.ResultSize;
    createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;

    if (!VKFunc::CreateAccelerationStructureKHR(m_Device->GetHandle(), &createInfo, &m_AccelerationStructure))
    {
        DestroyInstanceBuffer();
        DestroyStorageBuffer();
        return;
    }

    VkAccelerationStructureDeviceAddressInfoKHR addressInfo{};
    addressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    addressInfo.accelerationStructure = m_AccelerationStructure;
    m_DeviceAddress = VKFunc::GetAccelerationStructureDeviceAddressKHR(m_Device->GetHandle(), &addressInfo);
}

VulkanShaderResourceView::VulkanShaderResourceView(VulkanDevice* device,
    RHIViewableResource* resource,
    const RHITexSRVCreateInfo& srvInfo)
    : RHIShaderResourceView(resource)
{
    m_ResourcePtr = resource;
    m_Device = device;
    m_ResourceType = EResourceType::Texture;

    CreateTextureView(srvInfo);

    // Attach to resource
    if (auto* vulkanResource = dynamic_cast<VulkanTexture*>(m_ResourcePtr))
    {
        vulkanResource->AttachView(this);
    }
    else if (auto* vulkanResource = dynamic_cast<VulkanBuffer*>(m_ResourcePtr))
    {
        vulkanResource->AttachView(this);
    }
}

VulkanShaderResourceView::VulkanShaderResourceView(
    VulkanDevice* device,
    RHIViewableResource* resource,
    const RHIBufferSRVCreateInfo& srvInfo)
    : RHIShaderResourceView(resource)
{
    m_ResourcePtr = resource;
    m_Device = device;
    m_ResourceType = EResourceType::Buffer;

    CreateBufferView(srvInfo);

    // Attach to resource
    if (auto* vulkanResource = dynamic_cast<VulkanBuffer*>(m_ResourcePtr))
    {
        vulkanResource->AttachView(this);
    }
}

void VulkanShaderResourceView::CreateTextureView(
    const RHITexSRVCreateInfo& srvInfo)
{
    auto* texture = static_cast<VulkanTexture*>(m_ResourcePtr);

    m_Format = (srvInfo.Format == ERHIFormat::Unknown)
        ? texture->GetFormat()
        : TransformFormatFrom(srvInfo.Format);

    m_BaseMipLevel = srvInfo.FirstMipSlice;
    m_MipLevelCount = srvInfo.MipCount;
    m_BaseArrayLayer = srvInfo.FirstArraySlice;
    m_LayerCount = srvInfo.ArraySize;

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = texture->GetImage();
    VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_2D;
    if (srvInfo.ViewType == ERHITextureViewType::Derived) {
        viewType = TransformViewTypeFrom(texture->GetDesc().Type);
    }
    else {
        viewType = TransformViewTypeFrom(srvInfo.ViewType);
    }
    viewInfo.viewType = viewType;

    viewInfo.format = m_Format;

    viewInfo.subresourceRange.aspectMask = GetImageAspectFlags(m_Format);
    viewInfo.subresourceRange.baseMipLevel = m_BaseMipLevel;
    viewInfo.subresourceRange.levelCount = m_MipLevelCount;
    viewInfo.subresourceRange.baseArrayLayer = m_BaseArrayLayer;
    viewInfo.subresourceRange.layerCount = m_LayerCount;

    // 使用 VulkanTextureView 结构体创建
    m_TextureView.Create(m_Device, texture->GetImage(), viewInfo.viewType,
                      viewInfo.subresourceRange.aspectMask, m_Format,
                      m_BaseMipLevel, m_MipLevelCount, m_BaseArrayLayer, m_LayerCount);

    m_DescriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
}

void VulkanShaderResourceView::CreateBufferView(
    const RHIBufferSRVCreateInfo& srvInfo)
{
    auto* buffer = static_cast<VulkanBuffer*>(m_ResourcePtr);

    m_Format = TransformFormatFrom(srvInfo.Format);

    // 只有当 Buffer 的 usage 包含 Texel 标志时才创建 BufferView
    if (EnumHasAnyFlags(buffer->GetDesc().Usage, ERHIBufferUsageFlag::Texel))
    {
        VkDeviceSize range = (srvInfo.NumElements * srvInfo.Stride);
        m_BufferView.Create(m_Device, buffer->GetHandle(), m_Format, srvInfo.Offset, range);
        m_DescriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    }
    else
    {
		m_BufferView.Buffer = buffer->GetHandle();
        m_BufferView.Offset = srvInfo.Offset;
        m_BufferView.Size = (srvInfo.NumElements * srvInfo.Stride);
        // 如果没有 Texel 标志，默认使用 STORAGE_BUFFER 类型
        m_DescriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    }
}

VulkanShaderResourceView::~VulkanShaderResourceView()
{
    if (_bIsValid)
    {
        // Detach from resource
        if (auto* vulkanResource = dynamic_cast<VulkanTexture*>(m_ResourcePtr))
        {
            vulkanResource->DetachView(this);
        }
        else if (auto* vulkanResource = dynamic_cast<VulkanBuffer*>(m_ResourcePtr))
        {
            vulkanResource->DetachView(this);
        }
    }
    DestroyView();
}

const VulkanTextureView& VulkanShaderResourceView::GetTextureView() const
{
    return m_TextureView;
}

const VulkanBufferView& VulkanShaderResourceView::GetBufferView() const
{
    return m_BufferView;
}

void VulkanShaderResourceView::Invalidate()
{
    // 销毁 TextureView
    if (m_TextureView.View != VK_NULL_HANDLE)
    {
        m_TextureView.Destroy(m_Device);
    }

    // 销毁 BufferView
    if (m_BufferView.View != VK_NULL_HANDLE)
    {
        m_BufferView.Destroy(m_Device);
    }
    _bIsValid = false;
}

void VulkanShaderResourceView::DestroyView()
{
    Invalidate();
}

VulkanUnorderedAccessView::VulkanUnorderedAccessView(
    VulkanDevice* device,
    RHIViewableResource* resource,
    const RHITexUAVCreateInfo& uavInfo)
    : RHIUnorderedAccessView(resource)
{
    m_ResourcePtr = resource;
    m_Device = device;
    m_ResourceType = EResourceType::Texture;

    CreateTextureView(uavInfo);

    // Attach to resource
    if (auto* vulkanResource = dynamic_cast<VulkanTexture*>(m_ResourcePtr))
    {
        vulkanResource->AttachView(this);
    }
}

VulkanUnorderedAccessView::VulkanUnorderedAccessView(
    VulkanDevice* device,
    RHIViewableResource* resource,
    const RHIBufferUAVCreateInfo& uavInfo)
    : RHIUnorderedAccessView(resource)
{
    m_ResourcePtr = resource;
    m_Device = device;
    m_ResourceType = EResourceType::Buffer;

    CreateBufferView(uavInfo);

    // Attach to resource
    if (auto* vulkanResource = dynamic_cast<VulkanBuffer*>(m_ResourcePtr))
    {
        vulkanResource->AttachView(this);
    }
}


void VulkanUnorderedAccessView::CreateTextureView(
    const RHITexUAVCreateInfo& uavInfo)
{
    auto* texture = static_cast<VulkanTexture*>(m_ResourcePtr);

    m_Format = (uavInfo.Format == ERHIFormat::Unknown)
        ? texture->GetFormat()
        : TransformFormatFrom(uavInfo.Format);

    m_BaseMipLevel = uavInfo.FirstMipSlice;
    m_MipLevelCount = uavInfo.MipCount;
    m_BaseArrayLayer = uavInfo.FirstArraySlice;
    m_LayerCount = uavInfo.ArraySize;

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = texture->GetImage();
	VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_2D;
    if (uavInfo.ViewType == ERHITextureViewType::Derived) {
		viewType = TransformViewTypeFrom(texture->GetDesc().Type);
    }
    else {
        viewType = TransformViewTypeFrom(uavInfo.ViewType);
    }
    viewInfo.viewType = viewType;

    viewInfo.format = m_Format;

    viewInfo.subresourceRange.aspectMask = GetImageAspectFlags(m_Format);
    viewInfo.subresourceRange.baseMipLevel = m_BaseMipLevel;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = m_BaseArrayLayer;
    viewInfo.subresourceRange.layerCount = m_LayerCount;

    // 使用 VulkanTextureView 结构体创建
    m_TextureView.Create(m_Device, texture->GetImage(), viewInfo.viewType,
                      viewInfo.subresourceRange.aspectMask, m_Format,
                      m_BaseMipLevel, m_MipLevelCount, m_BaseArrayLayer, m_LayerCount);

    m_DescriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
}

void VulkanUnorderedAccessView::CreateBufferView(
    const RHIBufferUAVCreateInfo& uavInfo)
{
    auto* buffer = static_cast<VulkanBuffer*>(m_ResourcePtr);

    m_Format = TransformFormatFrom(uavInfo.Format);

    // 只有当 Buffer 的 usage 包含 Texel 标志时才创建 BufferView
    if (EnumHasAnyFlags(buffer->GetDesc().Usage, ERHIBufferUsageFlag::Texel))
    {
        VkDeviceSize range = (uavInfo.NumElements * uavInfo.Stride);
        m_BufferView.Create(m_Device, buffer->GetHandle(), m_Format, uavInfo.Offset, range);
        m_DescriptorType = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
    }
    else
    {
        m_BufferView.Buffer = buffer->GetHandle();
        m_BufferView.Offset = uavInfo.Offset;
        m_BufferView.Size = (uavInfo.NumElements * uavInfo.Stride);
        // 如果没有 Texel 标志，默认使用 STORAGE_BUFFER 类型
        m_DescriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    }
}

VulkanUnorderedAccessView::~VulkanUnorderedAccessView()
{
    if (_bIsValid)
    {
        // Detach from resource
        if (auto* vulkanResource = dynamic_cast<VulkanTexture*>(m_ResourcePtr))
        {
            vulkanResource->DetachView(this);
        }
        else if (auto* vulkanResource = dynamic_cast<VulkanBuffer*>(m_ResourcePtr))
        {
            vulkanResource->DetachView(this);
        }
    }

    DestroyView();
}


const VulkanTextureView& VulkanUnorderedAccessView::GetTextureView() const
{
    return m_TextureView;
}

const VulkanBufferView& VulkanUnorderedAccessView::GetBufferView() const
{
    return m_BufferView;
}

void VulkanUnorderedAccessView::Invalidate()
{
    // 销毁 TextureView
    if (m_TextureView.View != VK_NULL_HANDLE)
    {
        m_TextureView.Destroy(m_Device);
    }

    // 销毁 BufferView
    if (m_BufferView.View != VK_NULL_HANDLE)
    {
        m_BufferView.Destroy(m_Device);
    }
    _bIsValid = false;
}

void VulkanUnorderedAccessView::DestroyView()
{
    Invalidate();
}



// Vulkan RHISwapchain
VulkanRHISwapchain::VulkanRHISwapchain(VulkanDevice* device, uint32_t width, uint32_t height, void* windowHandle, ERHIFormat format)
    : m_Device(device), m_WindowHandle(windowHandle),m_Width(width),m_Height(height),m_Format(format)
{


    if (!m_WindowHandle)
    {
        LOG_ERROR("VulkanRHISwapchain: Invalid window handle");
    }
    CreateSwapchain();
    //acquireSemaphores.resize(swapchainImages_.size());
    //for (int i = 0; i < swapchainImages_.size(); i++) {
    //    auto semaphore = Device->GetSemaphoreManager()->AcquireBinary();
    //    acquireSemaphores[i] = new VulkanRHISyncPoint(device,device->GetPresentQueue()->GetType(), semaphore);
    //}
}

VulkanRHISwapchain::~VulkanRHISwapchain() {
    VkDevice device = m_Device->GetHandle();

    DestroySwapchain();


}

void VulkanRHISwapchain::Present(VulkanQueue* presentQueue, const RHI::RHIWaitInfo& waitInfo)
{
    if (m_CurrentBackBufferIndex == -1)
    {
        return;
    }

    RHISyncPoint* rhivkSyncPoint = nullptr;
    if (waitInfo.SyncPoint) {
        rhivkSyncPoint = waitInfo.SyncPoint;
    }
    else {
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


    // 1. 获取用于给 Present 等待的 Binary Semaphore
    // 注意：Present 只能等待 Binary Semaphore
    VulkanSemaphore* binaryWaitHandle = VK_NULL_HANDLE;

    if (rhivkSyncPoint)
    {
        // 【核心修改】：将 Timeline SyncPoint 桥接到一个 Binary Semaphore
        // 我们提交一个没有任何命令的空包，让它等待 Timeline 达到指定 Value，完成后触发一个 Binary Semaphore
        auto* vkSyncPoint = static_cast<VulkanRHISyncPoint*>(rhivkSyncPoint);

        // 执行桥接提交
        binaryWaitHandle = presentQueue->SubmitEmptyWithDependency(
            vkSyncPoint->GetSemaphore()->GetHandle(),
            waitInfo.Value
        );
    }
    // 2. 调用原生的 Present
    // 注意：Swapchain->Present 内部应调用 vkQueuePresentKHR
    m_Swapchain->Present(presentQueue, binaryWaitHandle);
    m_CurrentIndex++;
    m_CurrentIndex %= m_SwapchainImages.size();

    // 3. 更新索引和状态
    // ... 保持原有的索引更新逻辑 ...
    m_CurrentBackBufferIndex = -1;
}

RHISwapchain::RHISwapchainSlot VulkanRHISwapchain::AcquireNextSlot()
{
    RHISwapchain::RHISwapchainSlot slot{};

    // 1. 原有的获取 BackBuffer 逻辑 (内部调用 vkAcquireNextImageKHR)
    // 假设这个函数会更新 currentSemaphoreIndex 并触发 acquireSemaphores[i]
    auto backTexture = GetBackTexture();
    slot.Texture = backTexture.get();

    if (slot.Texture && m_CurrentBackBufferIndex >= 0)
    {
        // 4. 返回给 Slot
        // 注意：在随后的 FlushContext 中，这个 Dependency 会进入 WaitInfos
        slot.ReadySync = m_AcquireSemaphores[m_CurrentIndex];
    }
    else
    {
        slot.ReadySync = nullptr;
    }

    return slot;
}

void VulkanRHISwapchain::Resize(uint32_t inWidth, uint32_t inHeight)
{
    m_Width = inWidth;
    m_Height = inHeight;
    DestroySwapchain();
    CreateSwapchain();

}

VulkanTextureSP VulkanRHISwapchain::GetBackTexture()
{
    if (m_CurrentBackBufferIndex != -1) {
        return m_BackBufferTextures[m_CurrentBackBufferIndex];
    }
    auto seamphore = m_Device->GetSemaphoreManager()->AcquireBinary();
    m_AcquireSemaphores[m_CurrentIndex]->SetSemaphore(seamphore);
    m_Swapchain->AcquireNextImage(m_AcquireSemaphores[m_CurrentIndex]->GetSemaphore(), &m_CurrentBackBufferIndex);
    if (m_CurrentBackBufferIndex == -1) {
        return nullptr;
    }
    return m_BackBufferTextures[m_CurrentBackBufferIndex];
}

void VulkanRHISwapchain::CreateSwapchain()
{
    m_SwapchainImages.clear();
    m_BackBufferTextures.clear();
    m_AcquireSemaphores.clear();

    VulkanSwapchain::SwapchainDesc swapchainDesc = {};
    swapchainDesc.WindowHandle = m_WindowHandle;
    swapchainDesc.Width = m_Width;
    swapchainDesc.Height = m_Height;
    swapchainDesc.PreferredFormat = m_Format;
    m_Swapchain = new VulkanSwapchain(m_Device, swapchainDesc, m_SwapchainImages);
    m_Width = m_Swapchain->GetExtent().width;
    m_Height = m_Swapchain->GetExtent().height;

    RHITextureDesc textureDesc;
    textureDesc.Width = m_Width;                  // 纹理宽度
    textureDesc.Height = m_Height;                 // 纹理高度
    textureDesc.Depth = 1;                  // 纹理深度（3D纹理）
    textureDesc.MipLevels = 1;              // Mip层级数量
    textureDesc.ArraySize = 1;              // 数组大小
    textureDesc.Format = m_Format; // 像素格式

    ERHITextureType type = ERHITextureType::Texture2D;   // 纹理类型
    uint32_t sampleCount = 1;            // 多重采样数量
    uint32_t sampleQuality = 0;          // 多重采样质量
    textureDesc.Usage = ERHITextureCreateFlag::RenderTarget | ERHITextureCreateFlag::Presentable | ERHITextureCreateFlag::TransferDest | ERHITextureCreateFlag::TransferSrc; // 纹理用途
    auto imageCount = m_SwapchainImages.size();
    for (int i = 0; i < imageCount; i++) {
        auto name = std::string("BackBuffer") + std::to_string(i);
        textureDesc.DebugName = name.c_str(); // 调试名称
        auto texture = std::make_shared<VulkanTexture>(m_Device, textureDesc, m_SwapchainImages[i]);
        m_BackBufferTextures.push_back(texture);
    }
    m_AcquireSemaphores.resize(m_SwapchainImages.size());
    for (int i = 0; i < m_SwapchainImages.size(); i++) {
        auto semaphore = m_Device->GetSemaphoreManager()->AcquireBinary();
        m_AcquireSemaphores[i] = new VulkanRHISyncPoint(m_Device, m_Device->GetPresentQueue()->GetType(), semaphore);
    }

}

void VulkanRHISwapchain::DestroySwapchain()
{
    delete m_Swapchain;
    m_Swapchain = nullptr;
    m_BackBufferTextures.clear();
    for (auto* semaphore : m_AcquireSemaphores) {
        delete semaphore;
    }
    m_AcquireSemaphores.clear();
    m_CurrentBackBufferIndex = -1;
    m_CurrentIndex = 0;

}


VulkanVertexDescState::VulkanVertexDescState(VulkanDevice* device, const RHIVertexDescStateDesc& desc): RHIVertexDescState(desc), m_Device(device)
{
    // Initialize vertex input state
    VertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(desc.Bindings.size());
    VertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(desc.Attributes.size());
    BindingDescriptions.resize(desc.Bindings.size());
    AttributeDescriptions.resize(desc.Attributes.size());
    for (size_t i = 0; i < desc.Bindings.size(); ++i) {
        const auto& bindingDesc = desc.Bindings[i];
        BindingDescriptions[i].binding = bindingDesc.Binding;
        BindingDescriptions[i].stride = bindingDesc.Stride;
        BindingDescriptions[i].inputRate = (bindingDesc.InputRate == ERHIInputRate::PerVertex) ? VK_VERTEX_INPUT_RATE_VERTEX : VK_VERTEX_INPUT_RATE_INSTANCE;
    }
    for (size_t i = 0; i < desc.Attributes.size(); ++i) {
        const auto& attributeDesc = desc.Attributes[i];
        AttributeDescriptions[i].location = attributeDesc.Location;
        AttributeDescriptions[i].binding = attributeDesc.Binding;  // 必须
        AttributeDescriptions[i].format = TransformFormatFrom(attributeDesc.Format);
        AttributeDescriptions[i].offset = attributeDesc.Offset;   // 必须
    }
    VertexInputInfo.pVertexBindingDescriptions = BindingDescriptions.data();
    VertexInputInfo.pVertexAttributeDescriptions = AttributeDescriptions.data();

}
VulkanVertexDescState::~VulkanVertexDescState()
{

}


// ----------------------------
// 光栅化状态 Vulkan派生
// ----------------------------
VulkanRasterizerState::VulkanRasterizerState(VulkanDevice* device, const RHIRasterizerStateDesc& desc) : RHIRasterizerState(desc), m_Device(device)
{
    VkDevice vkDevice = m_Device->GetHandle();
    RasterizerInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    RasterizerInfo.depthClampEnable = desc.bDepthClampEnable;
    RasterizerInfo.rasterizerDiscardEnable = desc.bRasterizerDiscardEnable;

    switch (desc.PolygonMode) {
    case ERHIPolygonMode::Fill:
        RasterizerInfo.polygonMode = VK_POLYGON_MODE_FILL;
        break;
    case ERHIPolygonMode::Line:
        RasterizerInfo.polygonMode = VK_POLYGON_MODE_LINE;
        break;
    case ERHIPolygonMode::Point:
        RasterizerInfo.polygonMode = VK_POLYGON_MODE_POINT;
        break;
    default:
        RasterizerInfo.polygonMode = VK_POLYGON_MODE_FILL;
        break;
    }

    switch (desc.CullMode) {
    case ERHICullMode::None:
        RasterizerInfo.cullMode = VK_CULL_MODE_NONE;
        break;
    case ERHICullMode::Front:
        RasterizerInfo.cullMode = VK_CULL_MODE_FRONT_BIT;
        break;
    case ERHICullMode::Back:
        RasterizerInfo.cullMode = VK_CULL_MODE_BACK_BIT;
        break;
    case ERHICullMode::FrontAndBack:
        RasterizerInfo.cullMode = VK_CULL_MODE_FRONT_AND_BACK;
        break;
    default:
        RasterizerInfo.cullMode = VK_CULL_MODE_BACK_BIT;
        break;
    }

    switch (desc.FrontFace) {
    case ERHIFrontFace::CounterClockwise:
        RasterizerInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        break;
    case ERHIFrontFace::Clockwise:
        RasterizerInfo.frontFace = VK_FRONT_FACE_CLOCKWISE;
        break;
    default:
        RasterizerInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        break;
    }

    RasterizerInfo.depthBiasEnable = desc.bDepthBiasEnable;
    RasterizerInfo.depthBiasConstantFactor = desc.DepthBiasConstantFactor;
    RasterizerInfo.depthBiasClamp = desc.DepthBiasClamp;
    RasterizerInfo.depthBiasSlopeFactor = desc.DepthBiasSlopeFactor;
    RasterizerInfo.lineWidth = desc.LineWidth;
}

VulkanRasterizerState::~VulkanRasterizerState()
{
}

// ----------------------------
// 颜色混合状态 Vulkan派生
// ----------------------------
VulkanColorBlendState::VulkanColorBlendState(VulkanDevice* device, const RHIColorBlendStateDesc& desc) : RHIColorBlendState(desc), m_Device(device)
{
    VkDevice vkDevice = m_Device->GetHandle();
    ColorBlendInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    ColorBlendInfo.logicOpEnable = desc.bLogicOpEnable;
    ColorBlendInfo.logicOp = VK_LOGIC_OP_COPY; // TODO: Support other logic ops
    ColorBlendInfo.attachmentCount = static_cast<uint32_t>(desc.Attachments.size());
    AttachmentStates.resize(desc.Attachments.size());

    for (size_t i = 0; i < desc.Attachments.size(); ++i) {
        auto& attachmentDesc = desc.Attachments[i];
        auto& attachmentState = AttachmentStates[i];

        attachmentState.blendEnable = attachmentDesc.bBlendEnable;
        attachmentState.colorWriteMask = attachmentDesc.ColorWriteMask; // TODO: Support other write masks
        // TODO: Support other blend factors and operations
    }

    ColorBlendInfo.pAttachments = AttachmentStates.data();
    ColorBlendInfo.blendConstants[0] = desc.BlendConstants[0];
    ColorBlendInfo.blendConstants[1] = desc.BlendConstants[1];
    ColorBlendInfo.blendConstants[2] = desc.BlendConstants[2];
    ColorBlendInfo.blendConstants[3] = desc.BlendConstants[3];
}

VulkanColorBlendState::~VulkanColorBlendState()
{
}

// ----------------------------
// 深度模板状态 Vulkan派生
// ----------------------------
VulkanDepthStencilState::VulkanDepthStencilState(VulkanDevice* device, const RHIDepthStencilStateDesc& desc) : RHIDepthStencilState(desc), m_Device(device)
{
    VkDevice vkDevice = m_Device->GetHandle();
    DepthStencilInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    DepthStencilInfo.depthTestEnable = desc.bDepthTestEnable;
    DepthStencilInfo.depthWriteEnable = desc.bDepthWriteEnable;
    // TODO: map compare operation
    DepthStencilInfo.depthCompareOp = TransformCompareOp(desc.DepthCompareOp);
    DepthStencilInfo.depthBoundsTestEnable = VK_FALSE;
    DepthStencilInfo.stencilTestEnable = VK_FALSE;
}

VulkanDepthStencilState::~VulkanDepthStencilState()
{
}

// Vulkan Ring Buffer Implementation
VulkanRingBuffer::VulkanRingBuffer(VulkanDevice* device, uint64_t totalSize, VkBufferUsageFlags usage, VkMemoryPropertyFlags memPropertyFlags)
    : m_Device(device), m_BufferSize(totalSize), m_BufferOffset(0), m_MinAlignment(256) // 默认最小对齐为256字节
{
    VkDevice vkDevice = m_Device->GetHandle();
    VulkanMemoryManager* memoryManager = m_Device->GetMemoryManager();

    // 创建缓冲区
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = totalSize;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkBuffer buffer;
    VKFunc::CreateBuffer(vkDevice, &bufferInfo, &buffer);

    // 获取内存需求
    VkMemoryRequirements memRequirements;
    VKFunc::GetBufferMemoryRequirements(vkDevice, buffer, &memRequirements);

    // 分配内存
    if (!memoryManager->Allocate(memRequirements, memPropertyFlags, m_Allocation)) {
        LOG_ERROR("Failed to allocate memory for ring buffer");
        VKFunc::DestroyBuffer(vkDevice, buffer);
        return;
    }

    // 绑定内存
    VKFunc::BindBufferMemory(vkDevice, buffer, m_Allocation.GetMemory(), m_Allocation.GetOffset());

    // 设置缓冲区句柄到分配对象
    m_Allocation.SetBufferHandle(buffer);

#ifdef DEBUG_INFO
    char buf[64];
    snprintf(buf, sizeof(buf), "VulkanRingBuffer:0x%llx", (unsigned long long)buffer);
    VKFunc::SetDebugName(vkDevice, VK_OBJECT_TYPE_BUFFER, (uint64_t)buffer, buf);
#endif

}

VulkanRingBuffer::~VulkanRingBuffer()
{
    if (m_Allocation.GetMemory() != VK_NULL_HANDLE) {
        VkDevice vkDevice = m_Device->GetHandle();
        VulkanMemoryManager* memoryManager = m_Device->GetMemoryManager();
        // Memory block mapping is owned by VulkanMemoryBlock.
        // Do not unmap sub-allocations here, otherwise the same VkDeviceMemory
        // may be unmapped multiple times across different resources.

        // 销毁缓冲区
        VkBuffer buffer = m_Allocation.GetBufferHandle();
        if (buffer != VK_NULL_HANDLE) {
            VKFunc::DestroyBuffer(vkDevice, buffer);
        }

        // 释放内存
        memoryManager->Free(m_Allocation);
    }
}

uint64_t VulkanRingBuffer::AllocateMemory(uint64_t size, uint32_t alignment)
{
    alignment = alignment > m_MinAlignment ? alignment : m_MinAlignment;
    // 简单的对齐计算：向上对齐到alignment的倍数
    uint64_t alignedOffset = (m_BufferOffset + alignment - 1) & ~(alignment - 1);

    if (alignedOffset + size <= m_BufferSize) {
        m_BufferOffset = alignedOffset + size;
        return alignedOffset;
    }

    return WrapAroundAllocateMemory(size, alignment);
}

uint64_t VulkanRingBuffer::WrapAroundAllocateMemory(uint64_t size, uint32_t alignment)
{

    // 重置缓冲区偏移
    m_BufferOffset = 0;

    // 分配新内存
    uint64_t alignedOffset = (m_BufferOffset + alignment - 1) & ~(alignment - 1);
    m_BufferOffset = alignedOffset + size;

    return alignedOffset;
}

// Vulkan Uniform Buffer Uploader Implementation
VulkanLooseUniformDataUploader::VulkanLooseUniformDataUploader(VulkanDevice* device)
{
    // 创建CPU可见的环形缓冲区用于uniform buffer上传
    const uint64_t bufferSize = 1024 * 1024; // 1MB 缓冲区
    m_CPUBuffer = new VulkanRingBuffer(device, bufferSize,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
}

VulkanLooseUniformDataUploader::~VulkanLooseUniformDataUploader()
{
    delete m_CPUBuffer;
}


VulkanSampler::VulkanSampler(VulkanDevice* device, const RHISamplerDesc& desc) : RHISampler(desc), m_Device(device)
{
    VkSamplerCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;

    // =========================================
    // Filtering
    // =========================================

    info.magFilter = TransformFilter(desc.Filter);
    info.minFilter = TransformFilter(desc.Filter);

    info.mipmapMode = GetMipmapMode(desc.Filter);

    // =========================================
    // Address Modes
    // =========================================

    info.addressModeU = TransformAddressMode(desc.AddressU);
    info.addressModeV = TransformAddressMode(desc.AddressV);
    info.addressModeW = TransformAddressMode(desc.AddressW);

    // =========================================
    // LOD
    // =========================================

    info.mipLodBias = desc.MipLodBias;

    info.minLod = desc.MinLod;
    info.maxLod = desc.MaxLod;

    // =========================================
    // Anisotropy
    // =========================================

    info.anisotropyEnable =
        desc.bAnisotropyEnable ? VK_TRUE : VK_FALSE;

    info.maxAnisotropy = desc.MaxAnisotropy;

    // =========================================
    // Depth Compare
    // =========================================

    info.compareEnable =
        desc.bCompareEnable ? VK_TRUE : VK_FALSE;

    info.compareOp =
        TransformCompareOp(desc.CompareOp);

    // =========================================
    // Border Color
    // =========================================

    info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;

    // =========================================
    // Coordinates
    // =========================================

    info.unnormalizedCoordinates = VK_FALSE;

    // =========================================
    // Create
    // =========================================


    bool suc = VKFunc::CreateSampler(m_Device->GetHandle(), &info, &m_Sampler);
	if (!suc) {
		LOG_ERROR("Failed to create Vulkan sampler");
	}

}

VulkanSampler::~VulkanSampler()
{
    if (m_Sampler != VK_NULL_HANDLE) {
		m_Device->EnqueueSamplerForDeletion(m_Sampler);
    }

}

}
