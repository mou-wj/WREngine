#include "VulkanRenderPass.h"
#include "VulkanResource.h"
#include "VulkanRHIUtils.h"
#include "VulkanCommandBuffer.h"
namespace RHIVulkan {
    // Map render target actions to Vulkan load and store operations.
    inline VkAttachmentLoadOp RTActionToLoadOp(ERenderTargetActions action)
    {
        uint8_t load = uint8_t(action) >> uint8_t(ERenderTargetActions::LoadOpShift);
        switch (load)
        {
        case uint8_t(ERenderTargetLoadOp::DontCare): return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        case uint8_t(ERenderTargetLoadOp::Load):     return VK_ATTACHMENT_LOAD_OP_LOAD;
        case uint8_t(ERenderTargetLoadOp::Clear):    return VK_ATTACHMENT_LOAD_OP_CLEAR;
        default: return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        }
    }

    inline VkAttachmentStoreOp RTActionToStoreOp(ERenderTargetActions action)
    {
        uint8_t store = uint8_t(action) & uint8_t(ERenderTargetActions::StoreOpMask);
        switch (store)
        {
        case uint8_t(ERenderTargetStoreOp::DontCare): return VK_ATTACHMENT_STORE_OP_DONT_CARE;
        case uint8_t(ERenderTargetStoreOp::Store):    return VK_ATTACHMENT_STORE_OP_STORE;
        case uint8_t(ERenderTargetStoreOp::Resolve):  return VK_ATTACHMENT_STORE_OP_STORE; // Resolve targets use a separate attachment.
        default: return VK_ATTACHMENT_STORE_OP_DONT_CARE;
        }
    }

    // ----------------------------
// Reset
// ----------------------------
    void VulkanRenderTargetLayout::ResetAttachments()
    {
        m_AttachmentDescCount = 0;
        m_NumColorAttachments = 0;
        m_bHasDepthStencil = false;
        m_bHasResolveAttachments = false;

        std::fill(std::begin(m_ColorRefs), std::end(m_ColorRefs), VkAttachmentReference{ VK_ATTACHMENT_UNUSED, VK_IMAGE_LAYOUT_UNDEFINED });
        std::fill(std::begin(m_ResolveRefs), std::end(m_ResolveRefs), VkAttachmentReference{ VK_ATTACHMENT_UNUSED, VK_IMAGE_LAYOUT_UNDEFINED });
        m_DepthRef = { VK_ATTACHMENT_UNUSED, VK_IMAGE_LAYOUT_UNDEFINED };
    }

    // ----------------------------
    // Constructor
    // ----------------------------
    VulkanRenderTargetLayout::VulkanRenderTargetLayout(const RHIGraphicAttachmentDesc& desc)
    {
        ResetAttachments();
        m_NumColorAttachments = desc.ColorAttachmentCount;

        ParseColorAttachments(desc);
        ParseResolveAttachments(desc);
        ParseDepthAttachment(desc);
        CalculateHashes(desc);
    }

    // ----------------------------
    // Color Attachments
    // ----------------------------
    void VulkanRenderTargetLayout::ParseColorAttachments(const RHIGraphicAttachmentDesc& desc)
    {
        for (uint32_t i = 0; i < desc.ColorAttachmentCount; ++i)
        {
            const auto& src = desc.ColorAttachments[i];
            VkAttachmentDescription& dst = m_AttachmentDescs[m_AttachmentDescCount];

            dst.flags = 0;
            dst.format =  TransformFormatFrom(src.Format);
            dst.samples = TransformSampleCountFrom(src.SampleCount);

            dst.loadOp = RTActionToLoadOp(src.Actions);
            dst.storeOp = RTActionToStoreOp(src.Actions);

            dst.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            dst.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

            dst.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            dst.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

            m_ColorRefs[i].attachment = m_AttachmentDescCount;
            m_ColorRefs[i].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

            m_AttachmentDescCount++;
        }
    }

    // ----------------------------
    // Resolve Attachments
    // ----------------------------
    void VulkanRenderTargetLayout::ParseResolveAttachments(const RHIGraphicAttachmentDesc& desc)
    {
        for (uint32_t i = 0; i < desc.ColorAttachmentCount; ++i)
        {
            const auto& src = desc.ColorAttachments[i];
            if (src.bEnableResolve)
            {
                VkAttachmentDescription& dst = m_AttachmentDescs[m_AttachmentDescCount];

                dst.flags = 0;
                dst.format = static_cast<VkFormat>(src.Format); // Format of the resolve target.
                dst.samples = VK_SAMPLE_COUNT_1_BIT;
                dst.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                dst.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                dst.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                dst.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                dst.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                dst.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

                m_ResolveRefs[i].attachment = m_AttachmentDescCount;
                m_ResolveRefs[i].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

                m_bHasResolveAttachments = true;
                m_AttachmentDescCount++;
            }
            else
            {
                m_ResolveRefs[i].attachment = VK_ATTACHMENT_UNUSED;
            }
        }
    }

    // ----------------------------
    // Depth Attachment
    // ----------------------------
    void VulkanRenderTargetLayout::ParseDepthAttachment(const RHIGraphicAttachmentDesc& desc)
    {
        if (desc.bEnableDepth || desc.bEnableStencil)
        {
            VkAttachmentDescription& dst = m_AttachmentDescs[m_AttachmentDescCount];

            dst.flags = 0;
            dst.format = TransformFormatFrom(desc.DepthStencilFormat);
            dst.samples = TransformSampleCountFrom(desc.NumSamples);

            dst.loadOp = RTActionToLoadOp(desc.DepthActions);
            dst.storeOp = RTActionToStoreOp(desc.DepthActions);

            dst.stencilLoadOp = RTActionToLoadOp(desc.StencilActions);
            dst.stencilStoreOp = RTActionToStoreOp(desc.StencilActions);

            dst.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            dst.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

            m_DepthRef.attachment = m_AttachmentDescCount;
            m_DepthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

            m_bHasDepthStencil = true;
            m_AttachmentDescCount++;
        }
    }

    // ----------------------------
    // Calculate render pass hashes.
    // ----------------------------
    void VulkanRenderTargetLayout::CalculateHashes(const RHIGraphicAttachmentDesc& desc)
    {
        // Compatibility hash: attachment count, formats and sample counts.
        size_t hash = 14695981039346656037ull;
        hash ^= static_cast<uint64_t>(m_NumColorAttachments); hash *= 1099511628211ull;
        for (uint32_t i = 0; i < m_NumColorAttachments; ++i)
            hash ^= RHIColorAttachmentDesc::CalculateHash(desc.ColorAttachments[i]), hash *= 1099511628211ull;

        hash ^= static_cast<uint64_t>(m_bHasDepthStencil); hash *= 1099511628211ull;
        hash ^= static_cast<uint64_t>(desc.DepthStencilFormat); hash *= 1099511628211ull;
        hash ^= static_cast<uint64_t>(desc.NumSamples); hash *= 1099511628211ull;

        m_RenderPassCompatibleHash = hash;

        // Full hash includes load, store and resolve actions.
        hash = 14695981039346656037ull;
        hash ^= static_cast<uint64_t>(m_NumColorAttachments); hash *= 1099511628211ull;
        for (uint32_t i = 0; i < m_NumColorAttachments; ++i)
            hash ^= RHIColorAttachmentDesc::CalculateHash(desc.ColorAttachments[i]), hash *= 1099511628211ull;

        hash ^= static_cast<uint64_t>(m_bHasDepthStencil); hash *= 1099511628211ull;
        hash ^= static_cast<uint64_t>(desc.DepthActions); hash *= 1099511628211ull;
        hash ^= static_cast<uint64_t>(desc.StencilActions); hash *= 1099511628211ull;
        hash ^= static_cast<uint64_t>(desc.DepthStencilFormat); hash *= 1099511628211ull;
        hash ^= static_cast<uint64_t>(desc.NumSamples); hash *= 1099511628211ull;

        m_RenderPassFullHash = hash;
    }

    // ---------------------------------------------------
    // VulkanRenderPass
    // ---------------------------------------------------
    VulkanRenderPass::VulkanRenderPass(VulkanDevice* device, const VulkanRenderTargetLayout& layout)
        : m_Device(device), m_Layout(layout)
    {
        assert(m_Device);

        auto attachments = layout.GetAttachmentDescriptions();
        auto attachmentCount = layout.GetAttachmentDescriptionCount();


        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = layout.GetNumColorAttachments();
        subpass.pColorAttachments = layout.GetColorAttachmentRefs();
        if (layout.HasDepth())
            subpass.pDepthStencilAttachment = layout.GetDepthAttachmentRef();

        VkRenderPassCreateInfo rpInfo{};
        rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpInfo.attachmentCount = static_cast<uint32_t>(attachmentCount);
        rpInfo.pAttachments = attachments;
        rpInfo.subpassCount = 1;
        rpInfo.pSubpasses = &subpass;

        bool result = VKFunc::CreateRenderPass(m_Device->GetHandle(), &rpInfo, &m_RenderPass);
        assert(result == true);
    }

    VulkanRenderPass::~VulkanRenderPass()
    {
        if (m_RenderPass != VK_NULL_HANDLE && m_Device)
            m_Device->EnqueueRenderPassForDeletion(m_RenderPass);
    }

    // ---------------------------------------------------
    // VulkanFramebuffer
    // ---------------------------------------------------
    VulkanFramebuffer::VulkanFramebuffer(VulkanDevice* device, const RHIBoundRenderTargets& targetInfo, VulkanRenderPass* renderPass)
        : m_Device(device)
    {

        m_NumColorAttachments = targetInfo.NumColorAttachments;
        auto dimens = targetInfo.Dimensions;
        m_Extent.width = dimens.X;
        m_Extent.height = dimens.Y;

        std::vector<VkImageView> vkViews;
		VkImageViewCreateInfo viewInfo{};
        vkViews.resize(m_NumColorAttachments + (targetInfo.HasDepth() ? 1 : 0));
        m_AttachmentViews.resize(m_NumColorAttachments + (targetInfo.HasDepth() ? 1 : 0));
        // Create views for color attachments.
        for (uint32_t i = 0; i < m_NumColorAttachments; ++i)
        {
            VulkanTexture* tex = static_cast<VulkanTexture*>(targetInfo.ColorAttachments[i].Texture);
            assert(tex);

            m_ColorImages.push_back(tex->GetImage());
            // Create an image view for this attachment.
            VulkanTextureView& view = m_AttachmentViews[i];
            view.Create(
                m_Device,                                 // fvulkan_device&
                tex->GetImage(),                          // VkImage
                VK_IMAGE_VIEW_TYPE_2D,                    // Use a 2D image view.
                VK_IMAGE_ASPECT_COLOR_BIT,   // aspect mask
                TransformFormatFrom(tex->GetDesc().Format),    // VkFormat
                targetInfo.ColorAttachments[i].MipIndex,                                        // first mip
                1,                       // num mips
                targetInfo.ColorAttachments[i].ArraySlice,                                        // base array layer
                1,                       // num array layers
                true                                      // use identity swizzle
            );

            vkViews[i] = view.View;
        }

        // DepthStencil
        if (targetInfo.HasDepth())
        {
            VulkanTexture* depthTex = static_cast<VulkanTexture*>(targetInfo.DepthStencil.Texture);
            assert(depthTex);

            m_DepthImage = depthTex->GetImage();
            VulkanTextureView& view = m_AttachmentViews[m_NumColorAttachments];
            view.Create(
                m_Device,
                depthTex->GetImage(),
                VK_IMAGE_VIEW_TYPE_2D,
                VK_IMAGE_ASPECT_DEPTH_BIT,
                depthTex->GetFormat(),
                targetInfo.DepthStencil.MipIndex,
                1,
                targetInfo.DepthStencil.ArraySlice,
                1,
                true
            );

            vkViews[m_NumColorAttachments] = view.View;
        }

        // Create the Vulkan framebuffer.
        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = renderPass->GetHandle();
        fbInfo.attachmentCount = static_cast<uint32_t>(vkViews.size());
        fbInfo.pAttachments = vkViews.data();
        fbInfo.width = m_Extent.width;
        fbInfo.height = m_Extent.height;
        fbInfo.layers = 1;

        bool result = VKFunc::CreateFramebuffer(m_Device->GetHandle(), &fbInfo, &m_Framebuffer);
        assert(result);
    }

    VulkanFramebuffer::~VulkanFramebuffer()
    {
        if (m_Framebuffer != VK_NULL_HANDLE && m_Device)
            m_Device->EnqueueFramebufferForDeletion(m_Framebuffer);
        for (auto& view : m_AttachmentViews)
        {
            view.Destroy(m_Device);
        }


    }

    

    // ---------------------------------------------------
    // VulkanRenderPassManager
    // ---------------------------------------------------
    VulkanRenderPassManager::VulkanRenderPassManager(VulkanDevice* device)
        : m_Device(device)
    {
    }

    VulkanRenderPassManager::~VulkanRenderPassManager()
    {
        for (auto& kv : m_RenderPassCache)
            delete kv.second;
        for (auto& kv : m_FramebufferCache)
            delete kv.second;
    }

    VulkanRenderPass* VulkanRenderPassManager::GetOrCreateRenderPass(const VulkanRenderTargetLayout& layout)
    {
        std::lock_guard<std::mutex> lg(m_RenderPassMutex);
        auto it = m_RenderPassCache.find(layout.GetFullHash());
        if (it != m_RenderPassCache.end())
            return it->second;

        VulkanRenderPass* rp = new VulkanRenderPass(m_Device, layout);
        m_RenderPassCache[layout.GetFullHash()] = rp;
        return rp;
    }

    VulkanFramebuffer* VulkanRenderPassManager::GetOrCreateFramebuffer(const RHIBoundRenderTargets& passInfo, VulkanRenderPass* renderPass)
    {
        std::lock_guard<std::mutex> lg(m_FramebufferMutex);
        auto hash = CalcFramebufferHash(passInfo, renderPass);
        auto it = m_FramebufferCache.find(hash);
        if (it != m_FramebufferCache.end())
            return it->second;

        VulkanFramebuffer* fb = new VulkanFramebuffer(m_Device, passInfo,renderPass);
        m_FramebufferCache[hash] = fb;
        return fb;
    }
    std::vector<VkClearValue> GenerateClearValues(const RHIBoundRenderTargets& renderTargets)
    {
        std::vector<VkClearValue> clearValues;

        // Collect clear values for color attachments.
        for (uint8_t i = 0; i < renderTargets.NumColorAttachments; ++i)
        {
            const auto& colorAtt = renderTargets.ColorAttachments[i];
            VkClearValue clearValue{};

            if (colorAtt.ClearBinding.Binding == RHIClearValueBinding::ClearValueBinding::Color)
            {
                clearValue.color.float32[0] = colorAtt.ClearBinding.Color[0];
                clearValue.color.float32[1] = colorAtt.ClearBinding.Color[1];
                clearValue.color.float32[2] = colorAtt.ClearBinding.Color[2];
                clearValue.color.float32[3] = colorAtt.ClearBinding.Color[3];
            }
            else
            {
                // Provide a clear-value slot even when the attachment does not clear.
                clearValue.color.float32[0] = 0.f;
                clearValue.color.float32[1] = 0.f;
                clearValue.color.float32[2] = 0.f;
                clearValue.color.float32[3] = 0.f;
            }

            clearValues.push_back(clearValue);
        }

        // 2. Depth / Stencil attachment
        VkClearValue dsClear{};
        const auto& dsAtt = renderTargets.DepthStencil;
        if (dsAtt.ClearBinding.Binding == RHIClearValueBinding::ClearValueBinding::DepthStencil)
        {
            dsClear.depthStencil.depth = dsAtt.ClearBinding.Depth;
            dsClear.depthStencil.stencil = dsAtt.ClearBinding.Stencil;
        }
        else
        {
            dsClear.depthStencil.depth = 1.0f;
            dsClear.depthStencil.stencil = 0;
        }

        clearValues.push_back(dsClear);

        return clearValues;
    }

    RHIGraphicAttachmentDesc GetAttachmentDesc(const RHIBoundRenderTargets& renderTargets) {
        RHIGraphicAttachmentDesc desc;
        // ==========================================
    // 1. 填充 Color Attachments 信息
    // ==========================================
        desc.ColorAttachmentCount = renderTargets.NumColorAttachments;

        // 用于记录整个 Pass 的全局 MSAA 采样数
        uint32_t commonSampleCount = 1;
        bool bSampleCountSet = false;

        for (uint32_t i = 0; i < renderTargets.NumColorAttachments; ++i)
        {
            const auto& srcAtt = renderTargets.ColorAttachments[i];

            if (srcAtt.Texture != nullptr)
            {
                // 从物理纹理资源中提取静态属性
                desc.ColorAttachments[i].Format = srcAtt.Texture->GetDesc().Format;
                desc.ColorAttachments[i].Actions = srcAtt.Actions;

                // 记录并追踪第一个有效纹理的采样数
                if (!bSampleCountSet)
                {
                    commonSampleCount = srcAtt.Texture->GetDesc().SampleCount;
                    bSampleCountSet = true;
                }
                else
                {
                    // 严谨性校验：同一个 Pass 的多渲染目标采样率必须一致
                    assert(srcAtt.Texture->GetDesc().SampleCount == commonSampleCount &&
                        "MRT color textures must have the exact same sample count!");
                }
            }
            else
            {
                desc.ColorAttachments[i].Format = ERHIFormat::Unknown;
                desc.ColorAttachments[i].Actions = ERenderTargetActions::Load_Store; // 默认安全回滚
            }
        }

        // ==========================================
        // 2. 填充 Depth / Stencil 信息
        // ==========================================
        const auto& srcDepth = renderTargets.DepthStencil;

        if (srcDepth.Texture != nullptr)
        {
            desc.bEnableDepth = true;

            // 根据格式特征自动判断是否包含 Stencil（例如包含 D24S8 或 D32S8 元素）
            ERHIFormat depthFormat = srcDepth.Texture->GetDesc().Format;
            desc.DepthStencilFormat = depthFormat;

            // 常见的带模板的深度格式检测（请根据你实际的 ERHIFormat 枚举名调整）
            desc.bEnableStencil = (depthFormat == ERHIFormat::D24_UNorm_S8_UInt);

            desc.DepthActions = srcDepth.Actions;
            // 如果开启了 Stencil，保持与 Depth 一致的动作，或者默认 Load_Store
            desc.StencilActions = desc.bEnableStencil ? srcDepth.Actions : ERenderTargetActions::Load_Store;

            // 采样率处理与对齐
            if (!bSampleCountSet)
            {
                // 纯深度 Pass（如 ShadowMap），由深度纹理决定全局采样率
                commonSampleCount = srcDepth.Texture->GetDesc().SampleCount;
                bSampleCountSet = true;
            }
            else
            {
                // 深度与颜色同时存在时，两者的 MSAA 采样率必须完美对齐
                assert(srcDepth.Texture->GetDesc().SampleCount == commonSampleCount &&
                    "Depth stencil sample count must match color attachments!");
            }
        }
        else
        {
            // 明确未绑定深度的情况
            desc.bEnableDepth = false;
            desc.bEnableStencil = false;
            desc.DepthStencilFormat = ERHIFormat::Unknown;
            desc.DepthActions = ERenderTargetActions::Load_Store;
            desc.StencilActions = ERenderTargetActions::Load_Store;
        }

        // ==========================================
        // 3. 全局/管线级状态填充
        // ==========================================
        desc.NumSamples = commonSampleCount;
        desc.SubpassIndex = 0; // 默认初始子通道索引为 0


        return desc;
    }

    void VulkanRenderPassManager::BeginRenderPass(VulkanCommandBuffer* cmdBuffer, const RHIRenderPassInfo& renderPassInfo)
    {
        VkRenderPassBeginInfo renderPassBeginInfo{};
        renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO; VulkanRenderTargetLayout renderTargrtInfo(GetAttachmentDesc(renderPassInfo.RenderTargets));
        auto renderPass = GetOrCreateRenderPass(renderTargrtInfo);
        auto frameBuffer = GetOrCreateFramebuffer(renderPassInfo.RenderTargets, renderPass);
        renderPassBeginInfo.renderPass = renderPass->GetHandle();
        renderPassBeginInfo.framebuffer = frameBuffer->GetHandle();
        renderPassBeginInfo.renderArea.offset = { renderPassInfo.RenderArea.X, renderPassInfo.RenderArea.Y };
        renderPassBeginInfo.renderArea.extent = { renderPassInfo.RenderArea.Width, renderPassInfo.RenderArea.Height };
        renderPassBeginInfo.clearValueCount = renderTargrtInfo.GetAttachmentDescriptionCount();
		auto clearValues = GenerateClearValues(renderPassInfo.RenderTargets);
        renderPassBeginInfo.pClearValues = clearValues.data();

        VKFunc::CmdBeginRenderPass(cmdBuffer->GetHandle(),&renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
    }

    void VulkanRenderPassManager::EndRenderPass(VulkanCommandBuffer* cmdBuffer)
    {
        VKFunc::CmdEndRenderPass(cmdBuffer->GetHandle());
    }

    void VulkanRenderPassManager::NotifyDeletedImage(VkImage image)
    {
        std::lock_guard<std::mutex> lg(m_FramebufferMutex);
        for (auto it = m_FramebufferCache.begin(); it != m_FramebufferCache.end();)
        {
            VulkanFramebuffer* fb = it->second;
            auto found = std::find(fb->m_ColorImages.begin(), fb->m_ColorImages.end(), image);
            if (found != fb->m_ColorImages.end() || fb->m_DepthImage == image)
            {
                delete fb;
                it = m_FramebufferCache.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }


    uint32_t VulkanRenderPassManager::CalcFramebufferHash(const RHIBoundRenderTargets& renderTargetsInfo, VulkanRenderPass* renderPass)
    {
        uint64_t hash = 14695981039346656037ull; // FNV-1a offset
        auto fnHashBytes = [&](const void* data, size_t size)
            {
                const uint8_t* ptr = reinterpret_cast<const uint8_t*>(data);
                for (size_t i = 0; i < size; ++i)
                {
                    hash ^= static_cast<uint64_t>(ptr[i]);
                    hash *= 1099511628211ull;
                }
            };

        // Include render pass identity.
        uintptr_t rpPtr = reinterpret_cast<uintptr_t>(renderPass);
        fnHashBytes(&rpPtr, sizeof(rpPtr));

        // Include the bound render target hash.
        size_t rtHash = RHIBoundRenderTargets::CalculateHash(renderTargetsInfo);
        fnHashBytes(&rtHash, sizeof(rtHash));

        // Fold the hash to the cache key width.
        return static_cast<uint32_t>(hash);
    }
}