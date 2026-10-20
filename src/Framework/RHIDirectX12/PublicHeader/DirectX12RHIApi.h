#pragma once

#include "RHIApi.h"

#include <memory>
#include <string>
#include <unordered_map>

namespace RHIDirectX12
{
    class DirectX12Device;
    class DirectX12Queue;
    class DirectX12PresentExecutor;
    class DirectX12StagingBuffer;

    class RHIDIRECTX12_API DirectX12RHIApi : public RHI::RHIApi
    {
    public:
        DirectX12RHIApi();
        ~DirectX12RHIApi() override;

        bool Init() override;
        void Shutdown() override;
        const RHI::RHIPlatformInfo& GetPlatformInfo() const override;

        RHI::RHITextureSP CreateTexture(const RHI::RHITextureDesc& desc) override;
        RHI::RHIBufferSP CreateBuffer(const RHI::RHIBufferDesc& desc) override;
        void UpdateTexture(RHI::RHICommandListBase& cmdList, RHI::RHITexture* texture, const void* data,
                           const RHI::RHIUpdateTextureRegion& size) override;
        void UpdateBuffer(RHI::RHICommandListBase& cmdList, RHI::RHIBuffer* buffer, const void* data,
                          const RHI::RHIUpdateBufferRegion& region) override;
        void* MapReadTexture(RHI::RHICommandListBase& cmdList, RHI::RHITexture* texture,
                             const RHI::RHIReadTextureInfo& info) override;
        void* MapReadBuffer(RHI::RHICommandListBase& cmdList, RHI::RHIBuffer* buffer,
                            const RHI::RHIReadBufferInfo& info) override;
        void Unmap(void* mappedData) override;

        RHI::RHIShaderResourceViewSP CreateTextureShaderResourceView(RHI::RHITexture* Texture,
                                                                     const RHI::RHITexSRVCreateInfo& Desc) override;

        RHI::RHIUnorderedAccessViewSP CreateTextureUnorderedAccessView(RHI::RHITexture* Texture,
                                                                       const RHI::RHITexUAVCreateInfo& Desc) override;

        RHI::RHIShaderResourceViewSP CreateBufferShaderResourceView(RHI::RHIBuffer* Buffer,
                                                                    const RHI::RHIBufferSRVCreateInfo& Desc) override;

        RHI::RHIUnorderedAccessViewSP CreateBufferUnorderedAccessView(RHI::RHIBuffer* Buffer,
                                                                      const RHI::RHIBufferUAVCreateInfo& Desc) override;

        RHI::RHIRayTracingGeometrySP CreateRayTracingGeometry(const RHI::RHIRayTracingGeometryDesc& desc) override;
        RHI::RHIRayTracingInstanceSP CreateRayTracingInstance(const RHI::RHIRayTracingInstancesDesc& desc) override;

        RHI::RHIStagingBufferSP CreateStagingBuffer(uint32_t size) override;

        RHI::RHIGraphicsPipelineStateSP CreateGraphicsPipelineState(
            const RHI::RHIGraphicsPipelineStateDesc& desc) override;
        RHI::RHIComputePipelineStateSP CreateComputePipelineState(
            const RHI::RHIComputePipelineStateDesc& desc) override;
        RHI::RHIRayTracingPipelineStateSP CreateRayTracingsPipelineState(
            const RHI::RHIRayTracingPipelineStateDesc& desc) override;

        RHI::RHIVertexDescStateSP CreateVertexDescState(const RHI::RHIVertexDescStateDesc& desc) override;
        RHI::RHIRasterizerStateSP CreateRasterizerState(const RHI::RHIRasterizerStateDesc& desc) override;
        RHI::RHIColorBlendStateSP CreateColorBlendState(const RHI::RHIColorBlendStateDesc& desc) override;
        RHI::RHIDepthStencilStateSP CreateDepthStencilState(const RHI::RHIDepthStencilStateDesc& desc) override;

        RHI::RHIVertexShaderSP CreateVertexShader(const std::vector<char>& shaderSourceCode) override;
        RHI::RHIFragmentShaderSP CreateFragmentShader(const std::vector<char>& shaderSourceCode) override;
        RHI::RHIComputeShaderSP CreateComputeShader(const std::vector<char>& shaderSourceCode) override;
        RHI::RHIGeometryShaderSP CreateGeometryShader(const std::vector<char>& shaderSourceCode) override;
        RHI::RHITessControlShaderSP CreateTessControlShader(const std::vector<char>& shaderSourceCode) override;
        RHI::RHITessEvalShaderSP CreateTessEvalShader(const std::vector<char>& shaderSourceCode) override;
        RHI::RHIMeshShaderSP CreateMeshShader(const std::vector<char>& shaderSourceCode) override;
        RHI::RHITaskShaderSP CreateTaskShader(const std::vector<char>& shaderSourceCode) override;
        RHI::RHIRayGenShaderSP CreateRayGenShader(const std::vector<char>& shaderSourceCode) override;
        RHI::RHICloseHitShaderSP CreateCloseHitShader(const std::vector<char>& shaderSourceCode) override;
        RHI::RHIMissShaderSP CreateMissShader(const std::vector<char>& shaderSourceCode) override;
        RHI::RHIAnyHitShaderSP CreateAnyHitShader(const std::vector<char>& shaderSourceCode) override;
        RHI::RHIIntersectionShaderSP CreateIntersectionShader(const std::vector<char>& shaderSourceCode) override;
        RHI::RHICallableShaderSP CreateCallableShader(const std::vector<char>& shaderSourceCode) override;

        RHI::RHISwapchainSP CreateSwapchain(void* inWindowHandle, uint32_t w, uint32_t h,
                                            RHI::ERHIFormat format) override;
        RHI::RHISamplerSP CreateSampler(const RHI::RHISamplerDesc& desc) override;

        RHI::RHIQueue* GetQueue(RHI::EQueueType Type) override;
        RHI::RHIPresentExecutor* GetPresentExecutor() override;

        void RHICreateTransition(RHI::RHITransition* Transition,
                                 const RHI::RHITransitionCreateInfo& CreateInfo) override;
        void RHIReleaseTransition(RHI::RHITransition* Transition) override;

        RHI::RHITransientResourceManagerSP CreateTransientResourceManager() override;

    private:
        bool bInitialized = false;
        RHI::RHIPlatformInfo PlatformInfo{};
        std::unique_ptr<DirectX12Queue> GraphicsQueue;
        std::unique_ptr<DirectX12Queue> ComputeQueue;
        std::unique_ptr<DirectX12PresentExecutor> PresentExecutor;
        std::unique_ptr<DirectX12Device> Device;
        std::unordered_map<void*, std::shared_ptr<DirectX12StagingBuffer>> MappedStagingBuffers;
        std::unordered_map<void*, std::shared_ptr<std::vector<uint8_t>>> PackedReadbacks;
    };

    class RHIDIRECTX12_API DirectX12RHIModule final : public RHI::RHIModule
    {
    public:
        DirectX12RHIModule();
        ~DirectX12RHIModule() override;

        void StartupModule() override;
        void ShutdownModule() override;
        bool IsLoaded() const override;

        RHI::RHIApi* CreateRHIApi() override;

    private:
        bool bLoaded = false;
        std::string ModuleName = "DirectX12RHI";
    };
} // namespace RHIDirectX12
