#include "DirectX12TransientResource.h"
#include "DirectX12Queue.h"
#include <algorithm>
#include <stdexcept>

namespace RHIDirectX12
{
    namespace
    {
        bool Matches(const RHI::RHITextureDesc& a, const RHI::RHITextureDesc& b)
        {
            return a.Width == b.Width && a.Height == b.Height && a.Depth == b.Depth && a.MipLevels == b.MipLevels &&
                   a.ArraySize == b.ArraySize && a.Format == b.Format && a.Type == b.Type &&
                   a.SampleCount == b.SampleCount && a.SampleQuality == b.SampleQuality && a.Usage == b.Usage &&
                   a.bGenerateMips == b.bGenerateMips && a.bCPUAccessible == b.bCPUAccessible &&
                   a.InitialQueueType == b.InitialQueueType;
        }
        bool Matches(const RHI::RHIBufferDesc& a, const RHI::RHIBufferDesc& b)
        {
            return a.Size == b.Size && a.Stride == b.Stride && a.Usage == b.Usage &&
                   a.bCPUAccessible == b.bCPUAccessible && a.InitialQueueType == b.InitialQueueType;
        }
        template <class Entries, class Desc, class Factory>
        auto Acquire(Entries& entries, const Desc& desc, uint32_t begin, uint32_t end, Factory create)
        {
            if (begin > end)
                throw std::invalid_argument("Transient resource lifetime beginIndex exceeds endIndex");
            for (auto& entry : entries)
            {
                auto& resource = entry.Object;
                // Expired wrappers also return their allocation to the pool (RAII).
                if (!(entry.Released || entry.HandleOwner.expired()) || resource.use_count() != 1 ||
                    resource->ContextUses->load() != 0 || !Matches(resource->GetDesc(), desc))
                    continue;
                if (resource->LastQueue && resource->LastFence > resource->LastQueue->GetCurrentTimelineValue())
                    continue;
                auto handle = create(resource);
                resource->DeclaredAccess.clear();
                entry.HandleOwner = handle;
                entry.BeginIndex = begin;
                entry.EndIndex = end;
                entry.Released = false;
                return handle;
            }
            std::shared_ptr<typename Entries::value_type::ResourceType> resource;
            auto handle = create(resource);
            entries.push_back({resource, handle, begin, end, false});
            return handle;
        }
        template <class Entries, class Handle> void Release(Entries& entries, const Handle* handle)
        {
            if (!handle)
                throw std::invalid_argument("Null transient resource release");
            for (auto& entry : entries)
                if (auto owner = entry.HandleOwner.lock(); owner.get() == handle)
                {
                    if (entry.Released)
                        throw std::logic_error("Transient resource released twice");
                    entry.Released = true;
                    return;
                }
            throw std::invalid_argument("Transient resource belongs to another manager");
        }
    } // namespace
    RHI::RHITransientTextureSP DirectX12TransientResourceManager::CreateTransientTexture(
        const RHI::RHITextureDesc& desc, uint32_t beginIndex, uint32_t endIndex)
    {
        if (desc.InitialData)
            throw std::invalid_argument("Transient textures require explicit data upload");
        std::lock_guard<std::mutex> lock(Mutex);
        return Acquire(Textures, desc, beginIndex, endIndex, [&](std::shared_ptr<DirectX12Texture>& resource) {
            if (!resource)
                resource = std::make_shared<DirectX12Texture>(Device, desc);
            return std::make_shared<RHI::RHITransientTexture>(resource);
        });
    }
    RHI::RHITransientBufferSP DirectX12TransientResourceManager::CreateTransientBuffer(const RHI::RHIBufferDesc& desc,
                                                                                       uint32_t beginIndex,
                                                                                       uint32_t endIndex)
    {
        std::lock_guard<std::mutex> lock(Mutex);
        return Acquire(Buffers, desc, beginIndex, endIndex, [&](std::shared_ptr<DirectX12Buffer>& resource) {
            if (!resource)
                resource = std::make_shared<DirectX12Buffer>(Device, desc);
            return std::make_shared<RHI::RHITransientBuffer>(resource);
        });
    }
    void DirectX12TransientResourceManager::ReleaseTransientTexture(const RHI::RHITransientTexture* texture)
    {
        std::lock_guard<std::mutex> lock(Mutex);
        Release(Textures, texture);
    }
    void DirectX12TransientResourceManager::ReleaseTransientBuffer(const RHI::RHITransientBuffer* buffer)
    {
        std::lock_guard<std::mutex> lock(Mutex);
        Release(Buffers, buffer);
    }
} // namespace RHIDirectX12
