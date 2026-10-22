#pragma once

#include "RHIResource.h"
#include <unordered_map>
#include <mutex>
#include <map>

namespace RHI {
    
class RHI_API RHIPipelineStateCache {
public:

    static RHIGraphicsPipelineState* GetOrCreateGraphicsPipelineState(const RHIGraphicsPipelineStateDesc& desc);
    
  
    static RHIComputePipelineState* GetOrCreateComputePipelineState(const RHIComputePipelineStateDesc& desc);


    static RHIVertexDescState* GetOrCreateVertexDescState(const RHIVertexDescStateDesc& desc);


    static RHIRasterizerState* GetOrCreateRasterizerState(const RHIRasterizerStateDesc& desc);


    static RHIColorBlendState* GetOrCreateColorBlendState(const RHIColorBlendStateDesc& desc);


    static RHIDepthStencilState* GetOrCreateDepthStencilState(const RHIDepthStencilStateDesc& desc);


    static void ClearAll();

private:

    static size_t HashGraphicsPipelineDesc(const RHIGraphicsPipelineStateDesc& desc);
    static size_t HashComputePipelineDesc(const RHIComputePipelineStateDesc& desc);
    static size_t HashVertexDescState(const RHIVertexDescStateDesc& desc);
    static size_t HashRasterizerState(const RHIRasterizerStateDesc& desc);
    static size_t HashColorBlendState(const RHIColorBlendStateDesc& desc);
    static size_t HashDepthStencilState(const RHIDepthStencilStateDesc& desc);

    static ::std::unordered_map<size_t, RHIGraphicsPipelineStateSP> m_GraphicsCache;
    static std::unordered_map<size_t, RHIComputePipelineStateSP> m_ComputeCache;
    static std::unordered_map<size_t, RHIVertexDescStateSP> m_VertexDescCache;
    static std::unordered_map<size_t, RHIRasterizerStateSP> m_RasterizerCache;
    static std::unordered_map<size_t, RHIColorBlendStateSP> m_ColorBlendCache;
    static std::unordered_map<size_t, RHIDepthStencilStateSP> m_DepthStencilCache;
    
    static std::mutex m_GraphicsMutex;
    static std::mutex m_ComputeMutex;
    static std::mutex m_VertexDescMutex;
    static std::mutex m_RasterizerMutex;
    static std::mutex m_ColorBlendMutex;
    static std::mutex m_DepthStencilMutex;

};

} // namespace RHI
