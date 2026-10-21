#include "DirectX12TransientResource.h"
#include "DirectX12Device.h"
#include "DirectX12Queue.h"
#include "DirectX12RHIUtils.h"
#include <stdexcept>

namespace RHIDirectX12
{
    DirectX12TransientResourceManager::~DirectX12TransientResourceManager()
    {
        for (auto* queue : {Device->GetGraphicsCommandQueue(), Device->GetComputeCommandQueue()})
            if (queue)
            {
                DirectX12SyncPoint completion(RHI::EQueueType::Graphics, Device, queue);
                completion.Wait(completion.Signal());
            }
        ClearResources();
    }
    RHI::RHITextureSP DirectX12TransientResourceManager::CreateTextureInternal(const RHI::RHITextureDesc& desc)
    {
        auto resource = std::make_shared<DirectX12Texture>(Device, desc);
        if (!resource->GetNativeResource())
            throw std::runtime_error("Failed to create transient D3D12 texture");
        return resource;
    }
    RHI::RHIBufferSP DirectX12TransientResourceManager::CreateBufferInternal(const RHI::RHIBufferDesc& desc)
    {
        auto resource = std::make_shared<DirectX12Buffer>(Device, desc);
        if (!resource->GetNativeResource())
            throw std::runtime_error("Failed to create transient D3D12 buffer");
        return resource;
    }
    bool DirectX12TransientResourceManager::CanRecycle(RHI::RHIViewableResource& resource)
    {
        auto* state = GetResourceState(&resource);
        return state && !state->ContextUses->load() &&
               (!state->LastQueue || state->LastFence <= state->LastQueue->GetCurrentTimelineValue());
    }
    void DirectX12TransientResourceManager::PrepareForReuse(RHI::RHIViewableResource& resource)
    {
        GetResourceState(&resource)->DeclaredAccess.clear();
    }
} // namespace RHIDirectX12
