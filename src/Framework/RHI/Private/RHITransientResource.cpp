#include "RHITransientResource.h"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace RHI
{
    uint64_t RHITransientHeap::Align(uint64_t value, uint64_t alignment)
    {
        if (!alignment || (alignment & (alignment - 1)))
            throw std::invalid_argument("Transient alignment must be a nonzero power of two");
        if (value > std::numeric_limits<uint64_t>::max() - (alignment - 1))
            throw std::overflow_error("Transient alignment overflow");
        return (value + alignment - 1) & ~(alignment - 1);
    }
    RHITransientHeap::RHITransientHeap(uint64_t size, uint64_t memoryClass) : Size(size), MemoryClass(memoryClass)
    {
        if (!size)
            throw std::invalid_argument("Empty transient heap");
        Blocks.push_back({0, size, false});
    }
    std::optional<RHITransientHeap::Range> RHITransientHeap::Allocate(uint64_t size, uint64_t alignment)
    {
        if (!size)
            throw std::invalid_argument("Empty transient allocation");
        Align(0, alignment);
        std::lock_guard<std::mutex> lock(Mutex);
        for (size_t i = 0; i < Blocks.size(); ++i)
        {
            const Block block = Blocks[i];
            if (block.Used || block.Offset > std::numeric_limits<uint64_t>::max() - (alignment - 1))
                continue;
            const uint64_t offset = Align(block.Offset, alignment), padding = offset - block.Offset;
            if (padding > block.Size || size > block.Size - padding)
                continue;
            Blocks.reserve(Blocks.size() + 2);
            Blocks[i] = {offset, size, true};
            if (padding)
            {
                Blocks.insert(Blocks.begin() + i, {block.Offset, padding, false});
                ++i;
            }
            const uint64_t remaining = block.Size - padding - size;
            if (remaining)
                Blocks.insert(Blocks.begin() + i + 1, {offset + size, remaining, false});
            return Range{offset, size};
        }
        return std::nullopt;
    }
    void RHITransientHeap::Release(uint64_t offset, uint64_t size)
    {
        std::lock_guard<std::mutex> lock(Mutex);
        auto it = std::find_if(Blocks.begin(), Blocks.end(), [&](const Block& block) {
            return block.Used && block.Offset == offset && block.Size == size;
        });
        if (it == Blocks.end())
            throw std::invalid_argument("Unknown or already freed transient range");
        it->Used = false;
        for (size_t i = 1; i < Blocks.size();)
            if (!Blocks[i - 1].Used && !Blocks[i].Used)
            {
                Blocks[i - 1].Size += Blocks[i].Size;
                Blocks.erase(Blocks.begin() + i);
            }
            else
                ++i;
    }
    uint64_t RHITransientHeap::GetFreeSize() const
    {
        std::lock_guard<std::mutex> lock(Mutex);
        uint64_t size = 0;
        for (const auto& block : Blocks)
            if (!block.Used)
                size += block.Size;
        return size;
    }
    RHITransientAllocation::RHITransientAllocation(std::shared_ptr<RHITransientHeap> heap,
                                                   RHITransientHeap::Range range)
        : Heap(std::move(heap)), Offset(range.Offset), Size(range.Size)
    {
    }
    RHITransientAllocation::~RHITransientAllocation()
    {
        Heap->Release(Offset, Size);
    }

    RHITransientResource::RHITransientResource(ERHITransientResourceType type) : ResourceType(type)
    {
    }
    RHITransientResource::~RHITransientResource() = default;
    bool RHITransientResource::IsAcquired() const
    {
        return bAcquired;
    }
    const std::string& RHITransientResource::GetName() const
    {
        return Name;
    }
    ERHITransientResourceType RHITransientResource::GetResourceType() const
    {
        return ResourceType;
    }
    RHITransientTexture::RHITransientTexture(const RHITextureSP& texture)
        : RHITransientResource(ERHITransientResourceType::Texture), Texture(texture)
    {
    }
    RHITextureSP RHITransientTexture::GetTexture() const
    {
        return Texture;
    }
    RHITransientBuffer::RHITransientBuffer(const RHIBufferSP& buffer)
        : RHITransientResource(ERHITransientResourceType::Buffer), Buffer(buffer)
    {
    }
    RHIBufferSP RHITransientBuffer::GetBuffer() const
    {
        return Buffer;
    }

    namespace
    {
        bool Matches(const RHITextureDesc& a, const RHITextureDesc& b)
        {
            return a.Width == b.Width && a.Height == b.Height && a.Depth == b.Depth && a.MipLevels == b.MipLevels &&
                   a.ArraySize == b.ArraySize && a.Format == b.Format && a.Type == b.Type &&
                   a.SampleCount == b.SampleCount && a.SampleQuality == b.SampleQuality && a.Usage == b.Usage &&
                   a.bGenerateMips == b.bGenerateMips && a.bCPUAccessible == b.bCPUAccessible &&
                   a.InitialQueueType == b.InitialQueueType;
        }
        bool Matches(const RHIBufferDesc& a, const RHIBufferDesc& b)
        {
            return a.Size == b.Size && a.Stride == b.Stride && a.Usage == b.Usage &&
                   a.bCPUAccessible == b.bCPUAccessible && a.InitialQueueType == b.InitialQueueType;
        }
        template <class Resource, class Handle> struct Entry
        {
            std::shared_ptr<Resource> Object;
            std::weak_ptr<Handle> Owner;
            bool Released = false;
        };
        template <class Entry> bool Available(const Entry& entry)
        {
            return (entry.Released || entry.Owner.expired()) && entry.Object.use_count() == 1;
        }
        // Keep descriptor debug-name storage alive without changing RHIResource.
        template <class Resource> struct ResourceOwner
        {
            std::string Name;
            std::shared_ptr<Resource> Object;
        };
        template <class Resource, class Desc, class Factory>
        std::shared_ptr<Resource> CreateOwned(const Desc& desc, Factory create)
        {
            auto owner = std::make_shared<ResourceOwner<Resource>>();
            owner->Name = desc.DebugName ? desc.DebugName : "";
            auto nativeDesc = desc;
            nativeDesc.DebugName = desc.DebugName ? owner->Name.c_str() : nullptr;
            owner->Object = create(nativeDesc);
            if (!owner->Object)
                throw std::runtime_error("Transient backend returned a null resource");
            return std::shared_ptr<Resource>(owner, owner->Object.get());
        }
        template <class Entries, class Handle> void Release(Entries& entries, const Handle* handle)
        {
            if (!handle)
                throw std::invalid_argument("Null transient release");
            for (auto& entry : entries)
                if (auto owner = entry.Owner.lock(); owner.get() == handle)
                {
                    if (entry.Released)
                        throw std::logic_error("Transient resource released twice");
                    entry.Released = true;
                    return;
                }
            throw std::invalid_argument("Transient resource belongs to another manager");
        }
    } // namespace
    struct RHITransientResourceManager::State
    {
        mutable std::mutex Mutex;
        uint64_t DefaultHeapSize, ReuseCount = 0;
        std::vector<std::shared_ptr<RHITransientHeap>> Heaps;
        std::vector<Entry<RHITexture, RHITransientTexture>> Textures;
        std::vector<Entry<RHIBuffer, RHITransientBuffer>> Buffers;
        explicit State(uint64_t size) : DefaultHeapSize(size)
        {
        }
    };
    RHITransientResourceManager::RHITransientResourceManager(uint64_t size) : Data(std::make_unique<State>(size))
    {
        if (!size)
            throw std::invalid_argument("Empty default transient heap size");
    }
    RHITransientResourceManager::~RHITransientResourceManager() = default;
    void RHITransientResourceManager::Acquire(RHITransientResource& resource, const char* name, uint32_t begin,
                                              uint32_t end)
    {
        resource.Name = name ? name : "";
        resource.BeginIndex = begin;
        resource.EndIndex = end;
        resource.bAcquired = true;
    }
    RHITransientTextureSP RHITransientResourceManager::CreateTransientTexture(const RHITextureDesc& desc,
                                                                              uint32_t begin, uint32_t end)
    {
        if (begin > end || !desc.Width || !desc.Height || !desc.Depth || !desc.MipLevels || !desc.ArraySize ||
            !desc.SampleCount || desc.Format == ERHIFormat::Unknown || desc.InitialData)
            throw std::invalid_argument("Invalid transient texture descriptor/lifetime; upload data explicitly");
        std::lock_guard<std::mutex> lock(Data->Mutex);
        for (auto& entry : Data->Textures)
            if (Available(entry) && Matches(entry.Object->GetDesc(), desc) && CanRecycle(*entry.Object))
            {
                auto handle = std::make_shared<RHITransientTexture>(entry.Object);
                Acquire(*handle, desc.DebugName, begin, end);
                PrepareForReuse(*entry.Object);
                entry.Owner = handle;
                entry.Released = false;
                ++Data->ReuseCount;
                return handle;
            }
        auto object = CreateOwned<RHITexture>(desc, [&](const auto& value) { return CreateTextureInternal(value); });
        auto handle = std::make_shared<RHITransientTexture>(object);
        Acquire(*handle, desc.DebugName, begin, end);
        Data->Textures.push_back({object, handle, false});
        return handle;
    }
    RHITransientBufferSP RHITransientResourceManager::CreateTransientBuffer(const RHIBufferDesc& desc, uint32_t begin,
                                                                            uint32_t end)
    {
        if (begin > end || !desc.Size)
            throw std::invalid_argument("Invalid transient buffer descriptor/lifetime");
        std::lock_guard<std::mutex> lock(Data->Mutex);
        for (auto& entry : Data->Buffers)
            if (Available(entry) && Matches(entry.Object->GetDesc(), desc) && CanRecycle(*entry.Object))
            {
                auto handle = std::make_shared<RHITransientBuffer>(entry.Object);
                Acquire(*handle, desc.DebugName, begin, end);
                PrepareForReuse(*entry.Object);
                entry.Owner = handle;
                entry.Released = false;
                ++Data->ReuseCount;
                return handle;
            }
        auto object = CreateOwned<RHIBuffer>(desc, [&](const auto& value) { return CreateBufferInternal(value); });
        auto handle = std::make_shared<RHITransientBuffer>(object);
        Acquire(*handle, desc.DebugName, begin, end);
        Data->Buffers.push_back({object, handle, false});
        return handle;
    }
    void RHITransientResourceManager::ReleaseTransientTexture(const RHITransientTexture* texture)
    {
        std::lock_guard<std::mutex> lock(Data->Mutex);
        Release(Data->Textures, texture);
        const_cast<RHITransientTexture*>(texture)->bAcquired = false;
    }
    void RHITransientResourceManager::ReleaseTransientBuffer(const RHITransientBuffer* buffer)
    {
        std::lock_guard<std::mutex> lock(Data->Mutex);
        Release(Data->Buffers, buffer);
        const_cast<RHITransientBuffer*>(buffer)->bAcquired = false;
    }
    std::shared_ptr<RHITransientHeap> RHITransientResourceManager::CreateHeap(uint64_t, uint64_t)
    {
        throw std::logic_error("Backend storage is managed as whole resource objects");
    }
    RHITransientAllocationSP RHITransientResourceManager::AllocateMemory(uint64_t size, uint64_t alignment,
                                                                         uint64_t memoryClass)
    {
        if (!size)
            throw std::invalid_argument("Empty transient allocation");
        RHITransientHeap::Align(size, alignment);
        auto allocate = [&](const std::shared_ptr<RHITransientHeap>& heap) -> RHITransientAllocationSP {
            if (auto range = heap->Allocate(size, alignment))
            {
                try
                {
                    return std::make_shared<RHITransientAllocation>(heap, *range);
                }
                catch (...)
                {
                    heap->Release(range->Offset, range->Size);
                    throw;
                }
            }
            return {};
        };
        for (auto& heap : Data->Heaps)
            if (heap->MemoryClass == memoryClass)
                if (auto result = allocate(heap))
                    return result;
        auto heap = CreateHeap(RHITransientHeap::Align(std::max(size, Data->DefaultHeapSize), alignment), memoryClass);
        if (!heap || heap->MemoryClass != memoryClass)
            throw std::runtime_error("Incompatible transient native heap");
        Data->Heaps.push_back(heap);
        auto result = allocate(heap);
        if (!result)
            throw std::runtime_error("Fresh transient heap cannot satisfy request");
        return result;
    }
    void RHITransientResourceManager::GarbageCollect()
    {
        std::lock_guard<std::mutex> lock(Data->Mutex);
        auto trim = [&](auto& entries) {
            entries.erase(
                std::remove_if(entries.begin(), entries.end(),
                               [&](const auto& entry) { return Available(entry) && CanRecycle(*entry.Object); }),
                entries.end());
        };
        trim(Data->Textures);
        trim(Data->Buffers);
        auto& heaps = Data->Heaps;
        heaps.erase(
            std::remove_if(heaps.begin(), heaps.end(),
                           [](const auto& heap) { return heap.use_count() == 1 && heap->GetFreeSize() == heap->Size; }),
            heaps.end());
    }
    void RHITransientResourceManager::ClearResources()
    {
        std::lock_guard<std::mutex> lock(Data->Mutex);
        for (auto& entry : Data->Textures)
            if (auto owner = entry.Owner.lock())
                owner->bAcquired = false;
        for (auto& entry : Data->Buffers)
            if (auto owner = entry.Owner.lock())
                owner->bAcquired = false;
        Data->Textures.clear();
        Data->Buffers.clear();
        Data->Heaps.clear();
    }
    RHITransientResourceStats RHITransientResourceManager::GetStats() const
    {
        std::lock_guard<std::mutex> lock(Data->Mutex);
        RHITransientResourceStats stats;
        stats.TextureCount = Data->Textures.size();
        stats.BufferCount = Data->Buffers.size();
        stats.ReuseCount = Data->ReuseCount;
        stats.HeapCount = Data->Heaps.size();
        for (const auto& heap : Data->Heaps)
        {
            stats.ReservedBytes += heap->Size;
            stats.FreeBytes += heap->GetFreeSize();
        }
        return stats;
    }
} // namespace RHI
