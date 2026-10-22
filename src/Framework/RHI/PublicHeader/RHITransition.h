#pragma once
#include "RHIDefine.h"
#include "RHIResource.h"  // Assuming this includes necessary types like FRHIResource, etc.
#include <atomic>

namespace RHI {


struct RHI_API RHITransitionInfo : public RHISubresourceRange
{
	union
	{
		class RHIResource* Resource = nullptr;
		class RHIViewableResource* ViewableResource;
		class RHITexture* Texture;
		class RHIBuffer* Buffer;
		class RHIUnorderedAccessView* UAV;
		class RHIRayTracingAccelerationStructure* BVH;
	};
	enum class EType {
		Unknown,
		Texture,
		Buffer,
		UAV,
		BVH
	};
	EType Type = EType::Unknown;

	ERHIResourceAccess AccessBefore = ERHIResourceAccess::Unknown;
	ERHIResourceAccess AccessAfter = ERHIResourceAccess::Unknown;
	EQueueType QueueTypeBefore = EQueueType::Graphics;
	EQueueType QueueTypeAfter = EQueueType::Graphics;
	EResourceTransitionFlags Flags = EResourceTransitionFlags::None;

	RHITransitionInfo() = default;

	RHITransitionInfo(
		class RHITexture* inTexture,
		ERHIResourceAccess inPreviousState,
		ERHIResourceAccess inNewState,
		EQueueType inQueueTypeBefore = EQueueType::Graphics,
		EQueueType inQueueTypeAfter = EQueueType::Graphics,
		EResourceTransitionFlags inFlags = EResourceTransitionFlags::None,
		uint32_t inMipIndex = KAllSubresources,
		uint32_t inArraySlice = KAllSubresources,
		uint32_t inPlaneSlice = KAllSubresources)
		: RHISubresourceRange(inMipIndex, inArraySlice, inPlaneSlice)
		, Texture(inTexture)
		, Type(EType::Texture)
		, AccessBefore(inPreviousState)
		, AccessAfter(inNewState)
		, QueueTypeBefore(inQueueTypeBefore)
        , QueueTypeAfter(inQueueTypeAfter)
		, Flags(inFlags)
	{}

	RHITransitionInfo(class RHIUnorderedAccessView* inUav, ERHIResourceAccess inPreviousState, ERHIResourceAccess inNewState, EQueueType inQueueTypeBefore = EQueueType::Graphics,EQueueType inQueueTypeAfter = EQueueType::Graphics, EResourceTransitionFlags inFlags = EResourceTransitionFlags::None)
		: UAV(inUav)
		, Type(EType::UAV)
		, AccessBefore(inPreviousState)
		, AccessAfter(inNewState)
		, QueueTypeBefore(inQueueTypeBefore)
		, QueueTypeAfter(inQueueTypeAfter)
		, Flags(inFlags)
	{}

	RHITransitionInfo(class RHIBuffer* inRhiBuffer, ERHIResourceAccess inPreviousState, ERHIResourceAccess inNewState, EQueueType inQueueTypeBefore = EQueueType::Graphics, EQueueType inQueueTypeAfter = EQueueType::Graphics, EResourceTransitionFlags inFlags = EResourceTransitionFlags::None)
		: Buffer(inRhiBuffer)
		, Type(EType::Buffer)
		, AccessBefore(inPreviousState)
		, AccessAfter(inNewState)
		, QueueTypeBefore(inQueueTypeBefore)
		, QueueTypeAfter(inQueueTypeAfter)
		, Flags(inFlags)
	{}

	RHITransitionInfo(class RHIRayTracingAccelerationStructure* inBvh, ERHIResourceAccess inPreviousState, ERHIResourceAccess inNewState, EQueueType inQueueTypeBefore = EQueueType::Graphics, EQueueType inQueueTypeAfter = EQueueType::Graphics, EResourceTransitionFlags inFlags = EResourceTransitionFlags::None)
		: BVH(inBvh)
		, Type(EType::BVH)
		, AccessBefore(inPreviousState)
		, AccessAfter(inNewState)
		, QueueTypeBefore(inQueueTypeBefore)
		, QueueTypeAfter(inQueueTypeAfter)
		, Flags(inFlags)
	{}


	inline bool operator == (RHITransitionInfo const& rhs) const
	{
		return Resource == rhs.Resource
			&& Type == rhs.Type
			&& AccessBefore == rhs.AccessBefore
			&& AccessAfter == rhs.AccessAfter
			&& Flags == rhs.Flags
			&& RHISubresourceRange::operator==(rhs);
	}

	inline bool operator != (RHITransitionInfo const& rhs) const
	{
		return !(*this == rhs);
	}

};


struct RHI_API RHITransitionCreateInfo
{
	RHITransitionCreateInfo(
		ERHITransitionCreateFlags inFlags = ERHITransitionCreateFlags::None,
		std::vector<RHITransitionInfo> inTransitionInfos = {})
		: Flags(inFlags)
		, TransitionInfos(std::move(inTransitionInfos))
	{}

	ERHITransitionCreateFlags Flags = ERHITransitionCreateFlags::None;
	std::vector<RHITransitionInfo> TransitionInfos;
};

// 由 RHI 初始化时设置的转换对象总大小与后端数据偏移。
extern RHI_API uint32_t G_RHITransition_TotalSize;
extern RHI_API uint32_t G_RHITransition_PrivateDataOffset;

struct RHI_API RHITransition
{
public:
	// 禁用拷贝构造与赋值。
	RHITransition(const RHITransition&) = delete;
	RHITransition& operator=(const RHITransition&) = delete;

	// 初始化单次 Begin/End 触发标记。
	RHITransition()
		: m_bPendingBegin(true)
		, m_bPendingEnd(true)
	{
	}

	// 获取后端私有转换数据。
	template <typename T>
	inline T* GetPrivateData() const
	{
		// 根据初始化时设置的偏移定位后端数据。
		return reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(this) + G_RHITransition_PrivateDataOffset);
	}

	// 首次标记返回 true，调用方据此提交该阶段的屏障。
	inline bool MarkBegin() const
	{
		return m_bPendingBegin.exchange(false, std::memory_order_acq_rel);
	}

	inline bool MarkEnd() const
	{
		return m_bPendingEnd.exchange(false, std::memory_order_acq_rel);
	}

private:
	// 单次 Begin/End 触发标记
	mutable std::atomic<bool> m_bPendingBegin;
	mutable std::atomic<bool> m_bPendingEnd;
};

} // namespace RHI


