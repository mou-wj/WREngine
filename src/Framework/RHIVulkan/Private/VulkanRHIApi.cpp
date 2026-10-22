#include "VulkanRHIApi.h"
#include <iostream>
#include <vector>
#include "vulkan/vulkan.h"
#include "VulkanResource.h"
#include "RHIUtils.h"
#include "VulkanSwapchain.h"
#include "VulkanPipelineState.h"
#include "VulkanPlatformSurport.h"
#include "VulkanCommandContex.h"
#include "VulkanCommandBuffer.h"
#include "VulkanMemory.h"
#include "VulkanRHIUtils.h"
#include "VulkanShader.h"
#include "VulkanTransientResource.h"
#include "RHIPipelineStateCache.h"
#include "VulkanBarriers.h"
#include "RHITransition.h"
#include "VulkanQueue.h"
#include "RHICaptureHelper.h"
#include "VulkanFuncWrapper.h"

#define DynamicPtrCast(ptr, type) (std::dynamic_pointer_cast<type>(ptr))
VkDevice deviceLocal;
static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
	VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
	VkDebugUtilsMessageTypeFlagsEXT messageType,
	const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
	void* pUserData) {

	// 如果严重程度大于警告，可以使用红色输出或断点
	if (messageSeverity > VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
		fprintf(stderr, "Validation Layer: %s\n", pCallbackData->pMessage);

	}

	return VK_FALSE; // 永远返回 FALSE，否则 API 会在报错处中断并返回错误码
}

using namespace  RHI;
namespace RHIVulkan{

// 析构函数实现
    VulkanRHIApi::~VulkanRHIApi()
{
    Shutdown();
}

// 初始化和销毁接口实现
bool VulkanRHIApi::Init()
{
	if (m_bValidFlag) {
		return true;
	}
	VKFunc::InitializeLoader();
	GShaderPlatform = ERHIShaderPlatform::Vulkan;
    // 创建Vulkan实例
    VkApplicationInfo appInfo = {};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "WREngine";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "WREngine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
	VkValidationFeatureEnableEXT enabledFeatures[] = {
			VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT,             // 开启 GPU 辅助校验
			VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_RESERVE_BINDING_SLOT_EXT // 为其预留 Descriptor 槽位
	};

	VkValidationFeaturesEXT validationFeatures{};
	validationFeatures.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;
	validationFeatures.pNext = nullptr;
	validationFeatures.enabledValidationFeatureCount = 2;
	validationFeatures.pEnabledValidationFeatures = enabledFeatures;
	validationFeatures.disabledValidationFeatureCount = 0;
	validationFeatures.pDisabledValidationFeatures = nullptr;
	//createInfo.pNext = &validationFeatures;

    // 可以在这里添加扩展和验证层
	auto wantExtensions = GetWantedInstanceExtensions();
	createInfo.enabledExtensionCount = static_cast<uint32_t>(wantExtensions.size());
	createInfo.ppEnabledExtensionNames = wantExtensions.data();
	// 这里可以添加更多的实例创建信息，如验证层等
	auto wantedLayers = GetWantedInstanceLayers();
	createInfo.enabledLayerCount = static_cast<uint32_t>(wantedLayers.size());
	createInfo.ppEnabledLayerNames = wantedLayers.data();


    // 创建Vulkan实例
    bool result = VKFunc::CreateInstance(&createInfo, &m_Instance);
    if (result != true)
    {
        // 处理错误
        return false;
    }
#ifdef DEBUG_INFO
	// 设置调试回调（如果需要）
	// 这里可以添加创建 Debug Messenger 的逻辑
	VkDebugUtilsMessengerCreateInfoEXT debugMessagerCreateInfo{};
	debugMessagerCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;

	// 设置你关心的消息严重程度
	debugMessagerCreateInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
		VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
		VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;

	// 设置你关心的消息类型
	debugMessagerCreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
		VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
		VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;

	// 指定回调函数
	debugMessagerCreateInfo.pfnUserCallback = DebugCallback;
	debugMessagerCreateInfo.pUserData = nullptr; // 可选

	if (!VKFunc::CreateDebugUtilsMessengerEXT(m_Instance, &debugMessagerCreateInfo,  &m_DebugMessenger) != VK_SUCCESS) {
		assert(0);
	}
#endif

	//RHI::RHICaptureHelper::GetInstance().Init();
	// 初始化Vulkan设备和其他资源
	// 这里可以添加更多的初始化逻辑，如选择物理设备、创建逻辑设备等
	m_PhysicalDevice = PickPhysicalDevice();
	if (m_PhysicalDevice == VK_NULL_HANDLE)
	{
		return false;
	}
	VulkanDevice* device = new VulkanDevice(this, m_PhysicalDevice);
	deviceLocal = device->GetHandle();
	
	wantExtensions = GetWantedDeviceExtensions();
	wantedLayers = GetWantedDeviceLayers();

	if (!device->Init(wantedLayers, wantExtensions)) {
		delete device;
		device = nullptr;
		return false;
	}
	m_Device = device;
	// 1. 确定头大小
	uint32_t headerSize = sizeof(RHITransition);

	// 2. 确定私有数据的大小和对齐要求
	// 注意：如果你的私有数据需要动态支持多个 Barrier，
	// 这里的 PrivateSize 可以预留一个“常用最大值”，或者只存固定头
	uint32_t privateSize = sizeof(VulkanPipelineBarrier);
	uint32_t alignment = alignof(VulkanPipelineBarrier);

	// 3. 计算偏移量 (对齐 Header 之后的地址)
	G_RHITransition_PrivateDataOffset = (headerSize + alignment - 1) & ~(alignment - 1);

	// 4. 计算总分配大小
	G_RHITransition_TotalSize = G_RHITransition_PrivateDataOffset + privateSize;

	m_PlatformInfo.bEnableRayTracing = true;
	m_bValidFlag = true;
    return true;
    // 初始化其他Vulkan资源
    // 如物理设备、逻辑设备、队列等
}

void VulkanRHIApi::Shutdown()
{
	if (!m_bValidFlag) {
		return;
	}
	m_PlatformInfo.bEnableRayTracing = false;
	m_bValidFlag = false;
	RHI::RHIPipelineStateCache::ClearAll();

	delete m_Device;
	m_Device = nullptr;


    // 销毁Vulkan实例和其他资源
    if (m_Instance != VK_NULL_HANDLE)
    {
#ifdef DEBUG_INFO
		VKFunc::DestroyDebugUtilsMessengerEXT(m_Instance, m_DebugMessenger);
#endif
        VKFunc::DestroyInstance(m_Instance);
        m_Instance = VK_NULL_HANDLE;
    }

    // 销毁其他Vulkan资源
}

const RHIPlatformInfo& VulkanRHIApi::GetPlatformInfo() const
{
	return m_PlatformInfo;
}


// 资源创建接口实现
RHITextureSP VulkanRHIApi::CreateTexture(const RHITextureDesc& desc)
{
    // 创建Vulkan纹理资源
    // 这里需要实现Vulkan特定的纹理创建逻辑
    return DynamicPtrCast(std::make_shared<VulkanTexture>(m_Device, desc),RHITexture); // 暂时返回nullptr，实际实现需要返回有效的纹理对象
}

RHIBufferSP VulkanRHIApi::CreateBuffer(const RHIBufferDesc& desc)
{
    // 创建Vulkan缓冲区资源
    // 这里需要实现Vulkan特定的缓冲区创建逻辑
    return DynamicPtrCast(std::make_shared<VulkanBuffer>(m_Device, desc), RHIBuffer); // 暂时返回nullptr，实际实现需要返回有效的缓冲区对象
}




void VulkanRHIApi::UpdateTexture(RHICommandListBase& cmdList, RHITexture* texture, const void* data,const RHIUpdateTextureRegion& region)
{
	if (!texture || !data)
		return;
	auto format = texture->GetDesc().Format;

	VkDeviceSize totalSize = region.Width * region.Height * region.Depth * RHI::GFormatInfoMap.at(format).BytesPerPixel; // 假设格式是RGBA8


	// 1. 获取 staging buffer
	auto staging = m_Device->GetStagingManager()->Acquire(totalSize);
	void* mapped = staging->Map(0, totalSize);


	// 复制数据到 staging buffer
	memcpy(mapped, data, totalSize);

	auto vkFormat = TransformFormatFrom(format);
	auto imageFlag = GetImageAspectFlags(vkFormat);

	// 3. 记录 CopyBufferToImage 命令
	VkBufferImageCopy copyRegion{};
	copyRegion.bufferOffset = 0;
	copyRegion.bufferRowLength = 0;
	copyRegion.bufferImageHeight = 0;
	copyRegion.imageSubresource.aspectMask = imageFlag;
	copyRegion.imageSubresource.mipLevel = region.MipLevel;
	copyRegion.imageSubresource.baseArrayLayer = region.ArraySlice;
	copyRegion.imageSubresource.layerCount = region.NumArraySlices;
	copyRegion.imageOffset = { static_cast<int32_t>(region.XOffset),
	static_cast<int32_t>(region.YOffset),
	static_cast<int32_t>(region.ZOffset) };
	copyRegion.imageExtent = { region.Width, region.Height, region.Depth };

	VulkanTexture* vulkanTexture = dynamic_cast<VulkanTexture*>(texture);

	cmdList.AddCommand<VulkanCommandUpdateTexture>(vulkanTexture, staging, copyRegion);
	

}
void VulkanRHIApi::UpdateBuffer(RHICommandListBase& cmdList, RHIBuffer* buffer, const void* data, const RHIUpdateBufferRegion& region)
{
	if (!buffer || !data)
		return;

	VkDeviceSize totalSize = region.Size;

	// 1. 获取 staging buffer
	auto staging = m_Device->GetStagingManager()->Acquire(totalSize);
	void* mapped = staging->Map(0, totalSize);

	// 2. 拷贝 CPU 数据
	memcpy(mapped, data, totalSize);

	// 3. 构造 CopyBuffer 区域
	VkBufferCopy copyRegion{};
	copyRegion.srcOffset = 0;
	copyRegion.dstOffset = region.Offset;
	copyRegion.size = totalSize;

	VulkanBuffer* vulkanBuffer = dynamic_cast<VulkanBuffer*>(buffer);

	// 4. 记录 command
	cmdList.AddCommand<VulkanCommandUpdateBuffer>(vulkanBuffer, staging, copyRegion);
}

void* VulkanRHIApi::MapReadTexture(RHICommandListBase& cmdList, RHITexture* texture, const RHIReadTextureInfo& info)
{
	if (!texture)
		return nullptr;

	VulkanTexture* vkTexture =
		dynamic_cast<VulkanTexture*>(texture);

	const auto& desc =
		texture->GetDesc();

	const auto format =
		desc.Format;

	const auto vkFormat =
		TransformFormatFrom(format);

	const uint32_t bpp =
		RHI::GFormatInfoMap.at(format)
		.BytesPerPixel;

	// mip size
	auto mipsize = texture->GetMipSize(info.MipLevel);
	const uint32_t width = mipsize.X;
	const uint32_t height = mipsize.Y;
	const uint32_t depth = mipsize.Z;

	//---------------------------------------
	// Vulkan row pitch alignment
	//---------------------------------------

	VkDeviceSize tightRowPitch =
		width * bpp;

	// Vulkan buffer copy alignment
	VkDeviceSize alignedRowPitch =
		tightRowPitch;

	VkDeviceSize totalSize =
		alignedRowPitch *
		height *
		depth;

	//---------------------------------------
	// staging readback buffer
	//---------------------------------------

	auto staging =
		m_Device->GetStagingManager()
		->Acquire(totalSize);
	m_Device->GetStagingManager()->MarkMappedBuffersUsed(staging,true);
	
	//---------------------------------------
	// copy region
	//---------------------------------------

	VkBufferImageCopy region{};

	region.bufferOffset = 0;

	region.bufferRowLength = 0;
	region.bufferImageHeight = 0;

	region.imageSubresource.aspectMask =
		GetImageAspectFlags(vkFormat);

	region.imageSubresource.mipLevel =
		info.MipLevel;

	region.imageSubresource.baseArrayLayer =
		info.ArraySlice;

	region.imageSubresource.layerCount = info.ArrayCount;

	region.imageOffset = { 0, 0, 0 };

	region.imageExtent =
	{
		width,
		height,
		depth
	};

	//---------------------------------------
	// enqueue gpu copy
	//---------------------------------------

	cmdList.AddCommand<
		VulkanCommandReadTexture>(
			vkTexture,
			staging,
			region);

	//---------------------------------------
	// map readback
	//---------------------------------------

	void* mapped =
		staging->Map(0, totalSize);
	m_MappedStagingBuffers[mapped] = staging;

	return mapped;
}

void* VulkanRHIApi::MapReadBuffer(RHICommandListBase& cmdList, RHIBuffer* buffer, const RHIReadBufferInfo& info)
{
	if (!buffer)
		return nullptr;

	VulkanBuffer* vkBuffer =
		dynamic_cast<VulkanBuffer*>(buffer);

	const auto& desc =
		buffer->GetDesc();

	//---------------------------------------
	// region size
	//---------------------------------------

	VkDeviceSize offset =
		info.Offset;

	VkDeviceSize size =
		info.Size;

	if (size == 0)
	{
		size =
			desc.Size - offset;
	}

	//---------------------------------------
	// staging readback buffer
	//---------------------------------------

	auto staging =
		m_Device->GetStagingManager()
		->Acquire(size);

	m_Device->GetStagingManager()
		->MarkMappedBuffersUsed(
			staging,
			true);

	//---------------------------------------
	// copy region
	//---------------------------------------

	VkBufferCopy region{};

	region.srcOffset =
		offset;

	region.dstOffset = 0;

	region.size =
		size;

	//---------------------------------------
	// enqueue gpu copy
	//---------------------------------------

	cmdList.AddCommand<
		VulkanCommandReadBuffer>(
			vkBuffer,
			staging,
			region);

	//---------------------------------------
	// map readback memory
	//---------------------------------------

	void* mapped =
		staging->Map(
			0,
			size);

	m_MappedStagingBuffers[mapped] =
		staging;

	return mapped;
}

void VulkanRHIApi::Unmap(void* mappedPtr)
{
	auto it = m_MappedStagingBuffers.find(mappedPtr);
	if (it != m_MappedStagingBuffers.end())
	{
		it->second->Unmap();
		m_Device->GetStagingManager()->MarkMappedBuffersUsed(it->second, false);
		m_MappedStagingBuffers.erase(it);
	}
}

RHIShaderResourceViewSP VulkanRHIApi::CreateTextureShaderResourceView(
	RHITexture* texture, const RHITexSRVCreateInfo& desc)
{
	return DynamicPtrCast(std::make_shared<VulkanShaderResourceView>(m_Device, texture, desc), RHIShaderResourceView);
}

RHIUnorderedAccessViewSP VulkanRHIApi::CreateTextureUnorderedAccessView(
	RHITexture* texture, const RHITexUAVCreateInfo& desc)
{
	return DynamicPtrCast(std::make_shared<VulkanUnorderedAccessView>(m_Device, texture, desc), RHIUnorderedAccessView);
}

RHIShaderResourceViewSP VulkanRHIApi::CreateBufferShaderResourceView(
	RHIBuffer* buffer, const RHIBufferSRVCreateInfo& desc)
{
	return DynamicPtrCast(std::make_shared<VulkanShaderResourceView>(m_Device, buffer, desc), RHIShaderResourceView);
}

RHIUnorderedAccessViewSP VulkanRHIApi::CreateBufferUnorderedAccessView(
	RHIBuffer* buffer, const RHIBufferUAVCreateInfo& desc)
{
	return DynamicPtrCast(std::make_shared<VulkanUnorderedAccessView>(m_Device, buffer, desc), RHIUnorderedAccessView);
}

RHIRayTracingGeometrySP VulkanRHIApi::CreateRayTracingGeometry(const RHIRayTracingGeometryDesc& desc)
{
	return DynamicPtrCast(std::make_shared<VulkanRayTracingGeometry>(m_Device, desc), RHIRayTracingGeometry);
}

RHIRayTracingInstanceSP VulkanRHIApi::CreateRayTracingInstance(const RHIRayTracingInstancesDesc& desc)
{
	return DynamicPtrCast(std::make_shared<VulkanRayTracingInstance>(m_Device, desc), RHIRayTracingInstance);
}

// 创建 StagingBuffer
RHIStagingBufferSP VulkanRHIApi::CreateStagingBuffer(uint32_t size)
{
	return DynamicPtrCast(m_Device->GetStagingManager()->Acquire(size), RHIStagingBuffer) ;
}

// 管线状态相关接口实现
RHIGraphicsPipelineStateSP VulkanRHIApi::CreateGraphicsPipelineState(const RHIGraphicsPipelineStateDesc& desc)
{
    // 创建Vulkan图形管线状态
    return DynamicPtrCast(std::make_shared<VulkanGraphicsPipelineState>(m_Device, desc), RHIGraphicsPipelineState);; // 暂时返回nullptr
}

RHIComputePipelineStateSP VulkanRHIApi::CreateComputePipelineState(const RHIComputePipelineStateDesc& desc)
{
    // 创建Vulkan计算管线状态
    return DynamicPtrCast(std::make_shared<VulkanComputePipelineState>(m_Device, desc), RHIComputePipelineState);; // 暂时返回nullptr
}

RHIRayTracingPipelineStateSP VulkanRHIApi::CreateRayTracingsPipelineState(const RHIRayTracingPipelineStateDesc& desc)
{
    // 创建Vulkan光线追踪管线状态
    return DynamicPtrCast(std::make_shared<VulkanRayTracingPipeline>(m_Device, desc), RHIRayTracingPipelineState);; // 暂时返回nullptr
}

RHIVertexDescStateSP VulkanRHIApi::CreateVertexDescState(const RHIVertexDescStateDesc& desc)
{
    // 创建Vulkan顶点描述状态
    return DynamicPtrCast(std::make_shared<VulkanVertexDescState>(m_Device, desc), RHIVertexDescState);; // 暂时返回nullptr
}

RHIRasterizerStateSP VulkanRHIApi::CreateRasterizerState(const RHIRasterizerStateDesc& desc)
{
    // 创建Vulkan光栅化状态
    return DynamicPtrCast(std::make_shared<VulkanRasterizerState>(m_Device, desc), RHIRasterizerState);; // 暂时返回nullptr
}

RHIColorBlendStateSP VulkanRHIApi::CreateColorBlendState(const RHIColorBlendStateDesc& desc)
{
    // 创建Vulkan颜色混合状态
    return DynamicPtrCast(std::make_shared<VulkanColorBlendState>(m_Device, desc), RHIColorBlendState); // 暂时返回nullptr
}

RHIDepthStencilStateSP VulkanRHIApi::CreateDepthStencilState(const RHIDepthStencilStateDesc& desc)
{
    // 创建Vulkan深度模板状态
    return DynamicPtrCast(std::make_shared<VulkanDepthStencilState>(m_Device, desc), RHIDepthStencilState);; // 暂时返回nullptr
}

RHIVertexShaderSP VulkanRHIApi::CreateVertexShader(const std::vector<char>& shaderSourceCode)
{
    // 创建Vulkan顶点着色器
    return DynamicPtrCast(m_Device->GetShaderManager()->GetOrCreateShader<VulkanRHIVertexShader>(shaderSourceCode), RHIVertexShader); // 暂时返回nullptr
}

RHIFragmentShaderSP VulkanRHIApi::CreateFragmentShader(const std::vector<char>& shaderSourceCode)
{
    // 创建Vulkan片段着色器
    return DynamicPtrCast(m_Device->GetShaderManager()->GetOrCreateShader<VulkanRHIFragmentShader>(shaderSourceCode), RHIFragmentShader); // 暂时返回nullptr
}

RHIComputeShaderSP VulkanRHIApi::CreateComputeShader(const std::vector<char>& shaderSourceCode)
{
    // 创建Vulkan计算着色器
    return DynamicPtrCast(m_Device->GetShaderManager()->GetOrCreateShader<VulkanRHIComputeShader>(shaderSourceCode), RHIComputeShader); // 暂时返回nullptr
}

RHIGeometryShaderSP VulkanRHIApi::CreateGeometryShader(const std::vector<char>& shaderSourceCode)
{
    // 创建Vulkan几何着色器
    return DynamicPtrCast(m_Device->GetShaderManager()->GetOrCreateShader<VulkanRHIGeometryShader>(shaderSourceCode), RHIGeometryShader); // 暂时返回nullptr
}

RHITessControlShaderSP VulkanRHIApi::CreateTessControlShader(const std::vector<char>& shaderSourceCode)
{
    // 创建Vulkan细分控制着色器
    return DynamicPtrCast(m_Device->GetShaderManager()->GetOrCreateShader<VulkanRHITessControlShader>(shaderSourceCode), RHITessControlShader); // 暂时返回nullptr
}

RHITessEvalShaderSP VulkanRHIApi::CreateTessEvalShader(const std::vector<char>& shaderSourceCode)
{
    // 创建Vulkan细分评估着色器
    return DynamicPtrCast(m_Device->GetShaderManager()->GetOrCreateShader<VulkanRHITessEvalShader>(shaderSourceCode), RHITessEvalShader); // 暂时返回nullptr
}

RHIMeshShaderSP VulkanRHIApi::CreateMeshShader(const std::vector<char>& shaderSourceCode)
{
    // 创建Vulkan网格着色器
    return DynamicPtrCast(m_Device->GetShaderManager()->GetOrCreateShader<VulkanRHIMeshShader>(shaderSourceCode), RHIMeshShader); // 暂时返回nullptr
}

RHITaskShaderSP VulkanRHIApi::CreateTaskShader(const std::vector<char>& shaderSourceCode)
{
    // 创建Vulkan任务着色器
    return DynamicPtrCast(m_Device->GetShaderManager()->GetOrCreateShader<VulkanRHITaskShader>(shaderSourceCode), RHITaskShader); // 暂时返回nullptr
}

RHIRayGenShaderSP VulkanRHIApi::CreateRayGenShader(const std::vector<char>& shaderSourceCode)
{
    // 创建Vulkan光线生成着色器
    return DynamicPtrCast(m_Device->GetShaderManager()->GetOrCreateShader<VulkanRHIRayGenShader>(shaderSourceCode), RHIRayGenShader); // 暂时返回nullptr
}

RHICloseHitShaderSP VulkanRHIApi::CreateCloseHitShader(const std::vector<char>& shaderSourceCode)
{
    // 创建Vulkan最近命中着色器
    return DynamicPtrCast(m_Device->GetShaderManager()->GetOrCreateShader<VulkanRHICloseHitShader>(shaderSourceCode), RHICloseHitShader); // 暂时返回nullptr
}

RHIMissShaderSP VulkanRHIApi::CreateMissShader(const std::vector<char>& shaderSourceCode)
{
    // 创建Vulkan未命中着色器
    return DynamicPtrCast(m_Device->GetShaderManager()->GetOrCreateShader<VulkanRHIMissShader>(shaderSourceCode), RHIMissShader); // 暂时返回nullptr
}

RHIAnyHitShaderSP VulkanRHIApi::CreateAnyHitShader(const std::vector<char>& shaderSourceCode)
{
    // 创建Vulkan任何命中着色器
    return DynamicPtrCast(m_Device->GetShaderManager()->GetOrCreateShader<VulkanRHIAnyHitShader>(shaderSourceCode), RHIAnyHitShader); // 暂时返回nullptr
}

RHIIntersectionShaderSP VulkanRHIApi::CreateIntersectionShader(const std::vector<char>& shaderSourceCode)
{
    // 创建Vulkan相交着色器
    return DynamicPtrCast(m_Device->GetShaderManager()->GetOrCreateShader<VulkanRHIIntersectionShader>(shaderSourceCode), RHIIntersectionShader); // 暂时返回nullptr
}

RHICallableShaderSP VulkanRHIApi::CreateCallableShader(const std::vector<char>& shaderSourceCode)
{
    // 创建Vulkan可调用着色器
    return DynamicPtrCast(m_Device->GetShaderManager()->GetOrCreateShader<VulkanRHICallableShader>(shaderSourceCode), RHICallableShader); // 暂时返回nullptr
}


RHISwapchainSP VulkanRHIApi::CreateSwapchain(void* inWindowHandle, uint32_t w, uint32_t h, ERHIFormat format)
{
	return std::make_shared<VulkanRHISwapchain>(m_Device, w, h, inWindowHandle, format);
}

RHISamplerSP VulkanRHIApi::CreateSampler(const RHISamplerDesc& desc)
{
    // 创建Vulkan采样器
    return std::make_shared<VulkanSampler>(m_Device,desc); // 暂时返回nullptr
}

RHIQueue* VulkanRHIApi::GetQueue(EQueueType type)
{
	switch (type)
	{
	case EQueueType::Graphics:
		return m_Device ? m_Device->GetGraphicsQueue() : nullptr;
	case EQueueType::Compute:
		return m_Device ? (m_Device->GetComputeQueue() ? m_Device->GetComputeQueue() : m_Device->GetGraphicsQueue()) : nullptr;
	default:
		return nullptr;
	}
}

RHIPresentExecutor* VulkanRHIApi::GetPresentExecutor()
{
	return m_Device->GetPresentExecutor();
}

void VulkanRHIApi::RHICreateTransition(RHITransition* transition, const RHITransitionCreateInfo& createInfo)
{
    

    if (!transition)
        return;

    VulkanPipelineBarrier* pipelineBarrier = transition->GetPrivateData<VulkanPipelineBarrier>();
    if (!pipelineBarrier)
        return;

    // placement new 确保私有数据对象被构造
    new (pipelineBarrier) VulkanPipelineBarrier();

	auto resolveQueueFamilyIndex = [this](EQueueType queueType) -> uint32_t
	{
		if (!m_Device)
		{
			return VK_QUEUE_FAMILY_IGNORED;
		}
		switch (queueType)
		{
		case EQueueType::Graphics:
			return m_Device->GetGraphicsQueue() ? m_Device->GetGraphicsQueue()->GetFamilyIndex() : VK_QUEUE_FAMILY_IGNORED;
		case EQueueType::Compute:
			if (m_Device->GetComputeQueue())
			{
				return m_Device->GetComputeQueue()->GetFamilyIndex();
			}
			return m_Device->GetGraphicsQueue() ? m_Device->GetGraphicsQueue()->GetFamilyIndex() : VK_QUEUE_FAMILY_IGNORED;
		default:
			return VK_QUEUE_FAMILY_IGNORED;
		}
	};


    for (const auto& transitionInfo : createInfo.TransitionInfos)
    {
        if (transitionInfo.Type == RHITransitionInfo::EType::Texture)
        {
            VulkanTexture* vulkanTexture = static_cast<VulkanTexture*>(transitionInfo.Texture);
            if (!vulkanTexture)
                continue;
            ERHIResourceAccess accessBefore = transitionInfo.AccessBefore;
            ERHIResourceAccess accessAfter = transitionInfo.AccessAfter;

            VkImageLayout oldLayout = DetermineImageLayout(accessBefore);
            VkImageLayout newLayout = DetermineImageLayout(accessAfter);

            if (accessBefore == accessAfter)
            {
                continue;
            }

			const EQueueType srcQueueType = transitionInfo.QueueTypeBefore;
			const EQueueType dstQueueType = transitionInfo.QueueTypeAfter;
			const uint32_t srcQueueFamilyIndex = resolveQueueFamilyIndex(srcQueueType);
			const uint32_t dstQueueFamilyIndex = resolveQueueFamilyIndex(dstQueueType);
			const bool crossQueueOwnership = srcQueueFamilyIndex != VK_QUEUE_FAMILY_IGNORED
				&& dstQueueFamilyIndex != VK_QUEUE_FAMILY_IGNORED
				&& srcQueueFamilyIndex != dstQueueFamilyIndex;
			const uint32_t barrierSrcQueueFamily = crossQueueOwnership ? srcQueueFamilyIndex : VK_QUEUE_FAMILY_IGNORED;
			const uint32_t barrierDstQueueFamily = crossQueueOwnership ? dstQueueFamilyIndex : VK_QUEUE_FAMILY_IGNORED;

			auto textureDesc = vulkanTexture->GetDesc();
			VkImageSubresourceRange subresourceRange{};
            subresourceRange.aspectMask = vulkanTexture->GetAspectFlags();
            subresourceRange.baseMipLevel = transitionInfo.MipIndex != RHISubresourceRange::KAllSubresources ? transitionInfo.MipIndex : 0;
            subresourceRange.levelCount = transitionInfo.MipIndex != RHISubresourceRange::KAllSubresources ? 1 : vulkanTexture->GetDesc().MipLevels;
            subresourceRange.baseArrayLayer = transitionInfo.ArraySlice != RHISubresourceRange::KAllSubresources ? transitionInfo.ArraySlice : 0;
            subresourceRange.layerCount = transitionInfo.ArraySlice != RHISubresourceRange::KAllSubresources ? 1 : vulkanTexture->GetDesc().ArraySize;

			pipelineBarrier->TransitionLayout(vulkanTexture->GetImage(), oldLayout, newLayout, subresourceRange, barrierSrcQueueFamily, barrierDstQueueFamily);
        }
		else if (transitionInfo.Type == RHITransitionInfo::EType::UAV) {
			VulkanUnorderedAccessView* vulkanUAV = static_cast<VulkanUnorderedAccessView*>(transitionInfo.UAV);
			if (!vulkanUAV) continue;

			if (vulkanUAV->IsTexture())
			{
				// 1. 从 UAV 句柄中提取底层的 Texture 指针
				// 注意：这里需要你的 UAV 类提供获取 Resource 的接口
				VulkanTexture* vulkanTexture = static_cast<VulkanTexture*>(vulkanUAV->GetResource());

				// 2. 提取 UAV 定义的子资源范围
				// 这一点非常重要！UAV 可能只覆盖了某一个 Mip 或 Layer
				VkImageSubresourceRange subresourceRange{};
				subresourceRange.aspectMask = vulkanTexture->GetAspectFlags();
				subresourceRange.baseMipLevel = vulkanUAV->GetBaseMipLevel();
				subresourceRange.levelCount = vulkanUAV->GetMipLevelCount();
				subresourceRange.baseArrayLayer = vulkanUAV->GetBaseArrayLayer();
				subresourceRange.layerCount = vulkanUAV->GetLayerCount();

				const EQueueType srcQueueType = transitionInfo.QueueTypeBefore;
				const EQueueType dstQueueType = transitionInfo.QueueTypeAfter;
				const uint32_t srcQueueFamilyIndex = resolveQueueFamilyIndex(srcQueueType);
				const uint32_t dstQueueFamilyIndex = resolveQueueFamilyIndex(dstQueueType);
				const bool crossQueueOwnership = srcQueueFamilyIndex != VK_QUEUE_FAMILY_IGNORED
					&& dstQueueFamilyIndex != VK_QUEUE_FAMILY_IGNORED
					&& srcQueueFamilyIndex != dstQueueFamilyIndex;
				const uint32_t barrierSrcQueueFamily = crossQueueOwnership ? srcQueueFamilyIndex : VK_QUEUE_FAMILY_IGNORED;
				const uint32_t barrierDstQueueFamily = crossQueueOwnership ? dstQueueFamilyIndex : VK_QUEUE_FAMILY_IGNORED;

				pipelineBarrier->TransitionAccess(vulkanTexture->GetImage(), transitionInfo.AccessBefore, transitionInfo.AccessAfter, subresourceRange, barrierSrcQueueFamily, barrierDstQueueFamily);

			}
			else if (vulkanUAV->IsBuffer())
			{
				VulkanBuffer* vulkanBuffer = static_cast<VulkanBuffer*>(vulkanUAV->GetResource());
                pipelineBarrier->TransitionMemory(transitionInfo.AccessBefore, transitionInfo.AccessAfter);

				// Buffer 不需要 Layout，直接记录 Access 变化用于生成 VkBufferMemoryBarrier

			}
		}
        else if (transitionInfo.Type == RHITransitionInfo::EType::Buffer ||
                 transitionInfo.Type == RHITransitionInfo::EType::BVH)
        {
            pipelineBarrier->TransitionMemory(transitionInfo.AccessBefore, transitionInfo.AccessAfter);
        }
    }

    // 不在这里执行命令缓冲区屏障，交给命令上下文（Begin/End）执行。
}


void VulkanRHIApi::RHIReleaseTransition(RHITransition* transition)
{
    if (!transition)
        return;

    VulkanPipelineBarrier* pipelineBarrier = transition->GetPrivateData<VulkanPipelineBarrier>();
    if (pipelineBarrier)
    {
        pipelineBarrier->~VulkanPipelineBarrier();
    }
}

// 创建上下文接口实现
RHITransientResourceManagerSP VulkanRHIApi::CreateTransientResourceManager()
{
	return std::make_shared<VulkanTransientResourceManager>(m_Device);
}

VkPhysicalDevice VulkanRHIApi::PickPhysicalDevice() {
	// 选择合适的物理设备
	uint32_t deviceCount = 0;
	VKFunc::EnumeratePhysicalDevices(m_Instance, &deviceCount, nullptr);
	if (deviceCount == 0) {
		std::cerr << "No Vulkan-compatible devices found!" << std::endl;
		return VK_NULL_HANDLE;
	}
	std::vector<VkPhysicalDevice> physicalDevices(deviceCount);
	VKFunc::EnumeratePhysicalDevices(m_Instance, &deviceCount, physicalDevices.data());
	// 这里可以添加更多的逻辑来选择最合适的物理设备
	// 例如检查设备的特性、支持的扩展等
	auto preferredVendor = GetPreferredVendorId();
	for (const auto & device : physicalDevices) {
		VkPhysicalDeviceProperties deviceProperties;
		VKFunc::GetPhysicalDeviceProperties(device, &deviceProperties);

		VkPhysicalDeviceFeatures deviceFeatures;
		VKFunc::GetPhysicalDeviceFeatures(device, &deviceFeatures);
		std::cout << "Found device: " << deviceProperties.deviceName << std::endl;
		// 这里可以添加更多的检查逻辑
        if (GetVendorIdFromUint32(deviceProperties.vendorID) == preferredVendor) {
			return device; // 返回第一个符合条件的设备
        }
	}
	if (!physicalDevices.empty()) {
		return physicalDevices[0];
	}
    return nullptr;


}


std::vector<const char*> VulkanRHIApi::GetWantedInstanceLayers() {
	std::vector<const char*> wantLayers = VulkanPlatformSupport::GetPlatformWantedLayers();
    std::vector<const char*> res;
    //检查是否支持
	uint32_t layerCount = 0;
	VKFunc::EnumerateInstanceLayerProperties(&layerCount, nullptr);
	if (layerCount > 0) {
		std::vector<VkLayerProperties> availableLayers(layerCount);
		VKFunc::EnumerateInstanceLayerProperties(&layerCount, availableLayers.data());
		for (const auto& layer : availableLayers) {
			for (const auto& wantLayer : wantLayers) {
				if (strcmp(layer.layerName, wantLayer) == 0) {
                    res.push_back(wantLayer);
				}
			}
		}
	}
    return res;
}

std::vector<const char*> VulkanRHIApi::GetWantedInstanceExtensions() {
	std::vector<const char*> wantExtensions = VulkanPlatformSupport::GetPlatformWantedExtentions();
    std::vector<const char*> res;
	// 检查是否支持
	uint32_t extensionCount = 0;
	VKFunc::EnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
	if (extensionCount > 0) {
		std::vector<VkExtensionProperties> availableExtensions(extensionCount);
		VKFunc::EnumerateInstanceExtensionProperties(nullptr, &extensionCount, availableExtensions.data());
		for (const auto& extension : availableExtensions) {
			for (const auto& wantExtension : wantExtensions) {
				if (strcmp(extension.extensionName, wantExtension) == 0) {
					res.push_back(wantExtension);
				}
			}
		}
	}
    return res;
}

std::vector<const char*> VulkanRHIApi::GetWantedDeviceLayers() {
	std::vector<const char*> wantLayers = {
	};
	std::vector<const char*> res;
	// 检查是否支持
	uint32_t layerCount = 0;
	VKFunc::EnumerateDeviceLayerProperties(m_PhysicalDevice, &layerCount, nullptr);
	if (layerCount > 0) {
		std::vector<VkLayerProperties> availableLayers(layerCount);
		VKFunc::EnumerateDeviceLayerProperties(m_PhysicalDevice, &layerCount, availableLayers.data());
		for (const auto& layer : availableLayers) {
			for (const auto& wantLayer : wantLayers) {
				if (strcmp(layer.layerName, wantLayer) == 0) {
					res.push_back(wantLayer);
				}
			}
		}
	}
	return res;
}

std::vector<const char*> VulkanRHIApi::GetWantedDeviceExtensions() {
	std::vector<const char*> wantExtensions = VulkanPlatformSupport::GetPlatformWantedDeviceExtentions();
	wantExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME); // 交换链扩展
	wantExtensions.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
    wantExtensions.push_back(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
    wantExtensions.push_back(VK_KHR_RAY_QUERY_EXTENSION_NAME);
    wantExtensions.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
	wantExtensions.push_back(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);
	//dx编译器引入的，后续需要考虑移除
	wantExtensions.push_back(VK_KHR_COMPUTE_SHADER_DERIVATIVES_EXTENSION_NAME);
	wantExtensions.push_back(VK_NV_COMPUTE_SHADER_DERIVATIVES_EXTENSION_NAME);

    std::vector<const char*> res;
	// 检查是否支持
	uint32_t extensionCount = 0;
	VKFunc::EnumerateDeviceExtensionProperties(m_PhysicalDevice,VK_NULL_HANDLE, &extensionCount, nullptr);
	if (extensionCount > 0) {
		std::vector<VkExtensionProperties> availableExtensions(extensionCount);
		VKFunc::EnumerateDeviceExtensionProperties(m_PhysicalDevice, VK_NULL_HANDLE, &extensionCount, availableExtensions.data());
		for (const auto& extension : availableExtensions) {
			for (const auto& wantExtension : wantExtensions) {
				if (strcmp(extension.extensionName, wantExtension) == 0) {
					res.push_back(wantExtension);
				}
			}
		}
	}
	return res;
}







VulkanRHIModule::VulkanRHIModule() {

}
VulkanRHIModule::~VulkanRHIModule() {
}
void VulkanRHIModule::StartupModule()
{
	RHI::GRHIApi = CreateRHIApi();
	RHI::GRHIApi->Init();
	m_bLoaded = true;
}

void VulkanRHIModule::ShutdownModule()
{
	
	delete RHI::GRHIApi;
	RHI::GRHIApi = nullptr;
	m_bLoaded = false;
}

bool VulkanRHIModule::IsLoaded() const
{
	return m_bLoaded;
}

RHIApi* VulkanRHIModule::CreateRHIApi()
{
	// Module 只负责创建，不负责生命周期
	return new VulkanRHIApi();
}

IMPLEMENT_SIMPLE_MODULE(VulkanRHIModule, "RHIVulkan");


}