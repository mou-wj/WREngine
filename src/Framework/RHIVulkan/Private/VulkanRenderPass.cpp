#include "VulkanRenderPass.h"
#include "VulkanResource.h"
#include "VulkanRHIUtils.h"
#include "VulkanCommandBuffer.h"
namespace RHIVulkan {
    // Helper: �� ERenderTargetActions ת��Ϊ VkAttachmentLoadOp / VkAttachmentStoreOp
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
        case uint8_t(ERenderTargetStoreOp::Resolve):  return VK_ATTACHMENT_STORE_OP_STORE; // Resolve ���� resolve attachment
        default: return VK_ATTACHMENT_STORE_OP_DONT_CARE;
        }
    }

    // ----------------------------
// Reset
// ----------------------------
    void VulkanRenderTargetLayout::resetAttachments()
    {
        attachmentDescCount = 0;
        numColorAttachments = 0;
        hasDepthStencil = false;
        hasResolveAttachments = false;

        std::fill(std::begin(colorRefs), std::end(colorRefs), VkAttachmentReference{ VK_ATTACHMENT_UNUSED, VK_IMAGE_LAYOUT_UNDEFINED });
        std::fill(std::begin(resolveRefs), std::end(resolveRefs), VkAttachmentReference{ VK_ATTACHMENT_UNUSED, VK_IMAGE_LAYOUT_UNDEFINED });
        depthRef = { VK_ATTACHMENT_UNUSED, VK_IMAGE_LAYOUT_UNDEFINED };
    }

    // ----------------------------
    // Constructor
    // ----------------------------
    VulkanRenderTargetLayout::VulkanRenderTargetLayout(const RHIGraphicAttachmentDesc& desc)
    {
        resetAttachments();
        numColorAttachments = desc.ColorAttachmentCount;

        parseColorAttachments(desc);
        parseResolveAttachments(desc);
        parseDepthAttachment(desc);
        calculateHashes(desc);
    }

    // ----------------------------
    // Color Attachments
    // ----------------------------
    void VulkanRenderTargetLayout::parseColorAttachments(const RHIGraphicAttachmentDesc& desc)
    {
        for (uint32_t i = 0; i < desc.ColorAttachmentCount; ++i)
        {
            const auto& src = desc.ColorAttachments[i];
            VkAttachmentDescription& dst = attachmentDescs[attachmentDescCount];

            dst.flags = 0;
            dst.format =  TransformFormatFrom(src.Format);
            dst.samples = TransformSampleCountFrom(src.SampleCount);

            dst.loadOp = RTActionToLoadOp(src.Actions);
            dst.storeOp = RTActionToStoreOp(src.Actions);

            dst.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            dst.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

            dst.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            dst.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

            colorRefs[i].attachment = attachmentDescCount;
            colorRefs[i].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

            attachmentDescCount++;
        }
    }

    // ----------------------------
    // Resolve Attachments
    // ----------------------------
    void VulkanRenderTargetLayout::parseResolveAttachments(const RHIGraphicAttachmentDesc& desc)
    {
        for (uint32_t i = 0; i < desc.ColorAttachmentCount; ++i)
        {
            const auto& src = desc.ColorAttachments[i];
            if (src.bEnableResolve)
            {
                VkAttachmentDescription& dst = attachmentDescs[attachmentDescCount];

                dst.flags = 0;
                dst.format = static_cast<VkFormat>(src.Format); // ����Ϊ resolve Ŀ���ʽ
                dst.samples = VK_SAMPLE_COUNT_1_BIT;
                dst.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                dst.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                dst.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                dst.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                dst.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                dst.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

                resolveRefs[i].attachment = attachmentDescCount;
                resolveRefs[i].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

                hasResolveAttachments = true;
                attachmentDescCount++;
            }
            else
            {
                resolveRefs[i].attachment = VK_ATTACHMENT_UNUSED;
            }
        }
    }

    // ----------------------------
    // Depth Attachment
    // ----------------------------
    void VulkanRenderTargetLayout::parseDepthAttachment(const RHIGraphicAttachmentDesc& desc)
    {
        if (desc.bEnableDepth || desc.bEnableStencil)
        {
            VkAttachmentDescription& dst = attachmentDescs[attachmentDescCount];

            dst.flags = 0;
            dst.format = TransformFormatFrom(desc.DepthStencilFormat);
            dst.samples = TransformSampleCountFrom(desc.NumSamples);

            dst.loadOp = RTActionToLoadOp(desc.DepthActions);
            dst.storeOp = RTActionToStoreOp(desc.DepthActions);

            dst.stencilLoadOp = RTActionToLoadOp(desc.StencilActions);
            dst.stencilStoreOp = RTActionToStoreOp(desc.StencilActions);

            dst.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            dst.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

            depthRef.attachment = attachmentDescCount;
            depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

            hasDepthStencil = true;
            attachmentDescCount++;
        }
    }

    // ----------------------------
    // Hash ����
    // ----------------------------
    void VulkanRenderTargetLayout::calculateHashes(const RHIGraphicAttachmentDesc& desc)
    {
        // Compatible Hash��ֻ�� format / count / samples��
        size_t hash = 14695981039346656037ull;
        hash ^= static_cast<uint64_t>(numColorAttachments); hash *= 1099511628211ull;
        for (uint32_t i = 0; i < numColorAttachments; ++i)
            hash ^= RHIColorAttachmentDesc::CalculateHash(desc.ColorAttachments[i]), hash *= 1099511628211ull;

        hash ^= static_cast<uint64_t>(hasDepthStencil); hash *= 1099511628211ull;
        hash ^= static_cast<uint64_t>(desc.DepthStencilFormat); hash *= 1099511628211ull;
        hash ^= static_cast<uint64_t>(desc.NumSamples); hash *= 1099511628211ull;

        renderPassCompatibleHash = hash;

        // Full Hash������ Load/Store/Resolve��
        hash = 14695981039346656037ull;
        hash ^= static_cast<uint64_t>(numColorAttachments); hash *= 1099511628211ull;
        for (uint32_t i = 0; i < numColorAttachments; ++i)
            hash ^= RHIColorAttachmentDesc::CalculateHash(desc.ColorAttachments[i]), hash *= 1099511628211ull;

        hash ^= static_cast<uint64_t>(hasDepthStencil); hash *= 1099511628211ull;
        hash ^= static_cast<uint64_t>(desc.DepthActions); hash *= 1099511628211ull;
        hash ^= static_cast<uint64_t>(desc.StencilActions); hash *= 1099511628211ull;
        hash ^= static_cast<uint64_t>(desc.DepthStencilFormat); hash *= 1099511628211ull;
        hash ^= static_cast<uint64_t>(desc.NumSamples); hash *= 1099511628211ull;

        renderPassFullHash = hash;
    }

    // ---------------------------------------------------
    // VulkanRenderPass
    // ---------------------------------------------------
    VulkanRenderPass::VulkanRenderPass(VulkanDevice* device, const VulkanRenderTargetLayout& layout)
        : Device(device), Layout(layout)
    {
        assert(Device);

        auto attachments = layout.getAttachmentDescriptions();
        auto attachmentCount = layout.getAttachmentDescriptionCount();


        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = layout.getNumColorAttachments();
        subpass.pColorAttachments = layout.getColorAttachmentRefs();
        if (layout.hasDepth())
            subpass.pDepthStencilAttachment = layout.getDepthAttachmentRef();

        VkRenderPassCreateInfo rpInfo{};
        rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpInfo.attachmentCount = static_cast<uint32_t>(attachmentCount);
        rpInfo.pAttachments = attachments;
        rpInfo.subpassCount = 1;
        rpInfo.pSubpasses = &subpass;

        bool result = VKFunc::CreateRenderPass(Device->GetHandle(), &rpInfo, &RenderPass);
        assert(result == true);
    }

    VulkanRenderPass::~VulkanRenderPass()
    {
        if (RenderPass != VK_NULL_HANDLE && Device)
            Device->EnqueueRenderPassForDeletion(RenderPass);
    }

    // ---------------------------------------------------
    // VulkanFramebuffer
    // ---------------------------------------------------
    VulkanFramebuffer::VulkanFramebuffer(VulkanDevice* device, const RHIBoundRenderTargets& targetInfo, VulkanRenderPass* renderPass)
        : Device(device)
    {

        NumColorAttachments = targetInfo.NumColorAttachments;
        auto dimens = targetInfo.Dimensions;
        Extent.width = dimens.X;
        Extent.height = dimens.Y;

        std::vector<VkImageView> vkViews;
		VkImageViewCreateInfo viewInfo{};
        vkViews.resize(NumColorAttachments + (targetInfo.HasDepth() ? 1 : 0));
        attachmentViews.resize(NumColorAttachments + (targetInfo.HasDepth() ? 1 : 0));
        // ���� ColorAttachments
        for (uint32_t i = 0; i < NumColorAttachments; ++i)
        {
            VulkanTexture* tex = static_cast<VulkanTexture*>(targetInfo.ColorAttachments[i].Texture);
            assert(tex);

            ColorImages.push_back(tex->GetImage());
            // �½� FVulkanTextureView ������ ImageView
            VulkanTextureView& view = attachmentViews[i];
            view.Create(
                Device,                                 // fvulkan_device&
                tex->GetImage(),                          // VkImage
                VK_IMAGE_VIEW_TYPE_2D,                    // view type�����������
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

            DepthImage = depthTex->GetImage();
            VulkanTextureView& view = attachmentViews[NumColorAttachments];
            view.Create(
                Device,
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

            vkViews[NumColorAttachments] = view.View;
        }

        // ���� VkFramebuffer
        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = renderPass->GetHandle();
        fbInfo.attachmentCount = static_cast<uint32_t>(vkViews.size());
        fbInfo.pAttachments = vkViews.data();
        fbInfo.width = Extent.width;
        fbInfo.height = Extent.height;
        fbInfo.layers = 1;

        bool result = VKFunc::CreateFramebuffer(Device->GetHandle(), &fbInfo, &Framebuffer);
        assert(result);
    }

    VulkanFramebuffer::~VulkanFramebuffer()
    {
        if (Framebuffer != VK_NULL_HANDLE && Device)
            Device->EnqueueFramebufferForDeletion(Framebuffer);
        for (auto& view : attachmentViews) 
        {
            view.Destroy(Device);
        }


    }

    

    // ---------------------------------------------------
    // VulkanRenderPassManager
    // ---------------------------------------------------
    VulkanRenderPassManager::VulkanRenderPassManager(VulkanDevice* device)
        : Device(device)
    {
    }

    VulkanRenderPassManager::~VulkanRenderPassManager()
    {
        for (auto& kv : RenderPassCache)
            delete kv.second;
        for (auto& kv : FramebufferCache)
            delete kv.second;
    }

    VulkanRenderPass* VulkanRenderPassManager::GetOrCreateRenderPass(const VulkanRenderTargetLayout& layout)
    {
        std::lock_guard<std::mutex> lg(RenderPassMutex);
        auto it = RenderPassCache.find(layout.getFullHash());
        if (it != RenderPassCache.end())
            return it->second;

        VulkanRenderPass* rp = new VulkanRenderPass(Device, layout);
        RenderPassCache[layout.getFullHash()] = rp;
        return rp;
    }

    VulkanFramebuffer* VulkanRenderPassManager::GetOrCreateFramebuffer(const RHIBoundRenderTargets& passInfo, VulkanRenderPass* renderPass)
    {
        std::lock_guard<std::mutex> lg(FramebufferMutex);
        auto hash = CalcFramebufferHash(passInfo, renderPass);
        auto it = FramebufferCache.find(hash);
        if (it != FramebufferCache.end())
            return it->second;

        VulkanFramebuffer* fb = new VulkanFramebuffer(Device, passInfo,renderPass);
        FramebufferCache[hash] = fb;
        return fb;
    }
    std::vector<VkClearValue> GenerateClearValues(const RHIBoundRenderTargets& RenderTargets)
    {
        std::vector<VkClearValue> ClearValues;

        // 1. �������� color attachments
        for (uint8_t i = 0; i < RenderTargets.NumColorAttachments; ++i)
        {
            const auto& colorAtt = RenderTargets.ColorAttachments[i];
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
                // �������Ҫ����������䣨Vulkan ����ԣ�
                clearValue.color.float32[0] = 0.f;
                clearValue.color.float32[1] = 0.f;
                clearValue.color.float32[2] = 0.f;
                clearValue.color.float32[3] = 0.f;
            }

            ClearValues.push_back(clearValue);
        }

        // 2. Depth / Stencil attachment
        VkClearValue dsClear{};
        const auto& dsAtt = RenderTargets.DepthStencil;
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

        ClearValues.push_back(dsClear);

        return ClearValues;
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
        renderPassBeginInfo.clearValueCount = renderTargrtInfo.getAttachmentDescriptionCount();
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
        std::lock_guard<std::mutex> lg(FramebufferMutex);
        for (auto it = FramebufferCache.begin(); it != FramebufferCache.end();)
        {
            VulkanFramebuffer* fb = it->second;
            auto found = std::find(fb->ColorImages.begin(), fb->ColorImages.end(), image);
            if (found != fb->ColorImages.end() || fb->DepthImage == image)
            {
                delete fb;
                it = FramebufferCache.erase(it);
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

        // 1. RenderPass pointer ��Ϊ identity
        uintptr_t rpPtr = reinterpret_cast<uintptr_t>(renderPass);
        fnHashBytes(&rpPtr, sizeof(rpPtr));

        // 2. BoundRenderTargets ������ hash
        size_t rtHash = RHIBoundRenderTargets::CalculateHash(renderTargetsInfo);
        fnHashBytes(&rtHash, sizeof(rtHash));

        // 3. ���� 32-bit hash������ map / cache
        return static_cast<uint32_t>(hash);
    }
}