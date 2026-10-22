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
        m_Blocks.push_back({0, size, false});
    }
    std::optional<RHITransientHeap::Range> RHITransientHeap::Allocate(uint64_t size, uint64_t alignment)
    {
        if (!size)
            throw std::invalid_argument("Empty transient allocation");
        Align(0, alignment);
        std::lock_guard<std::mutex> lock(m_Mutex);
        for (size_t i = 0; i < m_Blocks.size(); ++i)
        {
            const Block block = m_Blocks[i];
            if (block.bUsed || block.Offset > std::numeric_limits<uint64_t>::max() - (alignment - 1))
                continue;
            const uint64_t offset = Align(block.Offset, alignment), padding = offset - block.Offset;
            if (padding > block.Size || size > block.Size - padding)
                continue;
            m_Blocks.reserve(m_Blocks.size() + 2);
            m_Blocks[i] = {offset, size, true};
            if (padding)
            {
                m_Blocks.insert(m_Blocks.begin() + i, {block.Offset, padding, false});
                ++i;
            }
            const uint64_t remaining = block.Size - padding - size;
            if (remaining)
                m_Blocks.insert(m_Blocks.begin() + i + 1, {offset + size, remaining, false});
            return Range{offset, size};
        }
        return std::nullopt;
    }
    void RHITransientHeap::Release(uint64_t offset, uint64_t size)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        auto it = std::find_if(m_Blocks.begin(), m_Blocks.end(), [&](const Block& block) {
            return block.bUsed && block.Offset == offset && block.Size == size;
        });
        if (it == m_Blocks.end())
            throw std::invalid_argument("Unknown or already freed transient range");
        it->bUsed = false;
        for (size_t i = 1; i < m_Blocks.size();)
            if (!m_Blocks[i - 1].bUsed && !m_Blocks[i].bUsed)
            {
                m_Blocks[i - 1].Size += m_Blocks[i].Size;
                m_Blocks.erase(m_Blocks.begin() + i);
            }
            else
                ++i;
    }
    uint64_t RHITransientHeap::GetFreeSize() const
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        uint64_t size = 0;
        for (const auto& block : m_Blocks)
            if (!block.bUsed)
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

    RHITransientResource::RHITransientResource(ERHITransientResourceType type) : m_ResourceType(type)
    {
    }
    RHITransientResource::~RHITransientResource() = default;
    bool RHITransientResource::IsAcquired() const
    {
        return m_bAcquired;
    }
    const std::string& RHITransientResource::GetName() const
    {
        return m_Name;
    }
    ERHITransientResourceType RHITransientResource::GetResourceType() const
    {
        return m_ResourceType;
    }
    RHITransientTexture::RHITransientTexture(const RHITextureSP& texture)
        : RHITransientResource(ERHITransientResourceType::Texture), m_Texture(texture)
    {
    }
    RHITextureSP RHITransientTexture::GetTexture() const
    {
        return m_Texture;
    }
    RHITransientBuffer::RHITransientBuffer(const RHIBufferSP& buffer)
        : RHITransientResource(ERHITransientResourceType::Buffer), m_Buffer(buffer)
    {
    }
    RHIBufferSP RHITransientBuffer::GetBuffer() const
    {
        return m_Buffer;
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
            bool bReleased = false;
        };
        template <class Entry> bool Available(const Entry& entry)
        {
            return (entry.bReleased || entry.Owner.expired()) && entry.Object.use_count() == 1;
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
                    if (entry.bReleased)
                        throw std::logic_error("Transient resource released twice");
                    entry.bReleased = true;
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
    RHITransientResourceManager::RHITransientResourceManager(uint64_t size) : m_Data(std::make_unique<State>(size))
    {
        if (!size)
            throw std::invalid_argument("Empty default transient heap size");
    }
    RHITransientResourceManager::~RHITransientResourceManager() = default;
    void RHITransientResourceManager::Acquire(RHITransientResource& resource, const char* name, uint32_t begin,
                                              uint32_t end)
    {
        resource.m_Name = name ? name : "";
        resource.m_BeginIndex = begin;
        resource.m_EndIndex = end;
        resource.m_bAcquired = true;
    }
    RHITransientTextureSP RHITransientResourceManager::CreateTransientTexture(const RHITextureDesc& desc,
                                                                              uint32_t begin, uint32_t end)
    {
        if (begin > end || !desc.Width || !desc.Height || !desc.Depth || !desc.MipLevels || !desc.ArraySize ||
            !desc.SampleCount || desc.Format == ERHIFormat::Unknown || desc.InitialData)
            throw std::invalid_argument("Invalid transient texture descriptor/lifetime; upload data explicitly");
        std::lock_guard<std::mutex> lock(m_Data->Mutex);
        for (auto& entry : m_Data->Textures)
            if (Available(entry) && Matches(entry.Object->GetDesc(), desc) && CanRecycle(*entry.Object))
            {
                auto handle = std::make_shared<RHITransientTexture>(entry.Object);
                Acquire(*handle, desc.DebugName, begin, end);
                PrepareForReuse(*entry.Object);
                entry.Owner = handle;
                entry.bReleased = false;
                ++m_Data->ReuseCount;
                return handle;
            }
        auto object = CreateOwned<RHITexture>(desc, [&](const auto& value) { return CreateTextureInternal(value); });
        auto handle = std::make_shared<RHITransientTexture>(object);
        Acquire(*handle, desc.DebugName, begin, end);
        m_Data->Textures.push_back({object, handle, false});
        return handle;
    }
    RHITransientBufferSP RHITransientResourceManager::CreateTransientBuffer(const RHIBufferDesc& desc, uint32_t begin,
                                                                            uint32_t end)
    {
        if (begin > end || !desc.Size)
            throw std::invalid_argument("Invalid transient buffer descriptor/lifetime");
        std::lock_guard<std::mutex> lock(m_Data->Mutex);
        for (auto& entry : m_Data->Buffers)
            if (Available(entry) && Matches(entry.Object->GetDesc(), desc) && CanRecycle(*entry.Object))
            {
                auto handle = std::make_shared<RHITransientBuffer>(entry.Object);
                Acquire(*handle, desc.DebugName, begin, end);
                PrepareForReuse(*entry.Object);
                entry.Owner = handle;
                entry.bReleased = false;
                ++m_Data->ReuseCount;
                return handle;
            }
        auto object = CreateOwned<RHIBuffer>(desc, [&](const auto& value) { return CreateBufferInternal(value); });
        auto handle = std::make_shared<RHITransientBuffer>(object);
        Acquire(*handle, desc.DebugName, begin, end);
        m_Data->Buffers.push_back({object, handle, false});
        return handle;
    }
    void RHITransientResourceManager::ReleaseTransientTexture(const RHITransientTexture* texture)
    {
        std::lock_guard<std::mutex> lock(m_Data->Mutex);
        Release(m_Data->Textures, texture);
        const_cast<RHITransientTexture*>(texture)->m_bAcquired = false;
    }
    void RHITransientResourceManager::ReleaseTransientBuffer(const RHITransientBuffer* buffer)
    {
        std::lock_guard<std::mutex> lock(m_Data->Mutex);
        Release(m_Data->Buffers, buffer);
        const_cast<RHITransientBuffer*>(buffer)->m_bAcquired = false;
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
        for (auto& heap : m_Data->Heaps)
            if (heap->MemoryClass == memoryClass)
                if (auto result = allocate(heap))
                    return result;
        auto heap = CreateHeap(RHITransientHeap::Align(std::max(size, m_Data->DefaultHeapSize), alignment), memoryClass);
        if (!heap || heap->MemoryClass != memoryClass)
            throw std::runtime_error("Incompatible transient native heap");
        m_Data->Heaps.push_back(heap);
        auto result = allocate(heap);
        if (!result)
            throw std::runtime_error("Fresh transient heap cannot satisfy request");
        return result;
    }
    void RHITransientResourceManager::GarbageCollect()
    {
        std::lock_guard<std::mutex> lock(m_Data->Mutex);
        auto trim = [&](auto& entries) {
            entries.erase(
                std::remove_if(entries.begin(), entries.end(),
                               [&](const auto& entry) { return Available(entry) && CanRecycle(*entry.Object); }),
                entries.end());
        };
        trim(m_Data->Textures);
        trim(m_Data->Buffers);
        auto& heaps = m_Data->Heaps;
        heaps.erase(
            std::remove_if(heaps.begin(), heaps.end(),
                           [](const auto& heap) { return heap.use_count() == 1 && heap->GetFreeSize() == heap->Size; }),
            heaps.end());
    }
    void RHITransientResourceManager::ClearResources()
    {
        std::lock_guard<std::mutex> lock(m_Data->Mutex);
        for (auto& entry : m_Data->Textures)
            if (auto owner = entry.Owner.lock())
                owner->m_bAcquired = false;
        for (auto& entry : m_Data->Buffers)
            if (auto owner = entry.Owner.lock())
                owner->m_bAcquired = false;
        m_Data->Textures.clear();
        m_Data->Buffers.clear();
        m_Data->Heaps.clear();
    }
    RHITransientResourceStats RHITransientResourceManager::GetStats() const
    {
        std::lock_guard<std::mutex> lock(m_Data->Mutex);
        RHITransientResourceStats stats;
        stats.TextureCount = m_Data->Textures.size();
        stats.BufferCount = m_Data->Buffers.size();
        stats.ReuseCount = m_Data->ReuseCount;
        stats.HeapCount = m_Data->Heaps.size();
        for (const auto& heap : m_Data->Heaps)
        {
            stats.ReservedBytes += heap->Size;
            stats.FreeBytes += heap->GetFreeSize();
        }
        return stats;
    }
} // namespace RHI
