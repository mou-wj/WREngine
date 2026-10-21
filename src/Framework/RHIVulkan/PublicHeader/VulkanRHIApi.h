#pragma once
#include "vulkan/vulkan.h"
#include "RHIApi.h"




namespace RHIVulkan{
    using namespace RHI;
class VulkanDevice;
class VulkanQueue;
class VulkanPresentExecutor;
class VulkanStagingBuffer;
class RHIVULKAN_API VulkanRHIApi : public RHIApi
{
public:
    ~VulkanRHIApi() override;

    bool Init() override;
    void Shutdown() override;
    const RHIPlatformInfo& GetPlatformInfo() const override;
    VkInstance GetInstance() const { return Instance; }

    RHITextureSP CreateTexture(const RHITextureDesc& desc) override;
    RHIBufferSP CreateBuffer(const RHIBufferDesc& desc) override;
    void UpdateTexture(RHICommandListBase& cmdList, RHITexture* texture, const void* data,const RHIUpdateTextureRegion& size) override;
    void UpdateBuffer(RHICommandListBase& cmdList, RHIBuffer* buffer, const void* data, const RHIUpdateBufferRegion& region) override;
    void* MapReadTexture(RHICommandListBase& cmdList, RHITexture* texture, const RHIReadTextureInfo& info) override;
    void* MapReadBuffer(RHICommandListBase& cmdList, RHIBuffer* buffer, const RHIReadBufferInfo& info) override;
    void Unmap(void* mappedPtr) override;
    RHIShaderResourceViewSP CreateTextureShaderResourceView(
        RHITexture* Texture, const RHITexSRVCreateInfo& Desc) override;

    RHIUnorderedAccessViewSP CreateTextureUnorderedAccessView(
        RHITexture* Texture, const RHITexUAVCreateInfo& Desc) override;

    RHIShaderResourceViewSP CreateBufferShaderResourceView(
        RHIBuffer* Buffer, const RHIBufferSRVCreateInfo& Desc) override;

    RHIUnorderedAccessViewSP CreateBufferUnorderedAccessView(
        RHIBuffer* Buffer, const RHIBufferUAVCreateInfo& Desc) override;
    RHIRayTracingGeometrySP CreateRayTracingGeometry(const RHIRayTracingGeometryDesc& desc) override;
    RHIRayTracingInstanceSP CreateRayTracingInstance(const RHIRayTracingInstancesDesc& desc) override;

    // 创建 StagingBuffer
    RHIStagingBufferSP CreateStagingBuffer(uint32_t size) override;

    RHIGraphicsPipelineStateSP CreateGraphicsPipelineState(const RHIGraphicsPipelineStateDesc& desc) override;
    RHIComputePipelineStateSP CreateComputePipelineState(const RHIComputePipelineStateDesc& desc) override;
    RHIRayTracingPipelineStateSP CreateRayTracingsPipelineState(const RHIRayTracingPipelineStateDesc& desc) override;
    
    RHIVertexDescStateSP CreateVertexDescState(const RHIVertexDescStateDesc& desc) override;
    RHIRasterizerStateSP CreateRasterizerState(const RHIRasterizerStateDesc& desc) override;
    RHIColorBlendStateSP CreateColorBlendState(const RHIColorBlendStateDesc& desc) override;
    RHIDepthStencilStateSP CreateDepthStencilState(const RHIDepthStencilStateDesc& desc) override;

    RHIVertexShaderSP CreateVertexShader(const std::vector<char>& shaderSourceCode) override;
    RHIFragmentShaderSP CreateFragmentShader(const std::vector<char>& shaderSourceCode) override;
    RHIComputeShaderSP CreateComputeShader(const std::vector<char>& shaderSourceCode) override;
    RHIGeometryShaderSP CreateGeometryShader(const std::vector<char>& shaderSourceCode) override;
    RHITessControlShaderSP CreateTessControlShader(const std::vector<char>& shaderSourceCode) override;
    RHITessEvalShaderSP CreateTessEvalShader(const std::vector<char>& shaderSourceCode) override;
    RHIMeshShaderSP CreateMeshShader(const std::vector<char>& shaderSourceCode) override;
    RHITaskShaderSP CreateTaskShader(const std::vector<char>& shaderSourceCode) override;
    RHIRayGenShaderSP CreateRayGenShader(const std::vector<char>& shaderSourceCode) override;
    RHICloseHitShaderSP CreateCloseHitShader(const std::vector<char>& shaderSourceCode) override;
    RHIMissShaderSP CreateMissShader(const std::vector<char>& shaderSourceCode) override;
    RHIAnyHitShaderSP CreateAnyHitShader(const std::vector<char>& shaderSourceCode) override;
    RHIIntersectionShaderSP CreateIntersectionShader(const std::vector<char>& shaderSourceCode) override;
    RHICallableShaderSP CreateCallableShader(const std::vector<char>& shaderSourceCode) override;
    RHISwapchainSP CreateSwapchain(void* inWindowHandle, uint32_t w, uint32_t h, ERHIFormat format) override;
    RHISamplerSP CreateSampler(const RHISamplerDesc& desc) override;

    RHIQueue* GetQueue(EQueueType Type) override;
    RHIPresentExecutor* GetPresentExecutor() override;

    void RHICreateTransition(RHITransition* Transition, const RHITransitionCreateInfo& CreateInfo) override;
    void RHIReleaseTransition(RHITransition* Transition) override;

    RHITransientResourceManagerSP CreateTransientResourceManager() override;
private:
	VkPhysicalDevice PickPhysicalDevice();

    std::vector<const char*> GetWantedInstanceLayers();

    std::vector<const char*> GetWantedInstanceExtensions();

	std::vector<const char*> GetWantedDeviceLayers();

    std::vector<const char*> GetWantedDeviceExtensions();
    std::map<void*,std::shared_ptr<VulkanStagingBuffer>> MappedStagingBuffers;


    // Vulkan 设备和上下文资源
    VkInstance Instance = VK_NULL_HANDLE;

    // 其他 Vulkan 资源管理结构
	VulkanDevice* Device = nullptr;
	VkPhysicalDevice PhysicalDevice = nullptr;
    // Keep the exported class layout identical in DLL and client builds.
    VkDebugUtilsMessengerEXT DebugMessenger = VK_NULL_HANDLE;
    RHI::RHIPlatformInfo PlatformInfo{ .DepthRange = RHI::EDepthRange::ZeroToOne, .EnableRayTracing = false };
    bool ValidFlag = false;
};


class RHIVULKAN_API VulkanRHIModule final : public RHI::RHIModule
{
public:
    VulkanRHIModule();
    ~VulkanRHIModule() override;

    // ===== Module 接口 =====
    void StartupModule() override;
    void ShutdownModule() override;
    bool IsLoaded() const override;

    // ===== RHIInterface =====
    RHIApi* CreateRHIApi() override;

private:
    bool bLoaded = false;
    std::string ModuleName = "VulkanRHI";
};

}