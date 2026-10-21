#pragma once
#include "RHIResource.h"
#include <memory>
#include <mutex>
#include <optional>
#include <string>

namespace RHI
{
    enum class ERHITransientResourceType
    {
        Unknown,
        Texture,
        Buffer
    };
    enum class ERHITransientAllocationType
    {
        Heap,
        Page
    };

    // Common physical range allocator. Native heaps derive from this class.
    // Live allocations are exclusive: graph-local pass numbers alone do not
    // establish ordering across frames or graphics/compute queues.
    class RHI_API RHITransientHeap
    {
    public:
        struct Range
        {
            uint64_t Offset, Size;
        };
        RHITransientHeap(uint64_t size, uint64_t memoryClass);
        virtual ~RHITransientHeap() = default;
        std::optional<Range> Allocate(uint64_t size, uint64_t alignment);
        void Release(uint64_t offset, uint64_t size);
        uint64_t GetFreeSize() const;
        static uint64_t Align(uint64_t value, uint64_t alignment);
        const uint64_t Size, MemoryClass;

    private:
        struct Block
        {
            uint64_t Offset, Size;
            bool Used;
        };
        std::vector<Block> Blocks;
        mutable std::mutex Mutex;
    };

    struct RHI_API RHITransientAllocation
    {
        RHITransientAllocation(std::shared_ptr<RHITransientHeap> heap, RHITransientHeap::Range range);
        ~RHITransientAllocation();
        RHITransientAllocation(const RHITransientAllocation&) = delete;
        RHITransientAllocation& operator=(const RHITransientAllocation&) = delete;
        const std::shared_ptr<RHITransientHeap> Heap;
        const uint64_t Offset, Size;
    };
    using RHITransientAllocationSP = std::shared_ptr<RHITransientAllocation>;

    class RHI_API RHITransientResource
    {
    public:
        virtual ~RHITransientResource();
        bool IsAcquired() const;
        const std::string& GetName() const;
        ERHITransientResourceType GetResourceType() const;
        uint32_t GetBeginIndex() const
        {
            return BeginIndex;
        }
        uint32_t GetEndIndex() const
        {
            return EndIndex;
        }

    protected:
        explicit RHITransientResource(ERHITransientResourceType type);

    private:
        friend class RHITransientResourceManager;
        ERHITransientResourceType ResourceType;
        bool bAcquired = false;
        std::string Name;
        uint32_t BeginIndex = 0, EndIndex = 0;
    };

    class RHI_API RHITransientTexture : public RHITransientResource
    {
    public:
        explicit RHITransientTexture(const std::shared_ptr<RHITexture>& texture);
        std::shared_ptr<RHITexture> GetTexture() const;

    private:
        std::shared_ptr<RHITexture> Texture;
    };
    using RHITransientTextureSP = std::shared_ptr<RHITransientTexture>;

    class RHI_API RHITransientBuffer : public RHITransientResource
    {
    public:
        explicit RHITransientBuffer(const std::shared_ptr<RHIBuffer>& buffer);
        std::shared_ptr<RHIBuffer> GetBuffer() const;

    private:
        std::shared_ptr<RHIBuffer> Buffer;
    };
    using RHITransientBufferSP = std::shared_ptr<RHITransientBuffer>;

    struct RHITransientResourceStats
    {
        uint64_t TextureCount = 0, BufferCount = 0, ReuseCount = 0;
        uint64_t HeapCount = 0, ReservedBytes = 0, FreeBytes = 0;
    };

    // Shared lifecycle, resource cache and heap-selection policy. A caller must
    // retain its resource/wrapper while views or unsubmitted recordings use it,
    // as required by the existing RHI raw-pointer view/context interfaces.
    class RHI_API RHITransientResourceManager
    {
    public:
        explicit RHITransientResourceManager(uint64_t defaultHeapSize = 64ull * 1024 * 1024);
        virtual ~RHITransientResourceManager();
        RHITransientResourceManager(const RHITransientResourceManager&) = delete;
        RHITransientResourceManager& operator=(const RHITransientResourceManager&) = delete;
        virtual RHITransientTextureSP CreateTransientTexture(const RHITextureDesc& desc, uint32_t begin,
                                                             uint32_t end) final;
        virtual RHITransientBufferSP CreateTransientBuffer(const RHIBufferDesc& desc, uint32_t begin,
                                                           uint32_t end) final;
        virtual void ReleaseTransientTexture(const RHITransientTexture* texture) final;
        virtual void ReleaseTransientBuffer(const RHITransientBuffer* buffer) final;
        void GarbageCollect();
        RHITransientResourceStats GetStats() const;

    protected:
        virtual RHITextureSP CreateTextureInternal(const RHITextureDesc& desc) = 0;
        virtual RHIBufferSP CreateBufferInternal(const RHIBufferDesc& desc) = 0;
        virtual bool CanRecycle(RHIViewableResource& resource) = 0;
        virtual void PrepareForReuse(RHIViewableResource&)
        {
        }
        virtual std::shared_ptr<RHITransientHeap> CreateHeap(uint64_t size, uint64_t memoryClass);
        RHITransientAllocationSP AllocateMemory(uint64_t size, uint64_t alignment, uint64_t memoryClass);
        // Derived destructors call this before destroying their native device.
        void ClearResources();

    private:
        void Acquire(RHITransientResource& resource, const char* name, uint32_t begin, uint32_t end);
        struct State;
        std::unique_ptr<State> Data;
    };
    using RHITransientResourceManagerSP = std::shared_ptr<RHITransientResourceManager>;
} // namespace RHI
