#pragma once
#include "vulkan/vulkan.h"
#include "RHIDefine.h"
#include <unordered_map>
namespace RHIVulkan {
	VkImageLayout DetermineImageLayout(RHI::ERHIResourceAccess access,bool bIsDepthStencil = false);

	class VulkanCommandBuffer;
	class VulkanTexture;
	class VulkanImageLayout
	{
	public:
		VulkanImageLayout() = default;
		VulkanImageLayout(VkImageLayout initial,
			uint32_t numMips,
			uint32_t numLayers,
			VkImageAspectFlags aspectMask);


		bool IsUniform() const;
		VkImageLayout Get(uint32_t mip, uint32_t layer) const;


		void Set(VkImageLayout layout, const VkImageSubresourceRange& range);
		void CollapseIfPossible();


		uint32_t GetNumMips() const { return m_NumMips; }
		uint32_t GetNumLayers() const { return m_NumLayers; }
		VkImageLayout GetMainLayout() const { return m_MainLayout; }


	private:
		uint32_t m_NumMips = 0;
		uint32_t m_NumLayers = 0;


		VkImageLayout m_MainLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		std::vector<VkImageLayout> m_SubresourceLayouts;


		uint32_t Index(uint32_t mip, uint32_t layer) const;
	};



	class VulkanImageLayoutManager
	{
	public:
		explicit VulkanImageLayoutManager(VulkanImageLayoutManager* fallback = nullptr);
		void Clear() { m_Layouts.clear(); }

		const VulkanImageLayout* GetFullLayout(VkImage image) const;
		const VulkanImageLayout* GetFullLayout(VulkanTexture* texture) const;

		void SetFullLayout(VkImage image, const VulkanImageLayout& layout);
		void SetFullLayout(VulkanTexture* texture, const VulkanImageLayout& layout);
		void SetFullLayout(VulkanTexture* texture, VkImageLayout layout);
		void SetLayout(VulkanTexture* texture,
			VkImageLayout layout,
			const VkImageSubresourceRange& range);


		void TransferTo(VulkanImageLayoutManager& dst);
		void Remove(VkImage image);

		void NotifyDeletedImage(VkImage image);

#ifdef DEBUG_INFO
		void PrintLayoutInfo();
#endif
	private:
		std::unordered_map<VkImage, VulkanImageLayout> m_Layouts;
		VulkanImageLayoutManager* m_Fallback = nullptr;
	};



	class VulkanPipelineBarrier
	{
	public:
		void TransitionLayout(
			VkImage image,
			VkImageLayout oldLayout,
			VkImageLayout newLayout,
			const VkImageSubresourceRange& range,
			uint32_t srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			uint32_t dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED);


		void TransitionLayout(
			VkImage image,
			const VulkanImageLayout& oldLayout,
			VkImageLayout newLayout,
			const VkImageSubresourceRange& range,
			uint32_t srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			uint32_t dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED);

		void TransitionAccess(
			VkImage image,
			RHI::ERHIResourceAccess oldAccess,
			RHI::ERHIResourceAccess newAccess,
			const VkImageSubresourceRange& range,
			uint32_t srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			uint32_t dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED);


		void TransitionMemory(RHI::ERHIResourceAccess before, RHI::ERHIResourceAccess after);
		void Execute(VulkanCommandBuffer* cmd);

		static VkImageSubresourceRange MakeSubresourceRange(VkImageAspectFlags aspectMask, uint32_t baseMip, uint32_t mipCount, uint32_t baseLayer, uint32_t layerCount);

	private:
		void Push(const VkImageMemoryBarrier& barrier);
		std::vector<VkImageMemoryBarrier> m_ImageBarriers;
		std::vector<VkMemoryBarrier> m_MemoryBarriers;
		std::vector<VkBufferMemoryBarrier> m_BufferBarriers;
	};

}