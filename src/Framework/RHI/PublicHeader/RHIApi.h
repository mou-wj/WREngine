#pragma once
#include <memory>
#include "RHIResource.h"
#include "RHICommandList.h"
#include "RHICommandContex.h"
#include "RHIShaderLibrary.h"
#include <map>
#include <string>
#include <vector>
#include "Module.h"
#include "RHITransientResource.h"
namespace RHI{
class RHIApi;


class RHIShaderLibrary;
using RHIShaderLibrarySP = std::shared_ptr<RHIShaderLibrary>;
enum class EDepthRange {
    ZeroToOne,
    NegativeOneToOne
};
struct RHIPlatformInfo {
    EDepthRange DepthRange;
    bool bEnableRayTracing;
};

class RHI_API RHIApi
{
public:

    virtual ~RHIApi() = default;
	virtual bool Init() = 0;
	virtual void Shutdown() = 0;
    virtual const RHIPlatformInfo & GetPlatformInfo() const = 0;

    virtual RHITextureSP CreateTexture(const RHITextureDesc& desc) = 0;
    virtual RHIBufferSP CreateBuffer(const RHIBufferDesc& desc) = 0;
    virtual void UpdateTexture(RHICommandListBase& cmdList,RHITexture* texture, const void* data,const RHIUpdateTextureRegion& size) = 0;
    virtual void UpdateBuffer(RHICommandListBase& cmdList, RHIBuffer* buffer, const void* data,const RHIUpdateBufferRegion& region) = 0;
    virtual void* MapReadTexture(RHICommandListBase& cmdList, RHITexture* texture, const RHIReadTextureInfo& info) = 0;
    virtual void* MapReadBuffer(RHICommandListBase& cmdList, RHIBuffer* buffer, const RHIReadBufferInfo& info) = 0;
    virtual void Unmap(void* mappedData) = 0;
    virtual RHIShaderResourceViewSP CreateTextureShaderResourceView(
        RHITexture* texture, const RHITexSRVCreateInfo& desc) = 0;

    virtual RHIUnorderedAccessViewSP CreateTextureUnorderedAccessView(
        RHITexture* texture, const RHITexUAVCreateInfo& desc) = 0;

    virtual RHIShaderResourceViewSP CreateBufferShaderResourceView(
        RHIBuffer* buffer, const RHIBufferSRVCreateInfo& desc) = 0;

    virtual RHIUnorderedAccessViewSP CreateBufferUnorderedAccessView(
        RHIBuffer* buffer, const RHIBufferUAVCreateInfo& desc) = 0;
    virtual RHIRayTracingGeometrySP CreateRayTracingGeometry(const RHIRayTracingGeometryDesc& desc) = 0;
    virtual RHIRayTracingInstanceSP CreateRayTracingInstance(const RHIRayTracingInstancesDesc& desc) = 0;
        

    // 创建用于数据传输的暂存缓冲区。
    virtual RHIStagingBufferSP CreateStagingBuffer(uint32_t size) = 0;

    virtual RHIGraphicsPipelineStateSP CreateGraphicsPipelineState(const RHIGraphicsPipelineStateDesc& desc) = 0;
    virtual RHIComputePipelineStateSP CreateComputePipelineState(const RHIComputePipelineStateDesc& desc) = 0;
    virtual RHIRayTracingPipelineStateSP CreateRayTracingsPipelineState(const RHIRayTracingPipelineStateDesc& desc) = 0;

    virtual RHIVertexDescStateSP CreateVertexDescState(const RHIVertexDescStateDesc& desc) = 0;
    virtual RHIRasterizerStateSP CreateRasterizerState(const RHIRasterizerStateDesc& desc) = 0;
    virtual RHIColorBlendStateSP CreateColorBlendState(const RHIColorBlendStateDesc& desc) = 0;
    virtual RHIDepthStencilStateSP CreateDepthStencilState(const RHIDepthStencilStateDesc& desc) = 0;

    virtual RHIVertexShaderSP CreateVertexShader(const std::vector<char>& shaderSourceCode) = 0;
    virtual RHIFragmentShaderSP CreateFragmentShader(const std::vector<char>& shaderSourceCode) = 0;
    virtual RHIComputeShaderSP CreateComputeShader(const std::vector<char>& shaderSourceCode) = 0;
    virtual RHIGeometryShaderSP CreateGeometryShader(const std::vector<char>& shaderSourceCode) = 0;
    virtual RHITessControlShaderSP CreateTessControlShader(const std::vector<char>& shaderSourceCode) = 0;
    virtual RHITessEvalShaderSP CreateTessEvalShader(const std::vector<char>& shaderSourceCode) = 0;
    virtual RHIMeshShaderSP CreateMeshShader(const std::vector<char>& shaderSourceCode) = 0;
    virtual RHITaskShaderSP CreateTaskShader(const std::vector<char>& shaderSourceCode) = 0;
    virtual RHIRayGenShaderSP CreateRayGenShader(const std::vector<char>& shaderSourceCode) = 0;
    virtual RHICloseHitShaderSP CreateCloseHitShader(const std::vector<char>& shaderSourceCode) = 0;
    virtual RHIMissShaderSP CreateMissShader(const std::vector<char>& shaderSourceCode) = 0;
    virtual RHIAnyHitShaderSP CreateAnyHitShader(const std::vector<char>& shaderSourceCode) = 0;
    virtual RHIIntersectionShaderSP CreateIntersectionShader(const std::vector<char>& shaderSourceCode) = 0;
    virtual RHICallableShaderSP CreateCallableShader(const std::vector<char>& shaderSourceCode) = 0;

    virtual RHISwapchainSP CreateSwapchain(void* inWindowHandle,uint32_t w,uint32_t h,ERHIFormat format) = 0;
    virtual RHISamplerSP CreateSampler(const RHISamplerDesc& desc) = 0;

    virtual RHIQueue* GetQueue(EQueueType type) = 0;
    virtual RHIPresentExecutor* GetPresentExecutor() = 0;

    virtual void RHICreateTransition(RHITransition* transition, const RHITransitionCreateInfo& createInfo) = 0;
    virtual void RHIReleaseTransition(RHITransition* transition) = 0;

    virtual RHITransientResourceManagerSP CreateTransientResourceManager() = 0;
    
    
 
};

class RHI_API RHIModule : public Core::Module {
public:
	virtual RHIApi* CreateRHIApi() = 0;
};


extern RHI_API RHIApi* GRHIApi;
extern RHI_API ERHIShaderPlatform GShaderPlatform;


}