#include "VulkanDevice.h"
#include <set>
#include <stdexcept>
#include <array>
#include "VulkanRHIApi.h"
#include "VulkanQueue.h"
#include "VulkanMemory.h"
#include "VulkanSync.h"
#include "VulkanFuncWrapper.h"
#include "VulkanCommandContex.h"
#include "VulkanResource.h"
#include "VulkanRenderPass.h"
#include "VulkanDescriptorSets.h"
#include "VulkanShader.h"
namespace RHIVulkan{

VulkanDevice::VulkanDevice(VulkanRHIApi* rhiApi,
                           VkPhysicalDevice physicalDevice)
    : m_RhiApi(rhiApi),
      m_PhysicalDevice(physicalDevice),
      m_Device(VK_NULL_HANDLE),
      m_GraphicsQueueFamilyIndex(UINT32_MAX),
      m_ComputeQueueFamilyIndex(UINT32_MAX),
      m_TransferQueueFamilyIndex(UINT32_MAX),
      m_PresentQueueFamilyIndex(UINT32_MAX),
      m_GraphicsQueue(nullptr),
      m_ComputeQueue(nullptr),
      m_TransferQueue(nullptr),
      m_PresentQueue(nullptr),
      m_MemoryManager(nullptr),
      m_PresentExecutor(nullptr)
{
    m_MemoryManager = new VulkanMemoryManager(this);
	m_StagingManager = new VulkanStagingManager(this, m_MemoryManager);

    m_FenceManager = new VulkanFenceManager(this);
    m_SemaphoreManager = new VulkanSemaphoreManager(this);
    m_RenderPassManager = new VulkanRenderPassManager(this);
    m_DescriptorSetLayoutManager = new VulkanDescriptorSetLayoutManager(this);
    m_DescriptorSetManager = new VulkanDescriptorSetManager(this, m_DescriptorSetLayoutManager);
	m_ShaderManager = new VulkanShaderManager(this);
	m_PipelineLayoutCache = new VulkanPipelineLayoutCache(this);
    m_DeferredDeleteQueue = new VulkanDeferredDeleteQueue(this);
    m_SupportedFeatures2.pNext
		= &m_TimelineSemaphoreFeatures;
	VKFunc::GetPhysicalDeviceFeatures2(m_PhysicalDevice, &m_SupportedFeatures2);
}

VulkanDevice::~VulkanDevice()
{
    Destroy();
}

bool VulkanDevice::Init(const std::vector<const char*>& enabledLayers,
                        const std::vector<const char*>& enabledExtensions)
{
    SelectQueueFamilies(m_PhysicalDevice);
    CreateLogicalDevice(m_PhysicalDevice, enabledExtensions, enabledLayers);
    QueryDeviceProperties();
    m_GlobalCommandContext = new VulkanGraphicContext(this, m_GraphicsQueue);
    return true;
}

void VulkanDevice::QueryDeviceProperties()
{
    // 物理设备的固定限制在初始化时统一查询，供后续创建光追管线和 SBT 复用。
    VkPhysicalDeviceProperties2 properties{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
    properties.pNext = &m_RayTracingPipelineProperties;
    VKFunc::GetPhysicalDeviceProperties2(m_PhysicalDevice, &properties);
}

void VulkanDevice::SelectQueueFamilies(VkPhysicalDevice physicalDevice)
{
    VKFunc::GetPhysicalDeviceQueueFamilyProperties(physicalDevice, &m_QueueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(m_QueueFamilyCount);
    VKFunc::GetPhysicalDeviceQueueFamilyProperties(physicalDevice, &m_QueueFamilyCount, queueFamilies.data());

    // Step 1: Try to find a queue family that supports all operations (Graphics + Compute + Transfer)
    for (uint32_t i = 0; i < m_QueueFamilyCount; ++i)
    {
        const auto& props = queueFamilies[i];
        if ((props.queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
            (props.queueFlags & VK_QUEUE_COMPUTE_BIT) &&
            (props.queueFlags & VK_QUEUE_TRANSFER_BIT))
        {
            m_GraphicsQueueFamilyIndex = i;
            m_ComputeQueueFamilyIndex = i;
            m_TransferQueueFamilyIndex = i;
            return; // Found ideal queue, use it for all operations
        }
    }

    // Step 2: Try to find a queue family that supports Graphics + Transfer
    for (uint32_t i = 0; i < m_QueueFamilyCount; ++i)
    {
        const auto& props = queueFamilies[i];
        if ((props.queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
            (props.queueFlags & VK_QUEUE_TRANSFER_BIT) &&
            m_GraphicsQueueFamilyIndex == UINT32_MAX)
        {
            m_GraphicsQueueFamilyIndex = i;
            m_TransferQueueFamilyIndex = i;
            break;
        }
    }

    // Step 3: Try to find a dedicated Compute queue (without Graphics)
    for (uint32_t i = 0; i < m_QueueFamilyCount; ++i)
    {
        const auto& props = queueFamilies[i];
        if ((props.queueFlags & VK_QUEUE_COMPUTE_BIT) &&
            !(props.queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
            m_ComputeQueueFamilyIndex == UINT32_MAX)
        {
            m_ComputeQueueFamilyIndex = i;
            break;
        }
    }

    // Step 4: Try to find a dedicated Transfer queue (without Graphics and Compute)
    for (uint32_t i = 0; i < m_QueueFamilyCount; ++i)
    {
        const auto& props = queueFamilies[i];
        if ((props.queueFlags & VK_QUEUE_TRANSFER_BIT) &&
            !(props.queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
            !(props.queueFlags & VK_QUEUE_COMPUTE_BIT) &&
            m_TransferQueueFamilyIndex == UINT32_MAX)
        {
            m_TransferQueueFamilyIndex = i;
            break;
        }
    }

    // Step 5: If Graphics queue not found, find any queue with Graphics capability
    if (m_GraphicsQueueFamilyIndex == UINT32_MAX)
    {
        for (uint32_t i = 0; i < m_QueueFamilyCount; ++i)
        {
            const auto& props = queueFamilies[i];
            if (props.queueFlags & VK_QUEUE_GRAPHICS_BIT)
            {
                m_GraphicsQueueFamilyIndex = i;
                break;
            }
        }
    }

    // Step 6: Fallback - use Graphics queue for missing operations
    if (m_ComputeQueueFamilyIndex == UINT32_MAX)
        m_ComputeQueueFamilyIndex = m_GraphicsQueueFamilyIndex;
    if (m_TransferQueueFamilyIndex == UINT32_MAX)
        m_TransferQueueFamilyIndex = m_GraphicsQueueFamilyIndex;

}

void VulkanDevice::CreateLogicalDevice(VkPhysicalDevice physicalDevice,
                                       const std::vector<const char*>& extensions,
                                       const std::vector<const char*>& layers)
{
    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    std::set<uint32_t> uniqueQueueFamilies = {
        m_GraphicsQueueFamilyIndex,
        m_ComputeQueueFamilyIndex,
        m_TransferQueueFamilyIndex,
    };

    float queuePriority = 1.0f;

    for (uint32_t familyIndex : uniqueQueueFamilies)
    {
        VkDeviceQueueCreateInfo queueInfo{};
        queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueInfo.queueFamilyIndex = familyIndex;
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueInfo);
    }

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();
    createInfo.enabledLayerCount = static_cast<uint32_t>(layers.size());
    createInfo.ppEnabledLayerNames = layers.data();
    VkPhysicalDeviceFeatures featuresToEnable = {};
    featuresToEnable.shaderStorageImageWriteWithoutFormat = VK_TRUE;
    createInfo.pEnabledFeatures = &featuresToEnable;


	VkPhysicalDeviceFeatures2 features2{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
    
    if (m_TimelineSemaphoreFeatures.timelineSemaphore) {
		createInfo.pNext = &m_TimelineSemaphoreFeatures;
    }
	VkPhysicalDeviceAccelerationStructureFeaturesKHR accelStructFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR };
    accelStructFeatures.accelerationStructure = VK_TRUE;
	accelStructFeatures.pNext = (void*)createInfo.pNext;
    createInfo.pNext = &accelStructFeatures;
    
	VkPhysicalDeviceBufferDeviceAddressFeaturesKHR bufferDeviceAddressFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_KHR };
    bufferDeviceAddressFeatures.bufferDeviceAddress = VK_TRUE;
    bufferDeviceAddressFeatures.pNext = (void*)createInfo.pNext;
    createInfo.pNext = &bufferDeviceAddressFeatures;

	VkPhysicalDeviceRayTracingPipelineFeaturesKHR rayTracingPipelineFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR };
	rayTracingPipelineFeatures.rayTracingPipeline = VK_TRUE;
	rayTracingPipelineFeatures.pNext = (void*)createInfo.pNext;
	createInfo.pNext = &rayTracingPipelineFeatures;

    VkPhysicalDeviceRayQueryFeaturesKHR rayQueryFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR };
    VkPhysicalDeviceFeatures2 rayQuerySupport{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
    rayQuerySupport.pNext = &rayQueryFeatures;
    VKFunc::GetPhysicalDeviceFeatures2(physicalDevice, &rayQuerySupport);
    if (!rayQueryFeatures.rayQuery)
        throw std::runtime_error("The selected Vulkan device does not support rayQuery");
    rayQueryFeatures.pNext = const_cast<void*>(createInfo.pNext);
    createInfo.pNext = &rayQueryFeatures;

    VkPhysicalDeviceScalarBlockLayoutFeatures scalarBlockLayoutFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCALAR_BLOCK_LAYOUT_FEATURES };
    scalarBlockLayoutFeatures.scalarBlockLayout = VK_TRUE;
    scalarBlockLayoutFeatures.pNext = (void*)createInfo.pNext;
    createInfo.pNext = &scalarBlockLayoutFeatures;

    //dx编译器自动开启的，后续需要看情况移除
    VkPhysicalDeviceComputeShaderDerivativesFeaturesKHR computeShaderDerivativesFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COMPUTE_SHADER_DERIVATIVES_FEATURES_KHR };
    computeShaderDerivativesFeatures.computeDerivativeGroupQuads = VK_TRUE;
    computeShaderDerivativesFeatures.computeDerivativeGroupLinear = VK_TRUE;
    computeShaderDerivativesFeatures.pNext = (void*)createInfo.pNext;
    createInfo.pNext = &computeShaderDerivativesFeatures;
	//VkPhysicalDeviceComputeShaderDerivativesFeaturesNV computeShaderDerivativesFeaturesNV{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COMPUTE_SHADER_DERIVATIVES_FEATURES_NV };
    //computeShaderDerivativesFeaturesNV.computeDerivativeGroupQuads = VK_TRUE;
    //computeShaderDerivativesFeaturesNV.computeDerivativeGroupLinear = VK_TRUE;
    //computeShaderDerivativesFeaturesNV.pNext = (void*)createInfo.pNext;
    //createInfo.pNext = &computeShaderDerivativesFeaturesNV;


    //VkPhysicalDeviceFaultFeaturesEXT faultFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FAULT_FEATURES_EXT };
    //faultFeatures.deviceFault = VK_TRUE; // ◄── 必须开启这个
    //faultFeatures.deviceFaultVendorBinary = VK_TRUE; // ◄── 必须开启这个
    //timelineSemaphoreFeatures_.pNext = &faultFeatures;
    VKFunc::CreateDevice(physicalDevice, &createInfo,&m_Device);
    
    // 获取队列
    VkQueue graphicsQueue;
    VKFunc::GetDeviceQueue(m_Device, m_GraphicsQueueFamilyIndex, 0, &graphicsQueue);
    VkQueue computeQueue;
    VKFunc::GetDeviceQueue(m_Device, m_ComputeQueueFamilyIndex, 0, &computeQueue);
    VkQueue transferQueue;
    VKFunc::GetDeviceQueue(m_Device, m_TransferQueueFamilyIndex, 0, &transferQueue);

    m_GraphicsQueue = new VulkanQueue(this, graphicsQueue, m_GraphicsQueueFamilyIndex, EQueueType::Graphics);
	m_GraphicsQueue->InitContextPool(10);
    m_ComputeQueue  = new VulkanQueue(this, computeQueue, m_ComputeQueueFamilyIndex, EQueueType::Compute);
	m_ComputeQueue->InitContextPool(10);
    m_TransferQueue = nullptr;//暂时不做async transfer
}

bool VulkanDevice::InitPresentQueue(VkSurfaceKHR surface)
{
    auto checkPresentSupport = [this](VulkanQueue* queue, VkSurfaceKHR surface)  {
        if (m_PresentQueue) return;
        VkBool32 surport = false;
        VKFunc::GetPhysicalDeviceSurfaceSupportKHR(m_PhysicalDevice, queue->GetFamilyIndex(), surface, &surport);
        if (surport) {
            m_PresentQueue = queue;
			m_PresentQueueFamilyIndex = queue->GetFamilyIndex();
        }
    };
    checkPresentSupport(m_GraphicsQueue, surface);
    checkPresentSupport(m_ComputeQueue, surface);
    checkPresentSupport(m_TransferQueue, surface);
    if (!m_PresentQueue) return false;
    if (!m_PresentExecutor) {
        m_PresentExecutor = new VulkanPresentExecutor(m_PresentQueue);
    }
    return true;
}

void VulkanDevice::Destroy()
{
    if (m_Device == VK_NULL_HANDLE)
    {
        return;
    }

    VKFunc::DeviceWaitIdle(m_Device);

    if (m_GlobalCommandContext != nullptr)
    {
        delete m_GlobalCommandContext;
        m_GlobalCommandContext = nullptr;
    }



    if (m_PipelineLayoutCache != nullptr)
    {
        delete m_PipelineLayoutCache;
        m_PipelineLayoutCache = nullptr;
    }
    if (m_DescriptorSetManager != nullptr) {
		delete m_DescriptorSetManager;
        m_DescriptorSetManager = nullptr;
    }

    if (m_ShaderManager != nullptr)
    {
        delete m_ShaderManager;
        m_ShaderManager = nullptr;
    }

    if (m_DescriptorSetManager != nullptr)
    {
        delete m_DescriptorSetManager;
        m_DescriptorSetManager = nullptr;
    }

    if (m_DescriptorSetLayoutManager != nullptr)
    {
        delete m_DescriptorSetLayoutManager;
        m_DescriptorSetLayoutManager = nullptr;
    }

    if (m_RenderPassManager != nullptr)
    {
        delete m_RenderPassManager;
        m_RenderPassManager = nullptr;
    }



  
    ReleaseDeferredResources();

    std::set<VulkanQueue*> uniqueQueues;
    uniqueQueues.insert(m_GraphicsQueue);
    uniqueQueues.insert(m_ComputeQueue);
    uniqueQueues.insert(m_TransferQueue);
    uniqueQueues.insert(m_PresentQueue);
    for (auto queue : uniqueQueues) {
        if (queue != nullptr)
        {
            delete queue;
        }
    }
    m_TransferQueue = nullptr;
    m_ComputeQueue = nullptr;
    m_GraphicsQueue = nullptr;
    m_PresentQueue = nullptr;

    if (m_StagingManager != nullptr)
    {
        delete m_StagingManager;
        m_StagingManager = nullptr;
    }


    if (m_SemaphoreManager != nullptr)
    {
        delete m_SemaphoreManager;
        m_SemaphoreManager = nullptr;
    }

    if (m_DeferredDeleteQueue != nullptr)
    {
        m_DeferredDeleteQueue->Clear();
        delete m_DeferredDeleteQueue;
        m_DeferredDeleteQueue = nullptr;
    }




    if (m_MemoryManager != nullptr)
    {
        delete m_MemoryManager;
        m_MemoryManager = nullptr;
    }

    if (m_FenceManager != nullptr)
    {
        delete m_FenceManager;
        m_FenceManager = nullptr;
    }



    if (m_Device != VK_NULL_HANDLE)
    {
        VKFunc::DestroyDevice(m_Device);
        m_Device = VK_NULL_HANDLE;
    }
}

// ============ VulkanDeferredDeleteQueue Implementation ============

VulkanDeferredDeleteQueue::VulkanDeferredDeleteQueue(VulkanDevice* inDevice)
    : m_Device(inDevice),
      m_CurrentFrameNumber(0)
{
}

VulkanDeferredDeleteQueue::~VulkanDeferredDeleteQueue()
{
    Clear();
}

void VulkanDeferredDeleteQueue::EnqueueGenericResource(EResourceType type, uint64_t handle)
{
    if (handle == 0)
        return;

    std::lock_guard<std::mutex> lock(m_QueueMutex);
    FDeferredDelete entry;
    entry.Handle = handle;
    entry.FrameNumber = m_CurrentFrameNumber;
    m_Queues[static_cast<size_t>(type)].push(entry);
}

void VulkanDeferredDeleteQueue::ReleaseResource(EResourceType type, const FDeferredDelete& entry)
{
    if (!m_Device || m_Device->GetHandle() == VK_NULL_HANDLE)
        return;

    VkDevice vulkanDevice = m_Device->GetHandle();
    switch (type)
    {
    case EResourceType::RenderPass:
        VKFunc::DestroyRenderPass(vulkanDevice, reinterpret_cast<VkRenderPass>(entry.Handle));
        break;
    case EResourceType::Buffer:
        VKFunc::DestroyBuffer(vulkanDevice, reinterpret_cast<VkBuffer>(entry.Handle));
        break;
    case EResourceType::BufferView:
        VKFunc::DestroyBufferView(vulkanDevice, reinterpret_cast<VkBufferView>(entry.Handle));
        break;
    case EResourceType::Image:
    {
        VkImage image = reinterpret_cast<VkImage>(entry.Handle);
        m_Device->GetGraphicsQueue()->GetImageLayoutManager()->NotifyDeletedImage(image);
        m_Device->GetComputeQueue()->GetImageLayoutManager()->NotifyDeletedImage(image);
        //device_->GetTransferQueue()->GetImageLayoutManager()->NotifyDeletedImage(Image);

        VKFunc::DestroyImage(vulkanDevice, image);
        break;
    }
    case EResourceType::ImageView:
        VKFunc::DestroyImageView(vulkanDevice, reinterpret_cast<VkImageView>(entry.Handle));
        break;
    case EResourceType::Pipeline:
        VKFunc::DestroyPipeline(vulkanDevice, reinterpret_cast<VkPipeline>(entry.Handle));
        break;
    case EResourceType::Framebuffer:
        VKFunc::DestroyFramebuffer(vulkanDevice, reinterpret_cast<VkFramebuffer>(entry.Handle));
        break;
    case EResourceType::Sampler:
        VKFunc::DestroySampler(vulkanDevice, reinterpret_cast<VkSampler>(entry.Handle));
        break;
    case EResourceType::ShaderModule:
        VKFunc::DestroyShaderModule(vulkanDevice, reinterpret_cast<VkShaderModule>(entry.Handle));
        break;
	case EResourceType::Semaphore:
        VKFunc::DestroySemaphore(vulkanDevice, reinterpret_cast<VkSemaphore>(entry.Handle));
		break;
    default:
        break;
    }
}

void VulkanDeferredDeleteQueue::ReleaseResources(uint32_t frameDelay)
{
    std::lock_guard<std::mutex> lock(m_QueueMutex);
    m_CurrentFrameNumber++;

    // 按照删除顺序处理每个类型的队列
    for (EResourceType type : DeletionOrder)
    {
        auto& queue = m_Queues[static_cast<size_t>(type)];
        while (!queue.empty())
        {
            const FDeferredDelete& entry = queue.front();

            ReleaseResource(type, entry);
            queue.pop();


        }
    }
}

void VulkanDeferredDeleteQueue::Clear()
{
    std::lock_guard<std::mutex> lock(m_QueueMutex);

    // 按照删除顺序立即释放所有待处理资源
    for (EResourceType type : DeletionOrder)
    {
        auto& queue = m_Queues[static_cast<size_t>(type)];
        while (!queue.empty())
        {
            const FDeferredDelete& entry = queue.front();
            ReleaseResource(type, entry);
            queue.pop();
        }
    }
}
void VulkanDevice::EnqueueRenderPassForDeletion(VkRenderPass renderPass)
{
    if (m_DeferredDeleteQueue)
        m_DeferredDeleteQueue->EnqueueResource(VulkanDeferredDeleteQueue::EResourceType::RenderPass, renderPass);
}

void VulkanDevice::EnqueueBufferForDeletion(VkBuffer buffer)
{
    if (m_DeferredDeleteQueue)
        m_DeferredDeleteQueue->EnqueueResource(VulkanDeferredDeleteQueue::EResourceType::Buffer, buffer);
}

void VulkanDevice::EnqueueImageForDeletion(VkImage image)
{
    if (m_DeferredDeleteQueue)
    {
        m_DeferredDeleteQueue->EnqueueResource(VulkanDeferredDeleteQueue::EResourceType::Image, image);
		// 通知 RenderPassManager 相关的 Framebuffer 可能需要更新
        if (m_RenderPassManager)
        {
            m_RenderPassManager->NotifyDeletedImage(image);
        }
            			
    }

}

void VulkanDevice::EnqueueImageViewForDeletion(VkImageView imageView)
{
    if (m_DeferredDeleteQueue)
        m_DeferredDeleteQueue->EnqueueResource(VulkanDeferredDeleteQueue::EResourceType::ImageView, imageView);
}

void VulkanDevice::EnqueueBufferViewForDeletion(VkBufferView bufferView)
{
    if (m_DeferredDeleteQueue)
        m_DeferredDeleteQueue->EnqueueResource(VulkanDeferredDeleteQueue::EResourceType::BufferView, bufferView);
}

void VulkanDevice::EnqueuePipelineForDeletion(VkPipeline pipeline)
{
    if (m_DeferredDeleteQueue)
        m_DeferredDeleteQueue->EnqueueResource(VulkanDeferredDeleteQueue::EResourceType::Pipeline, pipeline);
}


void VulkanDevice::EnqueueFramebufferForDeletion(VkFramebuffer framebuffer)
{
    if (m_DeferredDeleteQueue)
        m_DeferredDeleteQueue->EnqueueResource(VulkanDeferredDeleteQueue::EResourceType::Framebuffer, framebuffer);
}

void VulkanDevice::EnqueueSamplerForDeletion(VkSampler sampler)
{
    if (m_DeferredDeleteQueue)
        m_DeferredDeleteQueue->EnqueueResource(VulkanDeferredDeleteQueue::EResourceType::Sampler, sampler);
}

void VulkanDevice::EnqueueShaderModuleForDeletion(VkShaderModule shaderModule)
{
    if (m_DeferredDeleteQueue)
        m_DeferredDeleteQueue->EnqueueResource(VulkanDeferredDeleteQueue::EResourceType::ShaderModule, shaderModule);
}

void VulkanDevice::EnqueueSemaphoreForDeletion(VkSemaphore semaphore)
{
	if (m_DeferredDeleteQueue)
		m_DeferredDeleteQueue->EnqueueResource(VulkanDeferredDeleteQueue::EResourceType::Semaphore, semaphore);
}


void VulkanDevice::ReleaseDeferredResources(uint32_t frameDelay)
{
    if (m_DeferredDeleteQueue)
        m_DeferredDeleteQueue->ReleaseResources(frameDelay);
}

uint32_t VulkanDevice::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const
{
    VkPhysicalDeviceMemoryProperties memProperties;
    VKFunc::GetPhysicalDeviceMemoryProperties(m_PhysicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
    {
        // 1. typeFilter：bitmask，表示哪些 memoryType 可用
        // 2. properties：你想要的属性（比如 DEVICE_LOCAL）
        if ((typeFilter & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
        {
            return i;
        }
    }
    throw std::runtime_error("Failed to find suitable memory type!");
}

}