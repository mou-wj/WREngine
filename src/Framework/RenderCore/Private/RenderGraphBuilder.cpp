#ifndef NOMINMAX
#define NOMINMAX
#endif // !NOMINMAX

#include "RenderGraphBuilder.h"
#include "RHIApi.h"
#include "RenderGraphResource.h"
#include "TaskPool.h"
#include "RenderThread.h"
#include "RHICommandContex.h"
#include <unordered_set>
#include <queue>
using namespace RHI;

namespace RenderCore {


    RenderGraphBuilder::RenderGraphBuilder() : m_TransientAllocator(&GTransientResourceAllocator)
    {
    }

    RenderGraphBuilder::~RenderGraphBuilder()
    {
        // Clean up passes
        m_Passes.clear();
    }


    void RenderGraphBuilder::AddPassDependency(RenderGraphPass* pass, RenderGraphPass* passConsumer)
    {
        pass->_passConsumers.push_back(passConsumer);
        passConsumer->_passProducers.push_back(pass);
    }

    void RenderGraphBuilder::AddUploadBuffers(const std::vector<UploadBufferDesc>& desc, EUploadPolicy policy)
    {
        EQueueType queueType = EQueueType::Graphics;
        std::vector<PendingBufferUpload> graphicUploads;
        std::vector<PendingBufferUpload> computeUploads;
        for (auto& buf : desc) {
            if (buf.Buffer->m_bIsExternal) {
                queueType = m_ExternalBuffers[buf.Buffer->GetRHIBuffer()].Tracker->GetLastAccessFence().QueueType;
            }
            auto mem = m_Allocator.AllocateBytes(buf.Size, 16);
            memcpy(mem, buf.Data, buf.Size);
            PendingBufferUpload bufferUpload;
            bufferUpload.Buffer = buf.Buffer;
            bufferUpload.Data = mem;
            bufferUpload.Size = buf.Size;
            bufferUpload.QueueType = queueType;
            if (queueType == EQueueType::Graphics) {
                graphicUploads.push_back(bufferUpload);
			}
			else if (queueType == EQueueType::Compute){
			   computeUploads.push_back(bufferUpload); 
            }
        }
        if (policy == EUploadPolicy::Immediate) {
            AddUploadPass(std::move(graphicUploads), queueType);
            AddUploadPass(std::move(computeUploads), queueType);
        }
		m_PendingBufferUploads.insert(m_PendingBufferUploads.end(), graphicUploads.begin(), graphicUploads.end());
        m_PendingBufferUploads.insert(m_PendingBufferUploads.end(), computeUploads.begin(), computeUploads.end());
    }

    void RenderGraphBuilder::AddUploadBuffer(RenderGraphBufferRef buffer, const void* data, size_t size,EUploadPolicy policy)
    {
        UploadBufferDesc desc;
        desc.Buffer = buffer;
        desc.Data = (void*)data;
        desc.Size = size;
		AddUploadBuffers({ desc }, policy);
    }

    RenderGraphTextureRef RenderGraphBuilder::CreateTexture(const std::string& name, const RenderGraphTextureDesc& desc)
    {
        auto it = m_TextureCache.find(name);
        if (it != m_TextureCache.end())
        {
            return it->second;
        }

        // 用 Allocator
        auto texture = m_Allocator.Allocate<RenderGraphTexture>(name, desc);

        m_TextureCache[name] = texture;
        return texture;
    }

    RenderGraphBufferRef RenderGraphBuilder::CreateBuffer(const std::string& name, const RenderGraphBufferDesc& desc)
    {
        auto it = m_BufferCache.find(name);
        if (it != m_BufferCache.end())
        {
            return it->second;
        }

        auto buffer = m_Allocator.Allocate<RenderGraphBuffer>(name, desc);


        m_BufferCache[name] = buffer;
        return buffer;
    }

    RenderGraphTextureSRVRef RenderGraphBuilder::CreateTextureSRV(const std::string& name, const RenderGraphTextureSRVDesc& desc)
    {
        auto it = m_TextureSRVCache.find(name);
        if (it != m_TextureSRVCache.end())
        {
            return it->second;
        }

        auto srv = m_Allocator.Allocate<RenderGraphTextureSRV>(name, desc);

        m_TextureSRVCache[name] = srv;
        return srv;
    }
    
    RenderGraphBufferSRVRef RenderGraphBuilder::CreateBufferSRV(const std::string& name, const RenderGraphBufferSRVDesc& desc)
    {
        auto it = m_BufferSRVCache.find(name);
        if (it != m_BufferSRVCache.end())
        {
            return it->second;
        }

        auto srv = m_Allocator.Allocate<RenderGraphBufferSRV>(name, desc);

        m_BufferSRVCache[name] = srv;
        return srv;
    }

    RenderGraphTextureUAVRef RenderGraphBuilder::CreateTextureUAV(const std::string& name, const RenderGraphTextureUAVDesc& desc)
    {
        auto it = m_TextureUAVCache.find(name);
        if (it != m_TextureUAVCache.end())
        {
            return it->second;
        }

        auto uav = m_Allocator.Allocate<RenderGraphTextureUAV>(name, desc);

        m_TextureUAVCache[name] = uav;
        return uav;
    }

    RenderGraphBufferUAVRef RenderGraphBuilder::CreateBufferUAV(const std::string& name, const RenderGraphBufferUAVDesc& desc)
    {
        auto it = m_BufferUAVCache.find(name);
        if (it != m_BufferUAVCache.end())
        {
            return it->second;
        }

        auto uav = m_Allocator.Allocate<RenderGraphBufferUAV>(name, desc);

        m_BufferUAVCache[name] = uav;
        return uav;
    }
    RenderGraphTextureRef RenderGraphBuilder::RegisterExternalTexture(const std::string& name, PooledRenderTarget* target)
    {
        if (!target)
            return nullptr;

        RHI::RHITexture* rhiTex = target->GetRHI();

        // 👉 已注册（按 RHI 去重）
        auto it = m_ExternalTextures.find(rhiTex);
        if (it != m_ExternalTextures.end())
        {
            return it->second.RDGTexture;
        }

        // 👉 创建 RDGTexture
        RenderGraphTextureDesc desc;
        desc = (RenderGraphTextureDesc)PoolRenderTargetDesc::ConvertToRHITextureDesc(target->GetDesc());

        auto rdgTex = CreateTexture(name, desc);

        rdgTex->_resource = rhiTex;
        rdgTex->m_bIsExternal = true;

        // 👉 建立映射
        m_RDGToRHITexture[rdgTex] = rhiTex;

        ExternalTextureEntry entry;
        entry.RDGTexture = rdgTex;
        entry.Tracker = &target->GetTracker();
        entry.ViewCache = &target->GetViewCache();
        entry.RHITexture = rhiTex;

        m_ExternalTextures[rhiTex] = entry;
        return rdgTex;
    }
    RenderGraphTextureRef RenderGraphBuilder::RegisterExternalTexture(const std::string& name, RenderTexture* texture)
    {
        if (!texture)
            return nullptr;

        RHI::RHITexture* rhiTex = texture->GetRHI();

        auto it = m_ExternalTextures.find(rhiTex);
        if (it != m_ExternalTextures.end())
        {
            return it->second.RDGTexture;
        }

        // 👉 构造 desc（你可能需要补一个转换函数）
        RenderGraphTextureDesc desc;
        static_cast<RHI::RHITextureDesc&>(desc) = texture->GetRHI()->GetDesc(); // ⚠️ 如果没有，需要你自己实现转换
        
        auto rdgTex = CreateTexture(name, desc);

        rdgTex->_resource = rhiTex;
        rdgTex->m_bIsExternal = true;

        m_RDGToRHITexture[rdgTex] = rhiTex;

        ExternalTextureEntry entry;
        entry.RDGTexture = rdgTex;
        entry.Tracker = &texture->GetTracker();
        entry.RHITexture = rhiTex;
        entry.ViewCache = &texture->GetViewCache();

        m_ExternalTextures[rhiTex] = entry;

        return rdgTex;
    }
    RenderGraphBufferRef RenderGraphBuilder::RegisterExternalBuffer(const std::string& name, RenderBuffer* buffer)
    {
        if (!buffer)
            return nullptr;

        RHI::RHIBuffer* rhiBuf = buffer->GetRHI();

        auto it = m_ExternalBuffers.find(rhiBuf);
        if (it != m_ExternalBuffers.end())
        {
            return it->second.RDGBuffer;
        }

        // 👉 构造 desc
        RenderGraphBufferDesc desc;
        static_cast<RHI::RHIBufferDesc&>(desc) = buffer->GetRHI()->GetDesc(); // ⚠️ 同样需要转换

        auto rdgBuf = CreateBuffer(name, desc);

        rdgBuf->_resource = rhiBuf;
        rdgBuf->m_bIsExternal = true;

        m_RDGToRHIBuffer[rdgBuf] = rhiBuf;

        ExternalBufferEntry entry;
        entry.RDGBuffer = rdgBuf;
        entry.Tracker = &buffer->GetTracker();
        entry.RHIBuffer = rhiBuf;
        entry.ViewCache = &buffer->GetViewCache();

        m_ExternalBuffers[rhiBuf] = entry;

        return rdgBuf;
    }
    RHITexture* RenderGraphBuilder::GetTexture(RenderGraphResourceRef resource)
    {
        auto textureResource = static_cast<RenderGraphTexture*>(resource);
        if (!textureResource)
        {
            // handle error
        }
        return textureResource->GetRHITexture();
    }

    RHIBuffer* RenderGraphBuilder::GetBuffer(RenderGraphResourceRef resource)
    {
        auto bufferResource = static_cast<RenderGraphBuffer*>(resource);
        if (!bufferResource)
        {
            // handle error
        }
        return bufferResource->GetRHIBuffer();
    }

    RenderGraphTextureRef RenderGraphBuilder::GetTexture(const std::string& name)
    {
        return m_TextureCache[name];
    }

    RenderGraphBufferRef RenderGraphBuilder::GetBuffer(const std::string& name)
    {
        return m_BufferCache[name];
    }

    TextureViewCache* RenderGraphBuilder::GetExternalTextureViewCache(RenderGraphTextureRef tex)
    {
        auto it = m_ExternalTextures.find(tex->GetRHITexture());
		if (it != m_ExternalTextures.end())
		{
			return it->second.ViewCache;
		}
        return nullptr;
    }

    BufferViewCache* RenderGraphBuilder::GetExternalBufferViewCache(RenderGraphBufferRef buf)
    {
        auto it = m_ExternalBuffers.find(buf->GetRHIBuffer());
        if (it != m_ExternalBuffers.end())
        {
        	return it->second.ViewCache;
        }
        return nullptr;
    }

    void RenderGraphBuilder::Execute()
    {
        AnalyzeUpload();
        AnalyzePasses();
        AllocateResources();
        ExecutaPasses();
        ApplyFinalStates();
        m_TransientAllocator->GarbageCollect();
    }

    void RenderGraphBuilder::AnalyzePasses()
    {
        // =========================================
            // 清理旧数据
            // =========================================
        m_ParallelPasses.clear();
        m_FinalTextureStates.clear();
        m_FinalBufferStates.clear();
        m_ResourceLifetimes.clear();
        m_PassLastUseResources.clear();

        // =========================================
        // 工具函数
        // =========================================
        auto isWrite = [](ERHIResourceAccess a)
            {
                return EnumHasAnyFlags(a, ERHIResourceAccess::WritableMask);
            };

        auto isUav = [](ERHIResourceAccess a)
            {
                return EnumHasAnyFlags(a, ERHIResourceAccess::UAV);
            };

        auto needBarrier = [&](ERHIResourceAccess before, ERHIResourceAccess after)
            {
                if (before == ERHIResourceAccess::Unknown && after != ERHIResourceAccess::Unknown) {
                    return true;
                }
                const bool beforeWrite = isWrite(before);
                const bool afterWrite = isWrite(after);

                if (isUav(before) && isUav(after))
                    return true;

                if (!beforeWrite && !afterWrite)
                    return false;

                return before != after;
            };

        // =========================================
        // Runtime tracking
        // =========================================
        struct TextureState
        {
            RenderGraphPass* LastVisitor = nullptr;
            ERHIResourceAccess LastAccess = ERHIResourceAccess::Unknown;
            bool bInitialized = false;
        };

        struct BufferState
        {
            RenderGraphPass* LastVisitor = nullptr;
            ERHIResourceAccess LastAccess = ERHIResourceAccess::Unknown;
            bool bInitialized = false;
        };

        std::unordered_map<TextureKey, TextureState, TextureKeyHasher> textureStates;
        std::unordered_map<BufferKey, BufferState, BufferKeyHasher>  bufferStates;

        // =========================================
        // Pass index（用于 lifetime）
        // =========================================
        std::unordered_map<RenderGraphPass*, uint32_t> passIndex;
        uint32_t passOrder = 0;

        for (auto* p : m_Passes)
        {
            passIndex[p] = passOrder++;
        }

        // =========================================
        // 初始状态获取
        // =========================================
        auto getInitialTextureAccess = [&](RHI::RHITexture* texture,const RHI::RHISubresourceRange& range)
            {
                auto it = m_ExternalTextures.find(texture);
                if (it != m_ExternalTextures.end())
                    return it->second.Tracker->GetSubresourceAccess(range);

                return ERHIResourceAccess::Unknown;
            };

        auto getInitialBufferAccess = [&](RHI::RHIBuffer* buffer)
            {
                auto it = m_ExternalBuffers.find(buffer);
                if (it != m_ExternalBuffers.end())
                    return it->second.Tracker->GetLastAccess();
                return ERHIResourceAccess::Unknown;
            };

        // =========================================
        // Phase 1: Barrier + DAG + Lifetime
        // =========================================
        for (auto* pass : m_Passes)
        {
            // =========================
            // Texture
            // =========================
            for (const auto& intent : pass->_textureIntents)
            {
                TextureKey key{ intent.Texture, intent.SubresourceRange };
                auto& state = textureStates[key];

                if (!state.bInitialized)
                {
                    state.LastAccess = getInitialTextureAccess(intent.Texture->GetRHITexture(),intent.SubresourceRange);
                    state.bInitialized = true;
                }

                // ===== Lifetime =====
                {
                    auto* res = intent.Texture;
                    auto& lt = m_ResourceLifetimes[res];

                    if (!lt.FirstPass)
                        lt.FirstPass = pass;

                    lt.LastPass = pass;

                    uint32_t idx = passIndex[pass];
                    lt.BeginPassIndex = std::min(lt.BeginPassIndex, idx);
                    lt.EndPassIndex = std::max(lt.EndPassIndex, idx);
                }

                // ===== Barrier =====
                if (needBarrier(state.LastAccess, intent.RequiredAccess))
                {
                    RHI::RHITransitionInfo transition{};
                    transition.Type = RHI::RHITransitionInfo::EType::Texture;
                    transition.AccessBefore = state.LastAccess;
                    transition.AccessAfter = intent.RequiredAccess;

                    // ✅ RDG层信息
                    static_cast<RHI::RHISubresourceRange>(transition) = intent.SubresourceRange;

                    pass->_beginBarrier.AddTransition(intent.Texture, transition);


                }
                if (state.LastVisitor && state.LastVisitor != pass)
                {
                    state.LastVisitor->_passConsumers.push_back(pass);
                    pass->_passProducers.push_back(state.LastVisitor);
                }

                state.LastVisitor = pass;
                state.LastAccess = intent.RequiredAccess;

                m_FinalTextureStates[key] = intent.RequiredAccess;
            }

            // =========================
            // Buffer
            // =========================
            for (const auto& intent : pass->_bufferStates)
            {
                BufferKey key{ intent.Buffer };
                auto& state = bufferStates[key];

                if (!state.bInitialized)
                {
                    state.LastAccess = getInitialBufferAccess(intent.Buffer->GetRHIBuffer());
                    state.bInitialized = true;
                }

                // ===== Lifetime =====
                {
                    auto* res = intent.Buffer;
                    auto& lt = m_ResourceLifetimes[res];

                    if (!lt.FirstPass)
                        lt.FirstPass = pass;

                    lt.LastPass = pass;

                    uint32_t idx = passIndex[pass];
                    lt.BeginPassIndex = std::min(lt.BeginPassIndex, idx);
                    lt.EndPassIndex = std::max(lt.EndPassIndex, idx);
                }

                // ===== Barrier =====
                if (needBarrier(state.LastAccess, intent.RequiredAccess))
                {
                    RHI::RHITransitionInfo transition{};
                    transition.Type = RHI::RHITransitionInfo::EType::Buffer;
                    transition.AccessBefore = state.LastAccess;
                    transition.AccessAfter = intent.RequiredAccess;

                    pass->_beginBarrier.AddTransition(intent.Buffer, transition);
                }
                if (state.LastVisitor && state.LastVisitor != pass)
                {
                    state.LastVisitor->_passConsumers.push_back(pass);
                    pass->_passProducers.push_back(state.LastVisitor);
                }

                state.LastVisitor = pass;
                state.LastAccess = intent.RequiredAccess;

                m_FinalBufferStates[key] = intent.RequiredAccess;
            }
        }

        // =========================================
        // Phase 2: 拓扑排序（并行分组）
        // =========================================
        std::unordered_map<RenderGraphPass*, size_t> indegree;
        std::unordered_set<RenderGraphPass*> allNodes;

        for (auto* pass : m_Passes)
        {
            allNodes.insert(pass);
            indegree[pass] = pass->_passProducers.size();
        }

        std::queue<RenderGraphPass*> zeroQueue;

        for (auto* node : allNodes)
        {
            if (indegree[node] == 0)
                zeroQueue.push(node);
        }

        std::unordered_set<RenderGraphPass*> processed;

        while (!allNodes.empty())
        {
            std::vector<RenderGraphPass*> currentSet;

            if (zeroQueue.empty())
            {
                auto it = allNodes.begin();
                currentSet.push_back(*it);
            }
            else
            {
                size_t count = zeroQueue.size();
                for (size_t i = 0; i < count; ++i)
                {
                    auto* n = zeroQueue.front();
                    zeroQueue.pop();

                    if (!processed.count(n))
                        currentSet.push_back(n);
                }
            }

            PassList group;

            for (auto* node : currentSet)
            {
                group.push_back(node);
                processed.insert(node);
                allNodes.erase(node);
            }

            for (auto* node : currentSet)
            {
                for (auto* consumer : node->_passConsumers)
                {
                    if (--indegree[consumer] == 0)
                        zeroQueue.push(consumer);
                }
            }

            if (!group.empty())
                m_ParallelPasses.push_back(group);
        }

        // fallback
        for (auto* pass : m_Passes)
        {
            if (!processed.count(pass))
            {
                PassList single;
                single.push_back(pass);
                m_ParallelPasses.push_back(single);
            }
        }

        //last use resource
        for (auto& [res, lifetime] : m_ResourceLifetimes)
        {
            if (lifetime.LastPass)
            {
                m_PassLastUseResources[lifetime.LastPass].push_back(res);
            }
        }
    }

    void RenderGraphBuilder::AllocateResources()
    {
        std::unordered_map<RHI::RHITexture*, TextureViewCache*> internalTextureViewCaches;
        std::unordered_map<RHI::RHIBuffer*, BufferViewCache*> internalBufferViewCaches;
        // =========================
            // Texture
            // =========================
        for (auto& [name, tex] : m_TextureCache)
        {
            if (!tex) continue;

            // 外部资源跳过
            if (tex->m_bIsExternal)
                continue;
			

            // =========================================
            // 👉 关键改动：读取 lifetime
            // =========================================
            auto it = m_ResourceLifetimes.find(tex);
            if (it == m_ResourceLifetimes.end())
                continue; // 没有被 graph 使用的资源不创建

            const ResourceLifetime& lt = it->second;

            // =========================================
            // 创建 transient texture（带生命周期）
            // =========================================
            PoolRenderTargetDesc desc =
                PoolRenderTargetDesc::ConvertFromRHITextureDesc(tex->GetDesc());

            auto rhiTex = m_TransientAllocator->AllocateRenderTarget(
                desc,
                lt.BeginPassIndex,
                lt.EndPassIndex
            );
            internalTextureViewCaches[rhiTex->GetRHI()] = &rhiTex->GetViewCache(); // 内部资源创建独立 view cache
            tex->_resource = rhiTex->GetRHI();
            m_RHITextureToTransientTarget[tex->GetRHITexture()] = rhiTex.get();
        }

        // =========================
        // Buffer
        // =========================
        for (auto& [name, buf] : m_BufferCache)
        {
            if (!buf) continue;

            if (buf->m_bIsExternal)
                continue;

            if (buf->GetRHIBuffer())
                continue;

            // =========================================
            // 👉 lifetime driven
            // =========================================
            auto it = m_ResourceLifetimes.find(buf);
            if (it == m_ResourceLifetimes.end())
                continue;

            const ResourceLifetime& lt = it->second;

            auto rhiBuf = m_TransientAllocator->AllocateBuffer(
                TransientBufferDesc::ConvertFromRHIBufferDesc(buf->GetDesc()),
                lt.BeginPassIndex,
                lt.EndPassIndex
            );
            internalBufferViewCaches[rhiBuf->GetRHI()] = &rhiBuf->GetViewCache();
            buf->_resource = rhiBuf->GetRHI();
            m_RHIBufferTransientBuffer[buf->GetRHIBuffer()] = rhiBuf.get();
        }

        //view
            // =========================
            // Texture SRV
            // =========================
        for (auto& [name, srv] : m_TextureSRVCache)
        {
            if (!srv) continue;
            if (srv->_resource) continue;
            RenderGraphTexture* parent = srv->m_Desc.Texture;
            RHI::RHITexture* parentRHI = parent->GetRHITexture();
            TextureViewCache* viewCache = nullptr;
            if (!parent->m_bIsExternal) {
                viewCache = internalTextureViewCaches[parentRHI];
            }
            else {
                viewCache = m_ExternalTextures[parentRHI].ViewCache;
            }
            RHI::RHITexSRVCreateInfo info = static_cast<RHI::RHITexSRVCreateInfo>(srv->m_Desc);
            auto rhi = viewCache->GetOrCreateSRV(parentRHI, info);
            srv->_resource = rhi;
        }

        // =========================
        // Texture UAV
        // =========================
        for (auto& [name, uav] : m_TextureUAVCache)
        {
            if (!uav) continue;
            if (uav->_resource) continue;
            RenderGraphTexture* parent = uav->m_Desc.Texture;
            RHI::RHITexture* parentRHI = parent->GetRHITexture();
            TextureViewCache* viewCache = nullptr;
            if (!parent->m_bIsExternal) {
                viewCache = internalTextureViewCaches[parentRHI];
            }
            else {
                viewCache = m_ExternalTextures[parentRHI].ViewCache;
            }
            RHI::RHITexUAVCreateInfo info = static_cast<RHI::RHITexUAVCreateInfo>(uav->m_Desc);
            auto rhi = viewCache->GetOrCreateUAV(parentRHI, info);
            uav->_resource = rhi;
        }

        // =========================
        // Buffer SRV
        // =========================
        for (auto& [name, srv] : m_BufferSRVCache)
        {
            if (!srv) continue;
            if (srv->_resource) continue;
            RenderGraphBuffer* parent = srv->m_Desc.Buffer;
            RHI::RHIBuffer* parentRHI = parent->GetRHIBuffer();
            BufferViewCache* viewCache = nullptr;
            if (!parent->m_bIsExternal) {
                viewCache = internalBufferViewCaches[parentRHI];
            }
            else {
                viewCache = m_ExternalBuffers[parentRHI].ViewCache;
            }
            RHI::RHIBufferSRVCreateInfo info = static_cast<RHI::RHIBufferSRVCreateInfo>(srv->m_Desc);
            auto rhi = viewCache->GetOrCreateSRV(parentRHI, info);
            srv->_resource = rhi;
        }

        // =========================
        // Buffer UAV
        // =========================
        for (auto& [name, uav] : m_BufferUAVCache)
        {
            if (!uav) continue;
            if (uav->_resource) continue;
            RenderGraphBuffer* parent = uav->m_Desc.Buffer;
            RHI::RHIBuffer* parentRHI = parent->GetRHIBuffer();
            BufferViewCache* viewCache = nullptr;
            if (!parent->m_bIsExternal) {
                viewCache = internalBufferViewCaches[parentRHI];
            }
            else {
                viewCache = m_ExternalBuffers[parentRHI].ViewCache;
            }
            RHI::RHIBufferUAVCreateInfo info = static_cast<RHI::RHIBufferUAVCreateInfo>(uav->m_Desc);
            auto rhi = viewCache->GetOrCreateUAV(parentRHI, info);
            uav->_resource = rhi;
        }

    }

    void RenderGraphBuilder::SetupPassInternal(
        RenderGraphPass* pass,
        const ShaderParametersMetadata* metadata,
        const void* parameters)
    {
        const uint8_t* baseDataPtr = reinterpret_cast<const uint8_t*>(parameters);

        for (const auto& member : metadata->GetMembers())
        {
            const uint8_t* memberAddr = baseDataPtr + member.Offset;

            if (member.IsResource())
            {
                std::vector<const uint8_t*> resourcePtrs;
                if (member.IsArray()) {
                    auto* array =
                        reinterpret_cast<const ShaderParameterElementAccessor*>(memberAddr);
                    resourcePtrs.resize(array->GetElementCount());
                    for (uint32_t i = 0; i < array->GetElementCount(); ++i) {
                        resourcePtrs[i] = array->GetElementOffset(i);
                    }
                }
                else {
                    resourcePtrs.push_back(memberAddr);
                }
                for (uint32_t i = 0; i < resourcePtrs.size(); ++i) {

                    const uint8_t* memberAddr = resourcePtrs[i];
                    // ============================
                    // Texture SRV
                    // ============================
                    if (member.BaseType == EShaderParameterBaseType::RDGTexture_SRV)
                    {
                        auto srv = *reinterpret_cast<RenderGraphTextureSRV* const*>(memberAddr);

                        if (srv)
                        {
                            auto* tex = static_cast<RenderGraphTexture*>(srv->m_Desc.Texture);
                            const auto& texDesc = srv->m_Desc.Texture->GetDesc();
                            const auto& desc = srv->m_Desc;

                            RenderGraphPass::RenderGraphTextureIntent intent;
                            intent.Texture = tex;
                            uint32_t mipSlice = desc.FirstMipSlice;
                            if (desc.MipCount == texDesc.MipLevels && desc.FirstMipSlice == 0) {
                                mipSlice = RHISubresourceRange::KAllSubresources;
                            }
                            uint32_t arraySlice = desc.FirstArraySlice;
                            if (desc.ArraySize ==  texDesc.ArraySize && desc.FirstArraySlice == 0) {
                                arraySlice = RHISubresourceRange::KAllSubresources;
                            }

                            intent.SubresourceRange = RHISubresourceRange(
                                mipSlice,
                                arraySlice,
                                RHISubresourceRange::KAllSubresources
                            );

                            // SRV → ReadOnly
                            intent.RequiredAccess = ERHIResourceAccess::SRV;

                            pass->AddTextureIntent(intent);
                        }
                    }
                    // ============================
                    // Texture UAV
                    // ============================
                    else if (member.BaseType == EShaderParameterBaseType::RDGTexture_UAV)
                    {
                        auto uav = *reinterpret_cast<RenderGraphTextureUAV* const*>(memberAddr);

                        if (uav)
                        {
                            auto* tex = static_cast<RenderGraphTexture*>(uav->m_Desc.Texture);
                            const auto& texDesc = uav->m_Desc.Texture->GetDesc();
                            const auto& desc = uav->m_Desc;

                            RenderGraphPass::RenderGraphTextureIntent intent;
                            intent.Texture = tex;
                            uint32_t mipSlice = desc.FirstMipSlice;
                            if (desc.MipCount == texDesc.MipLevels && desc.FirstMipSlice == 0) {
                                mipSlice = RHISubresourceRange::KAllSubresources;
                            }
                            uint32_t arraySlice = desc.FirstArraySlice;
                            if (desc.ArraySize == texDesc.ArraySize && desc.FirstArraySlice == 0) {
                                arraySlice = RHISubresourceRange::KAllSubresources;
                            }
                            intent.SubresourceRange = RHISubresourceRange(
                                mipSlice,
                                arraySlice,
                                RHISubresourceRange::KAllSubresources
                            );

                            // UAV → ReadWrite
                            intent.RequiredAccess = ERHIResourceAccess::UAV;

                            pass->AddTextureIntent(intent);
                        }
                    }
                    // ============================
                    // Texture (裸Texture，极少用)
                    // ============================
                    else if (member.BaseType == EShaderParameterBaseType::RDGTexture)
                    {
                        auto tex = *reinterpret_cast<RenderGraphTexture* const*>(memberAddr);

                        if (tex)
                        {
                            RenderGraphPass::RenderGraphTextureIntent intent;
                            intent.Texture = tex;
                            intent.SubresourceRange = RHISubresourceRange(); // whole

                            intent.RequiredAccess = ERHIResourceAccess::SRV;

                            pass->AddTextureIntent(intent);
                        }
                    }

                    // ============================
                    // Buffer SRV
                    // ============================
                    else if (member.BaseType == EShaderParameterBaseType::RDGBuffer_SRV)
                    {
                        auto srv = *reinterpret_cast<RenderGraphBufferSRV* const*>(memberAddr);
                        
                        if (srv)
                        {
                            auto vDesc = srv->m_Desc;
                            auto* buf = static_cast<RenderGraphBuffer*>(srv->m_Desc.Buffer);

                            RenderGraphPass::RenderGraphBufferIntent intent;
                            intent.Buffer = buf;

                            intent.RequiredAccess = ERHIResourceAccess::SRV;

                            pass->AddBufferIntent(intent);
                        }
                    }

                    // ============================
                    // Buffer UAV
                    // ============================
                    else if (member.BaseType == EShaderParameterBaseType::RDGBuffer_UAV)
                    {
                        auto uav = *reinterpret_cast<RenderGraphBufferUAV* const*>(memberAddr);
                        auto uavDesc = uav->m_Desc;
                        if (uav)
                        {
                            auto* buf = static_cast<RenderGraphBuffer*>(uav->m_Desc.Buffer);

                            RenderGraphPass::RenderGraphBufferIntent intent;
                            intent.Buffer = buf;

                            intent.RequiredAccess = ERHIResourceAccess::UAV;

                            pass->AddBufferIntent(intent);
                        }
                    }

                    // ============================
                    // Buffer（裸）
                    // ============================
                    else if (member.BaseType == EShaderParameterBaseType::RDGBuffer)
                    {
                        auto buf = *reinterpret_cast<RenderGraphBuffer* const*>(memberAddr);

                        if (buf)
                        {
                            RenderGraphPass::RenderGraphBufferIntent intent;
                            intent.Buffer = buf;

                            intent.RequiredAccess = ERHIResourceAccess::SRV;

                            pass->AddBufferIntent(intent);
                        }
                    }


                }
            }
            else if (member.IsReferenceStruct() || member.IsIncludeStruct())
            {
                SetupPassInternal(pass, member.StructMetadata, memberAddr);
            }
            else if (member.IsRenderTargetSlots()) {
                const RenderTargetBindingSlots* src = reinterpret_cast<const RenderTargetBindingSlots*>(memberAddr);
                auto& rtSlots = *src;
                auto checkColorBinding = [this, pass](const RenderTargetBinding& binding) {
                    if (binding.IsValid()) {
                        RenderGraphPass::RenderGraphTextureIntent intent;
                        intent.Texture = binding.Texture;
                        auto desc = binding.Texture->GetDesc();
                        uint32_t mipSlice = binding.MipIndex;
                        if (desc.MipLevels == 1 && mipSlice == 0) {
                            mipSlice = RHISubresourceRange::KAllSubresources;
                        }
                        uint32_t arraySlice = binding.ArraySlice;
                        if (desc.ArraySize == 1 && arraySlice == 0) {
                            arraySlice = RHISubresourceRange::KAllSubresources;
                        }
                        intent.SubresourceRange = RHISubresourceRange(
                            mipSlice,
                            arraySlice,
                            RHISubresourceRange::KAllSubresources
                        );

                        intent.SubresourceRange = RHISubresourceRange(); // whole

                        intent.RequiredAccess = ERHIResourceAccess::RenderTargetView;

                        pass->AddTextureIntent(intent);
                    }
                    
                    };
                for (int i = 0; i < rtSlots.MaxRenderTargets; ++i) {
                    checkColorBinding(rtSlots.ColorRenderTargets[i]);
                }
                if (rtSlots.DepthStencil.IsValid()) {
                    RenderGraphPass::RenderGraphTextureIntent intent;
                    intent.Texture = rtSlots.DepthStencil.Texture;
                    auto desc = rtSlots.DepthStencil.Texture->GetDesc();
                    uint32_t mipSlice = rtSlots.DepthStencil.MipIndex;
                    if (desc.MipLevels == 1 && mipSlice == 0) {
                        mipSlice = RHISubresourceRange::KAllSubresources;
                    }
                    uint32_t arraySlice = rtSlots.DepthStencil.ArraySlice;
                    if (desc.ArraySize == 1 && arraySlice == 0) {
                        arraySlice = RHISubresourceRange::KAllSubresources;
                    }
                    intent.SubresourceRange = RHISubresourceRange(
                        mipSlice,
                        arraySlice,
                        RHISubresourceRange::KAllSubresources
                    );

                    intent.SubresourceRange = RHISubresourceRange(); // whole

                    intent.RequiredAccess = rtSlots.DepthStencil.bWriteDepth ? ERHIResourceAccess::DSVWrite : ERHIResourceAccess::DSVRead;

                    pass->AddTextureIntent(intent);
                }

            }

        }
    }

    void RenderGraphBuilder::ApplyFinalStates()
    {
        // =========================
    // Texture
    // =========================
        for (auto& [Key, Access] : m_FinalTextureStates)
        {
            auto* tex = Key.Texture;
            if (!tex || !tex->m_bIsExternal)
                continue;

            RHI::RHITexture* rhiTex = tex->GetRHITexture();
            if (!rhiTex)
                continue;

            auto it = m_ExternalTextures.find(rhiTex);
            if (it == m_ExternalTextures.end())
                continue;

            ExternalTextureEntry& entry = it->second;
            if (!entry.Tracker)
                continue;

            // ✅ 写回 subresource
            entry.Tracker->UpdateSubresourceAccess(Key.Range, Access);
            //写回fence
            entry.Tracker->UpdateLastAccessFence(tex->GetTracker().GetLastAccessFence());
        }

        // =========================
        // Buffer
        // =========================
        for (auto& [Key, Access] : m_FinalBufferStates)
        {
            auto* buf = Key.Buffer;
            if (!buf || !buf->m_bIsExternal)
                continue;

            RHI::RHIBuffer* rhiBuf = buf->GetRHIBuffer();
            if (!rhiBuf)
                continue;

            auto it = m_ExternalBuffers.find(rhiBuf);
            if (it == m_ExternalBuffers.end())
                continue;

            ExternalBufferEntry& entry = it->second;
            if (!entry.Tracker)
                continue;

            // =========================
            // 👉 如果你支持 range（推荐）
            // =========================

            // ❗ fallback：整 buffer
            entry.Tracker->UpdateAccess(Access);
            entry.Tracker->UpdateLastAccessFence(buf->GetTracker().GetLastAccessFence());
        }
    }

    void RenderGraphBuilder::ExecutaPasses() 
    {
        Core::TaskPool taskPool(std::thread::hardware_concurrency());

        struct RecordedGroup
        {
            std::unordered_map<RHI::EQueueType, RHI::RHICommandListBase*> CmdLists;
            std::unordered_map<RHI::EQueueType, std::vector<RHI::RHITransitionInfo>> BeginBarriers;
            std::unordered_map<RHI::EQueueType, RHI::RHIContextBase*> CmdContexts;
        };

        std::vector<RecordedGroup> recordedGroups(m_ParallelPasses.size());
        std::vector<Core::TaskHandle> handles;

        RHI::RHIApi* api = GRHIApi;
        if (!api) return;

        // =========================
        // 1️⃣ 并行录制
        // =========================
        int i = 0;
        for (auto iter = m_ParallelPasses.begin(); iter != m_ParallelPasses.end(); ++iter, ++i)
        {
            auto group = *iter;

            handles.push_back(taskPool.AddTask([this, api, group, &recordedGroups, i]()
                {
                    auto& outGroup = recordedGroups[i];
                    auto& cmdLists = outGroup.CmdLists;
                    auto& cmdContexts = outGroup.CmdContexts;

                    auto getCmd = [&](RHI::EQueueType q)
                        {
                            if (cmdLists.count(q)) return cmdLists[q];

                            auto* queue = api->GetQueue(q);
                            auto* ctx = queue->AcquireCommandContext();

                            RHI::RHICommandListBase* cmd = nullptr;

                            if (q == RHI::EQueueType::Graphics)
                                cmd = m_Allocator.Allocate<RHI::RHIGraphicCommandList>(dynamic_cast<RHI::RHIGraphicContex*>(ctx));
                            else if (q == RHI::EQueueType::Compute)
                                cmd = m_Allocator.Allocate <RHI::RHIComputeCommandList>(dynamic_cast<RHI::RHIComputeContex*>(ctx));
                            cmd->SetImmediate(false);
                            cmd->Begin();
                            cmdLists[q] = cmd;
                            cmdContexts[q] = ctx;
                            return cmd;
                        };

                    // 👉 收集 barrier
                    for (auto* pass : group)
                    {
                        auto q = (RHI::EQueueType)pass->GetPassFlag();
                        pass->_beginBarrier.Process();
                        for (auto& t : pass->_beginBarrier.GetTransitions())
                        {
                            outGroup.BeginBarriers[q].push_back(t);
                        }
                    }

                    // 👉 执行 barrier（合并）
                    for (auto& [q, transitions] : outGroup.BeginBarriers)
                    {
                        auto* cmd = getCmd(q);

                        if (!transitions.empty())
                        {
                            void* mem = m_Allocator.AllocateBytes(RHI::G_RHITransition_TotalSize);
                            RHI::RHITransition* transitionObj = new(mem) RHI::RHITransition();
                            RHI::RHITransitionCreateInfo createInfo;
                            createInfo.Flags = RHI::ERHITransitionCreateFlags::None;
                            createInfo.TransitionInfos = transitions;
                            RHI::GRHIApi->RHICreateTransition(transitionObj, createInfo);
                            cmd->BeginTransitions({ transitionObj });
                            cmd->EndTransitions({ transitionObj });
                        }
                    }

                    // 👉 执行 pass
                    for (auto* pass : group)
                    {
                        auto q = (RHI::EQueueType)pass->GetPassFlag();
                        auto* cmd = getCmd(q);

                        pass->Execute(*cmd);
                    }

                    for (auto& [_, cmd] : cmdLists)
                    {
                        cmd->End();
                    }
                }));
        }

        taskPool.WaitAll(handles);

        // =========================
        // 2️⃣ 顺序提交（带必要同步）
        // =========================
        std::unordered_map<RHI::EQueueType, uint64_t> timeline;
        auto passGroupIter = m_ParallelPasses.begin();
        for (int i = 0; i < recordedGroups.size(); ++i, passGroupIter++)
        {
            auto& group = recordedGroups[i];

            for (auto& [q, cmd] : group.CmdLists)
            {
                if (cmd) {
                    cmd->ExecuteAll();
                }
                auto* queue = api->GetQueue(q);
                std::vector<RHI::RHIWaitInfo> waitInfos;
                for (auto& [otherQ, value] : timeline)
                {
                    if (otherQ == q) continue;

                    RHI::RHIWaitInfo waitInfo;
                    waitInfo.QueueType = otherQ;
                    waitInfo.Value = value;
                    waitInfo.WaitStage = RHI::ERHIPipelineStage::AllCommands;

                    waitInfos.push_back(waitInfo);
                }

                RHI::RHIWaitInfo lastTransInfo;
                RHI::RHIFence fence;
                fence.QueueType = queue->GetType();
                fence.Value = queue->ExecuteContext({ group.CmdContexts[q] }, waitInfos);
                timeline[q] = fence.Value;

                // 👉 关键：更新 fence
                for (auto* pass : *passGroupIter)
                {
                    auto it = m_PassLastUseResources.find(pass);
                    if (it == m_PassLastUseResources.end())
                        continue;

                    for (auto* res : it->second)
                    {
                        if (auto* tex = dynamic_cast<RenderGraphTexture*>(res))
                        {
                            if (tex->GetRHITexture())
                            {
                                if (!tex->m_bIsExternal) {
                                    auto transientTarget = m_RHITextureToTransientTarget[tex->GetRHITexture()];
                                    transientTarget->GetTracker().UpdateLastAccessFence(fence);
                                }
                                else {
                                    tex->GetTracker().UpdateLastAccessFence(fence);
                                }
                                
                            }
                        }
                        else if (auto* buf = dynamic_cast<RenderGraphBuffer*>(res))
                        {
                            
                            if (buf->GetRHIBuffer())
                            {
                                if (!buf->m_bIsExternal) {
                                    auto transientBuffer = m_RHIBufferTransientBuffer[buf->GetRHIBuffer()];
                                    transientBuffer->GetTracker().UpdateLastAccessFence(fence);
                                }
                                else {
                                    buf->GetTracker().UpdateLastAccessFence(fence);
                                }
                                
                            }
                        }
                    }
                }
            }

            // 👉 TODO：只根据依赖插入 wait（你后面可以加）
        }
    }
    void RenderGraphBuilder::AnalyzeUpload()
    {
        //------------------------------------------------------------
        // 收集各 Queue 的 Upload
        //------------------------------------------------------------

        std::vector<PendingBufferUpload> graphicUploads;
        std::vector<PendingBufferUpload> computeUploads;

        for (const auto& upload : m_PendingBufferUploads)
        {
            switch (upload.QueueType)
            {
            case EQueueType::Graphics:
                graphicUploads.push_back(upload);
                break;
            case EQueueType::Compute:
                computeUploads.push_back(upload);
                break;

            default:
                break;
            }
        }

        m_PendingBufferUploads.clear();

        //------------------------------------------------------------
        // Graphic Upload Pass
        //------------------------------------------------------------
		struct UploadPassParameters
		{
		};
        if (!graphicUploads.empty())
        {
            AddUploadPass(std::move(graphicUploads), EQueueType::Graphics);

        }
        if (!computeUploads.empty())
        {
            AddUploadPass(std::move(computeUploads), EQueueType::Compute);
        }
    }
    void RenderGraphBuilder::AddUploadPass(std::vector<PendingBufferUpload>&& uploadInfo, EQueueType queueType)
    {
        if (uploadInfo.empty()) {
            return;
        }
        struct UploadPassParameters
        {
        };
		EPassFlag passFlag = EPassFlag::Graphic;
		std::string passName = "GraphicBufferUpload";
        if (queueType == EQueueType::Compute)
		{
			passFlag = EPassFlag::Compute;
			passName = "ComputeBufferUpload";
		}
        RenderGraphParameterStruct parameterStruct(nullptr, nullptr);

        auto uploadLamba = [uploads = std::move(uploadInfo)]
        (RHI::RHICommandListBase& rhiCmdList)
            {
                for (const auto& upload : uploads)
                {
                    if (!upload.Buffer)
                    {
                        continue;
                    }

                    RHI::RHIUpdateBufferRegion region;
                    region.Offset = 0;
                    region.Size = upload.Size;
                    GRHIApi->UpdateBuffer(
                        rhiCmdList,
                        upload.Buffer->GetRHIBuffer(),
                        upload.Data,
                        region);
                }
            };

        // Create the upload pass instance.
        auto pass = new RenderGraphLambdaPass<UploadPassParameters, decltype(uploadLamba)>(
            passName,
            passFlag,
            parameterStruct,
            std::move(uploadLamba)
        );
        for (auto& upload : uploadInfo) {
            pass->AddBufferIntent({ upload.Buffer, ERHIResourceAccess::TransferDest });
        }
        m_Passes.insert(m_Passes.begin(), pass);
    }
} // namespace RenderCore
