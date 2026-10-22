#pragma once
#include <unordered_map>
#include <memory>
#include "RHIDefine.h"
#include "RHIApi.h"

namespace std {
    template<>
    struct hash<RHI::RHISubresourceRange> {
        size_t operator()(const RHI::RHISubresourceRange& range) const noexcept {
            size_t h = 0;
            h ^= std::hash<uint32_t>{}(range.MipIndex) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<uint32_t>{}(range.ArraySlice) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<uint32_t>{}(range.PlaneSlice) + 0x9e3779b9 + (h << 6) + (h >> 2);
            return h;
        }
    };
}
template<typename TState>
class TextureSubresourceStateContainer
{
public:
    void SetWhole(TState state)
    {
        m_bUseSubresource = false;
        m_WholeState = state;
        m_SubStates.clear();
    }

    void SetSubresource(const RHI::RHISubresourceRange& range, const TState& state)
    {
        if (range.IsWholeResource())
        {
            SetWhole(state);
            return;
        }

        m_bUseSubresource = true;

        SubresourceKey key{
            range.MipIndex,
            range.ArraySlice,
            range.PlaneSlice
        };

        m_SubStates[key] = state;
    }

    TState Get(const RHI::RHISubresourceRange& range) const
    {
        if (!m_bUseSubresource || range.IsWholeResource())
        {
            return m_WholeState;
        }

        SubresourceKey key{
            range.MipIndex,
            range.ArraySlice,
            range.PlaneSlice
        };

        auto it = m_SubStates.find(key);
        return (it != m_SubStates.end()) ? it->second : m_WholeState;
    }

    bool IsWholeStateOnly() const
    {
        return !m_bUseSubresource;
    }

private:
    struct SubresourceKey
    {
        uint32_t Mip, Array, Plane;

        bool operator==(const SubresourceKey&) const = default;
    };

    struct Hasher
    {
        size_t operator()(const SubresourceKey& k) const
        {
            return ((k.Mip * 73856093) ^
                (k.Array * 19349663) ^
                (k.Plane * 83492791));
        }
    };

    bool m_bUseSubresource = false;

    TState m_WholeState{};
    std::unordered_map<SubresourceKey, TState, Hasher> m_SubStates;
};

namespace RenderCore {


// 资源追踪基类
class RenderResourceTrackerBase {
public:
    virtual ~RenderResourceTrackerBase() = default;
    // 追踪和更新最后的访问Fence
    void UpdateLastAccessFence(const RHI::RHIFence& fence) { _lastAccessFence = fence; }
    const RHI::RHIFence& GetLastAccessFence() const { return _lastAccessFence; }
protected:
    RHI::RHIFence _lastAccessFence;
};

// 纹理资源追踪
class RenderTextureTracker : public RenderResourceTrackerBase {
public:
    void Initialize(uint32_t numLayer, uint32_t numMip, uint32_t numPlane) {
		m_NumLayer = numLayer;
		m_NumMip = numMip;
		m_NumPlane = numPlane;
		m_SubresourceAccess.SetWhole(RHI::ERHIResourceAccess::Unknown);
    }
    struct NeededTransientSubInfo {
        RHI::RHISubresourceRange Range;
        RHI::ERHIResourceAccess CurrenAccess;
    };
    std::vector<NeededTransientSubInfo> GetNeededTransientSubInfo(RHI::ERHIResourceAccess dstAccess,uint32_t firstLayer, uint32_t numLayer, uint32_t firstMip, uint32_t numMip, uint32_t firstPlane = 0, uint32_t numPlane = 1) {
        std::vector<NeededTransientSubInfo> results;

        const bool isWholeResource = firstLayer == 0
            && numLayer == m_NumLayer
            && firstMip == 0
            && numMip == m_NumMip
            && firstPlane == 0
            && numPlane == m_NumPlane;

        if (isWholeResource && m_SubresourceAccess.IsWholeStateOnly()) {
            const RHI::ERHIResourceAccess currentAccess = m_SubresourceAccess.Get(RHI::RHISubresourceRange{});
            if (currentAccess != dstAccess) {
                results.push_back({ RHI::RHISubresourceRange{}, currentAccess });
            }
            return results;
        }

        for (uint32_t plane = firstPlane; plane < firstPlane + numPlane; ++plane) {
            for (uint32_t layer = firstLayer; layer < firstLayer + numLayer; ++layer) {
                for (uint32_t mip = firstMip; mip < firstMip + numMip; ++mip) {
                    RHI::RHISubresourceRange range(mip, layer, plane);
                    const RHI::ERHIResourceAccess currentAccess = m_SubresourceAccess.Get(range);
                    if (currentAccess != dstAccess) {
                        results.push_back({ range, currentAccess });
                    }
                }
            }
        }

        return results;
    }

    // 更新子资源访问控制
    void UpdateSubresourceAccess(const RHI::RHISubresourceRange& range, RHI::ERHIResourceAccess access) {
        if (range.ArraySlice == 0 && range.MipIndex == 0 && range.PlaneSlice == 0
			&& m_NumLayer == 1 && m_NumMip == 1 && m_NumPlane == 1
            ) {
            m_SubresourceAccess.SetWhole(access);
            return;
        }
        m_SubresourceAccess.SetSubresource(range, access);
    }
    // 获取子资源访问控制
    RHI::ERHIResourceAccess GetSubresourceAccess(const RHI::RHISubresourceRange& range) const {
		return m_SubresourceAccess.Get(range);
    }
private:
    TextureSubresourceStateContainer<RHI::ERHIResourceAccess> m_SubresourceAccess;
    uint32_t m_NumLayer = 1;
    uint32_t m_NumMip = 1;
	uint32_t m_NumPlane = 1;
};

// 缓冲区资源追踪
class RenderBufferTracker : public RenderResourceTrackerBase {
public:
    void UpdateAccess(RHI::ERHIResourceAccess access) { m_BufferAccess = access; }
    RHI::ERHIResourceAccess GetLastAccess() const { return m_BufferAccess; }
private:
    RHI::ERHIResourceAccess m_BufferAccess = RHI::ERHIResourceAccess::Unknown;
};

inline void TransitionResource(
    RHI::RHIApi* api,
    RHI::RHICommandListBase& cmdList,
    RHI::RHIViewableResource* resource,
    RHI::ERHIResourceAccess currentAccess,
    RHI::ERHIResourceAccess targetAccess,
    RHI::EQueueType currentQueueType,
    RHI::EQueueType targetQueueType)
{
    if (!api || !resource)
    {
        return;
    }

    if (currentAccess == targetAccess && currentQueueType == targetQueueType)
    {
        return;
    }

    std::vector<RHI::RHITransitionInfo> infos;
    if (auto* texture = dynamic_cast<RHI::RHITexture*>(resource))
    {
        infos.emplace_back(texture, currentAccess, targetAccess);
    }
    else if (auto* buffer = dynamic_cast<RHI::RHIBuffer*>(resource))
    {
        infos.emplace_back(buffer, currentAccess, targetAccess);
    }
    else
    {
        return;
    }

    char* transitionMem = new char[RHI::G_RHITransition_TotalSize];
    auto* transition = new(transitionMem) RHI::RHITransition();
    api->RHICreateTransition(transition, RHI::RHITransitionCreateInfo(RHI::ERHITransitionCreateFlags::None, std::move(infos)));

    cmdList.BeginTransitions({ transition });
    cmdList.EndTransitions({ transition });

    api->RHIReleaseTransition(transition);
    delete transitionMem;
}


} // namespace RenderCore