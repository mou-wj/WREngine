#include "VulkanFuncWrapper.h"
#include "Log.h"
#include <assert.h>

// ������ں�����ͨ����̬��ֱ�ӵ�����
#define VK_EXPORTED_FUNC_LIST(V) \
    V(vkGetInstanceProcAddr)

// ʵ����������ͨ�� vkGetInstanceProcAddr ���أ�
#define VK_INSTANCE_FUNC_LIST(V) \
    V(vkCreateInstance) \
    V(vkDestroyInstance) \
    V(vkEnumerateInstanceExtensionProperties) \
    V(vkEnumerateInstanceLayerProperties) \
    V(vkEnumeratePhysicalDevices) \
    V(vkGetPhysicalDeviceProperties) \
    V(vkGetPhysicalDeviceProperties2) \
    V(vkGetPhysicalDeviceFeatures) \
    V(vkGetPhysicalDeviceFeatures2) \
    V(vkGetPhysicalDeviceMemoryProperties) \
    V(vkGetPhysicalDeviceQueueFamilyProperties) \
    V(vkEnumerateDeviceExtensionProperties) \
    V(vkEnumerateDeviceLayerProperties) \
    V(vkCreateDevice) \
    V(vkGetDeviceProcAddr) \
    V(vkCreateWin32SurfaceKHR) \
    V(vkDestroySurfaceKHR) \
    V(vkGetPhysicalDeviceSurfaceSupportKHR) \
    V(vkGetPhysicalDeviceSurfaceCapabilitiesKHR) \
    V(vkGetPhysicalDeviceSurfaceFormatsKHR) \
    V(vkGetPhysicalDeviceSurfacePresentModesKHR)\
    V(vkCreateDebugUtilsMessengerEXT) \
    V(vkDestroyDebugUtilsMessengerEXT)

// �豸��������ͨ�� vkGetDeviceProcAddr ���أ�Ч����ߣ�
#define VK_DEVICE_FUNC_LIST(V) \
    V(vkDestroyDevice) \
    V(vkGetDeviceQueue) \
    V(vkDeviceWaitIdle) \
    V(vkQueueWaitIdle) \
    V(vkQueueSubmit) \
    V(vkAllocateMemory) \
    V(vkFreeMemory) \
    V(vkMapMemory) \
    V(vkUnmapMemory) \
    V(vkCreateBuffer) \
    V(vkDestroyBuffer) \
    V(vkGetBufferMemoryRequirements) \
    V(vkBindBufferMemory) \
    V(vkCreateBufferView) \
    V(vkDestroyBufferView) \
    V(vkCreateImage) \
    V(vkDestroyImage) \
    V(vkGetImageMemoryRequirements) \
    V(vkBindImageMemory) \
    V(vkCreateImageView) \
    V(vkDestroyImageView) \
    V(vkCreateSampler) \
    V(vkDestroySampler) \
    V(vkCreateDescriptorSetLayout) \
    V(vkDestroyDescriptorSetLayout) \
    V(vkCreateDescriptorPool) \
    V(vkResetDescriptorPool) \
    V(vkDestroyDescriptorPool) \
    V(vkAllocateDescriptorSets) \
    V(vkFreeDescriptorSets) \
    V(vkUpdateDescriptorSets) \
    V(vkCreateShaderModule) \
    V(vkDestroyShaderModule) \
    V(vkCreatePipelineLayout) \
    V(vkDestroyPipelineLayout) \
    V(vkCreateGraphicsPipelines) \
    V(vkCreateComputePipelines) \
        V(vkCreateRayTracingPipelinesKHR) \
        V(vkGetRayTracingShaderGroupHandlesKHR) \
    V(vkDestroyPipeline) \
        V(vkCreateAccelerationStructureKHR) \
        V(vkDestroyAccelerationStructureKHR) \
        V(vkGetAccelerationStructureBuildSizesKHR) \
        V(vkCmdBuildAccelerationStructuresKHR) \
        V(vkGetAccelerationStructureDeviceAddressKHR) \
        V(vkGetBufferDeviceAddress) \
    V(vkCreateRenderPass) \
    V(vkDestroyRenderPass) \
    V(vkCreateFramebuffer) \
    V(vkDestroyFramebuffer) \
    V(vkCreateCommandPool) \
    V(vkResetCommandPool) \
    V(vkDestroyCommandPool) \
    V(vkAllocateCommandBuffers) \
    V(vkFreeCommandBuffers) \
    V(vkBeginCommandBuffer) \
    V(vkEndCommandBuffer) \
    V(vkResetCommandBuffer) \
    V(vkCmdBindPipeline) \
    V(vkCmdBindDescriptorSets) \
    V(vkCmdBindVertexBuffers) \
    V(vkCmdBindIndexBuffer) \
    V(vkCmdSetViewport) \
    V(vkCmdSetScissor) \
    V(vkCmdDraw) \
    V(vkCmdDrawIndexed) \
    V(vkCmdDispatch) \
    V(vkCmdCopyBuffer) \
    V(vkCmdCopyBufferToImage) \
    V(vkCmdCopyImageToBuffer) \
    V(vkCmdCopyImage)\
    V(vkCmdBlitImage)\
    V(vkCmdPipelineBarrier) \
    V(vkCmdBeginRenderPass)\
    V(vkCmdEndRenderPass) \
        V(vkCmdCopyAccelerationStructureKHR) \
        V(vkCmdCopyAccelerationStructureToMemoryKHR) \
        V(vkCmdCopyMemoryToAccelerationStructureKHR) \
        V(vkCmdWriteAccelerationStructuresPropertiesKHR) \
        V(vkCmdTraceRaysKHR) \
        V(vkCmdTraceRaysIndirectKHR) \
    V(vkCreateFence) \
    V(vkDestroyFence) \
    V(vkGetFenceStatus) \
    V(vkWaitForFences) \
    V(vkResetFences) \
    V(vkCreateSemaphore) \
    V(vkDestroySemaphore) \
    V(vkGetSemaphoreCounterValue) \
    V(vkWaitSemaphores) \
    V(vkCreateEvent) \
    V(vkDestroyEvent) \
    V(vkSetEvent) \
    V(vkResetEvent) \
    V(vkGetEventStatus) \
    V(vkCreateSwapchainKHR) \
    V(vkDestroySwapchainKHR) \
    V(vkGetSwapchainImagesKHR) \
    V(vkAcquireNextImageKHR) \
    V(vkQueuePresentKHR) \
    V(vkGetDeviceFaultInfoEXT)\
    V(vkSetDebugUtilsObjectNameEXT) // ��Ӧ SetDebugName

#define DECLARE_PFN(name) static PFN_##name name = nullptr;

VK_EXPORTED_FUNC_LIST(DECLARE_PFN)
VK_INSTANCE_FUNC_LIST(DECLARE_PFN)
VK_DEVICE_FUNC_LIST(DECLARE_PFN)

#undef DECLARE_PFN

#if defined(_WIN32)
HMODULE vulkanLib = NULL;
#elif defined(__linux__) || defined(__APPLE__)
void* vulkanLib = nullptr;
#endif




using namespace Core;
namespace VKFunc {

    bool InitializeLoader() {
        // 1. ���ض�̬�� (LoadLibrary/dlopen) �߼�ʡ��...
#if defined(_WIN32)
        vulkanLib = LoadLibraryA("vulkan-1.dll");
#elif defined(__linux__) || defined(__APPLE__)
        vulkanLib = dlopen("vulkan-1.so", RTLD_LAZY);
#endif
        // 2. ���ص����Ļ�������
#define LOAD_EXPORTED(name) name = (PFN_##name)GetProcAddress(vulkanLib, #name);
        VK_EXPORTED_FUNC_LIST(LOAD_EXPORTED)
#undef LOAD_EXPORTED
        //3. Ԥ����ȫ�ֽӿ�
       vkEnumerateInstanceExtensionProperties = (PFN_vkEnumerateInstanceExtensionProperties)
            vkGetInstanceProcAddr(nullptr, "vkEnumerateInstanceExtensionProperties");

        vkCreateInstance = (PFN_vkCreateInstance)
            vkGetInstanceProcAddr(nullptr, "vkCreateInstance");

        vkEnumerateInstanceLayerProperties = (PFN_vkEnumerateInstanceLayerProperties)vkGetInstanceProcAddr(nullptr, "vkEnumerateInstanceLayerProperties");
        return vkGetInstanceProcAddr != nullptr;
    }

    void LoadInstanceFunctions(VkInstance instance) {
#define LOAD_INSTANCE(name) name = (PFN_##name)vkGetInstanceProcAddr(instance, #name);
        VK_INSTANCE_FUNC_LIST(LOAD_INSTANCE)
#undef LOAD_INSTANCE
    }

    void LoadDeviceFunctions(VkDevice device) {
#define LOAD_DEVICE(name) name = (PFN_##name)vkGetDeviceProcAddr(device, #name);
        VK_DEVICE_FUNC_LIST(LOAD_DEVICE)
#undef LOAD_DEVICE
    }

    bool CreateInstance(const VkInstanceCreateInfo* pCreateInfo, VkInstance* pInstance) {
#ifdef DEBUG_INFO
        if (!pCreateInfo || !pInstance) { LOG_ERROR("VK: CreateInstance params invalid"); return false; }
#endif
        VkResult res = vkCreateInstance(pCreateInfo, nullptr, pInstance);
#ifdef DEBUG_INFO
        if (res != VK_SUCCESS) { LOG_ERROR("VK: CreateInstance failed with %d", res); return false; }
        LOG_INFO("VK: Instance created successfully");

#endif
        if (res == VK_SUCCESS) LoadInstanceFunctions(*pInstance);
        return res == VK_SUCCESS;
    }

    void DestroyInstance(VkInstance instance) {
#ifdef DEBUG_INFO
        if (!instance) { LOG_ERROR("VK: DestroyInstance - instance is null"); return; }
#endif
        vkDestroyInstance(instance, nullptr);
    }

    void EnumerateInstanceExtensionProperties(const char* pLayerName, uint32_t* pPropertyCount, VkExtensionProperties* pProperties) {
#ifdef DEBUG_INFO
        if (!pPropertyCount) { LOG_ERROR("VK: EnumerateInstanceExtensionProperties - pPropertyCount is null"); return; }
#endif
        vkEnumerateInstanceExtensionProperties(pLayerName, pPropertyCount, pProperties);
    }

    void EnumerateInstanceLayerProperties(uint32_t* pPropertyCount, VkLayerProperties* pProperties) {
#ifdef DEBUG_INFO
        if (!pPropertyCount) { LOG_ERROR("VK: EnumerateInstanceLayerProperties - pPropertyCount is null"); return; }
#endif
        vkEnumerateInstanceLayerProperties(pPropertyCount, pProperties);
    }

    void SetDebugName(VkDevice device, VkObjectType type, uint64_t handle, const char* name) {
#ifdef DEBUG_INFO
        if (!device || !name) return;
        if (vkSetDebugUtilsObjectNameEXT) {
            VkDebugUtilsObjectNameInfoEXT info = { VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };
            info.objectType = type;
            info.objectHandle = handle;
            info.pObjectName = name;
            vkSetDebugUtilsObjectNameEXT(device, &info);
        }
#endif
    }
    // ����������������ز����ô�������
    bool CreateDebugUtilsMessengerEXT(VkInstance instance,
        const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
        VkDebugUtilsMessengerEXT* pDebugMessenger) {

        if (vkCreateDebugUtilsMessengerEXT) {
			auto res = vkCreateDebugUtilsMessengerEXT(instance, pCreateInfo, nullptr, pDebugMessenger);
            return res == VK_SUCCESS;
        }
        return false;
    }

    // ����������������ز��������ٺ���
    void DestroyDebugUtilsMessengerEXT(VkInstance instance,
        VkDebugUtilsMessengerEXT debugMessenger) {
        if (vkDestroyDebugUtilsMessengerEXT) {
			vkDestroyDebugUtilsMessengerEXT(instance, debugMessenger, nullptr);
        }
    }

    //---

    bool EnumeratePhysicalDevices(VkInstance instance, uint32_t* pPhysicalDeviceCount, VkPhysicalDevice* pPhysicalDevices) {
#ifdef DEBUG_INFO
        if (!instance || !pPhysicalDeviceCount) { LOG_ERROR("VK: EnumeratePhysicalDevices params invalid"); return false; }
#endif
        VkResult res = vkEnumeratePhysicalDevices(instance, pPhysicalDeviceCount, pPhysicalDevices);
#ifdef DEBUG_INFO
        if (res < 0) { LOG_ERROR("VK: EnumeratePhysicalDevices failed: %d", res); }
#endif
        return res >= 0;
    }

    void GetPhysicalDeviceProperties(VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties* pProperties) {
#ifdef DEBUG_INFO
        if (!physicalDevice || !pProperties) return;
#endif
        vkGetPhysicalDeviceProperties(physicalDevice, pProperties);
    }

    void GetPhysicalDeviceFeatures(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures* pFeatures) {
#ifdef DEBUG_INFO
        if (!physicalDevice || !pFeatures) return;
#endif
        vkGetPhysicalDeviceFeatures(physicalDevice, pFeatures);
    }

    void GetPhysicalDeviceProperties2(VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties2* properties) {
        vkGetPhysicalDeviceProperties2(physicalDevice, properties);
    }

    void GetPhysicalDeviceFeatures2(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures2* pFeatures) {
#ifdef DEBUG_INFO
        if (!physicalDevice || !pFeatures) return;
#endif
        vkGetPhysicalDeviceFeatures2(physicalDevice, pFeatures);
    }

    void GetPhysicalDeviceMemoryProperties(VkPhysicalDevice physicalDevice, VkPhysicalDeviceMemoryProperties* pMemoryProperties) {
#ifdef DEBUG_INFO
        if (!physicalDevice || !pMemoryProperties) return;
#endif
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, pMemoryProperties);
    }

    void GetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice physicalDevice, uint32_t* pQueueFamilyCount, VkQueueFamilyProperties* pQueueFamilies) {
#ifdef DEBUG_INFO
        if (!physicalDevice || !pQueueFamilyCount) return;
#endif
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, pQueueFamilyCount, pQueueFamilies);
    }

    void EnumerateDeviceExtensionProperties(VkPhysicalDevice physicalDevice, const char* pLayerName, uint32_t* pPropertyCount, VkExtensionProperties* pProperties) {
#ifdef DEBUG_INFO
        if (!physicalDevice || !pPropertyCount) return;
#endif
        vkEnumerateDeviceExtensionProperties(physicalDevice, pLayerName, pPropertyCount, pProperties);
    }

    void EnumerateDeviceLayerProperties(VkPhysicalDevice physicalDevice, uint32_t* pPropertyCount, VkLayerProperties* pProperties) {
#ifdef DEBUG_INFO
        if (!physicalDevice || !pPropertyCount) return;
#endif
        vkEnumerateDeviceLayerProperties(physicalDevice, pPropertyCount, pProperties);
    }

    //----
    bool CreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo* pCreateInfo, VkDevice* pDevice) {
#ifdef DEBUG_INFO
        if (!physicalDevice || !pCreateInfo || !pDevice) { LOG_ERROR("VK: CreateDevice params invalid"); return false; }
#endif
        VkResult res = vkCreateDevice(physicalDevice, pCreateInfo, nullptr, pDevice);
#ifdef DEBUG_INFO
        if (res != VK_SUCCESS) { LOG_ERROR("VK: CreateDevice failed: %d", res); return false; }
        LOG_INFO("VK: Logical Device created successfully");
#endif
        printf("vkCreateDevice ptr = %p\n", vkCreateDevice);
        if (res == VK_SUCCESS) LoadDeviceFunctions(*pDevice);
        return res == VK_SUCCESS;
    }

    void DestroyDevice(VkDevice device) {
#ifdef DEBUG_INFO
        if (!device) return;
#endif
        vkDestroyDevice(device, nullptr);
    }

    void GetDeviceQueue(VkDevice device, uint32_t queueFamilyIndex, uint32_t queueIndex, VkQueue* pQueue) {
#ifdef DEBUG_INFO
        if (!device || !pQueue) return;
#endif
        vkGetDeviceQueue(device, queueFamilyIndex, queueIndex, pQueue);
    }

    void DeviceWaitIdle(VkDevice device) {
        if (device) {
            VkResult result = vkDeviceWaitIdle(device);
            if (result != VK_SUCCESS) {
                fprintf(stderr, "[ERROR] vkDeviceWaitIdle failed with code: %d\n", result);
                if (result == VK_ERROR_DEVICE_LOST) {
                    // ǿ�ƴ����ϵ㣨Windows �¿��� __debugbreak()��Linux ���� raise(SIGTRAP) ��ֱ�� assert��
                    assert(false && "GPU ������Device Lost ����������� WaitIdle ֮ǰ��");
                }
            }
        }
    }

    void QueueWaitIdle(VkQueue queue) {
        if (queue) vkQueueWaitIdle(queue);
    }

    bool QueueSubmit(VkQueue queue, uint32_t submitCount, const VkSubmitInfo* pSubmits, VkFence fence) {
#ifdef DEBUG_INFO
        if (!queue) { LOG_ERROR("VK: QueueSubmit - queue is null"); return false; }
#endif
        VkResult res = vkQueueSubmit(queue, submitCount, pSubmits, fence);
#ifdef DEBUG_INFO
        if (res != VK_SUCCESS) { 
           
            LOG_ERROR("VK: QueueSubmit failed: %d", res); 
        }
#endif
        return res == VK_SUCCESS;
    }

    //---
    bool AllocateMemory(VkDevice device, const VkMemoryAllocateInfo* pAllocateInfo, VkDeviceMemory* pMemory) {
#ifdef DEBUG_INFO
        if (!device || !pAllocateInfo || !pMemory) return false;
#endif
        VkResult res = vkAllocateMemory(device, pAllocateInfo, nullptr, pMemory);
#ifdef DEBUG_INFO
        if (res != VK_SUCCESS) { LOG_ERROR("VK: AllocateMemory failed: %d", res); }
#endif
        return res == VK_SUCCESS;
    }

    void FreeMemory(VkDevice device, VkDeviceMemory memory) {
        if (device && memory) vkFreeMemory(device, memory, nullptr);
    }

    bool MapMemory(VkDevice device, VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size, VkMemoryMapFlags flags, void** ppData) {
#ifdef DEBUG_INFO
        if (!device || !memory || !ppData) return false;
#endif
        VkResult res = vkMapMemory(device, memory, offset, size, flags, ppData);
#ifdef DEBUG_INFO
        if (res != VK_SUCCESS) { LOG_ERROR("VK: MapMemory failed: %d", res); }
#endif
        return res == VK_SUCCESS;
    }

    void UnmapMemory(VkDevice device, VkDeviceMemory memory) {
        if (device && memory) vkUnmapMemory(device, memory);
    }

    bool CreateBuffer(VkDevice device, const VkBufferCreateInfo* pCreateInfo, VkBuffer* pBuffer) {
#ifdef DEBUG_INFO
        if (!device || !pCreateInfo || !pBuffer) return false;
#endif
        VkResult res = vkCreateBuffer(device, pCreateInfo, nullptr, pBuffer);
#ifdef DEBUG_INFO
        if (res != VK_SUCCESS) { LOG_ERROR("VK: CreateBuffer failed: %d", res); }
#endif
        return res == VK_SUCCESS;
    }

    void DestroyBuffer(VkDevice device, VkBuffer buffer) {
        if (device && buffer) vkDestroyBuffer(device, buffer, nullptr);
    }

    void GetBufferMemoryRequirements(VkDevice device, VkBuffer buffer, VkMemoryRequirements* pMemoryRequirements) {
#ifdef DEBUG_INFO
        if (!device || !buffer || !pMemoryRequirements) return;
#endif
        vkGetBufferMemoryRequirements(device, buffer, pMemoryRequirements);
    }

    bool BindBufferMemory(VkDevice device, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize offset) {
        VkResult res = vkBindBufferMemory(device, buffer, memory, offset);
#ifdef DEBUG_INFO
        if (res != VK_SUCCESS) { LOG_ERROR("VK: BindBufferMemory failed: %d", res); }
#endif
        return res == VK_SUCCESS;
    }

    bool CreateBufferView(VkDevice device, const VkBufferViewCreateInfo* pCreateInfo, VkBufferView* pView) {
#ifdef DEBUG_INFO
        if (!device || !pCreateInfo || !pView) return false;
#endif
        VkResult res = vkCreateBufferView(device, pCreateInfo, nullptr, pView);
#ifdef DEBUG_INFO
        if (res != VK_SUCCESS) { LOG_ERROR("VK: CreateBufferView failed: %d", res); }
#endif
        return res == VK_SUCCESS;
    }

    void DestroyBufferView(VkDevice device, VkBufferView bufferView) {
        if (device && bufferView) vkDestroyBufferView(device, bufferView, nullptr);
    }

    bool CreateImage(VkDevice device, const VkImageCreateInfo* pCreateInfo, VkImage* pImage) {
#ifdef DEBUG_INFO
        if (!device || !pCreateInfo || !pImage) return false;
#endif
        VkResult res = vkCreateImage(device, pCreateInfo, nullptr, pImage);
#ifdef DEBUG_INFO
        if (res != VK_SUCCESS) { LOG_ERROR("VK: CreateImage failed: %d", res); }
#endif
        return res == VK_SUCCESS;
    }

    void DestroyImage(VkDevice device, VkImage image) {
        if (device && image) vkDestroyImage(device, image, nullptr);
    }

    void GetImageMemoryRequirements(VkDevice device, VkImage image, VkMemoryRequirements* pMemoryRequirements) {
#ifdef DEBUG_INFO
        if (!device || !image || !pMemoryRequirements) return;
#endif
        vkGetImageMemoryRequirements(device, image, pMemoryRequirements);
    }

    bool BindImageMemory(VkDevice device, VkImage image, VkDeviceMemory memory, VkDeviceSize offset) {
        VkResult res = vkBindImageMemory(device, image, memory, offset);
#ifdef DEBUG_INFO
        if (res != VK_SUCCESS) { LOG_ERROR("VK: BindImageMemory failed: %d", res); }
#endif
        return res == VK_SUCCESS;
    }

    bool CreateImageView(VkDevice device, const VkImageViewCreateInfo* pCreateInfo, VkImageView* pImageView) {
#ifdef DEBUG_INFO
        if (!device || !pCreateInfo || !pImageView) return false;
#endif
        VkResult res = vkCreateImageView(device, pCreateInfo, nullptr, pImageView);
#ifdef DEBUG_INFO
        if (res != VK_SUCCESS) { LOG_ERROR("VK: CreateImageView failed: %d", res); }
#endif
        return res == VK_SUCCESS;
    }

    void DestroyImageView(VkDevice device, VkImageView imageView) {
        if (device && imageView) vkDestroyImageView(device, imageView, nullptr);
    }

    bool CreateSampler(VkDevice device, const VkSamplerCreateInfo* pCreateInfo, VkSampler* pSampler) {
#ifdef DEBUG_INFO
        if (!device || !pCreateInfo || !pSampler) return false;
#endif
        VkResult res = vkCreateSampler(device, pCreateInfo, nullptr, pSampler);
#ifdef DEBUG_INFO
        if (res != VK_SUCCESS) { LOG_ERROR("VK: CreateSampler failed: %d", res); }
#endif
        return res == VK_SUCCESS;
    }

    void DestroySampler(VkDevice device, VkSampler sampler) {
        if (device && sampler) vkDestroySampler(device, sampler, nullptr);
    }

    //---
    bool CreateDescriptorSetLayout(VkDevice device, const VkDescriptorSetLayoutCreateInfo* pCreateInfo, VkDescriptorSetLayout* pSetLayout) {
#ifdef DEBUG_INFO
        if (!device || !pCreateInfo || !pSetLayout) return false;
#endif
        VkResult res = vkCreateDescriptorSetLayout(device, pCreateInfo, nullptr, pSetLayout);
#ifdef DEBUG_INFO
        if (res != VK_SUCCESS) { LOG_ERROR("VK: CreateDescriptorSetLayout failed: %d", res); }
#endif
        return res == VK_SUCCESS;
    }

    void DestroyDescriptorSetLayout(VkDevice device, VkDescriptorSetLayout descriptorSetLayout) {
        if (device && descriptorSetLayout) vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
    }

    bool CreateDescriptorPool(VkDevice device, const VkDescriptorPoolCreateInfo* pCreateInfo, VkDescriptorPool* pDescriptorPool) {
#ifdef DEBUG_INFO
        if (!device || !pCreateInfo || !pDescriptorPool) return false;
#endif
        VkResult res = vkCreateDescriptorPool(device, pCreateInfo, nullptr, pDescriptorPool);
#ifdef DEBUG_INFO
        if (res != VK_SUCCESS) { LOG_ERROR("VK: CreateDescriptorPool failed: %d", res); }
#endif
        return res == VK_SUCCESS;
    }

    void ResetDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool) {
        if (device && descriptorPool) vkResetDescriptorPool(device, descriptorPool, 0);
    }

    void DestroyDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool) {
        if (device && descriptorPool) vkDestroyDescriptorPool(device, descriptorPool, nullptr);
    }

    bool AllocateDescriptorSets(VkDevice device, const VkDescriptorSetAllocateInfo* pAllocateInfo, VkDescriptorSet* pDescriptorSets) {
#ifdef DEBUG_INFO
        if (!device || !pAllocateInfo || !pDescriptorSets) return false;
#endif
        VkResult res = vkAllocateDescriptorSets(device, pAllocateInfo, pDescriptorSets);
#ifdef DEBUG_INFO
        if (res != VK_SUCCESS) { LOG_ERROR("VK: AllocateDescriptorSets failed: %d", res); }
#endif
        return res == VK_SUCCESS;
    }

    void FreeDescriptorSets(VkDevice device, VkDescriptorPool descriptorPool, uint32_t descriptorSetCount, const VkDescriptorSet* pDescriptorSets) {
        if (device && descriptorPool && pDescriptorSets) vkFreeDescriptorSets(device, descriptorPool, descriptorSetCount, pDescriptorSets);
    }

    void UpdateDescriptorSets(VkDevice device, uint32_t descriptorWriteCount, const VkWriteDescriptorSet* pDescriptorWrites, uint32_t descriptorCopyCount, const VkCopyDescriptorSet* pDescriptorCopies) {
        if (device) vkUpdateDescriptorSets(device, descriptorWriteCount, pDescriptorWrites, descriptorCopyCount, pDescriptorCopies);
    }

    //---
    bool CreateShaderModule(VkDevice device, const VkShaderModuleCreateInfo* pCreateInfo, VkShaderModule* pShaderModule) {
#ifdef DEBUG_INFO
        if (!device || !pCreateInfo || !pShaderModule) return false;
#endif
        VkResult res = vkCreateShaderModule(device, pCreateInfo, nullptr, pShaderModule);
#ifdef DEBUG_INFO
        if (res != VK_SUCCESS) { LOG_ERROR("VK: CreateShaderModule failed: %d", res); }
#endif
        return res == VK_SUCCESS;
    }

    void DestroyShaderModule(VkDevice device, VkShaderModule shaderModule) {
        if (device && shaderModule) vkDestroyShaderModule(device, shaderModule, nullptr);
    }

    bool CreatePipelineLayout(VkDevice device, const VkPipelineLayoutCreateInfo* pCreateInfo, VkPipelineLayout* pPipelineLayout) {
#ifdef DEBUG_INFO
        if (!device || !pCreateInfo || !pPipelineLayout) return false;
#endif
        VkResult res = vkCreatePipelineLayout(device, pCreateInfo, nullptr, pPipelineLayout);
#ifdef DEBUG_INFO
        if (res != VK_SUCCESS) { LOG_ERROR("VK: CreatePipelineLayout failed: %d", res); }
#endif
        return res == VK_SUCCESS;
    }

    void DestroyPipelineLayout(VkDevice device, VkPipelineLayout layout) {
        if (device && layout) vkDestroyPipelineLayout(device, layout, nullptr);
    }

    bool CreateGraphicsPipelines(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkGraphicsPipelineCreateInfo* pCreateInfos, VkPipeline* pPipelines) {
#ifdef DEBUG_INFO
        if (!device || !pCreateInfos || !pPipelines) return false;
#endif
        VkResult res = vkCreateGraphicsPipelines(device, pipelineCache, createInfoCount, pCreateInfos, nullptr, pPipelines);
#ifdef DEBUG_INFO
        if (res != VK_SUCCESS) { LOG_ERROR("VK: CreateGraphicsPipelines failed: %d", res); }
#endif
        return res == VK_SUCCESS;
    }

    bool CreateComputePipelines(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkComputePipelineCreateInfo* pCreateInfos, VkPipeline* pPipelines) {
#ifdef DEBUG_INFO
        if (!device || !pCreateInfos || !pPipelines) return false;
#endif
        VkResult res = vkCreateComputePipelines(device, pipelineCache, createInfoCount, pCreateInfos, nullptr, pPipelines);
#ifdef DEBUG_INFO
        if (res != VK_SUCCESS) { LOG_ERROR("VK: CreateComputePipelines failed: %d", res); }
#endif
        return res == VK_SUCCESS;
    }

    bool CreateRayTracingPipelinesKHR(VkDevice device, VkDeferredOperationKHR deferredOperation, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkRayTracingPipelineCreateInfoKHR* pCreateInfos, VkPipeline* pPipelines) {
#ifdef DEBUG_INFO
        if (!device || !pCreateInfos || !pPipelines) return false;
#endif
        VkResult res = vkCreateRayTracingPipelinesKHR(device, deferredOperation, pipelineCache, createInfoCount, pCreateInfos, nullptr, pPipelines);
#ifdef DEBUG_INFO
        if (res != VK_SUCCESS) { LOG_ERROR("VK: CreateRayTracingPipelinesKHR failed: %d", res); }
#endif
        return res == VK_SUCCESS;
    }
    
    bool GetRayTracingShaderGroupHandlesKHR(VkDevice device, VkPipeline pipeline, uint32_t firstGroup, uint32_t groupCount, size_t dataSize, void* pData) {
#ifdef DEBUG_INFO
        if (!device || !pipeline || !pData) return false;
#endif
        VkResult res = vkGetRayTracingShaderGroupHandlesKHR(device, pipeline, firstGroup, groupCount, dataSize, pData);
#ifdef DEBUG_INFO
        if (res != VK_SUCCESS) { LOG_ERROR("VK: GetRayTracingShaderGroupHandlesKHR failed: %d", res); }
#endif
        return res == VK_SUCCESS;
    }

    void DestroyPipeline(VkDevice device, VkPipeline pipeline) {
        if (device && pipeline) vkDestroyPipeline(device, pipeline, nullptr);
    }

    bool CreateAccelerationStructureKHR(VkDevice device, const VkAccelerationStructureCreateInfoKHR* pCreateInfo, VkAccelerationStructureKHR* pAccelerationStructure) {
#ifdef DEBUG_INFO
        if (!device || !pCreateInfo || !pAccelerationStructure) return false;
#endif
        VkResult res = vkCreateAccelerationStructureKHR(device, pCreateInfo, nullptr, pAccelerationStructure);
#ifdef DEBUG_INFO
        if (res != VK_SUCCESS) { LOG_ERROR("VK: CreateAccelerationStructureKHR failed: %d", res); }
#endif
        return res == VK_SUCCESS;
    }

    void DestroyAccelerationStructureKHR(VkDevice device, VkAccelerationStructureKHR accelerationStructure) {
        if (device && accelerationStructure) vkDestroyAccelerationStructureKHR(device, accelerationStructure, nullptr);
    }

    void GetAccelerationStructureBuildSizesKHR(VkDevice device, VkAccelerationStructureBuildTypeKHR buildType, const VkAccelerationStructureBuildGeometryInfoKHR* pBuildInfo, const uint32_t* pMaxPrimitiveCounts, VkAccelerationStructureBuildSizesInfoKHR* pSizeInfo) {
        if (device && pBuildInfo && pMaxPrimitiveCounts && pSizeInfo) {
            vkGetAccelerationStructureBuildSizesKHR(device, buildType, pBuildInfo, pMaxPrimitiveCounts, pSizeInfo);
        }
    }

    void CmdBuildAccelerationStructuresKHR(VkCommandBuffer commandBuffer, uint32_t infoCount, const VkAccelerationStructureBuildGeometryInfoKHR* pInfos, const VkAccelerationStructureBuildRangeInfoKHR* const* ppBuildRangeInfos) {
        if (commandBuffer && pInfos && ppBuildRangeInfos) {
            vkCmdBuildAccelerationStructuresKHR(commandBuffer, infoCount, pInfos, ppBuildRangeInfos);
        }
    }

    VkDeviceAddress GetAccelerationStructureDeviceAddressKHR(VkDevice device, const VkAccelerationStructureDeviceAddressInfoKHR* pInfo) {
        if (!device || !pInfo) return 0;
        return vkGetAccelerationStructureDeviceAddressKHR(device, pInfo);
    }

    VkDeviceAddress GetBufferDeviceAddress(VkDevice device, const VkBufferDeviceAddressInfo* pInfo) {
        if (!device || !pInfo) return 0;
        return vkGetBufferDeviceAddress(device, pInfo);
    }

    //---
    bool CreateRenderPass(VkDevice device, const VkRenderPassCreateInfo* pCreateInfo, VkRenderPass* pRenderPass) {
#ifdef DEBUG_INFO
        if (!device || !pCreateInfo || !pRenderPass) return false;
#endif
        VkResult res = vkCreateRenderPass(device, pCreateInfo, nullptr, pRenderPass);
#ifdef DEBUG_INFO
        if (res != VK_SUCCESS) { LOG_ERROR("VK: CreateRenderPass failed: %d", res); }
#endif
        return res == VK_SUCCESS;
    }

    void DestroyRenderPass(VkDevice device, VkRenderPass renderPass) {
        if (device && renderPass) vkDestroyRenderPass(device, renderPass, nullptr);
    }

    bool CreateFramebuffer(VkDevice device, const VkFramebufferCreateInfo* pCreateInfo, VkFramebuffer* pFramebuffer) {
#ifdef DEBUG_INFO
        if (!device || !pCreateInfo || !pFramebuffer) return false;
#endif
        VkResult res = vkCreateFramebuffer(device, pCreateInfo, nullptr, pFramebuffer);
#ifdef DEBUG_INFO
        if (res != VK_SUCCESS) { LOG_ERROR("VK: CreateFramebuffer failed: %d", res); }
#endif
        return res == VK_SUCCESS;
    }

    void DestroyFramebuffer(VkDevice device, VkFramebuffer framebuffer) {
        if (device && framebuffer) vkDestroyFramebuffer(device, framebuffer, nullptr);
    }

    //----
    bool CreateCommandPool(VkDevice device, const VkCommandPoolCreateInfo* pCreateInfo, VkCommandPool* pCommandPool) {
#ifdef DEBUG_INFO
        if (!device || !pCreateInfo || !pCommandPool) return false;
#endif
        VkResult res = vkCreateCommandPool(device, pCreateInfo, nullptr, pCommandPool);
#ifdef DEBUG_INFO
        if (res != VK_SUCCESS) { LOG_ERROR("VK: CreateCommandPool failed: %d", res); }
#endif
        return res == VK_SUCCESS;
    }

    void ResetCommandPool(VkDevice device, VkCommandPool commandPool, VkCommandPoolResetFlags flags) {
        if (device && commandPool) vkResetCommandPool(device, commandPool, flags);
    }

    void DestroyCommandPool(VkDevice device, VkCommandPool commandPool) {
        if (device && commandPool) vkDestroyCommandPool(device, commandPool, nullptr);
    }

    bool AllocateCommandBuffers(VkDevice device, const VkCommandBufferAllocateInfo* pAllocateInfo, VkCommandBuffer* pCommandBuffers) {
#ifdef DEBUG_INFO
        if (!device || !pAllocateInfo || !pCommandBuffers) return false;
#endif
        VkResult res = vkAllocateCommandBuffers(device, pAllocateInfo, pCommandBuffers);
#ifdef DEBUG_INFO
        if (res != VK_SUCCESS) { LOG_ERROR("VK: AllocateCommandBuffers failed: %d", res); }
#endif
        return res == VK_SUCCESS;
    }

    void FreeCommandBuffers(VkDevice device, VkCommandPool commandPool, uint32_t commandBufferCount, const VkCommandBuffer* pCommandBuffers) {
        if (device && commandPool && pCommandBuffers) vkFreeCommandBuffers(device, commandPool, commandBufferCount, pCommandBuffers);
    }

    void BeginCommandBuffer(VkCommandBuffer commandBuffer, const VkCommandBufferBeginInfo* pBeginInfo) {
#ifdef DEBUG_INFO
        if (!commandBuffer || !pBeginInfo) return;
#endif
        VkResult res = vkBeginCommandBuffer(commandBuffer, pBeginInfo);
#ifdef DEBUG_INFO
        if (res != VK_SUCCESS) { LOG_ERROR("VK: BeginCommandBuffer failed: %d", res); }
#endif
    }

    void EndCommandBuffer(VkCommandBuffer commandBuffer) {
        if (commandBuffer) {
            VkResult res = vkEndCommandBuffer(commandBuffer);
#ifdef DEBUG_INFO
            if (res != VK_SUCCESS) { LOG_ERROR("VK: EndCommandBuffer failed: %d", res); }
#endif
        }
    }

    void ResetCommandBuffer(VkCommandBuffer commandBuffer, VkCommandBufferResetFlags flags) {
        if (commandBuffer) vkResetCommandBuffer(commandBuffer, flags);
    }

    // ָ��¼�ƽӿ� (�����������Ŀռ��)
    void CmdBindPipeline(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipeline pipeline) {
        if (commandBuffer && pipeline) vkCmdBindPipeline(commandBuffer, pipelineBindPoint, pipeline);
    }

    void CmdBindDescriptorSets(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout, uint32_t firstSet, uint32_t descriptorSetCount, const VkDescriptorSet* pDescriptorSets, uint32_t dynamicOffsetCount, const uint32_t* pDynamicOffsets) {
        if (commandBuffer && layout && pDescriptorSets) vkCmdBindDescriptorSets(commandBuffer, pipelineBindPoint, layout, firstSet, descriptorSetCount, pDescriptorSets, dynamicOffsetCount, pDynamicOffsets);
    }

    void CmdBindVertexBuffers(VkCommandBuffer commandBuffer, uint32_t firstBinding, uint32_t bindingCount, const VkBuffer* pBuffers, const VkDeviceSize* pOffsets) {
        if (commandBuffer && pBuffers && pOffsets) vkCmdBindVertexBuffers(commandBuffer, firstBinding, bindingCount, pBuffers, pOffsets);
    }

    void CmdBindIndexBuffer(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkIndexType indexType) {
        if (commandBuffer && buffer) vkCmdBindIndexBuffer(commandBuffer, buffer, offset, indexType);
    }

    void CmdSetViewport(VkCommandBuffer commandBuffer, uint32_t firstViewport, uint32_t viewportCount, const VkViewport* pViewports) {
        if (commandBuffer && pViewports) vkCmdSetViewport(commandBuffer, firstViewport, viewportCount, pViewports);
    }

    void CmdSetScissor(VkCommandBuffer commandBuffer, uint32_t firstScissor, uint32_t scissorCount, const VkRect2D* pScissors) {
        if (commandBuffer && pScissors) vkCmdSetScissor(commandBuffer, firstScissor, scissorCount, pScissors);
    }

    void CmdDraw(VkCommandBuffer commandBuffer, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) {
        if (commandBuffer) vkCmdDraw(commandBuffer, vertexCount, instanceCount, firstVertex, firstInstance);
    }

    void CmdDrawIndexed(VkCommandBuffer commandBuffer, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) {
        if (commandBuffer) vkCmdDrawIndexed(commandBuffer, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
    }

    void CmdDispatch(VkCommandBuffer commandBuffer, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) {
        if (commandBuffer) vkCmdDispatch(commandBuffer, groupCountX, groupCountY, groupCountZ);
    }

    void CmdCopyBuffer(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferCopy* pRegions) {
        if (commandBuffer && srcBuffer && dstBuffer && pRegions) vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, regionCount, pRegions);
    }
    void CmdCopyBufferToImage(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkBufferImageCopy* pRegions) {
        if (commandBuffer && srcBuffer && dstImage && pRegions) vkCmdCopyBufferToImage(commandBuffer, srcBuffer, dstImage, dstImageLayout, regionCount, pRegions);
    }
    void CmdCopyImageToBuffer(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferImageCopy* pRegions)
    {
        if (commandBuffer && srcImage && dstBuffer && pRegions) vkCmdCopyImageToBuffer(commandBuffer, srcImage, srcImageLayout,dstBuffer, regionCount, pRegions);
    }
    void CmdCopyImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageCopy* pRegions)
    {
        if (commandBuffer && srcImage && dstImage && pRegions) vkCmdCopyImage(commandBuffer, srcImage, srcImageLayout, dstImage, dstImageLayout, regionCount, pRegions);
    }
    void CmdBlitImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageBlit* pRegions, VkFilter filter)
    {
        if (commandBuffer && srcImage && dstImage && pRegions) vkCmdBlitImage(commandBuffer, srcImage, srcImageLayout, dstImage, dstImageLayout, regionCount, pRegions, filter);
    }

    void CmdPipelineBarrier(VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask, VkDependencyFlags dependencyFlags, uint32_t memoryBarrierCount, const VkMemoryBarrier* pMemoryBarriers, uint32_t bufferMemoryBarrierCount, const VkBufferMemoryBarrier* pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount, const VkImageMemoryBarrier* pImageMemoryBarriers) {
        if (commandBuffer) vkCmdPipelineBarrier(commandBuffer, srcStageMask, dstStageMask, dependencyFlags, memoryBarrierCount, pMemoryBarriers, bufferMemoryBarrierCount, pBufferMemoryBarriers, imageMemoryBarrierCount, pImageMemoryBarriers);
    }
    void CmdBeginRenderPass(VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo* pRenderPassBegin, VkSubpassContents contents)
    {
        if (commandBuffer && pRenderPassBegin) vkCmdBeginRenderPass(commandBuffer, pRenderPassBegin, contents);
    }
    void CmdEndRenderPass(VkCommandBuffer commandBuffer)
    {
        if (commandBuffer) vkCmdEndRenderPass(commandBuffer);
    }

        void CmdCopyAccelerationStructureKHR(VkCommandBuffer commandBuffer, const VkCopyAccelerationStructureInfoKHR* pInfo)
        {
                if (commandBuffer && pInfo) {
                        vkCmdCopyAccelerationStructureKHR(commandBuffer, pInfo);
                }
        }

        void CmdCopyAccelerationStructureToMemoryKHR(VkCommandBuffer commandBuffer, const VkCopyAccelerationStructureToMemoryInfoKHR* pInfo)
        {
                if (commandBuffer && pInfo) {
                        vkCmdCopyAccelerationStructureToMemoryKHR(commandBuffer, pInfo);
                }
        }

        void CmdCopyMemoryToAccelerationStructureKHR(VkCommandBuffer commandBuffer, const VkCopyMemoryToAccelerationStructureInfoKHR* pInfo)
        {
                if (commandBuffer && pInfo) {
                        vkCmdCopyMemoryToAccelerationStructureKHR(commandBuffer, pInfo);
                }
        }

        void CmdWriteAccelerationStructuresPropertiesKHR(VkCommandBuffer commandBuffer, uint32_t accelerationStructureCount, const VkAccelerationStructureKHR* pAccelerationStructures, VkQueryType queryType, VkQueryPool queryPool, uint32_t firstQuery)
        {
                if (commandBuffer && pAccelerationStructures && queryPool) {
                        vkCmdWriteAccelerationStructuresPropertiesKHR(commandBuffer, accelerationStructureCount, pAccelerationStructures, queryType, queryPool, firstQuery);
                }
        }

        void CmdTraceRaysKHR(VkCommandBuffer commandBuffer, const VkStridedDeviceAddressRegionKHR* pRaygenShaderBindingTable, const VkStridedDeviceAddressRegionKHR* pMissShaderBindingTable, const VkStridedDeviceAddressRegionKHR* pHitShaderBindingTable, const VkStridedDeviceAddressRegionKHR* pCallableShaderBindingTable, uint32_t width, uint32_t height, uint32_t depth)
        {
                if (commandBuffer && pRaygenShaderBindingTable && pMissShaderBindingTable && pHitShaderBindingTable && pCallableShaderBindingTable) {
                        vkCmdTraceRaysKHR(commandBuffer, pRaygenShaderBindingTable, pMissShaderBindingTable, pHitShaderBindingTable, pCallableShaderBindingTable, width, height, depth);
                }
        }

        void CmdTraceRaysIndirectKHR(VkCommandBuffer commandBuffer, const VkStridedDeviceAddressRegionKHR* pRaygenShaderBindingTable, const VkStridedDeviceAddressRegionKHR* pMissShaderBindingTable, const VkStridedDeviceAddressRegionKHR* pHitShaderBindingTable, const VkStridedDeviceAddressRegionKHR* pCallableShaderBindingTable, VkDeviceAddress indirectDeviceAddress)
        {
                if (commandBuffer && pRaygenShaderBindingTable && pMissShaderBindingTable && pHitShaderBindingTable && pCallableShaderBindingTable) {
                        vkCmdTraceRaysIndirectKHR(commandBuffer, pRaygenShaderBindingTable, pMissShaderBindingTable, pHitShaderBindingTable, pCallableShaderBindingTable, indirectDeviceAddress);
                }
        }
    //----
    bool CreateFence(VkDevice device, const VkFenceCreateInfo* pCreateInfo, VkFence* pFence) {
#ifdef DEBUG_INFO
        if (!device || !pCreateInfo || !pFence) return false;
#endif
        VkResult res = vkCreateFence(device, pCreateInfo, nullptr, pFence);
#ifdef DEBUG_INFO
        if (res != VK_SUCCESS) { LOG_ERROR("VK: CreateFence failed: %d", res); }
#endif
        return res == VK_SUCCESS;
    }

    void DestroyFence(VkDevice device, VkFence fence) {
        if (device && fence) vkDestroyFence(device, fence, nullptr);
    }

    bool GetFenceStatus(VkDevice device, VkFence fence) {
        if (!device || !fence) return false;
        return vkGetFenceStatus(device, fence) == VK_SUCCESS;
    }

    bool WaitForFences(VkDevice device, uint32_t fenceCount, const VkFence* pFences, VkBool32 waitAll, uint64_t timeout) {
#ifdef DEBUG_INFO
        if (!device || !pFences) return false;
#endif
        VkResult res = vkWaitForFences(device, fenceCount, pFences, waitAll, timeout);
#ifdef DEBUG_INFO
        if (res != VK_SUCCESS && res != VK_TIMEOUT) { LOG_ERROR("VK: WaitForFences failed: %d", res); }
#endif
        return res == VK_SUCCESS;
    }

    void ResetFences(VkDevice device, uint32_t fenceCount, const VkFence* pFences) {
        if (device && pFences) vkResetFences(device, fenceCount, pFences);
    }

    bool CreateSemaphore_(VkDevice device, const VkSemaphoreCreateInfo* pCreateInfo, VkSemaphore* pSemaphore) {
#ifdef DEBUG_INFO
        if (!device || !pCreateInfo || !pSemaphore) return false;
#endif
        VkResult res = vkCreateSemaphore(device, pCreateInfo, nullptr, pSemaphore);
#ifdef DEBUG_INFO
        if (res != VK_SUCCESS) { LOG_ERROR("VK: CreateSemaphore failed: %d", res); }
#endif
        return res == VK_SUCCESS;
    }

    void DestroySemaphore(VkDevice device, VkSemaphore semaphore) {
        if (device && semaphore) vkDestroySemaphore(device, semaphore, nullptr);
    }

    void GetSemaphoreCounterValue(VkDevice device, VkSemaphore semaphore, uint64_t* pValue) {
        if (device && semaphore && pValue) vkGetSemaphoreCounterValue(device, semaphore, pValue);
    }

    bool WaitSemaphores(VkDevice device, const VkSemaphoreWaitInfo* pWaitInfo, uint64_t timeout) {
        VkResult res = vkWaitSemaphores(device, pWaitInfo, timeout);
        return res == VK_SUCCESS;
    }

    bool CreateEvent_(VkDevice device, const VkEventCreateInfo* pCreateInfo, VkEvent* pEvent) {
#ifdef DEBUG_INFO
        if (!device || !pCreateInfo || !pEvent) return false;
#endif
        VkResult res = vkCreateEvent(device, pCreateInfo, nullptr, pEvent);
#ifdef DEBUG_INFO
        if (res != VK_SUCCESS) { LOG_ERROR("VK: CreateEvent failed: %d", res); }
#endif
        return res == VK_SUCCESS;
    }

    void DestroyEvent(VkDevice device, VkEvent event) {
        if (device && event) vkDestroyEvent(device, event, nullptr);
    }

    void SetEvent(VkDevice device, VkEvent event) {
        if (device && event) vkSetEvent(device, event);
    }

    void ResetEvent(VkDevice device, VkEvent event) {
        if (device && event) vkResetEvent(device, event);
    }

    bool GetEventStatus(VkDevice device, VkEvent event) {
        if (!device || !event) return false;
        return vkGetEventStatus(device, event) == VK_EVENT_SET;
    }

    //---
    bool CreateWin32SurfaceKHR(VkInstance instance, const VkWin32SurfaceCreateInfoKHR* pCreateInfo, VkSurfaceKHR* pSurface) {
#ifdef DEBUG_INFO
        if (!instance || !pCreateInfo || !pSurface) return false;
#endif
        VkResult res = vkCreateWin32SurfaceKHR(instance, pCreateInfo, nullptr, pSurface);
#ifdef DEBUG_INFO
        if (res != VK_SUCCESS) { LOG_ERROR("VK: CreateWin32SurfaceKHR failed: %d", res); }
#endif
        return res == VK_SUCCESS;
    }

    void DestroySurfaceKHR(VkInstance instance, VkSurfaceKHR surface) {
        if (instance && surface) vkDestroySurfaceKHR(instance, surface, nullptr);
    }

    void GetPhysicalDeviceSurfaceSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, VkSurfaceKHR surface, VkBool32* pSupported) {
        if (physicalDevice && surface && pSupported) vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, queueFamilyIndex, surface, pSupported);
    }

    void GetPhysicalDeviceSurfaceCapabilitiesKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, VkSurfaceCapabilitiesKHR* pCapabilities) {
        if (physicalDevice && surface && pCapabilities) vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, pCapabilities);
    }

    void GetPhysicalDeviceSurfaceFormatsKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t* pSurfaceFormatCount, VkSurfaceFormatKHR* pSurfaceFormats) {
        if (physicalDevice && surface && pSurfaceFormatCount) vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, pSurfaceFormatCount, pSurfaceFormats);
    }

    void GetPhysicalDeviceSurfacePresentModesKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t* pPresentModeCount, VkPresentModeKHR* pPresentModes) {
        if (physicalDevice && surface && pPresentModeCount) vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, pPresentModeCount, pPresentModes);
    }

    bool CreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR* pCreateInfo, VkSwapchainKHR* pSwapchain) {
#ifdef DEBUG_INFO
        if (!device || !pCreateInfo || !pSwapchain) return false;
#endif
        VkResult res = vkCreateSwapchainKHR(device, pCreateInfo, nullptr, pSwapchain);
#ifdef DEBUG_INFO
        if (res != VK_SUCCESS) { LOG_ERROR("VK: CreateSwapchainKHR failed: %d", res); }
#endif
        return res == VK_SUCCESS;
    }

    void DestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain) {
        if (device && swapchain) vkDestroySwapchainKHR(device, swapchain, nullptr);
    }

    void GetSwapchainImagesKHR(VkDevice device, VkSwapchainKHR swapchain, uint32_t* pImageCount, VkImage* pImages) {
        if (device && swapchain && pImageCount) vkGetSwapchainImagesKHR(device, swapchain, pImageCount, pImages);
    }

    bool AcquireNextImageKHR(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout, VkSemaphore semaphore, VkFence fence, uint32_t* pImageIndex) {
        VkResult res = vkAcquireNextImageKHR(device, swapchain, timeout, semaphore, fence, pImageIndex);
#ifdef DEBUG_INFO
        if (res != VK_SUCCESS && res != VK_SUBOPTIMAL_KHR) { LOG_ERROR("VK: AcquireNextImageKHR failed: %d", res); }
#endif
        return (res == VK_SUCCESS || res == VK_SUBOPTIMAL_KHR);
    }

    bool QueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pPresentInfo) {
        if (!queue || !pPresentInfo) return false;
        VkResult res = vkQueuePresentKHR(queue, pPresentInfo);
        return res == VK_SUCCESS;
    }

    void GetDeviceFaultInfoEXT(VkDevice device, VkDeviceFaultCountsEXT* pFaultCounts, VkDeviceFaultInfoEXT* pFaultInfo)
    {
        vkGetDeviceFaultInfoEXT(device, pFaultCounts, pFaultInfo);
    }











}
