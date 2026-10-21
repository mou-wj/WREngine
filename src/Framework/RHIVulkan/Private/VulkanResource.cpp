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
    : RHITexture(desc), Device(device),ExternalAllocated(externalAllocated) {
    VkDevice vkDevice = Device->GetHandle();
    
    Format = TransformFormatFrom(desc.Format);
	ImageAspectFlags = GetImageAspectFlags(Format);
    // Create VkImage
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = TransformImageTypeFrom(desc.Type); // TODO: Support other image types
    imageInfo.format = Format; // TODO: Support other formats
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
    VulkanQueue* initialQueue = ResolveInitialQueue(Device, desc.InitialQueueType);
    queueFamilyIndices.push_back(initialQueue ? initialQueue->GetFamilyIndex() : Device->GetGraphicsQueue()->GetFamilyIndex());
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageInfo.queueFamilyIndexCount = static_cast<uint32_t>(queueFamilyIndices.size());
	imageInfo.pQueueFamilyIndices = queueFamilyIndices.data();
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VKFunc::CreateImage(vkDevice, &imageInfo, &Image);

    // Get memory requirements
    VkMemoryRequirements memRequirements;
    VKFunc::GetImageMemoryRequirements(vkDevice, Image, &memRequirements);
    VulkanMemoryManager* memoryManager = Device->GetMemoryManager();
    if (!externalAllocated)
    {
        // Allocate memory
        if (!memoryManager->Allocate(memRequirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, Allocation)) {
            VKFunc::DestroyImage(vkDevice, Image);
        }

        // Bind memory to image
        VKFunc::BindImageMemory(vkDevice, Image, Allocation.GetMemory(), Allocation.GetOffset());
        CreateDefaultView();
    }




#ifdef DEBUG_INFO
    std::string debugName;
    if (Desc.DebugName) {
        debugName = Desc.DebugName;
    }
    else {
        char buf[64];
        snprintf(buf, sizeof(buf), "VulkanTexture:0x%llx", (unsigned long long)Image);
        debugName = buf;
    }
    VKFunc::SetDebugName(vkDevice, VK_OBJECT_TYPE_IMAGE, (uint64_t)Image, debugName.c_str());
#endif
}

VulkanTexture::VulkanTexture(VulkanDevice* device, const RHITextureDesc& desc, VkImage image)
    : RHITexture(desc), Device(device), Image(image)
{
    owner = false;
    VkDevice vkDevice = Device->GetHandle();
    Format = TransformFormatFrom(desc.Format);
    ImageAspectFlags = GetImageAspectFlags(Format);
    auto usage = TransformTextureUsageFlagsFrom(desc.Usage);
    // Create VkImageView
    DefaltView.Create(device,                                // fvulkan_device& device
        Image,                                 // VkImage
        TransformViewTypeFrom(desc.Type),      // VkImageViewType
        GetImageAspectFlags(TransformFormatFrom(desc.Format)), // VkImageAspectFlags
        Format,       // VkFormat
        0,                                      // first mip level
        desc.MipLevels,                         // number of mips
        0,                                      // base array layer
        desc.ArraySize,                          // number of array layers
        true,                                    // use identity swizzle)
        usage
    );
#ifdef DEBUG_INFO
    std::string debugName;
    if (Desc.DebugName) {
        debugName = Desc.DebugName;
    }
    else {
        char buf[64];
        snprintf(buf, sizeof(buf), "VulkanTexture:0x%llx", (unsigned long long)Image);
        debugName = buf;
    }
    VKFunc::SetDebugName(vkDevice, VK_OBJECT_TYPE_IMAGE, (uint64_t)Image, debugName.c_str());
#endif
}

VulkanTexture::~VulkanTexture() {
    VkDevice device = Device->GetHandle();
    VulkanMemoryManager* memoryManager = Device->GetMemoryManager();

	DefaltView.Destroy(Device);

    if (Image != VK_NULL_HANDLE && owner) {
        Device->EnqueueImageForDeletion(Image);
    }
    for (auto view : views) {
        view->Invalidate();
    }
    if (!ExternalAllocated)
    {
        memoryManager->Free(Allocation);
    }
}

void VulkanTexture::CreateDefaultView()
{
    auto usage = TransformTextureUsageFlagsFrom(Desc.Usage);
    // Create VkImageView
    bool viewSuccess = DefaltView.Create(
        Device,                                // fvulkan_device& device
        Image,                                 // VkImage
        TransformViewTypeFrom(Desc.Type),      // VkImageViewType
        GetImageAspectFlags(TransformFormatFrom(Desc.Format)), // VkImageAspectFlags
        TransformFormatFrom(Desc.Format),       // VkFormat
        0,                                      // first mip level
        Desc.MipLevels,                         // number of mips
        0,                                      // base array layer
        Desc.ArraySize,                          // number of array layers
        true,                                   // use identity swizzle
        usage
    );
    VulkanMemoryManager* memoryManager = Device->GetMemoryManager();
    if (!viewSuccess) {
        Image = VK_NULL_HANDLE;
        VKFunc::DestroyImage(Device->GetHandle(), Image);
        if (!ExternalAllocated)
        {
            memoryManager->Free(Allocation);
        }
    }
}

void VulkanTexture::AttachView(VulkanViewBase* view)
{
    views.push_back(view);
}

void VulkanTexture::DetachView(VulkanViewBase* view)
{
    auto it = std::find(views.begin(), views.end(), view);
    if (it != views.end())
    {
        views.erase(it);
    }
}

void VulkanTexture::DetermineDefaultLayout(ERHITextureCreateFlags Flags, VkImageLayout& OutLayout, ERHIResourceAccess& OutAccess)
{
    // --- 0. 初始默认值 ---
    OutLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    OutAccess = ERHIResourceAccess::Unknown;

    // --- 1. 优先级最高：呈现 (Swapchain) ---
    // Swapchain 图像在 Vulkan 中非常特殊，必须处于 PRESENT 布局才能交给窗口系统
    if (EnumHasAnyFlags(Flags, ERHITextureCreateFlag::Presentable))
    {
        OutLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        OutAccess = ERHIResourceAccess::Present;
        return;
    }

    // --- 2. 手机端优化：Memoryless (Tile Memory Only) ---
    // Memoryless 通常只用于 Transient 的 Depth 或 Color Attachment
    // 它们不需要从显存 Load，也不需要 Store 到显存，因此初始状态必须是 Attachment
    if (EnumHasAnyFlags(Flags, ERHITextureCreateFlag::Memoryless))
    {
        if (EnumHasAnyFlags(Flags, ERHITextureCreateFlag::DepthStencil))
        {
            OutLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            OutAccess = ERHIResourceAccess::DSVWrite;
        }
        else
        {
            OutLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            OutAccess = ERHIResourceAccess::RenderTargetView;
        }
        return;
    }

    // --- 3. 读写冲突处理：UAV (Storage Image) ---
    // 如果一个资源被标记为 UAV，无论它是否也是 ShaderResource，
    // 在初始状态下为了通用性，通常映射到 GENERAL 布局
    if (EnumHasAnyFlags(Flags, ERHITextureCreateFlag::UAV))
    {
        OutLayout = VK_IMAGE_LAYOUT_GENERAL;
        OutAccess = ERHIResourceAccess::UAV;
        return;
    }

    // --- 4. CPU 回读 (Readback) ---
    // CPU Readback 资源通常由底层 Buffer 支撑或者是线性布局的 Image
    // 在 Vulkan 中，这类需要映射内存读取的资源建议使用 GENERAL
    if (EnumHasAnyFlags(Flags, ERHITextureCreateFlag::CPUReadback))
    {
        OutLayout = VK_IMAGE_LAYOUT_GENERAL;
        OutAccess = ERHIResourceAccess::CPURead;
        return;
    }

    // --- 5. 标准附件路径 (RenderTarget / DepthStencil) ---
    if (EnumHasAnyFlags(Flags, ERHITextureCreateFlag::DepthStencil))
    {
        OutLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        OutAccess = ERHIResourceAccess::DSVWrite;
        return;
    }

    if (EnumHasAnyFlags(Flags, ERHITextureCreateFlag::RenderTarget))
    {
        OutLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        OutAccess = ERHIResourceAccess::RenderTargetView;
        return;
    }

    // --- 6. 拷贝路径 ---
    // 如果资源的主要意图是作为拷贝目标（例如上传贴图数据）
    if (EnumHasAnyFlags(Flags, ERHITextureCreateFlag::TransferDest))
    {
        OutLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        OutAccess = ERHIResourceAccess::TransferDest;
        return;
    }

    // --- 7. 只读贴图路径 (SRV) ---
    if (EnumHasAnyFlags(Flags, ERHITextureCreateFlag::ShaderResource))
    {
        // 如果是 Dynamic 的，可能意味着它会频繁更新
        // 但初始状态下依然建议进入只读，等待第一次 RHIUpdate 触发转换
        OutLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        OutAccess = ERHIResourceAccess::SRV;
        return;
    }

    // --- 8. 拷贝源 ---
    if (EnumHasAnyFlags(Flags, ERHITextureCreateFlag::TransferSrc))
    {
        OutLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        OutAccess = ERHIResourceAccess::TransferSrc;
        return;
    }

    // --- 9. 兜底：Undefined ---
    // 没有任何明确用途标志时，保持 Undefined
    OutLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    OutAccess = ERHIResourceAccess::Unknown;
}

// Vulkan Buffer
VulkanBuffer::VulkanBuffer(VulkanDevice* device, const RHIBufferDesc& desc, bool externalAllocated)
    : RHIBuffer(desc), Device(device),ExternalAllocated(externalAllocated) {
    VkDevice vkDevice = Device->GetHandle();


    // Create VkBuffer
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    Size = Desc.Size;
    bufferInfo.size = Desc.Size;
	auto usageFlags = TransformBufferUsageFlagsFrom(Desc.Usage);
    bufferInfo.usage = usageFlags; // TODO: Support other usages
    std::vector<uint32_t> queueFamilyIndices;
    VulkanQueue* initialQueue = ResolveInitialQueue(Device, desc.InitialQueueType);
    queueFamilyIndices.push_back(initialQueue ? initialQueue->GetFamilyIndex() : Device->GetGraphicsQueue()->GetFamilyIndex());
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    bufferInfo.queueFamilyIndexCount = static_cast<uint32_t>(queueFamilyIndices.size());
    bufferInfo.pQueueFamilyIndices = queueFamilyIndices.data();

    if (VKFunc::CreateBuffer(vkDevice, &bufferInfo, &Buffer) != true) {
    }

    VulkanMemoryManager* memoryManager = Device->GetMemoryManager();
    // Get memory requirements
    if (!externalAllocated) {
        VkMemoryRequirements memRequirements;
        VKFunc::GetBufferMemoryRequirements(vkDevice, Buffer, &memRequirements);
        // Allocate memory
        if (!memoryManager->Allocate(memRequirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, Allocation)) {
            VKFunc::DestroyBuffer(vkDevice, Buffer);
        }

        // Bind memory to buffer
        VKFunc::BindBufferMemory(vkDevice, Buffer, Allocation.GetMemory(), Allocation.GetOffset());

    }

#ifdef DEBUG_INFO
    std::string debugName;
    if (Desc.DebugName) {
        debugName = Desc.DebugName;
    } else {
        char buf[64];
        snprintf(buf, sizeof(buf), "VulkanBuffer:0x%llx", (unsigned long long)Buffer);
        debugName = buf;
    }
    VKFunc::SetDebugName(vkDevice, VK_OBJECT_TYPE_BUFFER, (uint64_t)Buffer, debugName.c_str());
#endif
}

VulkanBuffer::~VulkanBuffer() {
    VkDevice device = Device->GetHandle();
    VulkanMemoryManager* memoryManager = Device->GetMemoryManager();

    if (Buffer != VK_NULL_HANDLE) {
        Device->EnqueueBufferForDeletion(Buffer);
    }
    for (auto view : views) {
        view->Invalidate();
    }
    if (!ExternalAllocated) {
        memoryManager->Free(Allocation);
    }

}

void VulkanBuffer::AttachView(VulkanViewBase* view)
{
    views.push_back(view);
}

void VulkanBuffer::DetachView(VulkanViewBase* view)
{
    auto it = std::find(views.begin(), views.end(), view);
    if (it != views.end())
    {
        views.erase(it);
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

VulkanRayTracingGeometry::VulkanRayTracingGeometry(VulkanDevice* device, const RHIRayTracingGeometryDesc& Desc) : Device(device)
{
    SetGeometryDesc(Desc);
}

VulkanRayTracingGeometry::~VulkanRayTracingGeometry()
{
    DestroyAccelerationStructure();
}

void VulkanRayTracingGeometry::DestroyAccelerationStructure()
{
    if (Device && AccelerationStructure != VK_NULL_HANDLE)
    {
        VKFunc::DestroyAccelerationStructureKHR(Device->GetHandle(), AccelerationStructure);
        AccelerationStructure = VK_NULL_HANDLE;
        DeviceAddress = 0;
    }
    DestroyStorageBuffer();
}

void VulkanRayTracingGeometry::DestroyStorageBuffer()
{
    if (!Device)
    {
        return;
    }

    if (StorageBuffer != VK_NULL_HANDLE)
    {
        Device->EnqueueBufferForDeletion(StorageBuffer);
        StorageBuffer = VK_NULL_HANDLE;
    }

    if (StorageAllocation.GetMemory() != VK_NULL_HANDLE)
    {
        Device->GetMemoryManager()->Free(StorageAllocation);
        StorageAllocation = VulkanAllocation{};
    }
}

void VulkanRayTracingGeometry::SetGeometryDesc(const RHIRayTracingGeometryDesc& Desc)
{
    GeometryDesc = Desc;
    DestroyAccelerationStructure();

    if (!Device || !GeometryDesc.VertexBuffer || !GeometryDesc.IndexBuffer)
    {
        return;
    }

    VulkanBuffer* vertexBuffer = static_cast<VulkanBuffer*>(GeometryDesc.VertexBuffer);
    VulkanBuffer* indexBuffer = static_cast<VulkanBuffer*>(GeometryDesc.IndexBuffer);
    if (!vertexBuffer || !indexBuffer || GeometryDesc.IndexCount < 3 || GeometryDesc.VertexCount == 0)
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
    trianglesData.vertexData.deviceAddress = VKFunc::GetBufferDeviceAddress(Device->GetHandle(), &vertexAddressInfo);
    trianglesData.vertexStride = GeometryDesc.VertexStride;
    trianglesData.maxVertex = GeometryDesc.VertexCount - 1;
    trianglesData.indexType = indexBuffer->GetDesc().Stride == 4 ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16;
    trianglesData.indexData.deviceAddress = VKFunc::GetBufferDeviceAddress(Device->GetHandle(), &indexAddressInfo);
    trianglesData.transformData.deviceAddress = 0;

    GeometryInfo = {};
    GeometryInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    GeometryInfo.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    GeometryInfo.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    GeometryInfo.geometry.triangles = trianglesData;

    const uint32_t primitiveCount = GeometryDesc.IndexCount / 3;
    BuildRangeInfo = {};
    BuildRangeInfo.primitiveCount = primitiveCount;
    BuildRangeInfo.primitiveOffset = 0;
    BuildRangeInfo.firstVertex = 0;
    BuildRangeInfo.transformOffset = 0;

    VkBuildAccelerationStructureFlagsKHR buildFlags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    if (GeometryDesc.AllowUpdate)
    {
        buildFlags |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
    }
    if (GeometryDesc.AllowCompaction)
    {
        buildFlags |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR;
    }

    VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
    buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    buildInfo.flags = buildFlags;
    BuildFlags = buildFlags;
    buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries = &GeometryInfo;

    VkAccelerationStructureBuildSizesInfoKHR sizeInfo{};
    sizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    VKFunc::GetAccelerationStructureBuildSizesKHR(
        Device->GetHandle(),
        VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &buildInfo,
        &primitiveCount,
        &sizeInfo);

    SizeInfo.ResultSize = sizeInfo.accelerationStructureSize;
    SizeInfo.BuildScratchSize = sizeInfo.buildScratchSize;
    SizeInfo.UpdateScratchSize = sizeInfo.updateScratchSize;

    if (!CreateRawBuffer(
        Device,
        SizeInfo.ResultSize,
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        StorageBuffer,
        StorageAllocation))
    {
        return;
    }

    VkAccelerationStructureCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    createInfo.buffer = StorageBuffer;
    createInfo.offset = 0;
    createInfo.size = SizeInfo.ResultSize;
    createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;

    if (!VKFunc::CreateAccelerationStructureKHR(Device->GetHandle(), &createInfo, &AccelerationStructure))
    {
        DestroyStorageBuffer();
        return;
    }

    VkAccelerationStructureDeviceAddressInfoKHR addressInfo{};
    addressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    addressInfo.accelerationStructure = AccelerationStructure;
    DeviceAddress = VKFunc::GetAccelerationStructureDeviceAddressKHR(Device->GetHandle(), &addressInfo);
}


VulkanRayTracingInstance::VulkanRayTracingInstance(VulkanDevice* device, const RHIRayTracingInstancesDesc& Instances) : Device(device)
{
    SetInstancesDesc(Instances);
}

VulkanRayTracingInstance::~VulkanRayTracingInstance()
{
    DestroyAccelerationStructure();
}

void VulkanRayTracingInstance::DestroyAccelerationStructure()
{
    if (Device && AccelerationStructure != VK_NULL_HANDLE)
    {
        VKFunc::DestroyAccelerationStructureKHR(Device->GetHandle(), AccelerationStructure);
        AccelerationStructure = VK_NULL_HANDLE;
        DeviceAddress = 0;
    }
    DestroyInstanceBuffer();
    DestroyStorageBuffer();
}

void VulkanRayTracingInstance::DestroyStorageBuffer()
{
    if (!Device)
    {
        return;
    }

    if (StorageBuffer != VK_NULL_HANDLE)
    {
        Device->EnqueueBufferForDeletion(StorageBuffer);
        StorageBuffer = VK_NULL_HANDLE;
    }

    if (StorageAllocation.GetMemory() != VK_NULL_HANDLE)
    {
        Device->GetMemoryManager()->Free(StorageAllocation);
        StorageAllocation = VulkanAllocation{};
    }
}

void VulkanRayTracingInstance::DestroyInstanceBuffer()
{
    if (!Device)
    {
        return;
    }

    if (InstanceBuffer != VK_NULL_HANDLE)
    {
        Device->EnqueueBufferForDeletion(InstanceBuffer);
        InstanceBuffer = VK_NULL_HANDLE;
    }

    if (InstanceAllocation.GetMemory() != VK_NULL_HANDLE)
    {
        Device->GetMemoryManager()->Free(InstanceAllocation);
        InstanceAllocation = VulkanAllocation{};
    }
}

void VulkanRayTracingInstance::SetInstancesDesc(const RHIRayTracingInstancesDesc& Desc)
{

    this->Instances = Desc;
    const auto& Instances = Desc.Instances;
    DestroyAccelerationStructure();

    if (!Device || Instances.empty())
    {
        return;
    }

    std::vector<VkAccelerationStructureInstanceKHR> vkInstances;
    vkInstances.reserve(Instances.size());

    for (const RHIRayTracingInstanceDesc& instance : Instances)
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
        Device,
        instanceBufferSize,
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        InstanceBuffer,
        InstanceAllocation))
    {
        return;
    }


    

    if (!InstanceAllocation.GetMappedPointer())
        throw std::runtime_error("The TLAS instance upload buffer is not mapped");
    memcpy(InstanceAllocation.GetMappedPointer(), vkInstances.data(), static_cast<size_t>(instanceBufferSize));

    VkBufferDeviceAddressInfo instanceAddressInfo{};
    instanceAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    instanceAddressInfo.buffer = InstanceBuffer;

    GeometryInfo = {};
    GeometryInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    GeometryInfo.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    GeometryInfo.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    GeometryInfo.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    GeometryInfo.geometry.instances.arrayOfPointers = VK_FALSE;
    GeometryInfo.geometry.instances.data.deviceAddress = VKFunc::GetBufferDeviceAddress(Device->GetHandle(), &instanceAddressInfo);

    const uint32_t primitiveCount = static_cast<uint32_t>(Instances.size());
    BuildRangeInfo = {};
    BuildRangeInfo.primitiveCount = primitiveCount;
    BuildRangeInfo.primitiveOffset = 0;
    BuildRangeInfo.firstVertex = 0;
    BuildRangeInfo.transformOffset = 0;

    VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
    buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
    buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries = &GeometryInfo;

    VkAccelerationStructureBuildSizesInfoKHR sizeInfo{};
    sizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    VKFunc::GetAccelerationStructureBuildSizesKHR(
        Device->GetHandle(),
        VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &buildInfo,
        &primitiveCount,
        &sizeInfo);

    SizeInfo.ResultSize = sizeInfo.accelerationStructureSize;
    SizeInfo.BuildScratchSize = sizeInfo.buildScratchSize;
    SizeInfo.UpdateScratchSize = sizeInfo.updateScratchSize;

    if (!CreateRawBuffer(
        Device,
        SizeInfo.ResultSize,
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        StorageBuffer,
        StorageAllocation))
    {
        DestroyInstanceBuffer();
        return;
    }

    VkAccelerationStructureCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    createInfo.buffer = StorageBuffer;
    createInfo.offset = 0;
    createInfo.size = SizeInfo.ResultSize;
    createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;

    if (!VKFunc::CreateAccelerationStructureKHR(Device->GetHandle(), &createInfo, &AccelerationStructure))
    {
        DestroyInstanceBuffer();
        DestroyStorageBuffer();
        return;
    }

    VkAccelerationStructureDeviceAddressInfoKHR addressInfo{};
    addressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    addressInfo.accelerationStructure = AccelerationStructure;
    DeviceAddress = VKFunc::GetAccelerationStructureDeviceAddressKHR(Device->GetHandle(), &addressInfo);
}

VulkanShaderResourceView::VulkanShaderResourceView(VulkanDevice* device,
    RHIViewableResource* Resource,
    const RHITexSRVCreateInfo& SRVInfo)
    : RHIShaderResourceView(Resource)
{
    ResourcePtr = Resource;
    Device = device;
    ResourceType = EResourceType::Texture;

    CreateTextureView(SRVInfo);

    // Attach to resource
    if (auto* vulkanResource = dynamic_cast<VulkanTexture*>(ResourcePtr))
    {
        vulkanResource->AttachView(this);
    }
    else if (auto* vulkanResource = dynamic_cast<VulkanBuffer*>(ResourcePtr))
    {
        vulkanResource->AttachView(this);
    }
}

VulkanShaderResourceView::VulkanShaderResourceView(
    VulkanDevice* device,
    RHIViewableResource* Resource,
    const RHIBufferSRVCreateInfo& SRVInfo)
    : RHIShaderResourceView(Resource)
{
    ResourcePtr = Resource;
    Device = device;
    ResourceType = EResourceType::Buffer;

    CreateBufferView(SRVInfo);

    // Attach to resource
    if (auto* vulkanResource = dynamic_cast<VulkanBuffer*>(ResourcePtr))
    {
        vulkanResource->AttachView(this);
    }
}

void VulkanShaderResourceView::CreateTextureView(
    const RHITexSRVCreateInfo& SRVInfo)
{
    auto* texture = static_cast<VulkanTexture*>(ResourcePtr);

    Format = (SRVInfo.Format == ERHIFormat::Unknown)
        ? texture->GetFormat()
        : TransformFormatFrom(SRVInfo.Format);

    BaseMipLevel = SRVInfo.FirstMipSlice;
    MipLevelCount = SRVInfo.MipCount;
    BaseArrayLayer = SRVInfo.FirstArraySlice;
    LayerCount = SRVInfo.ArraySize;

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = texture->GetImage();
    VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_2D;
    if (SRVInfo.ViewType == ERHITextureViewType::Derived) {
        viewType = TransformViewTypeFrom(texture->GetDesc().Type);
    }
    else {
        viewType = TransformViewTypeFrom(SRVInfo.ViewType);
    }
    viewInfo.viewType = viewType;

    viewInfo.format = Format;

    viewInfo.subresourceRange.aspectMask = GetImageAspectFlags(Format);
    viewInfo.subresourceRange.baseMipLevel = BaseMipLevel;
    viewInfo.subresourceRange.levelCount = MipLevelCount;
    viewInfo.subresourceRange.baseArrayLayer = BaseArrayLayer;
    viewInfo.subresourceRange.layerCount = LayerCount;

    // 使用 VulkanTextureView 结构体创建
    TextureView.Create(Device, texture->GetImage(), viewInfo.viewType, 
                      viewInfo.subresourceRange.aspectMask, Format,
                      BaseMipLevel, MipLevelCount, BaseArrayLayer, LayerCount);

    DescriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
}

void VulkanShaderResourceView::CreateBufferView(
    const RHIBufferSRVCreateInfo& SRVInfo)
{
    auto* buffer = static_cast<VulkanBuffer*>(ResourcePtr);

    Format = TransformFormatFrom(SRVInfo.Format);

    // 只有当 Buffer 的 usage 包含 Texel 标志时才创建 BufferView
    if (EnumHasAnyFlags(buffer->GetDesc().Usage, ERHIBufferUsageFlag::Texel))
    {
        VkDeviceSize range = (SRVInfo.NumElements * SRVInfo.Stride);
        BufferView.Create(Device, buffer->GetHandle(), Format, SRVInfo.Offset, range);
        DescriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    }
    else
    {
		BufferView.Buffer = buffer->GetHandle();
        BufferView.Offset = SRVInfo.Offset;
        BufferView.Size = (SRVInfo.NumElements * SRVInfo.Stride);
        // 如果没有 Texel 标志，默认使用 STORAGE_BUFFER 类型
        DescriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    }
}

VulkanShaderResourceView::~VulkanShaderResourceView()
{
    if (IsValid) 
    {
        // Detach from resource
        if (auto* vulkanResource = dynamic_cast<VulkanTexture*>(ResourcePtr))
        {
            vulkanResource->DetachView(this);
        }
        else if (auto* vulkanResource = dynamic_cast<VulkanBuffer*>(ResourcePtr))
        {
            vulkanResource->DetachView(this);
        }
    }
    DestroyView();
}

const VulkanTextureView& VulkanShaderResourceView::GetTextureView() const
{
    return TextureView;
}

const VulkanBufferView& VulkanShaderResourceView::GetBufferView() const
{
    return BufferView;
}

void VulkanShaderResourceView::Invalidate()
{
    // 销毁 TextureView
    if (TextureView.View != VK_NULL_HANDLE)
    {
        TextureView.Destroy(Device);
    }

    // 销毁 BufferView
    if (BufferView.View != VK_NULL_HANDLE)
    {
        BufferView.Destroy(Device);
    }
    IsValid = false;
}

void VulkanShaderResourceView::DestroyView()
{
    Invalidate();
}

VulkanUnorderedAccessView::VulkanUnorderedAccessView(
    VulkanDevice* device,
    RHIViewableResource* Resource,
    const RHITexUAVCreateInfo& UAVInfo)
    : RHIUnorderedAccessView(Resource)
{
    ResourcePtr = Resource;
    Device = device;
    ResourceType = EResourceType::Texture;

    CreateTextureView(UAVInfo);

    // Attach to resource
    if (auto* vulkanResource = dynamic_cast<VulkanTexture*>(ResourcePtr))
    {
        vulkanResource->AttachView(this);
    }
}

VulkanUnorderedAccessView::VulkanUnorderedAccessView(
    VulkanDevice* device,
    RHIViewableResource* Resource,
    const RHIBufferUAVCreateInfo& UAVInfo)
    : RHIUnorderedAccessView(Resource)
{
    ResourcePtr = Resource;
    Device = device;
    ResourceType = EResourceType::Buffer;

    CreateBufferView(UAVInfo);

    // Attach to resource
    if (auto* vulkanResource = dynamic_cast<VulkanBuffer*>(ResourcePtr))
    {
        vulkanResource->AttachView(this);
    }
}


void VulkanUnorderedAccessView::CreateTextureView(
    const RHITexUAVCreateInfo& UAVInfo)
{
    auto* texture = static_cast<VulkanTexture*>(ResourcePtr);

    Format = (UAVInfo.Format == ERHIFormat::Unknown)
        ? texture->GetFormat()
        : TransformFormatFrom(UAVInfo.Format);

    BaseMipLevel = UAVInfo.FirstMipSlice;
    MipLevelCount = UAVInfo.MipCount;
    BaseArrayLayer = UAVInfo.FirstArraySlice;
    LayerCount = UAVInfo.ArraySize;

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = texture->GetImage();
	VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_2D;
    if (UAVInfo.ViewType == ERHITextureViewType::Derived) {
		viewType = TransformViewTypeFrom(texture->GetDesc().Type);
    }
    else {
        viewType = TransformViewTypeFrom(UAVInfo.ViewType);
    }
    viewInfo.viewType = viewType;

    viewInfo.format = Format;

    viewInfo.subresourceRange.aspectMask = GetImageAspectFlags(Format);
    viewInfo.subresourceRange.baseMipLevel = BaseMipLevel;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = BaseArrayLayer;
    viewInfo.subresourceRange.layerCount = LayerCount;

    // 使用 VulkanTextureView 结构体创建
    TextureView.Create(Device, texture->GetImage(), viewInfo.viewType,
                      viewInfo.subresourceRange.aspectMask, Format,
                      BaseMipLevel, MipLevelCount, BaseArrayLayer, LayerCount);

    DescriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
}

void VulkanUnorderedAccessView::CreateBufferView(
    const RHIBufferUAVCreateInfo& UAVInfo)
{
    auto* buffer = static_cast<VulkanBuffer*>(ResourcePtr);

    Format = TransformFormatFrom(UAVInfo.Format);

    // 只有当 Buffer 的 usage 包含 Texel 标志时才创建 BufferView
    if (EnumHasAnyFlags(buffer->GetDesc().Usage, ERHIBufferUsageFlag::Texel))
    {
        VkDeviceSize range = (UAVInfo.NumElements * UAVInfo.Stride);
        BufferView.Create(Device, buffer->GetHandle(), Format, UAVInfo.Offset, range);
        DescriptorType = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
    }
    else
    {
        BufferView.Buffer = buffer->GetHandle();
        BufferView.Offset = UAVInfo.Offset;
        BufferView.Size = (UAVInfo.NumElements * UAVInfo.Stride);
        // 如果没有 Texel 标志，默认使用 STORAGE_BUFFER 类型
        DescriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    }
}

VulkanUnorderedAccessView::~VulkanUnorderedAccessView()
{
    if (IsValid)
    {
        // Detach from resource
        if (auto* vulkanResource = dynamic_cast<VulkanTexture*>(ResourcePtr))
        {
            vulkanResource->DetachView(this);
        }
        else if (auto* vulkanResource = dynamic_cast<VulkanBuffer*>(ResourcePtr))
        {
            vulkanResource->DetachView(this);
        }
    }

    DestroyView();
}


const VulkanTextureView& VulkanUnorderedAccessView::GetTextureView() const
{
    return TextureView;
}

const VulkanBufferView& VulkanUnorderedAccessView::GetBufferView() const
{
    return BufferView;
}

void VulkanUnorderedAccessView::Invalidate()
{
    // 销毁 TextureView
    if (TextureView.View != VK_NULL_HANDLE)
    {
        TextureView.Destroy(Device);
    }

    // 销毁 BufferView
    if (BufferView.View != VK_NULL_HANDLE)
    {
        BufferView.Destroy(Device);
    }
    IsValid = false;
}

void VulkanUnorderedAccessView::DestroyView()
{
    Invalidate();
}



// Vulkan RHISwapchain
VulkanRHISwapchain::VulkanRHISwapchain(VulkanDevice* device, uint32_t width, uint32_t height, void* windowHandle, ERHIFormat format)
    : Device(device), WindowHandle(windowHandle),Width(width),Height(height),Format(format)
{


    if (!WindowHandle)
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
    VkDevice device = Device->GetHandle();

    DestroySwapchain();


}

void VulkanRHISwapchain::Present(VulkanQueue* presentQueue, const RHI::RHIWaitInfo& waitInfo)
{
    if (currentBackBufferIndex == -1)
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
            rhivkSyncPoint = Device->GetGraphicsQueue()->GetSyncPoint();
            break;
        case EQueueType::Compute:
            rhivkSyncPoint = Device->GetComputeQueue()->GetSyncPoint();
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
    Swapchain->Present(presentQueue, binaryWaitHandle);
    currentIndex++;
    currentIndex %= swapchainImages_.size();

    // 3. 更新索引和状态
    // ... 保持原有的索引更新逻辑 ...
    currentBackBufferIndex = -1;
}

RHISwapchain::RHISwapchainSlot VulkanRHISwapchain::AcquireNextSlot()
{
    RHISwapchain::RHISwapchainSlot slot{};

    // 1. 原有的获取 BackBuffer 逻辑 (内部调用 vkAcquireNextImageKHR)
    // 假设这个函数会更新 currentSemaphoreIndex 并触发 acquireSemaphores[i]
    auto backTexture = GetBackTexture();
    slot.Texture = backTexture.get();

    if (slot.Texture && currentBackBufferIndex >= 0)
    {
        // 4. 返回给 Slot
        // 注意：在随后的 FlushContext 中，这个 Dependency 会进入 WaitInfos
        slot.ReadySync = acquireSemaphores[currentIndex];
    }
    else
    {
        slot.ReadySync = nullptr;
    }

    return slot;
}

void VulkanRHISwapchain::Resize(uint32_t inWidth, uint32_t inHeight)
{
    Width = inWidth;
    Height = inHeight;
    DestroySwapchain();
    CreateSwapchain();

}

VulkanTextureSP VulkanRHISwapchain::GetBackTexture()
{
    if (currentBackBufferIndex != -1) {
        return backBufferTextures[currentBackBufferIndex];
    }
    auto seamphore = Device->GetSemaphoreManager()->AcquireBinary();
    acquireSemaphores[currentIndex]->SetSemaphore(seamphore);
    Swapchain->AcquireNextImage(acquireSemaphores[currentIndex]->GetSemaphore(), &currentBackBufferIndex);
    if (currentBackBufferIndex == -1) {
        return nullptr;
    }
    return backBufferTextures[currentBackBufferIndex];
}

void VulkanRHISwapchain::CreateSwapchain()
{
    swapchainImages_.clear();
    backBufferTextures.clear();
    acquireSemaphores.clear();

    VulkanSwapchain::SwapchainDesc swapchainDesc = {};
    swapchainDesc.windowHandle = WindowHandle;
    swapchainDesc.width = Width;
    swapchainDesc.height = Height;
    swapchainDesc.preferredFormat = Format;
    Swapchain = new VulkanSwapchain(Device, swapchainDesc, swapchainImages_);
    Width = Swapchain->GetExtent().width;
    Height = Swapchain->GetExtent().height;

    RHITextureDesc textureDesc;
    textureDesc.Width = Width;                  // 纹理宽度
    textureDesc.Height = Height;                 // 纹理高度
    textureDesc.Depth = 1;                  // 纹理深度（3D纹理）
    textureDesc.MipLevels = 1;              // Mip层级数量
    textureDesc.ArraySize = 1;              // 数组大小
    textureDesc.Format = Format; // 像素格式

    ERHITextureType Type = ERHITextureType::Texture2D;   // 纹理类型
    uint32_t SampleCount = 1;            // 多重采样数量
    uint32_t SampleQuality = 0;          // 多重采样质量
    textureDesc.Usage = ERHITextureCreateFlag::RenderTarget | ERHITextureCreateFlag::Presentable | ERHITextureCreateFlag::TransferDest | ERHITextureCreateFlag::TransferSrc; // 纹理用途
    auto imageCount = swapchainImages_.size();
    for (int i = 0; i < imageCount; i++) {
        auto name = std::string("BackBuffer") + std::to_string(i);
        textureDesc.DebugName = name.c_str(); // 调试名称
        auto texture = std::make_shared<VulkanTexture>(Device, textureDesc, swapchainImages_[i]);
        backBufferTextures.push_back(texture);
    }
    acquireSemaphores.resize(swapchainImages_.size());
    for (int i = 0; i < swapchainImages_.size(); i++) {
        auto semaphore = Device->GetSemaphoreManager()->AcquireBinary();
        acquireSemaphores[i] = new VulkanRHISyncPoint(Device, Device->GetPresentQueue()->GetType(), semaphore);
    }

}

void VulkanRHISwapchain::DestroySwapchain()
{
    delete Swapchain;
    Swapchain = nullptr;
    backBufferTextures.clear();
    for (auto* semaphore : acquireSemaphores) {
        delete semaphore;
    }
    acquireSemaphores.clear();
    currentBackBufferIndex = -1;
    currentIndex = 0;

}


VulkanVertexDescState::VulkanVertexDescState(VulkanDevice* device, const RHIVertexDescStateDesc& desc): RHIVertexDescState(desc), Device(device)
{
    // Initialize vertex input state
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(desc.bindings.size());
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(desc.attributes.size());
    bindingDescriptions.resize(desc.bindings.size());
    attributeDescriptions.resize(desc.attributes.size());
    for (size_t i = 0; i < desc.bindings.size(); ++i) {
        const auto& bindingDesc = desc.bindings[i];
        bindingDescriptions[i].binding = bindingDesc.binding;
        bindingDescriptions[i].stride = bindingDesc.stride;
        bindingDescriptions[i].inputRate = (bindingDesc.inputRate == ERHIInputRate::PerVertex) ? VK_VERTEX_INPUT_RATE_VERTEX : VK_VERTEX_INPUT_RATE_INSTANCE;
    }
    for (size_t i = 0; i < desc.attributes.size(); ++i) {
        const auto& attributeDesc = desc.attributes[i];
        attributeDescriptions[i].location = attributeDesc.location;
        attributeDescriptions[i].binding = attributeDesc.binding;  // 必须
        attributeDescriptions[i].format = TransformFormatFrom(attributeDesc.format);
        attributeDescriptions[i].offset = attributeDesc.offset;   // 必须
    }
    vertexInputInfo.pVertexBindingDescriptions = bindingDescriptions.data();
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

}
VulkanVertexDescState::~VulkanVertexDescState()
{

}


// ----------------------------
// 光栅化状态 Vulkan派生
// ----------------------------
VulkanRasterizerState::VulkanRasterizerState(VulkanDevice* device, const RHIRasterizerStateDesc& desc) : RHIRasterizerState(desc), Device(device)
{
    VkDevice vkDevice = Device->GetHandle();
    rasterizerInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizerInfo.depthClampEnable = desc.depthClampEnable;
    rasterizerInfo.rasterizerDiscardEnable = desc.rasterizerDiscardEnable;

    switch (desc.polygonMode) {
    case ERHIPolygonMode::Fill:
        rasterizerInfo.polygonMode = VK_POLYGON_MODE_FILL;
        break;
    case ERHIPolygonMode::Line:
        rasterizerInfo.polygonMode = VK_POLYGON_MODE_LINE;
        break;
    case ERHIPolygonMode::Point:
        rasterizerInfo.polygonMode = VK_POLYGON_MODE_POINT;
        break;
    default:
        rasterizerInfo.polygonMode = VK_POLYGON_MODE_FILL;
        break;
    }

    switch (desc.cullMode) {
    case ERHICullMode::None:
        rasterizerInfo.cullMode = VK_CULL_MODE_NONE;
        break;
    case ERHICullMode::Front:
        rasterizerInfo.cullMode = VK_CULL_MODE_FRONT_BIT;
        break;
    case ERHICullMode::Back:
        rasterizerInfo.cullMode = VK_CULL_MODE_BACK_BIT;
        break;
    case ERHICullMode::FrontAndBack:
        rasterizerInfo.cullMode = VK_CULL_MODE_FRONT_AND_BACK;
        break;
    default:
        rasterizerInfo.cullMode = VK_CULL_MODE_BACK_BIT;
        break;
    }

    switch (desc.frontFace) {
    case ERHIFrontFace::CounterClockwise:
        rasterizerInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        break;
    case ERHIFrontFace::Clockwise:
        rasterizerInfo.frontFace = VK_FRONT_FACE_CLOCKWISE;
        break;
    default:
        rasterizerInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        break;
    }

    rasterizerInfo.depthBiasEnable = desc.depthBiasEnable;
    rasterizerInfo.depthBiasConstantFactor = desc.depthBiasConstantFactor;
    rasterizerInfo.depthBiasClamp = desc.depthBiasClamp;
    rasterizerInfo.depthBiasSlopeFactor = desc.depthBiasSlopeFactor;
    rasterizerInfo.lineWidth = desc.lineWidth;
}

VulkanRasterizerState::~VulkanRasterizerState()
{
}

// ----------------------------
// 颜色混合状态 Vulkan派生
// ----------------------------
VulkanColorBlendState::VulkanColorBlendState(VulkanDevice* device, const RHIColorBlendStateDesc& desc) : RHIColorBlendState(desc), Device(device)
{
    VkDevice vkDevice = Device->GetHandle();
    colorBlendInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlendInfo.logicOpEnable = desc.logicOpEnable;
    colorBlendInfo.logicOp = VK_LOGIC_OP_COPY; // TODO: Support other logic ops
    colorBlendInfo.attachmentCount = static_cast<uint32_t>(desc.attachments.size());
    attachmentStates.resize(desc.attachments.size());

    for (size_t i = 0; i < desc.attachments.size(); ++i) {
        auto& attachmentDesc = desc.attachments[i];
        auto& attachmentState = attachmentStates[i];

        attachmentState.blendEnable = attachmentDesc.blendEnable;
        attachmentState.colorWriteMask = attachmentDesc.colorWriteMask; // TODO: Support other write masks
        // TODO: Support other blend factors and operations
    }

    colorBlendInfo.pAttachments = attachmentStates.data();
    colorBlendInfo.blendConstants[0] = desc.blendConstants[0];
    colorBlendInfo.blendConstants[1] = desc.blendConstants[1];
    colorBlendInfo.blendConstants[2] = desc.blendConstants[2];
    colorBlendInfo.blendConstants[3] = desc.blendConstants[3];
}

VulkanColorBlendState::~VulkanColorBlendState()
{
}

// ----------------------------
// 深度模板状态 Vulkan派生
// ----------------------------
VulkanDepthStencilState::VulkanDepthStencilState(VulkanDevice* device, const RHIDepthStencilStateDesc& desc) : RHIDepthStencilState(desc), Device(device)
{
    VkDevice vkDevice = Device->GetHandle();
    depthStencilInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencilInfo.depthTestEnable = desc.depthTestEnable;
    depthStencilInfo.depthWriteEnable = desc.depthWriteEnable;
    // TODO: map compare operation
    depthStencilInfo.depthCompareOp = TransformCompareOp(desc.depthCompareOp);
    depthStencilInfo.depthBoundsTestEnable = VK_FALSE;
    depthStencilInfo.stencilTestEnable = VK_FALSE;
}

VulkanDepthStencilState::~VulkanDepthStencilState()
{
}

// Vulkan Ring Buffer Implementation
VulkanRingBuffer::VulkanRingBuffer(VulkanDevice* device, uint64_t totalSize, VkBufferUsageFlags usage, VkMemoryPropertyFlags memPropertyFlags)
    : Device(device), BufferSize(totalSize), BufferOffset(0), MinAlignment(256) // 默认最小对齐为256字节
{
    VkDevice vkDevice = Device->GetHandle();
    VulkanMemoryManager* memoryManager = Device->GetMemoryManager();

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
    if (!memoryManager->Allocate(memRequirements, memPropertyFlags, Allocation)) {
        LOG_ERROR("Failed to allocate memory for ring buffer");
        VKFunc::DestroyBuffer(vkDevice, buffer);
        return;
    }

    // 绑定内存
    VKFunc::BindBufferMemory(vkDevice, buffer, Allocation.GetMemory(), Allocation.GetOffset());

    // 设置缓冲区句柄到分配对象
    Allocation.SetBufferHandle(buffer);

#ifdef DEBUG_INFO
    char buf[64];
    snprintf(buf, sizeof(buf), "VulkanRingBuffer:0x%llx", (unsigned long long)buffer);
    VKFunc::SetDebugName(vkDevice, VK_OBJECT_TYPE_BUFFER, (uint64_t)buffer, buf);
#endif

}

VulkanRingBuffer::~VulkanRingBuffer()
{
    if (Allocation.GetMemory() != VK_NULL_HANDLE) {
        VkDevice vkDevice = Device->GetHandle();
        VulkanMemoryManager* memoryManager = Device->GetMemoryManager();
        // Memory block mapping is owned by VulkanMemoryBlock.
        // Do not unmap sub-allocations here, otherwise the same VkDeviceMemory
        // may be unmapped multiple times across different resources.

        // 销毁缓冲区
        VkBuffer buffer = Allocation.GetBufferHandle();
        if (buffer != VK_NULL_HANDLE) {
            VKFunc::DestroyBuffer(vkDevice, buffer);
        }

        // 释放内存
        memoryManager->Free(Allocation);
    }
}

uint64_t VulkanRingBuffer::AllocateMemory(uint64_t size, uint32_t alignment)
{
    alignment = alignment > MinAlignment ? alignment : MinAlignment;
    // 简单的对齐计算：向上对齐到alignment的倍数
    uint64_t alignedOffset = (BufferOffset + alignment - 1) & ~(alignment - 1);

    if (alignedOffset + size <= BufferSize) {
        BufferOffset = alignedOffset + size;
        return alignedOffset;
    }

    return WrapAroundAllocateMemory(size, alignment);
}

uint64_t VulkanRingBuffer::WrapAroundAllocateMemory(uint64_t size, uint32_t alignment)
{

    // 重置缓冲区偏移
    BufferOffset = 0;

    // 分配新内存
    uint64_t alignedOffset = (BufferOffset + alignment - 1) & ~(alignment - 1);
    BufferOffset = alignedOffset + size;

    return alignedOffset;
}

// Vulkan Uniform Buffer Uploader Implementation
VulkanLooseUniformDataUploader::VulkanLooseUniformDataUploader(VulkanDevice* device)
{
    // 创建CPU可见的环形缓冲区用于uniform buffer上传
    const uint64_t BufferSize = 1024 * 1024; // 1MB 缓冲区
    CPUBuffer = new VulkanRingBuffer(device, BufferSize,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
}

VulkanLooseUniformDataUploader::~VulkanLooseUniformDataUploader()
{
    delete CPUBuffer;
}


VulkanSampler::VulkanSampler(VulkanDevice* device, const RHISamplerDesc& desc) : RHISampler(desc), Device(device)
{
    VkSamplerCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;

    // =========================================
    // Filtering
    // =========================================

    info.magFilter = TransformFilter(desc.filter);
    info.minFilter = TransformFilter(desc.filter);

    info.mipmapMode = GetMipmapMode(desc.filter);

    // =========================================
    // Address Modes
    // =========================================

    info.addressModeU = TransformAddressMode(desc.addressU);
    info.addressModeV = TransformAddressMode(desc.addressV);
    info.addressModeW = TransformAddressMode(desc.addressW);

    // =========================================
    // LOD
    // =========================================

    info.mipLodBias = desc.mipLodBias;

    info.minLod = desc.minLod;
    info.maxLod = desc.maxLod;

    // =========================================
    // Anisotropy
    // =========================================

    info.anisotropyEnable =
        desc.anisotropyEnable ? VK_TRUE : VK_FALSE;

    info.maxAnisotropy = desc.maxAnisotropy;

    // =========================================
    // Depth Compare
    // =========================================

    info.compareEnable =
        desc.CompareEnable ? VK_TRUE : VK_FALSE;

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


    bool suc = VKFunc::CreateSampler(Device->GetHandle(), &info, &Sampler);
	if (!suc) {
		LOG_ERROR("Failed to create Vulkan sampler");
	}

}

VulkanSampler::~VulkanSampler()
{
    if (Sampler != VK_NULL_HANDLE) {
		Device->EnqueueSamplerForDeletion(Sampler);
    }

}

}
