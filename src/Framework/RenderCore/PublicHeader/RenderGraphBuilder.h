#pragma once

#include "RenderGraphResource.h" // For RenderGraphResource
#include "RenderGraphPass.h"
#include "RenderResource.h"
#include <list>
#include <vector>
#include <unordered_map>

namespace RenderCore {



/**
 * Frame allocator with page reuse and explicit object destruction.
 */
class RenderGraphAllocator {
public:
    RenderGraphAllocator(size_t inPageSize = 64 * 1024)
        : m_PageSize(inPageSize), m_CurrentPage(-1), m_CurrentOffset(0) {
    }

    ~RenderGraphAllocator() {
        // Destroy allocated objects and release every retained page.
        Reset();
        for (void* page : m_Pages) {
            free(page);
        }
    }

    /**
     * Allocate and construct an object in the graph arena.
     */
    template<typename T, typename... Args>
    T* Allocate(Args&&... args) {
        std::lock_guard<std::recursive_mutex> lock(m_Mutex);
        // Allocate aligned storage.
        void* rawMem = AllocateRaw(sizeof(T), alignof(T));

        // Construct the object with placement new.
        T* object = new (rawMem) T(std::forward<Args>(args)...);

        // Record a destructor for non-trivially destructible objects.
        if constexpr (!std::is_trivially_destructible_v<T>) {
            m_DestructionStack.push_back({
                object,
                [](void* ptr) { static_cast<T*>(ptr)->~T(); }
                });
        }
        return object;
    }

    void* AllocateBytes(size_t size, size_t alignment = alignof(std::max_align_t))
    {
        return AllocateRaw(size, alignment);
    }

    /**
     * Reset objects and cursors at the end of the frame.
     */
    void Reset() {
        // Destroy objects in reverse construction order.
        for (auto it = m_DestructionStack.rbegin(); it != m_DestructionStack.rend(); ++it) {
            it->Deleter(it->Ptr);
        }
        m_DestructionStack.clear();

        // Reuse retained pages in the next frame.
        m_CurrentPage = 0;
        m_CurrentOffset = 0;
    }

private:
    void* AllocateRaw(size_t size, size_t alignment) {
        std::lock_guard<std::recursive_mutex> lock(m_Mutex);
        // Calculate padding for the requested CPU alignment.
        size_t padding = (alignment - (m_CurrentOffset % alignment)) % alignment;

        if (m_CurrentPage == -1 || m_CurrentOffset + padding + size > m_PageSize) {
            MoveToNextPage();
            padding = 0; // Start at the beginning of a fresh page.
        }

        void* result = (uint8_t*)m_Pages[m_CurrentPage] + m_CurrentOffset + padding;
        m_CurrentOffset += (padding + size);
        return result;
    }

    void MoveToNextPage() {
        m_CurrentPage++;
        if (m_CurrentPage >= m_Pages.size()) {
            m_Pages.push_back(malloc(m_PageSize));
        }
        m_CurrentOffset = 0;
    }

    struct DestructionItem {
        void* Ptr;
        void (*Deleter)(void*);
    };

    size_t m_PageSize;
    int32_t m_CurrentPage;
    size_t m_CurrentOffset;
    std::recursive_mutex m_Mutex;
    std::vector<void*> m_Pages;                // Retained allocation pages.
    std::vector<DestructionItem> m_DestructionStack; // Object destruction callbacks.
};


// -------------------------------------------------------------------------------------------------
//  Render Graph Builder (Now also managing passes and execution)
// -------------------------------------------------------------------------------------------------
class RENDERCORE_API RenderGraphBuilder
{
public:
    // Construction/Destruction
    RenderGraphBuilder();
    ~RenderGraphBuilder();

    // Pass Management
    /**
     * Construct a typed render graph pass.
     */
    template <typename TParameterStruct, typename TExecuteLambda>
    RenderGraphPassRef AddPass(
        const std::string& inName,
        const ShaderParametersMetadata* inMetadata,
        TParameterStruct* inParameters, // Parameters allocated by AllocateParameter.
        EPassFlag inPassFlags,
        TExecuteLambda&& inExecuteLambda)
    {
        // Wrap the parameter data and metadata.
        RenderGraphParameterStruct parameterStruct(inParameters, inMetadata);

        // Create the lambda pass instance.
        auto pass = new RenderGraphLambdaPass<TParameterStruct, TExecuteLambda>(
            inName,
            inPassFlags,
            parameterStruct,
            std::forward<TExecuteLambda>(inExecuteLambda)
        );

        // Inspect parameter resource references to build graph dependencies.
        // SetupPassInternal records the pass resource access requirements.
        SetupPassInternal(pass, inMetadata, inParameters);

        // Append the pass to the execution list.
        m_Passes.push_back(pass);
        return pass;
    }
    void AddPassDependency(RenderGraphPass* pass, RenderGraphPass* passConsumer);
    enum class EUploadPolicy {
		Immediate, // 立即插入一个upload pass
		Deferred   // 延迟插入upload pass，直到 Execute() 被调用是收集所有的upload buffer，然后统一插入upload pass
    };
    struct UploadBufferDesc {
        RenderGraphBufferRef Buffer;
        void* Data;
		size_t Size;
    };
    void AddUploadBuffers(const std::vector<UploadBufferDesc>& desc, EUploadPolicy policy = EUploadPolicy::Deferred);
    void AddUploadBuffer(RenderGraphBufferRef buffer, const void* data, size_t size, EUploadPolicy policy = EUploadPolicy::Deferred);

    // Resource Creation (Examples - Add more as needed)
    RenderGraphTextureRef CreateTexture(const std::string& name, const RenderGraphTextureDesc& desc);
    RenderGraphBufferRef CreateBuffer(const std::string& name, const RenderGraphBufferDesc& desc);

    RenderGraphTextureSRVRef CreateTextureSRV(const std::string& name, const RenderGraphTextureSRVDesc& desc);
    RenderGraphBufferSRVRef CreateBufferSRV(const std::string& name, const RenderGraphBufferSRVDesc& desc);
    RenderGraphTextureUAVRef CreateTextureUAV(const std::string& name, const RenderGraphTextureUAVDesc& desc);
    RenderGraphBufferUAVRef CreateBufferUAV(const std::string& name, const RenderGraphBufferUAVDesc& desc);

    RenderGraphTextureRef RegisterExternalTexture(const std::string& name, PooledRenderTarget* target);
    RenderGraphTextureRef RegisterExternalTexture(const std::string& name, RenderTexture* texture);
    RenderGraphBufferRef RegisterExternalBuffer(const std::string& name, RenderBuffer* buffer);

    // Resource Access (Get RHI resources from RenderGraphResources)
    RHI::RHITexture* GetTexture(RenderGraphResourceRef resource);
    RHI::RHIBuffer* GetBuffer(RenderGraphResourceRef resource);

    RenderGraphTextureRef GetTexture(const std::string& name);
    RenderGraphBufferRef GetBuffer(const std::string& name);

    TextureViewCache* GetExternalTextureViewCache(RenderGraphTextureRef tex);
	BufferViewCache* GetExternalBufferViewCache(RenderGraphBufferRef buf);
    template<typename T>
    T* AllocateParameter(){
        return m_Allocator.Allocate<T>();
    }



    // Execution
    void Execute(); // Executes all added passes
protected:
    void AnalyzePasses(); // Analyzes passes and groups them for execution
    void AllocateResources();
    void SetupPassInternal(
        RenderGraphPass* pass,
        const ShaderParametersMetadata* metadata,
        const void* parameters);
    void ApplyFinalStates();
    void ExecutaPasses();
private:
    void AnalyzeUpload();
    struct PendingBufferUpload;
    void AddUploadPass(std::vector<PendingBufferUpload>&& uploadInfo,RHI::EQueueType queueType);


    using PassList = std::list<RenderGraphPass*>; // Using std::list for pass management
    using PassListGroup = std::list<PassList>; // Group of passes for execution
    PassList m_Passes; // List of passes to execute (using std::list)
    PassListGroup m_ParallelPasses; // Group of passes to execute in parallel (using std::list)
    // Resource Cache
    std::unordered_map<std::string, RenderGraphTextureRef> m_TextureCache; // Cache for textures
    std::unordered_map<std::string, RenderGraphBufferRef> m_BufferCache;   // Cache for buffers
    std::unordered_map<std::string, RenderGraphTextureSRVRef> m_TextureSRVCache; // Cache for texture SRVs
    std::unordered_map<std::string, RenderGraphBufferSRVRef> m_BufferSRVCache;   // Cache for buffer SRVs
	std::unordered_map<std::string, RenderGraphTextureUAVRef> m_TextureUAVCache; // Cache for texture UAVs
    std::unordered_map<std::string, RenderGraphBufferUAVRef> m_BufferUAVCache;   // Cache for buffer UAVs

    // 新增：Builder 内部状态缓存
    struct TextureKey
    {
        RenderGraphTexture* Texture;
        RHI::RHISubresourceRange Range;

        bool operator==(const TextureKey& o) const
        {
            return Texture == o.Texture && Range == o.Range;
        }
    };

    struct TextureKeyHasher
    {
        size_t operator()(const TextureKey& k) const
        {
            size_t h = std::hash<void*>()(k.Texture);
            h ^= (size_t(k.Range.MipIndex) << 1);
            h ^= (size_t(k.Range.ArraySlice) << 2);
            h ^= (size_t(k.Range.PlaneSlice) << 3);
            return h;
        }
    };

    struct BufferKey
    {
        RenderGraphBuffer* Buffer;

        bool operator==(const BufferKey& o) const
        {
            return Buffer == o.Buffer ;
        }
    };

    struct BufferKeyHasher
    {
        size_t operator()(const BufferKey& k) const
        {
            size_t h = std::hash<void*>()(k.Buffer);
            return h;
        }
    };
    std::unordered_map<TextureKey, RHI::ERHIResourceAccess, TextureKeyHasher> m_FinalTextureStates;

    std::unordered_map<BufferKey, RHI::ERHIResourceAccess, BufferKeyHasher>   m_FinalBufferStates;

    //
// Texture
    struct ExternalTextureEntry
    {
        RenderGraphTextureRef RDGTexture;
        RenderTextureTracker* Tracker = nullptr;
        TextureViewCache* ViewCache = nullptr;
		RHI::RHITexture* RHITexture = nullptr;
    };

    std::unordered_map<RHI::RHITexture*, ExternalTextureEntry> m_ExternalTextures;


    // Buffer
    struct ExternalBufferEntry
    {
        RenderGraphBufferRef RDGBuffer;
        RenderBufferTracker* Tracker = nullptr;
        BufferViewCache* ViewCache = nullptr;
		RHI::RHIBuffer* RHIBuffer = nullptr;
    };

    std::unordered_map<RHI::RHIBuffer*, ExternalBufferEntry> m_ExternalBuffers;

    //
    std::unordered_map<RenderGraphTexture*, RHI::RHITexture*> m_RDGToRHITexture;
    std::unordered_map<RenderGraphBuffer*, RHI::RHIBuffer*>  m_RDGToRHIBuffer;

    std::unordered_map<RHI::RHITexture*, PooledTransientRenderTarget*> m_RHITextureToTransientTarget;
    std::unordered_map<RHI::RHIBuffer*, PooledTransientBuffer*>  m_RHIBufferTransientBuffer;
    RenderGraphAllocator m_Allocator;
    TransientResourceAllocator* m_TransientAllocator;

    struct ResourceLifetime
    {
        RenderGraphPass* FirstPass = nullptr;
        RenderGraphPass* LastPass = nullptr;

        uint32_t BeginPassIndex = UINT32_MAX;
        uint32_t EndPassIndex = 0;
    };

    std::unordered_map<RenderGraphResource*, ResourceLifetime> m_ResourceLifetimes;
    std::unordered_map<RenderGraphPass*, std::vector<RenderGraphResource*>> m_PassLastUseResources;

	struct PendingBufferUpload
	{
		RenderGraphBufferRef Buffer;
		const void* Data;
		size_t Size;
        RHI::EQueueType QueueType;
	};
    std::vector<PendingBufferUpload> m_PendingBufferUploads;
};

} // namespace WR::RenderCore