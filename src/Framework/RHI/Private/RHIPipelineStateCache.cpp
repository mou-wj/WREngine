#include "RHIPipelineStateCache.h"
#include <functional>
#include "RHIApi.h"
#include "HashHelper.hpp"

namespace RHI {

std::unordered_map<size_t, RHIGraphicsPipelineStateSP> RHIPipelineStateCache::m_GraphicsCache;
std::unordered_map<size_t, RHIComputePipelineStateSP> RHIPipelineStateCache::m_ComputeCache;
std::unordered_map<size_t, RHIVertexDescStateSP> RHIPipelineStateCache::m_VertexDescCache;
std::unordered_map<size_t, RHIRasterizerStateSP> RHIPipelineStateCache::m_RasterizerCache;
std::unordered_map<size_t, RHIColorBlendStateSP> RHIPipelineStateCache::m_ColorBlendCache;
std::unordered_map<size_t, RHIDepthStencilStateSP> RHIPipelineStateCache::m_DepthStencilCache;

std::mutex RHIPipelineStateCache::m_GraphicsMutex;
std::mutex RHIPipelineStateCache::m_ComputeMutex;
std::mutex RHIPipelineStateCache::m_VertexDescMutex;
std::mutex RHIPipelineStateCache::m_RasterizerMutex;
std::mutex RHIPipelineStateCache::m_ColorBlendMutex;
std::mutex RHIPipelineStateCache::m_DepthStencilMutex;

RHIGraphicsPipelineState* RHIPipelineStateCache::GetOrCreateGraphicsPipelineState(const RHIGraphicsPipelineStateDesc& desc) {
	std::lock_guard<std::mutex> lock(RHIPipelineStateCache::m_GraphicsMutex);
	size_t hash = RHIPipelineStateCache::HashGraphicsPipelineDesc(desc);

	auto it = RHIPipelineStateCache::m_GraphicsCache.find(hash);
	if (it != RHIPipelineStateCache::m_GraphicsCache.end()) {
		return it->second.get();
	}
	auto state = GRHIApi->CreateGraphicsPipelineState(desc);
	RHIPipelineStateCache::m_GraphicsCache[hash] = state;
	return state.get();
}


RHIComputePipelineState* RHIPipelineStateCache::GetOrCreateComputePipelineState(const RHIComputePipelineStateDesc& desc) {
	std::lock_guard<std::mutex> lock(m_ComputeMutex);
	size_t hash = HashComputePipelineDesc(desc);

	auto it = m_ComputeCache.find(hash);
	if (it != m_ComputeCache.end()) {
		return it->second.get();
	}
	auto state = GRHIApi->CreateComputePipelineState(desc);
	m_ComputeCache[hash] = state;
	return state.get();

}

RHIVertexDescState* RHIPipelineStateCache::GetOrCreateVertexDescState(const RHIVertexDescStateDesc& desc) {
    std::lock_guard<std::mutex> lock(m_VertexDescMutex);
    size_t hash = HashVertexDescState(desc);
    
    auto it = m_VertexDescCache.find(hash);
    if (it != m_VertexDescCache.end()) {
        return it->second.get();
    }

    auto state = GRHIApi->CreateVertexDescState(desc);
    m_VertexDescCache[hash] = state;
    return state.get();
}

RHIRasterizerState* RHIPipelineStateCache::GetOrCreateRasterizerState(const RHIRasterizerStateDesc& desc) {
    std::lock_guard<std::mutex> lock(m_RasterizerMutex);

    size_t hash = HashRasterizerState(desc);
    
    auto it = m_RasterizerCache.find(hash);
    if (it != m_RasterizerCache.end()) {
        return it->second.get();
    }

    auto state = GRHIApi->CreateRasterizerState(desc);
    m_RasterizerCache[hash] = state;
    return state.get();
}

RHIColorBlendState* RHIPipelineStateCache::GetOrCreateColorBlendState(const RHIColorBlendStateDesc& desc) {
    std::lock_guard<std::mutex> lock(m_ColorBlendMutex);
    size_t hash = HashColorBlendState(desc);
    
    auto it = m_ColorBlendCache.find(hash);
    if (it != m_ColorBlendCache.end()) {
        return it->second.get();
    }

    auto state = GRHIApi->CreateColorBlendState(desc);
    m_ColorBlendCache[hash] = state;
    return state.get();
}

RHIDepthStencilState* RHIPipelineStateCache::GetOrCreateDepthStencilState(const RHIDepthStencilStateDesc& desc) {
    std::lock_guard<std::mutex> lock(m_DepthStencilMutex);
    size_t hash = HashDepthStencilState(desc);
    
    auto it = m_DepthStencilCache.find(hash);
    if (it != m_DepthStencilCache.end()) {
        return it->second.get();
    }

    auto state = GRHIApi->CreateDepthStencilState(desc);
    m_DepthStencilCache[hash] = state;
    return state.get();
}

void RHIPipelineStateCache::ClearAll() {
    {
        std::lock_guard<std::mutex> lock(m_GraphicsMutex);
        m_GraphicsCache.clear();
    }
    {
        std::lock_guard<std::mutex> lock(m_ComputeMutex);
        m_ComputeCache.clear();
    }
    {
        std::lock_guard<std::mutex> lock(m_VertexDescMutex);
        m_VertexDescCache.clear();
    }
    {
        std::lock_guard<std::mutex> lock(m_RasterizerMutex);
        m_RasterizerCache.clear();
    }
    {
        std::lock_guard<std::mutex> lock(m_ColorBlendMutex);
        m_ColorBlendCache.clear();
    }
    {
        std::lock_guard<std::mutex> lock(m_DepthStencilMutex);
        m_DepthStencilCache.clear();
    }
}

size_t RHIPipelineStateCache::HashGraphicsPipelineDesc(const RHIGraphicsPipelineStateDesc& desc) {
	size_t hash = 0;
    if (desc.ShaderStages.VertexShader) {
        HashCombine(hash, (uint64_t)desc.ShaderStages.VertexShader);
    }
    if (desc.ShaderStages.FragmentShader) {
		HashCombine(hash, (uint64_t)desc.ShaderStages.FragmentShader);
	}
	hash ^= HashVertexDescState(desc.VertexDescState->GetDesc());
	hash ^= HashRasterizerState(desc.RasterizerState->GetDesc());
	hash ^= HashColorBlendState(desc.ColorBlendState->GetDesc());
	hash ^= HashDepthStencilState(desc.DepthStencilState->GetDesc());
    hash ^= RHIGraphicAttachmentDesc::CalculateHash(desc.AttachmentDesc);
	return hash;

}
size_t RHIPipelineStateCache::HashComputePipelineDesc(const RHIComputePipelineStateDesc& desc) {
	size_t hash = 0;
	hash ^= std::hash<uint64_t>()(desc.ComputeShader ? (uint64_t)desc.ComputeShader : 0);
	return hash;

}


size_t RHIPipelineStateCache::HashVertexDescState(const RHIVertexDescStateDesc& desc) {
    size_t hash = 0;
    for (const auto& binding : desc.Bindings) {
        hash ^= std::hash<uint32_t>()(binding.Binding);
        hash ^= std::hash<uint32_t>()(binding.Stride);
        hash ^= std::hash<int>()(static_cast<int>(binding.InputRate));
    }
    for (const auto& attr : desc.Attributes) {
        hash ^= std::hash<uint32_t>()(attr.Location);
        hash ^= std::hash<uint32_t>()(attr.Offset);
        hash ^= std::hash<int>()(static_cast<int>(attr.Format));
    }
    return hash;
}

size_t RHIPipelineStateCache::HashRasterizerState(const RHIRasterizerStateDesc& desc) {
    size_t hash = 0;
    HashCombine(hash, desc.bDepthClampEnable);
    HashCombine(hash, desc.bRasterizerDiscardEnable);
    HashCombine(hash, desc.PolygonMode);
    HashCombine(hash, desc.CullMode);
    HashCombine(hash, desc.FrontFace);
    HashCombine(hash, desc.bDepthBiasEnable);
    HashCombine(hash, desc.DepthBiasConstantFactor);
    HashCombine(hash, desc.DepthBiasClamp);
    HashCombine(hash, desc.DepthBiasSlopeFactor);
    HashCombine(hash, desc.LineWidth);
    return hash;
}

size_t RHIPipelineStateCache::HashColorBlendState(const RHIColorBlendStateDesc& desc) {
    size_t hash = std::hash<bool>()(desc.bLogicOpEnable);
    hash ^= std::hash<int>()(static_cast<int>(desc.Attachments.size()));
    for (const auto& attachment : desc.Attachments) {
        hash ^= std::hash<bool>()(attachment.bBlendEnable);
        hash ^= std::hash<uint32_t>()(attachment.ColorWriteMask);
    }
    for (int i = 0; i < 4; ++i) {
        hash ^= std::hash<float>()(desc.BlendConstants[i]);
    }
    return hash;
}

size_t RHIPipelineStateCache::HashDepthStencilState(const RHIDepthStencilStateDesc& desc) {
    size_t hash = 0;
    hash ^= std::hash<bool>()(desc.bDepthTestEnable);
    hash ^= std::hash<bool>()(desc.bDepthWriteEnable);
    hash ^= std::hash<int>()(static_cast<int>(desc.DepthCompareOp));
    hash ^= std::hash<bool>()(desc.bDepthBoundsTestEnable);
    hash ^= std::hash<bool>()(desc.bStencilTestEnable);
    hash ^= std::hash<uint32_t>()(desc.StencilReadMask);
    hash ^= std::hash<uint32_t>()(desc.StencilWriteMask);
    return hash;
}

} // namespace WR::RHI
