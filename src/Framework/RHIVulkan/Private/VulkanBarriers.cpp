#include "VulkanRHIUtils.h"
#include "VulkanBarriers.h"
#include "VulkanCommandBuffer.h"
#include "VulkanFuncWrapper.h"
#include "RHIDefine.h"
#include "VulkanResource.h"
namespace RHIVulkan {

	static void GetVulkanBarrierMasksByLayout(
		VkImageLayout InLayout,
		VkAccessFlags& OutAccessMask,
		VkPipelineStageFlags& OutStageMask)
	{
		switch (InLayout)
		{
		case VK_IMAGE_LAYOUT_UNDEFINED:
			OutAccessMask = 0;
			OutStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
			break;

		case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
			OutAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			OutStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
			break;

		case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
			OutAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			OutStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
			break;

		case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
			OutAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			OutStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			break;

		case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
			OutAccessMask = VK_ACCESS_SHADER_READ_BIT;
			// ��������ڶ�������ؽ׶ζ�ȡ��ȡ��������ȫ
			OutStageMask = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			break;

		case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
			OutAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			OutStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
			break;

		case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
			OutAccessMask = 0;
			OutStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
			break;

		default:
			OutAccessMask = 0;
			OutStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
			break;
		}
	}
	
	void GetVulkanBarrierInfo(
		bool bIsTexture,
		ERHIResourceAccess InAccess,
		EQueueType InQueueType,
		VkImageLayout& OutLayout,
		VkAccessFlags& OutAccessMask,
		VkPipelineStageFlags& OutStageMask)
	{
		OutLayout = VK_IMAGE_LAYOUT_GENERAL;
		OutAccessMask = 0;
		OutStageMask = 0;

		if (InAccess == ERHIResourceAccess::Unknown)
		{
			OutLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			OutAccessMask = 0;
			OutStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
			return;
		}

		// --- ֻ����֧ ---
		if (EnumHasAnyFlags(InAccess, ERHIResourceAccess::SRV))
		{
			OutLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			OutAccessMask |= VK_ACCESS_SHADER_READ_BIT;
			OutStageMask |= VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		}
		if (EnumHasAnyFlags(InAccess, ERHIResourceAccess::SRV))
		{
			OutLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			OutAccessMask |= VK_ACCESS_SHADER_READ_BIT;
			OutStageMask |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
		}
		if (EnumHasAnyFlags(InAccess, ERHIResourceAccess::TransferSrc))
		{
			OutLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			OutAccessMask |= VK_ACCESS_TRANSFER_READ_BIT;
			OutStageMask |= VK_PIPELINE_STAGE_TRANSFER_BIT;
		}
		if (EnumHasAnyFlags(InAccess, ERHIResourceAccess::DSVRead))
		{
			OutLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
			OutAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
			OutStageMask |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		}

		// --- д���֧ ---
		if (EnumHasAnyFlags(InAccess, ERHIResourceAccess::RenderTargetView))
		{
			OutLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			OutAccessMask |= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			OutStageMask |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		}
		if (EnumHasAnyFlags(InAccess, ERHIResourceAccess::UAV))
		{
			OutLayout = VK_IMAGE_LAYOUT_GENERAL;
			OutAccessMask |= VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
			if (InQueueType == EQueueType::Graphics) {
				OutStageMask |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			}else if (InQueueType == EQueueType::Compute) {
				OutStageMask |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
			}
		}
		if (EnumHasAnyFlags(InAccess, ERHIResourceAccess::DSVWrite))
		{
			OutLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			OutAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			OutStageMask |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		}
		if (EnumHasAnyFlags(InAccess, ERHIResourceAccess::TransferDest))
		{
			OutLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			OutAccessMask |= VK_ACCESS_TRANSFER_WRITE_BIT;
			OutStageMask |= VK_PIPELINE_STAGE_TRANSFER_BIT;
		}

		// --- Buffer ����״̬�Ƶ� ---
		if (EnumHasAnyFlags(InAccess, ERHIResourceAccess::VertexOrIndexBuffer))
		{
			OutAccessMask |= VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT;
			OutStageMask |= VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
		}
		if (EnumHasAnyFlags(InAccess, ERHIResourceAccess::IndirectArgs))
		{
			OutAccessMask |= VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
			OutStageMask |= VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT;
		}
		if (EnumHasAnyFlags(InAccess, ERHIResourceAccess::Present))
		{
			OutLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
			OutStageMask |= VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
		}

		// ��Է�������Դ������
		if (!bIsTexture)
		{
			OutLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		}
	}


	VulkanImageLayout::VulkanImageLayout(VkImageLayout initial,
		uint32_t numMips,
		uint32_t numLayers,
		VkImageAspectFlags)
		: NumMips(numMips)
		, NumLayers(numLayers)
		, MainLayout(initial)
	{

	}


	bool VulkanImageLayout::IsUniform() const
	{
		return SubresourceLayouts.empty();
	}


	uint32_t VulkanImageLayout::Index(uint32_t mip, uint32_t layer) const
	{
		return layer * NumMips + mip;
	}


	VkImageLayout VulkanImageLayout::Get(uint32_t mip, uint32_t layer) const
	{
		if (SubresourceLayouts.empty())
			return MainLayout;


		return SubresourceLayouts[Index(mip, layer)];
	}


	void VulkanImageLayout::Set(VkImageLayout layout, const VkImageSubresourceRange& range)
	{
		uint32_t baseMip = range.baseMipLevel;
		uint32_t mipCount = range.levelCount == VK_REMAINING_MIP_LEVELS
			? NumMips - baseMip
			: range.levelCount;


		uint32_t baseLayer = range.baseArrayLayer;
		uint32_t layerCount = range.layerCount == VK_REMAINING_ARRAY_LAYERS
			? NumLayers - baseLayer
			: range.layerCount;


		if (SubresourceLayouts.empty())
		{
			SubresourceLayouts.resize(NumMips * NumLayers, MainLayout);
		}


		for (uint32_t l = 0; l < layerCount; ++l)
			for (uint32_t m = 0; m < mipCount; ++m)
				SubresourceLayouts[Index(baseMip + m, baseLayer + l)] = layout;


		CollapseIfPossible();
	}


	void VulkanImageLayout::CollapseIfPossible()
	{
		if (SubresourceLayouts.empty())
			return;


		VkImageLayout first = SubresourceLayouts[0];
		for (auto l : SubresourceLayouts)
		{
			if (l != first)
				return;
		}


		MainLayout = first;
		SubresourceLayouts.clear();
	}



	VulkanImageLayoutManager::VulkanImageLayoutManager(VulkanImageLayoutManager* fallback)
		: Fallback(fallback)
	{
	}


	const VulkanImageLayout* VulkanImageLayoutManager::GetFullLayout(VkImage image) const
	{
		auto it = Layouts.find(image);
		if (it != Layouts.end())
			return &it->second;

		return Fallback ? Fallback->GetFullLayout(image) : nullptr;
	}


	const VulkanImageLayout* VulkanImageLayoutManager::GetFullLayout(VulkanTexture* texture) const
	{
		return GetFullLayout(texture->GetImage());
	}


	void VulkanImageLayoutManager::SetFullLayout(VkImage image, const VulkanImageLayout& layout)
	{
		Layouts[image] = layout;
	}


	void VulkanImageLayoutManager::SetFullLayout(VulkanTexture* texture, const VulkanImageLayout& layout)
	{
		SetFullLayout(texture->GetImage(), layout);
	}


	void VulkanImageLayoutManager::SetFullLayout(VulkanTexture* texture, VkImageLayout layout)
	{
		// Create a full layout with the specified layout for all subresources
		const auto& desc = texture->GetDesc();
		VkImageAspectFlags aspect = texture->GetAspectFlags();
		uint32_t numMips = desc.MipLevels;
		uint32_t numLayers = desc.ArraySize;
		VulkanImageLayout fullLayout(layout, numMips, numLayers, aspect);
		SetFullLayout(texture, fullLayout);
	}


	void VulkanImageLayoutManager::SetLayout(VulkanTexture* texture,
		VkImageLayout layout,
		const VkImageSubresourceRange& range)
	{
		auto& imgLayout = Layouts[texture->GetImage()];
		imgLayout.Set(layout, range);
	}


	void VulkanImageLayoutManager::TransferTo(VulkanImageLayoutManager& dst)
	{
		for (auto& it : Layouts)
			dst.Layouts[it.first] = it.second;
	}


	void VulkanImageLayoutManager::Remove(VkImage image)
	{
		Layouts.erase(image);
	}

	void VulkanImageLayoutManager::NotifyDeletedImage(VkImage image)
	{
        Remove(image);
	}
#ifdef DEBUG_INFO
	void VulkanImageLayoutManager::PrintLayoutInfo()
	{
		for (const auto& pair : Layouts)
		{
			VkImage image = pair.first;
			const VulkanImageLayout& layout = pair.second;
			printf("Image: %p, MainLayout: %d, IsUniform: %d\n", image, layout.GetMainLayout(), layout.IsUniform());
			//
			for (uint32_t i = 0; i < layout.GetNumMips(); ++i)
			{
				for (uint32_t j = 0; j < layout.GetNumLayers(); ++j)
				{
					VkImageLayout subLayout = layout.Get(i, j);
					printf("  Mip %d, Layer %d: Layout %d\n", i, j, subLayout);
				}
			}
		}
	}
#endif
	void VulkanPipelineBarrier::Push(const VkImageMemoryBarrier& barrier)
	{
		ImageBarriers.push_back(barrier);
	}

	void VulkanPipelineBarrier::TransitionLayout(
		VkImage image,
		VkImageLayout oldLayout,
		VkImageLayout newLayout,
		const VkImageSubresourceRange& range,
		uint32_t srcQueueFamilyIndex,
		uint32_t dstQueueFamilyIndex)
	{
		VkAccessFlags srcAccess, dstAccess;
		VkPipelineStageFlags srcStage, dstStage;
		GetVulkanBarrierMasksByLayout(oldLayout, srcAccess, srcStage);
		GetVulkanBarrierMasksByLayout(newLayout, dstAccess, dstStage);

		// Only insert barrier if access flags or layout differ
		if (srcAccess != dstAccess || oldLayout != newLayout)
		{
			VkImageMemoryBarrier b{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
			b.image = image;
			b.oldLayout = oldLayout;
			b.newLayout = newLayout;
			b.subresourceRange = range;
			b.srcAccessMask = srcAccess;
			b.dstAccessMask = dstAccess;
			b.srcQueueFamilyIndex = srcQueueFamilyIndex;
			b.dstQueueFamilyIndex = dstQueueFamilyIndex;
			Push(b);
		}
	}

	void VulkanPipelineBarrier::TransitionLayout(
		VkImage image,
		const VulkanImageLayout& oldLayout,
		VkImageLayout newLayout,
		const VkImageSubresourceRange& range,
		uint32_t srcQueueFamilyIndex,
		uint32_t dstQueueFamilyIndex)
	{
		// 如果所有 subresource 都有相同的布局
		if (oldLayout.IsUniform())
		{
			VkImageLayout oldLayoutVk = oldLayout.GetMainLayout();
			
			// 只在布局不同时才插入 barrier
			if (oldLayoutVk != newLayout)
			{
				VkAccessFlags srcAccess, dstAccess;
				VkPipelineStageFlags srcStage, dstStage;
				GetVulkanBarrierMasksByLayout(oldLayoutVk, srcAccess, srcStage);
				GetVulkanBarrierMasksByLayout(newLayout, dstAccess, dstStage);
				
				VkImageMemoryBarrier b{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
				b.image = image;
				b.oldLayout = oldLayoutVk;
				b.newLayout = newLayout;
				b.subresourceRange = range;
				b.srcAccessMask = srcAccess;
				b.dstAccessMask = dstAccess;
				b.srcQueueFamilyIndex = srcQueueFamilyIndex;
				b.dstQueueFamilyIndex = dstQueueFamilyIndex;
				Push(b);
			}
		}
		else
		{
			// 不同的 subresource 有不同的布局，需要差分处理
			uint32_t baseMip = range.baseMipLevel;
			uint32_t mipCount = range.levelCount == VK_REMAINING_MIP_LEVELS
				? oldLayout.GetNumMips() - baseMip
				: range.levelCount;

			uint32_t baseLayer = range.baseArrayLayer;
			uint32_t layerCount = range.layerCount == VK_REMAINING_ARRAY_LAYERS
				? oldLayout.GetNumLayers() - baseLayer
				: range.layerCount;

			// 按当前布局分组，只为需要转换的 subresource 插入 barrier
			std::unordered_map<VkImageLayout, VkImageSubresourceRange> layoutGroups;

			for (uint32_t layer = 0; layer < layerCount; ++layer)
			{
				for (uint32_t mip = 0; mip < mipCount; ++mip)
				{
					VkImageLayout currentLayout = oldLayout.Get(baseMip + mip, baseLayer + layer);
					
					// 只处理需要转换的 subresource
					if (currentLayout != newLayout)
					{
						auto it = layoutGroups.find(currentLayout);
						if (it == layoutGroups.end())
						{
							// 创建新的分组
							VkImageSubresourceRange newRange{};
							newRange.aspectMask = range.aspectMask;
							newRange.baseMipLevel = baseMip + mip;
							newRange.levelCount = 1;
							newRange.baseArrayLayer = baseLayer + layer;
							newRange.layerCount = 1;
							layoutGroups[currentLayout] = newRange;
						}
						else
						{
							// 尝试合并相邻的 subresource
							it->second.levelCount++;
						}
					}
				}
			}

			// 为每个布局组插入 barrier
			for (auto& pair : layoutGroups)
			{
				VkImageLayout currentLayout = pair.first;
				VkImageSubresourceRange subRange = pair.second;
				
				VkAccessFlags srcAccess, dstAccess;
				VkPipelineStageFlags srcStage, dstStage;
				GetVulkanBarrierMasksByLayout(currentLayout, srcAccess, srcStage);
				GetVulkanBarrierMasksByLayout(newLayout, dstAccess, dstStage);

				VkImageMemoryBarrier b{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
				b.image = image;
				b.oldLayout = currentLayout;
				b.newLayout = newLayout;
				b.subresourceRange = subRange;
				b.srcAccessMask = srcAccess;
				b.dstAccessMask = dstAccess;
				b.srcQueueFamilyIndex = srcQueueFamilyIndex;
				b.dstQueueFamilyIndex = dstQueueFamilyIndex;
				Push(b);
			}
		}
	}

	void VulkanPipelineBarrier::TransitionAccess(
		VkImage image,
		ERHIResourceAccess oldAccess,
		ERHIResourceAccess newAccess,
		const VkImageSubresourceRange& range,
		uint32_t srcQueueFamilyIndex,
		uint32_t dstQueueFamilyIndex)
	{
		if (oldAccess != newAccess)
		{
			VkImageLayout oldLayout = DetermineImageLayout(oldAccess);
            VkImageLayout newLayout = DetermineImageLayout(newAccess);
			VkAccessFlags srcAccess, dstAccess;
			VkPipelineStageFlags srcStage, dstStage;
			GetVulkanBarrierMasksByLayout(oldLayout, srcAccess, srcStage);
			GetVulkanBarrierMasksByLayout(newLayout, dstAccess, dstStage);

			VkImageMemoryBarrier b{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
			b.image = image;
			b.oldLayout = oldLayout; // Access 转换不涉及布局变化
			b.newLayout = newLayout;
			b.subresourceRange = range;
			b.srcAccessMask = srcAccess;
			b.dstAccessMask = dstAccess;
			b.srcQueueFamilyIndex = srcQueueFamilyIndex;
			b.dstQueueFamilyIndex = dstQueueFamilyIndex;
			Push(b);
		}
	}

    void VulkanPipelineBarrier::TransitionMemory(ERHIResourceAccess before, ERHIResourceAccess after)
    {
        VkMemoryBarrier barrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
        barrier.srcAccessMask = TransformResourceAccess(before);
        barrier.dstAccessMask = TransformResourceAccess(after);
        MemoryBarriers.push_back(barrier);
    }

	void VulkanPipelineBarrier::Execute(
		VulkanCommandBuffer* cmd)
	{
		if (ImageBarriers.empty() && MemoryBarriers.empty())
			return;
		VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
		VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;

		for (int i = 0; i < ImageBarriers.size(); i++) 
		{
			VkAccessFlags srcAccess;
			VkAccessFlags dstAccess;
			VkPipelineStageFlags srcCurStage;
            VkPipelineStageFlags dstCurStage;
				
			GetVulkanBarrierMasksByLayout(ImageBarriers[i].oldLayout, srcAccess, srcCurStage);
			GetVulkanBarrierMasksByLayout(ImageBarriers[i].newLayout, dstAccess, dstCurStage);
			srcStage |= srcCurStage;
            dstStage |= dstCurStage;

		}
		VKFunc::CmdPipelineBarrier(
			cmd->GetHandle(),
			srcStage,
			dstStage,
			0,
			static_cast<uint32_t>(MemoryBarriers.size()), MemoryBarriers.data(),
			0, nullptr,
			static_cast<uint32_t>(ImageBarriers.size()),
			ImageBarriers.data());
		ImageBarriers.clear();
        MemoryBarriers.clear();
	}

	VkImageSubresourceRange VulkanPipelineBarrier::MakeSubresourceRange(VkImageAspectFlags aspectMask, uint32_t baseMip, uint32_t mipCount, uint32_t baseLayer, uint32_t layerCount)
	{
		VkImageSubresourceRange Range;
		Range.aspectMask = aspectMask;
		Range.baseMipLevel = baseMip;
		Range.levelCount = mipCount;
		Range.baseArrayLayer = baseLayer;
		Range.layerCount = layerCount;
		return Range;
	}

	VkImageLayout DetermineImageLayout(ERHIResourceAccess access, bool bIsDepthStencil)
	{
		// 1. 无效/初始状态
		if (access == ERHIResourceAccess::Unknown)
		{
			return VK_IMAGE_LAYOUT_UNDEFINED;
		}

		// 2. 呈现状态 (Swapchain)
		if (EnumHasAnyFlags(access, ERHIResourceAccess::Present))
		{
			return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		}

		// 3. 渲染附件 (RTV / DSV)
		if (EnumHasAnyFlags(access, ERHIResourceAccess::RenderTargetView))
		{
			return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		}

		if (EnumHasAnyFlags(access, ERHIResourceAccess::DSVWrite))
		{
			return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		}

		if (EnumHasAnyFlags(access, ERHIResourceAccess::DSVRead))
		{
			// 如果是深度图只读（用于 Shader 采样），通常使用这个布局
			return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
		}

		// 4. 可随机读写状态 (UAV)
		if (EnumHasAnyFlags(access, ERHIResourceAccess::UAV))
		{
			// UAV 在 Vulkan 中最稳健的选择是 GENERAL
			return VK_IMAGE_LAYOUT_GENERAL;
		}

		// 5. 只读采样状态 (SRV)
		if (EnumHasAnyFlags(access, ERHIResourceAccess::SRV))
		{
			if (bIsDepthStencil)
			{
				// 深度图作为 SRV 采样时，必须使用专用的只读布局
				return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
			}
			return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		}

		// 6. 拷贝/传输状态
		if (EnumHasAnyFlags(access, ERHIResourceAccess::TransferDest))
		{
			return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		}

		if (EnumHasAnyFlags(access, ERHIResourceAccess::TransferSrc))
		{
			return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		}

		if (EnumHasAnyFlags(access, ERHIResourceAccess::ResolveDst))
		{
			// Resolve 目标在 Vulkan 中通常也是 Color Attachment 布局
			return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		}

		// 7. 其他特殊状态
		if (EnumHasAnyFlags(access, ERHIResourceAccess::ShadingRateSource))
		{
			return VK_IMAGE_LAYOUT_FRAGMENT_SHADING_RATE_ATTACHMENT_OPTIMAL_KHR;
		}

		if (EnumHasAnyFlags(access, ERHIResourceAccess::CPURead))
		{
			return VK_IMAGE_LAYOUT_GENERAL;
		}

		// 兜底返回 GENERAL 虽安全但性能低，建议根据需求调整
		return VK_IMAGE_LAYOUT_GENERAL;
	}


}