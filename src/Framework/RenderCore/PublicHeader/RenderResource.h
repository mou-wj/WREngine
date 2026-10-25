#pragma once

#include <string>
#include <memory>
#include <atomic>
#include <mutex>
#include "RHITransientResource.h"
#include "RHIDefine.h"
#include "RHICommandContex.h"
#include "RenderResourceTracker.h"
#include "RHIApi.h"

namespace RenderCore {

    class TextureViewCache
    {
    public:
        TextureViewCache()
        {
        }

        RHI::RHIShaderResourceView* GetOrCreateSRV(
            RHI::RHITexture* texture,
            const RHI::RHITexSRVCreateInfo& desc)
        {
            auto it = m_SRVs.find(desc);
            if (it != m_SRVs.end())
                return it->second.get
                ();

            auto view = RHI::GRHIApi->CreateTextureShaderResourceView(texture, desc);
            m_SRVs.emplace(desc, view);
            return view.get();
        }

        RHI::RHIUnorderedAccessView* GetOrCreateUAV(
            RHI::RHITexture* texture,
            const RHI::RHITexUAVCreateInfo& desc)
        {
            auto it = m_UAVs.find(desc);
            if (it != m_UAVs.end())
                return it->second.get();

            auto view = RHI::GRHIApi->CreateTextureUnorderedAccessView(texture, desc);
            m_UAVs.emplace(desc, view);
            return view.get();
        }

        void Clear()
        {
            m_SRVs.clear();
            m_UAVs.clear();
        }

    private:
        std::unordered_map<RHI::RHITexSRVCreateInfo, RHI::RHIShaderResourceViewSP> m_SRVs;
        std::unordered_map<RHI::RHITexUAVCreateInfo, RHI::RHIUnorderedAccessViewSP> m_UAVs;
    };


    class BufferViewCache
    {
    public:
        BufferViewCache()
        {
        }

        RHI::RHIShaderResourceView* GetOrCreateSRV(
            RHI::RHIBuffer* buffer,
            const RHI::RHIBufferSRVCreateInfo& desc)
        {
            auto it = m_SRVs.find(desc);
            if (it != m_SRVs.end())
                return it->second.get();

            auto view = RHI::GRHIApi->CreateBufferShaderResourceView(buffer, desc);
            m_SRVs.emplace(desc, view);
            return view.get();
        }

        RHI::RHIUnorderedAccessView* GetOrCreateUAV(
            RHI::RHIBuffer* buffer,
            const RHI::RHIBufferUAVCreateInfo& desc)
        {
            auto it = m_UAVs.find(desc);
            if (it != m_UAVs.end())
                return it->second.get();

            auto view = RHI::GRHIApi->CreateBufferUnorderedAccessView(buffer, desc);
            m_UAVs.emplace(desc, view);
            return view.get();
        }

        void Clear()
        {
            m_SRVs.clear();
            m_UAVs.clear();
        }

    private:
        std::unordered_map<RHI::RHIBufferSRVCreateInfo, RHI::RHIShaderResourceViewSP> m_SRVs;
        std::unordered_map<RHI::RHIBufferUAVCreateInfo, RHI::RHIUnorderedAccessViewSP> m_UAVs;
    };

// 渲染资源生命周期管理基类
class RENDERCORE_API RenderResource
{
public:
    RenderResource();
    virtual ~RenderResource();

    virtual void InitRHIResource();
    virtual void ReleaseRHIResource();

    void SetName(const std::string& name);
    const std::string& GetName() const;

    bool IsInitialized() const;

protected:
    std::string _name;
    std::atomic<bool> _bInitialized{ false };
};

// 纹理资源
class RENDERCORE_API RenderTexture : public RenderResource
{
public:
    RenderTexture(const RHI::RHITextureDesc& inDesc);
    ~RenderTexture() override;

    void InitRHIResource() override;
    void ReleaseRHIResource() override;
    void UploadData(const void* data, uint32_t mipIndex ,uint32_t arraySlice,uint32_t planeSlice = 0);
    void UploadData(const void* data, const RHI::RHIUpdateTextureRegion& updateRegion);
    void ReadData(void* data, uint32_t mipIndex, uint32_t arraySlice, uint32_t planeSlice = 0);
    void GenerateMipMaps();
    RHI::RHITexture* GetRHI() const { return m_Texture.get(); }
    RenderTextureTracker& GetTracker() { return m_Tracker; }
    TextureViewCache& GetViewCache() { return m_ViewCache; }


private:
    TextureViewCache m_ViewCache;
    RenderTextureTracker m_Tracker;
    // 其他纹理相关接口
	RHI::RHITextureDesc m_Desc;
    RHI::RHITextureSP m_Texture;
};


RENDERCORE_API void TransitionTextureImmediate(
    RHI::RHIApi* api,
    RenderTexture* resource,
    RHI::ERHIResourceAccess targetAccess,
    RHI::EQueueType targetQueueType);



// 通用缓冲区资源
class RENDERCORE_API RenderBuffer : public RenderResource
{
public:
    RenderBuffer(const RHI::RHIBufferDesc& inDesc);
    ~RenderBuffer() override;

    void InitRHIResource() override;
    void ReleaseRHIResource() override;
	void UploadData(const void* data, uint32_t size, uint32_t offset = 0);
    // 其他缓冲区相关接口
	RHI::RHIBuffer* GetRHI() const { return m_Buffer.get(); }
    RenderBufferTracker& GetTracker() { return m_Tracker; }
	BufferViewCache& GetViewCache() { return m_ViewCache; }
private:
    BufferViewCache m_ViewCache;
    RenderBufferTracker m_Tracker;
    RHI::RHIBufferDesc m_Desc;
    RHI::RHIBufferSP m_Buffer;
};
RENDERCORE_API void TransitionBufferImmediate(
    RHI::RHIApi* api,
    RenderBuffer* resource,
    RHI::ERHIResourceAccess targetAccess,
    RHI::EQueueType targetQueueType);


struct RENDERCORE_API PoolRenderTargetDesc
{

    // ----------------------
    // 1. 尺寸 / 形态
    // ----------------------
    uint32_t Width = 1;
    uint32_t Height = 1;
    uint32_t Depth = 1;

    uint32_t ArraySize = 1;
    uint32_t MipLevels = 1;
    uint32_t SampleCount = 1;

    RHI::ERHITextureType Type = RHI::ERHITextureType::Texture2D;

    // ----------------------
    // 2. 格式
    // ----------------------
    RHI::ERHIFormat Format = RHI::ERHIFormat::Unknown;

    // ----------------------
    // 3. 用途（ImageUsage / BindFlags）
    // ----------------------
    RHI::ERHITextureCreateFlags Usage = RHI::ERHITextureCreateFlag::None;
   
    // ----------------------
    // 4. Clear（RHI 级）
    // ----------------------
    RHI::RHIClearValueBinding ClearValue;

    // ----------------------
    // 5. Debug
    // ----------------------
    const char* DebugName = "UnnamedPoolRT";

    // ----------------------
    // 6. 语义函数
    // ----------------------
    bool IsValid() const
    {
        return Width > 0
            && Height > 0
            && MipLevels > 0
            && SampleCount > 0
            && Format != RHI::ERHIFormat::Unknown;
    }

    bool IsDepthStencil() const
    {
        return ClearValue.Binding == RHI::RHIClearValueBinding::ClearValueBinding::DepthStencil;
    }

    bool IsColor() const
    {
        return ClearValue.Binding == RHI::RHIClearValueBinding::ClearValueBinding::Color;
    }

    static PoolRenderTargetDesc ConvertFromRHITextureDesc(const RHI::RHITextureDesc& inDesc)
    {
        PoolRenderTargetDesc desc;

        desc.Width = static_cast<int32_t>(inDesc.Width);
        desc.Height =static_cast<int32_t>(inDesc.Height) ;
        desc.Depth = static_cast<uint16_t>(inDesc.Depth);
        desc.ArraySize = static_cast<uint16_t>(inDesc.ArraySize);
        desc.MipLevels = static_cast<uint8_t>(inDesc.MipLevels);
        desc.SampleCount = static_cast<uint8_t>(inDesc.SampleCount);
        desc.Format = inDesc.Format;  // 假设 ERHIFormat 与 EPixelFormat 可映射
        desc.Usage = inDesc.Usage;

        desc.DebugName = inDesc.DebugName ? inDesc.DebugName : "RT_From_RHITextureDesc";

        return desc;
    }

    static RHI::RHITextureDesc ConvertToRHITextureDesc(const PoolRenderTargetDesc& inDesc) {
		RHI::RHITextureDesc desc;
		desc.Width = inDesc.Width;
		desc.Height = inDesc.Height;
		desc.Depth = inDesc.Depth;
		desc.ArraySize = inDesc.ArraySize;
		desc.MipLevels = inDesc.MipLevels;
		desc.SampleCount = inDesc.SampleCount;
		desc.Format = inDesc.Format;
		desc.Type = inDesc.Type;
		desc.Usage = inDesc.Usage;
		desc.DebugName = inDesc.DebugName;
		return desc;
    }
    bool Matches(const PoolRenderTargetDesc& rhs) const
    {
        return
            Type == rhs.Type &&
            Format == rhs.Format &&
            SampleCount == rhs.SampleCount &&

            IsDepthStencil() == rhs.IsDepthStencil() &&
            IsColor() == rhs.IsColor() &&

            (Usage & rhs.Usage) == rhs.Usage &&

            Width == rhs.Width &&
            Height == rhs.Height &&
            Depth == rhs.Depth &&

            MipLevels == rhs.MipLevels &&
            ArraySize == rhs.ArraySize;
    }
};

struct RENDERCORE_API IPooledRenderTarget
{
    virtual ~IPooledRenderTarget() = default;

    // ------------------------
    // Description
    // ------------------------
    virtual const PoolRenderTargetDesc& GetDesc() const = 0;

    // ------------------------
    // RHI Access
    // ------------------------
    virtual RHI::RHITexture* GetRHI() { return nullptr; };

    RenderTextureTracker& GetTracker() { return _tracker; }
    TextureViewCache& GetViewCache() { return _viewCache; }
protected:
    TextureViewCache _viewCache;
    RenderTextureTracker _tracker;
};


class RENDERCORE_API PooledRenderTarget final : public IPooledRenderTarget
{
public:
    PooledRenderTarget(
        const PoolRenderTargetDesc& inDesc,
        RHI::RHITextureSP inTexture)
        : m_Desc(inDesc)
    {
		_tracker.Initialize(inDesc.ArraySize, inDesc.MipLevels, 1);
        m_TargetTexture = inTexture;
    }

    virtual const PoolRenderTargetDesc& GetDesc() const override
    {
        return m_Desc;
    }
    RHI::RHITexture* GetRHI() override { return m_TargetTexture ? m_TargetTexture.get() : nullptr ; } ;

    void MarkUsed(bool inIsUsed = true) { m_bIsUse = inIsUsed; }
    bool IsUsed() const { return m_bIsUse; }
private:
    bool m_bIsUse = false;
    PoolRenderTargetDesc m_Desc;
    RHI::RHITextureSP m_TargetTexture;
};

// -------------------------------
// 精简 RenderTargetPool
// -------------------------------

class RENDERCORE_API RenderTargetPool
{
public:
    RenderTargetPool() = default;

    // 分配或复用 RenderTarget（重命名）
    std::shared_ptr<PooledRenderTarget> GetFreeRenderTarget(
        const PoolRenderTargetDesc& desc);

    // 可选：清空池
    void Clear();

private:
    // 垃圾回收：检查所有已分配target，若frame小于当前queue的frame则回收
    void GarbageCollect();

    std::mutex m_Mutex;
    std::vector<std::shared_ptr<PooledRenderTarget>> m_FreeList;
    // 记录所有已分配的target，便于垃圾回收
    std::vector<std::shared_ptr<PooledRenderTarget>> m_AllocatedList;
};

extern RENDERCORE_API RenderTargetPool GRenderTargetPool;

struct TransientBufferDesc {
    uint64_t Size = 0;                   // 缓冲区大小（字节）
    uint32_t Stride = 0;                 // 结构化缓冲区的元素大小
    RHI::ERHIBufferUsageFlags Usage = RHI::ERHIBufferUsageFlag::None; // 缓冲区用途
    bool bCPUAccessible = false;         // CPU是否可访问
    RHI::EQueueType InitialQueueType = RHI::EQueueType::Graphics; // 初始所属队列
    static RHI::RHIBufferDesc ConvertToRHIBufferDesc(const TransientBufferDesc& inDesc) {
        RHI::RHIBufferDesc desc;
        desc.Size = inDesc.Size;
        desc.Stride = inDesc.Stride;
        desc.Usage = inDesc.Usage;
        desc.bCPUAccessible = inDesc.bCPUAccessible;
        desc.InitialQueueType = inDesc.InitialQueueType;
        return desc;
    }
    static TransientBufferDesc ConvertFromRHIBufferDesc(const RHI::RHIBufferDesc& inDesc) {
        TransientBufferDesc desc;
        desc.Size = inDesc.Size;
        desc.Stride = inDesc.Stride;
        desc.Usage = inDesc.Usage;
        desc.bCPUAccessible = inDesc.bCPUAccessible;
        desc.InitialQueueType = inDesc.InitialQueueType;
        return desc;
    }
};

class PooledTransientBuffer
{
public:
	PooledTransientBuffer(
		const TransientBufferDesc& inDesc,
		RHI::RHITransientBufferSP inTransientBuffer)
		: m_Desc(inDesc)
		, m_TransientBuffer(std::move(inTransientBuffer))
	{
	}
    ~PooledTransientBuffer()
    {
        m_ViewCache.Clear();
        m_TransientBuffer.reset();
    }
    const TransientBufferDesc& GetDesc() const { return m_Desc; }
    RHI::RHIBuffer* GetRHI() const { return m_TransientBuffer->GetBuffer().get(); }
    RHI::RHITransientBuffer* GetTransientRHI() const { return m_TransientBuffer.get(); }
    RenderBufferTracker& GetTracker() { return m_Tracker; }
    BufferViewCache& GetViewCache() { return m_ViewCache; }
private:
    friend class TransientResourceAllocator;
    BufferViewCache m_ViewCache;
    RenderBufferTracker m_Tracker;
    TransientBufferDesc m_Desc;
    RHI::RHITransientBufferSP m_TransientBuffer = nullptr;


};


class RENDERCORE_API PooledTransientRenderTarget final : public IPooledRenderTarget
{
public:
    PooledTransientRenderTarget(
        const PoolRenderTargetDesc& inDesc,
        RHI::RHITransientTextureSP inTransientTexture)
        : m_Desc(inDesc)
        , m_TransientTexture(std::move(inTransientTexture))
    {
        _tracker.Initialize(inDesc.ArraySize, inDesc.MipLevels, 1);
    }
    ~PooledTransientRenderTarget()
    {
        _viewCache.Clear();
        m_TransientTexture.reset();
    }
    virtual const PoolRenderTargetDesc& GetDesc() const override
    {
        return m_Desc;
    }

    virtual RHI::RHITransientTexture* GetTransientRHI()
    {
        return m_TransientTexture.get();
    }

    virtual RHI::RHITexture* GetRHI() override
    {
        return m_TransientTexture
            ? m_TransientTexture->GetTexture().get()
            : nullptr;
    }


private:
    friend class TransientResourceAllocator;
    PoolRenderTargetDesc m_Desc;
    // ✅ 改成 shared_ptr（核心）
    RHI::RHITransientTextureSP m_TransientTexture;
};


// -------------------------------
// 精简 Transient Allocator（无 PassHandle）
// -------------------------------
// --------------------------------------
// TransientResourceAllocator（优化版）
// --------------------------------------
class RENDERCORE_API TransientResourceAllocator
{
public:
    TransientResourceAllocator() = default;
    ~TransientResourceAllocator() = default;
    void InitRHI();
    void ReleaseRHI();

    // -------------------------------
    // Allocate
    // -------------------------------
    std::shared_ptr<PooledTransientRenderTarget> AllocateRenderTarget(
        const PoolRenderTargetDesc& desc,
        uint32_t beginIndex,
        uint32_t endIndex)
    {
        // 👉 直接创建 transient texture（核心）
        auto transientTex = m_TransientResourceManager->CreateTransientTexture(
            PoolRenderTargetDesc::ConvertToRHITextureDesc(desc),
            beginIndex,
            endIndex);

        // 👉 创建 wrapper（不复用）
        auto target = std::make_shared<PooledTransientRenderTarget>(
            desc,
            transientTex);
        m_AllocatedTextures.push_back(target);
        return target;
    }
    std::shared_ptr<PooledTransientBuffer> AllocateBuffer(
        const TransientBufferDesc& desc,
        uint32_t beginIndex,
        uint32_t endIndex)
    {
        // 👉 直接创建 transient texture（核心）
        auto transientBuf = m_TransientResourceManager->CreateTransientBuffer(
            TransientBufferDesc::ConvertToRHIBufferDesc(desc),
            beginIndex,
            endIndex);

        // 👉 创建 wrapper（不复用）
        auto buf = std::make_shared<PooledTransientBuffer>(
            desc,
            transientBuf);
        m_AllocatedBuffers.push_back(buf);
        return buf;
    }

    void GarbageCollect();
private:
    
    std::vector<std::shared_ptr<PooledTransientRenderTarget>> m_AllocatedTextures;
    std::vector<std::shared_ptr<PooledTransientBuffer>>      m_AllocatedBuffers;
    RHI::RHITransientResourceManagerSP m_TransientResourceManager;
};

extern RENDERCORE_API TransientResourceAllocator GTransientResourceAllocator;

// 智能指针类型
using RenderResourceSP = std::shared_ptr<RenderResource>;
using RenderTextureSP = std::shared_ptr<RenderTexture>;
using RenderBufferSP = std::shared_ptr<RenderBuffer>;

extern RENDERCORE_API RenderTextureSP GlobalTestTexture;
extern RENDERCORE_API RenderTextureSP GlobalWhiteTexture;
extern RENDERCORE_API RenderTextureSP GlobalFlatNormalTexture;
extern RENDERCORE_API RenderTextureSP GlobalEmptyCubeTexture;
extern RENDERCORE_API RenderTextureSP GlobalEmptyTexture2DArray;
extern RENDERCORE_API RHI::RHISamplerSP GlobalSampler;
extern RENDERCORE_API RHI::RHISamplerSP GlobalNearestSampler;
extern RENDERCORE_API RenderBufferSP GlobalEmptyBuffer;
RENDERCORE_API bool InitGlobalRenderResource();
RENDERCORE_API void ReleaseGlobalRenderResource();

RENDERCORE_API RenderTextureSP CreateTexture(const std::string& path);
RENDERCORE_API void SaveTexture(RenderTexture* texture,const std::string& path,uint32_t array,uint32_t mip);

RENDERCORE_API RenderTextureSP Create3DTexture(uint32_t width, uint32_t height, uint32_t depth, RHI::ERHIFormat format, RHI::ERHITextureCreateFlags usage, const char* debugName);

} // namespace RenderCore