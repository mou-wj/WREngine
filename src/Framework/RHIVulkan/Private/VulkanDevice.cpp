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
    : rhiApi_(rhiApi),
      physicalDevice_(physicalDevice),
      device_(VK_NULL_HANDLE),
      graphicsQueueFamilyIndex_(UINT32_MAX),
      computeQueueFamilyIndex_(UINT32_MAX),
      transferQueueFamilyIndex_(UINT32_MAX),
      presentQueueFamilyIndex_(UINT32_MAX),
      graphicsQueue_(nullptr),
      computeQueue_(nullptr),
      transferQueue_(nullptr),
      presentQueue_(nullptr),
      memoryManager_(nullptr),
      presentExecutor_(nullptr)
{
    memoryManager_ = new VulkanMemoryManager(this);
	stagingManager_ = new VulkanStagingManager(this, memoryManager_);

    fenceManager_ = new VulkanFenceManager(this);
    semaphoreManager_ = new VulkanSemaphoreManager(this);
    renderPassManager_ = new VulkanRenderPassManager(this);
    descriptorSetLayoutManager_ = new VulkanDescriptorSetLayoutManager(this);
    descriptorSetManager_ = new VulkanDescriptorSetManager(this, descriptorSetLayoutManager_);
	shaderManager_ = new VulkanShaderManager(this);
	pipelineLayoutCache_ = new VulkanPipelineLayoutCache(this);
    deferredDeleteQueue_ = new VulkanDeferredDeleteQueue(this);
    supportedFeatures2_.pNext
		= &timelineSemaphoreFeatures_;
	VKFunc::GetPhysicalDeviceFeatures2(physicalDevice_, &supportedFeatures2_);
}

VulkanDevice::~VulkanDevice()
{
    Destroy();
}

bool VulkanDevice::Init(const std::vector<const char*>& enabledLayers,
                        const std::vector<const char*>& enabledExtensions)
{
    SelectQueueFamilies(physicalDevice_);
    CreateLogicalDevice(physicalDevice_, enabledExtensions, enabledLayers);
    globalCommandContext_ = new VulkanGraphicContext(this, graphicsQueue_);
    return true;
}

void VulkanDevice::SelectQueueFamilies(VkPhysicalDevice physicalDevice)
{
    VKFunc::GetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    VKFunc::GetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies.data());

    // Step 1: Try to find a queue family that supports all operations (Graphics + Compute + Transfer)
    for (uint32_t i = 0; i < queueFamilyCount; ++i)
    {
        const auto& props = queueFamilies[i];
        if ((props.queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
            (props.queueFlags & VK_QUEUE_COMPUTE_BIT) &&
            (props.queueFlags & VK_QUEUE_TRANSFER_BIT))
        {
            graphicsQueueFamilyIndex_ = i;
            computeQueueFamilyIndex_ = i;
            transferQueueFamilyIndex_ = i;
            return; // Found ideal queue, use it for all operations
        }
    }

    // Step 2: Try to find a queue family that supports Graphics + Transfer
    for (uint32_t i = 0; i < queueFamilyCount; ++i)
    {
        const auto& props = queueFamilies[i];
        if ((props.queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
            (props.queueFlags & VK_QUEUE_TRANSFER_BIT) &&
            graphicsQueueFamilyIndex_ == UINT32_MAX)
        {
            graphicsQueueFamilyIndex_ = i;
            transferQueueFamilyIndex_ = i;
            break;
        }
    }

    // Step 3: Try to find a dedicated Compute queue (without Graphics)
    for (uint32_t i = 0; i < queueFamilyCount; ++i)
    {
        const auto& props = queueFamilies[i];
        if ((props.queueFlags & VK_QUEUE_COMPUTE_BIT) &&
            !(props.queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
            computeQueueFamilyIndex_ == UINT32_MAX)
        {
            computeQueueFamilyIndex_ = i;
            break;
        }
    }

    // Step 4: Try to find a dedicated Transfer queue (without Graphics and Compute)
    for (uint32_t i = 0; i < queueFamilyCount; ++i)
    {
        const auto& props = queueFamilies[i];
        if ((props.queueFlags & VK_QUEUE_TRANSFER_BIT) &&
            !(props.queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
            !(props.queueFlags & VK_QUEUE_COMPUTE_BIT) &&
            transferQueueFamilyIndex_ == UINT32_MAX)
        {
            transferQueueFamilyIndex_ = i;
            break;
        }
    }

    // Step 5: If Graphics queue not found, find any queue with Graphics capability
    if (graphicsQueueFamilyIndex_ == UINT32_MAX)
    {
        for (uint32_t i = 0; i < queueFamilyCount; ++i)
        {
            const auto& props = queueFamilies[i];
            if (props.queueFlags & VK_QUEUE_GRAPHICS_BIT)
            {
                graphicsQueueFamilyIndex_ = i;
                break;
            }
        }
    }

    // Step 6: Fallback - use Graphics queue for missing operations
    if (computeQueueFamilyIndex_ == UINT32_MAX)
        computeQueueFamilyIndex_ = graphicsQueueFamilyIndex_;
    if (transferQueueFamilyIndex_ == UINT32_MAX)
        transferQueueFamilyIndex_ = graphicsQueueFamilyIndex_;

}

void VulkanDevice::CreateLogicalDevice(VkPhysicalDevice physicalDevice,
                                       const std::vector<const char*>& extensions,
                                       const std::vector<const char*>& layers)
{
    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    std::set<uint32_t> uniqueQueueFamilies = {
        graphicsQueueFamilyIndex_,
        computeQueueFamilyIndex_,
        transferQueueFamilyIndex_,
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
    
    if (timelineSemaphoreFeatures_.timelineSemaphore) {
		createInfo.pNext = &timelineSemaphoreFeatures_;
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
    VKFunc::CreateDevice(physicalDevice, &createInfo,&device_);
    
    // 获取队列
    VkQueue graphicsQueue;
    VKFunc::GetDeviceQueue(device_, graphicsQueueFamilyIndex_, 0, &graphicsQueue);
    VkQueue computeQueue;
    VKFunc::GetDeviceQueue(device_, computeQueueFamilyIndex_, 0, &computeQueue);
    VkQueue transferQueue;
    VKFunc::GetDeviceQueue(device_, transferQueueFamilyIndex_, 0, &transferQueue);

    graphicsQueue_ = new VulkanQueue(this, graphicsQueue, graphicsQueueFamilyIndex_, EQueueType::Graphics);
	graphicsQueue_->InitContextPool(10);
    computeQueue_  = new VulkanQueue(this, computeQueue, computeQueueFamilyIndex_, EQueueType::Compute);
	computeQueue_->InitContextPool(10);
    transferQueue_ = nullptr;//暂时不做async transfer
}

bool VulkanDevice::InitPresentQueue(VkSurfaceKHR Surface)
{
    auto CheckPresentSupport = [this](VulkanQueue* queue, VkSurfaceKHR surface)  {
        if (presentQueue_) return;
        VkBool32 surport = false;
        VKFunc::GetPhysicalDeviceSurfaceSupportKHR(physicalDevice_, queue->GetFamilyIndex(), surface, &surport);
        if (surport) {
            presentQueue_ = queue;
			presentQueueFamilyIndex_ = queue->GetFamilyIndex();
        }
    };
    CheckPresentSupport(graphicsQueue_, Surface);
    CheckPresentSupport(computeQueue_, Surface);
    CheckPresentSupport(transferQueue_, Surface);
    if (!presentQueue_) return false;
    if (!presentExecutor_) {
        presentExecutor_ = new VulkanPresentExecutor(presentQueue_);
    }
    return true;
}

void VulkanDevice::Destroy()
{
    if (device_ == VK_NULL_HANDLE)
    {
        return;
    }

    VKFunc::DeviceWaitIdle(device_);

    if (globalCommandContext_ != nullptr)
    {
        delete globalCommandContext_;
        globalCommandContext_ = nullptr;
    }



    if (pipelineLayoutCache_ != nullptr)
    {
        delete pipelineLayoutCache_;
        pipelineLayoutCache_ = nullptr;
    }
    if (descriptorSetManager_ != nullptr) {
		delete descriptorSetManager_;
        descriptorSetManager_ = nullptr;
    }

    if (shaderManager_ != nullptr)
    {
        delete shaderManager_;
        shaderManager_ = nullptr;
    }

    if (descriptorSetManager_ != nullptr)
    {
        delete descriptorSetManager_;
        descriptorSetManager_ = nullptr;
    }

    if (descriptorSetLayoutManager_ != nullptr)
    {
        delete descriptorSetLayoutManager_;
        descriptorSetLayoutManager_ = nullptr;
    }

    if (renderPassManager_ != nullptr)
    {
        delete renderPassManager_;
        renderPassManager_ = nullptr;
    }



  
    ReleaseDeferredResources();

    std::set<VulkanQueue*> uniqueQueues;
    uniqueQueues.insert(graphicsQueue_);
    uniqueQueues.insert(computeQueue_);
    uniqueQueues.insert(transferQueue_);
    uniqueQueues.insert(presentQueue_);
    for (auto queue : uniqueQueues) {
        if (queue != nullptr)
        {
            delete queue;
        }
    }
    transferQueue_ = nullptr;
    computeQueue_ = nullptr;
    graphicsQueue_ = nullptr;
    presentQueue_ = nullptr;

    if (stagingManager_ != nullptr)
    {
        delete stagingManager_;
        stagingManager_ = nullptr;
    }


    if (semaphoreManager_ != nullptr)
    {
        delete semaphoreManager_;
        semaphoreManager_ = nullptr;
    }

    if (deferredDeleteQueue_ != nullptr)
    {
        deferredDeleteQueue_->Clear();
        delete deferredDeleteQueue_;
        deferredDeleteQueue_ = nullptr;
    }




    if (memoryManager_ != nullptr)
    {
        delete memoryManager_;
        memoryManager_ = nullptr;
    }

    if (fenceManager_ != nullptr)
    {
        delete fenceManager_;
        fenceManager_ = nullptr;
    }



    if (device_ != VK_NULL_HANDLE)
    {
        VKFunc::DestroyDevice(device_);
        device_ = VK_NULL_HANDLE;
    }
}

// ============ VulkanDeferredDeleteQueue Implementation ============

VulkanDeferredDeleteQueue::VulkanDeferredDeleteQueue(VulkanDevice* InDevice)
    : device_(InDevice),
      currentFrameNumber_(0)
{
}

VulkanDeferredDeleteQueue::~VulkanDeferredDeleteQueue()
{
    Clear();
}

void VulkanDeferredDeleteQueue::EnqueueGenericResource(EResourceType Type, uint64_t Handle)
{
    if (Handle == 0)
        return;

    std::lock_guard<std::mutex> Lock(queueMutex_);
    FDeferredDelete Entry;
    Entry.Handle = Handle;
    Entry.FrameNumber = currentFrameNumber_;
    queues_[static_cast<size_t>(Type)].push(Entry);
}

void VulkanDeferredDeleteQueue::ReleaseResource(EResourceType Type, const FDeferredDelete& Entry)
{
    if (!device_ || device_->GetHandle() == VK_NULL_HANDLE)
        return;

    VkDevice VulkanDevice = device_->GetHandle();
    switch (Type)
    {
    case EResourceType::RenderPass:
        VKFunc::DestroyRenderPass(VulkanDevice, reinterpret_cast<VkRenderPass>(Entry.Handle));
        break;
    case EResourceType::Buffer:
        VKFunc::DestroyBuffer(VulkanDevice, reinterpret_cast<VkBuffer>(Entry.Handle));
        break;
    case EResourceType::BufferView:
        VKFunc::DestroyBufferView(VulkanDevice, reinterpret_cast<VkBufferView>(Entry.Handle));
        break;
    case EResourceType::Image:
    {
        VkImage Image = reinterpret_cast<VkImage>(Entry.Handle);
        device_->GetGraphicsQueue()->GetImageLayoutManager()->NotifyDeletedImage(Image);
        device_->GetComputeQueue()->GetImageLayoutManager()->NotifyDeletedImage(Image);
        //device_->GetTransferQueue()->GetImageLayoutManager()->NotifyDeletedImage(Image);

        VKFunc::DestroyImage(VulkanDevice, Image);
        break;
    }
    case EResourceType::ImageView:
        VKFunc::DestroyImageView(VulkanDevice, reinterpret_cast<VkImageView>(Entry.Handle));
        break;
    case EResourceType::Pipeline:
        VKFunc::DestroyPipeline(VulkanDevice, reinterpret_cast<VkPipeline>(Entry.Handle));
        break;
    case EResourceType::Framebuffer:
        VKFunc::DestroyFramebuffer(VulkanDevice, reinterpret_cast<VkFramebuffer>(Entry.Handle));
        break;
    case EResourceType::Sampler:
        VKFunc::DestroySampler(VulkanDevice, reinterpret_cast<VkSampler>(Entry.Handle));
        break;
    case EResourceType::ShaderModule:
        VKFunc::DestroyShaderModule(VulkanDevice, reinterpret_cast<VkShaderModule>(Entry.Handle));
        break;
	case EResourceType::Semaphore:
        VKFunc::DestroySemaphore(VulkanDevice, reinterpret_cast<VkSemaphore>(Entry.Handle));
		break;
    default:
        break;
    }
}

void VulkanDeferredDeleteQueue::ReleaseResources(uint32_t FrameDelay)
{
    std::lock_guard<std::mutex> Lock(queueMutex_);
    currentFrameNumber_++;

    // 按照删除顺序处理每个类型的队列
    for (EResourceType Type : DeletionOrder)
    {
        auto& queue = queues_[static_cast<size_t>(Type)];
        while (!queue.empty())
        {
            const FDeferredDelete& Entry = queue.front();

            ReleaseResource(Type, Entry);
            queue.pop();


        }
    }
}

void VulkanDeferredDeleteQueue::Clear()
{
    std::lock_guard<std::mutex> Lock(queueMutex_);

    // 按照删除顺序立即释放所有待处理资源
    for (EResourceType Type : DeletionOrder)
    {
        auto& queue = queues_[static_cast<size_t>(Type)];
        while (!queue.empty())
        {
            const FDeferredDelete& Entry = queue.front();
            ReleaseResource(Type, Entry);
            queue.pop();
        }
    }
}
void VulkanDevice::EnqueueRenderPassForDeletion(VkRenderPass RenderPass)
{
    if (deferredDeleteQueue_)
        deferredDeleteQueue_->EnqueueResource(VulkanDeferredDeleteQueue::EResourceType::RenderPass, RenderPass);
}

void VulkanDevice::EnqueueBufferForDeletion(VkBuffer Buffer)
{
    if (deferredDeleteQueue_)
        deferredDeleteQueue_->EnqueueResource(VulkanDeferredDeleteQueue::EResourceType::Buffer, Buffer);
}

void VulkanDevice::EnqueueImageForDeletion(VkImage Image)
{
    if (deferredDeleteQueue_)
    {
        deferredDeleteQueue_->EnqueueResource(VulkanDeferredDeleteQueue::EResourceType::Image, Image);
		// 通知 RenderPassManager 相关的 Framebuffer 可能需要更新
        if (renderPassManager_)
        {
            renderPassManager_->NotifyDeletedImage(Image);
        }
            			
    }

}

void VulkanDevice::EnqueueImageViewForDeletion(VkImageView ImageView)
{
    if (deferredDeleteQueue_)
        deferredDeleteQueue_->EnqueueResource(VulkanDeferredDeleteQueue::EResourceType::ImageView, ImageView);
}

void VulkanDevice::EnqueueBufferViewForDeletion(VkBufferView BufferView)
{
    if (deferredDeleteQueue_)
        deferredDeleteQueue_->EnqueueResource(VulkanDeferredDeleteQueue::EResourceType::BufferView, BufferView);
}

void VulkanDevice::EnqueuePipelineForDeletion(VkPipeline Pipeline)
{
    if (deferredDeleteQueue_)
        deferredDeleteQueue_->EnqueueResource(VulkanDeferredDeleteQueue::EResourceType::Pipeline, Pipeline);
}


void VulkanDevice::EnqueueFramebufferForDeletion(VkFramebuffer Framebuffer)
{
    if (deferredDeleteQueue_)
        deferredDeleteQueue_->EnqueueResource(VulkanDeferredDeleteQueue::EResourceType::Framebuffer, Framebuffer);
}

void VulkanDevice::EnqueueSamplerForDeletion(VkSampler Sampler)
{
    if (deferredDeleteQueue_)
        deferredDeleteQueue_->EnqueueResource(VulkanDeferredDeleteQueue::EResourceType::Sampler, Sampler);
}

void VulkanDevice::EnqueueShaderModuleForDeletion(VkShaderModule ShaderModule)
{
    if (deferredDeleteQueue_)
        deferredDeleteQueue_->EnqueueResource(VulkanDeferredDeleteQueue::EResourceType::ShaderModule, ShaderModule);
}

void VulkanDevice::EnqueueSemaphoreForDeletion(VkSemaphore Semaphore)
{
	if (deferredDeleteQueue_)
		deferredDeleteQueue_->EnqueueResource(VulkanDeferredDeleteQueue::EResourceType::Semaphore, Semaphore);
}


void VulkanDevice::ReleaseDeferredResources(uint32_t FrameDelay)
{
    if (deferredDeleteQueue_)
        deferredDeleteQueue_->ReleaseResources(FrameDelay);
}

uint32_t VulkanDevice::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const
{
    VkPhysicalDeviceMemoryProperties memProperties;
    VKFunc::GetPhysicalDeviceMemoryProperties(physicalDevice_, &memProperties);

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