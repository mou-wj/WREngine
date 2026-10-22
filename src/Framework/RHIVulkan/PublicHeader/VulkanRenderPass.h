#pragma once

#include <vector>
#include <unordered_map>
#include <cassert>
#include "VulkanDevice.h"  // 你的 VulkanDevice 头文件
#include "VulkanResource.h"

namespace RHIVulkan{

    class VulkanDevice;
    class VulkanCommandContext;
    class VulkanCommandBuffer;

    // ---------------------------------------------------
    // VulkanRenderTargetLayout: 表示 RenderPass 附件布局
    // ---------------------------------------------------
    class VulkanRenderTargetLayout
    {
    public:
        VulkanRenderTargetLayout() = default;
        explicit VulkanRenderTargetLayout(const RHIGraphicAttachmentDesc& desc);

        // ----------------------------
        // 获取信息
        // ----------------------------
        uint32_t GetCompatibleHash() const { return static_cast<uint32_t>(m_RenderPassCompatibleHash); }
        uint32_t GetFullHash() const { return static_cast<uint32_t>(m_RenderPassFullHash); }

        const VkAttachmentReference* GetColorAttachmentRefs() const { return m_ColorRefs; }
        const VkAttachmentReference* GetResolveAttachmentRefs() const { return m_bHasResolveAttachments ? m_ResolveRefs : nullptr; }
        const VkAttachmentReference* GetDepthAttachmentRef() const { return m_bHasDepthStencil ? &m_DepthRef : nullptr; }
        const VkAttachmentDescription* GetAttachmentDescriptions() const { return m_AttachmentDescs; }
        const VkAttachmentDescription* GetColorAttachmentDescription(int index) { return &m_AttachmentDescs[index]; }
		const VkAttachmentDescription* GetDepthAttachmentDescription() { return m_bHasDepthStencil ? &m_AttachmentDescs[m_AttachmentDescCount - 1] : nullptr; }
        uint32_t GetAttachmentDescriptionCount() const { return m_AttachmentDescCount; }
        uint32_t GetNumColorAttachments() const { return m_NumColorAttachments; }
        bool HasDepth() const { return m_bHasDepthStencil; }
        bool HasResolve() const { return m_bHasResolveAttachments; }

    private:
        void ResetAttachments();
        void ParseColorAttachments(const RHIGraphicAttachmentDesc& desc);
        void ParseResolveAttachments(const RHIGraphicAttachmentDesc& desc);
        void ParseDepthAttachment(const RHIGraphicAttachmentDesc& desc);
        void CalculateHashes(const RHIGraphicAttachmentDesc& desc);

    private:
        VkAttachmentReference m_ColorRefs[MAX_RENDER_TARGETS]{};
        VkAttachmentReference m_DepthRef{};
        VkAttachmentReference m_ResolveRefs[MAX_RENDER_TARGETS]{};

        VkAttachmentDescription m_AttachmentDescs[MAX_RENDER_TARGETS * 2 + 1]{};
        uint32_t m_AttachmentDescCount = 0;

        uint8_t m_NumColorAttachments = 0;
        bool m_bHasDepthStencil = false;
        bool m_bHasResolveAttachments = false;

        size_t m_RenderPassCompatibleHash = 0;
        size_t m_RenderPassFullHash = 0;
    };

    // RenderPass 管理
    class VulkanRenderPass
    {
    public:
        VulkanRenderPass(VulkanDevice* device, const VulkanRenderTargetLayout& layout);
        ~VulkanRenderPass();

        VkRenderPass GetHandle() const { return m_RenderPass; }
        const VulkanRenderTargetLayout& GetLayout() const { return m_Layout; }

    private:
        VulkanDevice* m_Device = nullptr;
        VkRenderPass m_RenderPass = VK_NULL_HANDLE;
        VulkanRenderTargetLayout m_Layout;
    };

    // Framebuffer 管理
    class VulkanFramebuffer
    {
    public:
        VulkanFramebuffer(VulkanDevice* device, const RHIBoundRenderTargets& targetInfo, VulkanRenderPass* renderPass);
        ~VulkanFramebuffer();

        VkFramebuffer GetHandle() const { return m_Framebuffer; }

    private:
        friend class VulkanRenderPassManager;
        VulkanDevice* m_Device = nullptr;
        VkFramebuffer m_Framebuffer = VK_NULL_HANDLE;

        // 缓存 RenderTarget 的 VkImage
        std::vector<VkImage> m_ColorImages;
        VkImage m_DepthImage = VK_NULL_HANDLE;
        std::vector<VulkanTextureView> m_AttachmentViews;
        VkExtent2D m_Extent{};
        uint32_t m_NumColorAttachments = 0;
        bool m_bHasDepth;
    };



    // RenderPassManager
    class VulkanRenderPassManager
    {
    public:
        VulkanRenderPassManager(VulkanDevice* device);
        ~VulkanRenderPassManager();

        // 获取或创建 RenderPass
        VulkanRenderPass* GetOrCreateRenderPass(const VulkanRenderTargetLayout& layout);

        // 获取或创建 Framebuffer
        VulkanFramebuffer* GetOrCreateFramebuffer(const RHIBoundRenderTargets& renderTargets, VulkanRenderPass* renderPass);

		void BeginRenderPass(VulkanCommandBuffer* cmdBuffer, const RHIRenderPassInfo& renderPassInfo);

        void EndRenderPass(VulkanCommandBuffer* cmdBuffer);

        // 清理被删除的 Image
        void NotifyDeletedImage(VkImage image);

    private:
        uint32_t CalcFramebufferHash(const RHIBoundRenderTargets& renderTargetsInfo, VulkanRenderPass* renderPass);



        VulkanDevice* m_Device = nullptr;


        std::mutex m_RenderPassMutex;
        std::mutex m_FramebufferMutex;

        std::unordered_map<uint32_t, VulkanRenderPass*> m_RenderPassCache;
        std::unordered_map<uint32_t, VulkanFramebuffer*> m_FramebufferCache;
    };

}