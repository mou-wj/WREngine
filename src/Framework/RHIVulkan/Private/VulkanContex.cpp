#include "VulkanCommandContex.h"
#include "VulkanCommandBuffer.h"
#include "VulkanQueue.h"
#include "VulkanShader.h"
#include "VulkanDescriptorSets.h"
#include "VulkanRHIUtils.h"
#include "Log.h"

namespace RHIVulkan {

VulkanCommandContext::VulkanCommandContext(VulkanDevice* device, VulkanQueue* queue)
    : _device(device)
    , _queue(queue)
    , _commandBufferManager(new VulkanCommandBufferManager(device, this))
{
}

VulkanCommandContext::~VulkanCommandContext()
{
    delete _commandBufferManager;
}

void VulkanCommandContext::Begin()
{
    _commandBufferManager->GetActiveCommandBuffer();
    InnerBegin();
}

void VulkanCommandContext::End()
{
    _device->GetDescriptorSetManager()->GarbageCollect();
	_device->GetStagingManager()->GarbageCollect();
    _commandBufferManager->GarbageCollect();


    auto commandBuffer = _commandBufferManager->EndActiveCommandBuffer();
    _recordedCommandBuffer = commandBuffer;
}

void VulkanCommandContext::BeginTransitions(std::vector<const RHITransition*> transitions)
{
    for (const RHITransition* transition : transitions)
    {
        const VulkanPipelineBarrier* pipelineBarrier = transition->GetPrivateData<VulkanPipelineBarrier>();
        if (pipelineBarrier)
        {
        }
    }
}

void VulkanCommandContext::EndTransitions(std::vector<const RHITransition*> transitions)
{
    for (const RHITransition* transition : transitions)
    {
        VulkanPipelineBarrier* pipelineBarrier = transition->GetPrivateData<VulkanPipelineBarrier>();
        if (pipelineBarrier)
        {
            auto commandBuffer = _commandBufferManager->GetActiveCommandBuffer();
            pipelineBarrier->Execute(commandBuffer);
        }
    }
}

void VulkanCommandContext::CopyTexture(RHITexture* src, RHITexture* dst, const RHICopyTextureDesc& copyDesc)
{

    if (!(copyDesc.SrcRegion.Width == copyDesc.DstRegion.Width &&
        copyDesc.SrcRegion.Height == copyDesc.DstRegion.Height &&
        copyDesc.SrcRegion.Depth == copyDesc.DstRegion.Depth)) {
#ifdef DEBUG_INFO
        LOG_ERROR("%s", "src extent no match dst");
#endif // DEBUG_INFO
        return;
     }



    VulkanTexture* vkSrc = static_cast<VulkanTexture*>(src);
    VulkanTexture* vkDst = static_cast<VulkanTexture*>(dst);

    VkImage srcImage = vkSrc->GetImage();
    VkImage dstImage = vkDst->GetImage();

    auto srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

    auto dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

    VkImageCopy region{};

    // =========================
    // Src
    // =========================
    region.srcSubresource.aspectMask = vkSrc->GetAspectFlags();
    region.srcSubresource.mipLevel = copyDesc.SrcMipIndex;
    region.srcSubresource.baseArrayLayer = copyDesc.SrcArraySlice;
    region.srcSubresource.layerCount = 1;

    region.srcOffset = {
        copyDesc.SrcRegion.OffsetX,
        copyDesc.SrcRegion.OffsetY,
        copyDesc.SrcRegion.OffsetZ
    };

    // =========================
    // Destination subresource and offsets may differ from the source.
    // =========================
    region.dstSubresource.aspectMask = vkDst->GetAspectFlags();
    region.dstSubresource.mipLevel = copyDesc.DstMipIndex;
    region.dstSubresource.baseArrayLayer = copyDesc.DstArraySlice;
    region.dstSubresource.layerCount = 1;

    region.dstOffset = {
        copyDesc.DstRegion.OffsetX,
        copyDesc.DstRegion.OffsetY,
        copyDesc.DstRegion.OffsetZ
    };


    region.extent = {
        copyDesc.SrcRegion.Width,
        copyDesc.SrcRegion.Height,
        copyDesc.SrcRegion.Depth == 0 ? 1u : copyDesc.SrcRegion.Depth
    };

    auto commandBuffer = _commandBufferManager->GetActiveCommandBuffer();

    VKFunc::CmdCopyImage(
        commandBuffer->GetHandle(),
        srcImage,
        srcImageLayout,
        dstImage,
        dstImageLayout,
        1,
        &region);

}
void VulkanCommandContext::BlitTexture(RHITexture* src, RHITexture* dst, const RHIBlitTextureDesc& blitDesc)
{
    if (!(blitDesc.SrcRegion.Width > 0 &&
        blitDesc.SrcRegion.Height > 0 &&
        blitDesc.DstRegion.Width > 0 &&
        blitDesc.DstRegion.Height > 0)) {
#ifdef DEBUG_INFO
        LOG_ERROR("%s","BlitTexture: Region size must be > 0");
#endif // DEBUG_INFO


    }
    VulkanTexture* vkSrc = static_cast<VulkanTexture*>(src);
    VulkanTexture* vkDst = static_cast<VulkanTexture*>(dst);

    VkImage srcImage = vkSrc->GetImage();
    VkImage dstImage = vkDst->GetImage();

    // =========================
    // Transfer layouts used by the blit command.
    // =========================
    auto srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

    auto dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

    // =========================
    // Validate the blit region as needed.
    // =========================


    // =========================
    // Build the Vulkan image blit region.
    // =========================
    VkImageBlit region{};

    // ---- Src ----
    region.srcSubresource.aspectMask = vkSrc->GetAspectFlags();
    region.srcSubresource.mipLevel = blitDesc.SrcMipIndex;
    region.srcSubresource.baseArrayLayer = blitDesc.SrcArraySlice;
    region.srcSubresource.layerCount = blitDesc.LayerCount;

    region.srcOffsets[0] = {
        blitDesc.SrcRegion.OffsetX,
        blitDesc.SrcRegion.OffsetY,
        blitDesc.SrcRegion.OffsetZ
    };

    region.srcOffsets[1] = {
        blitDesc.SrcRegion.OffsetX + (int32_t)blitDesc.SrcRegion.Width,
        blitDesc.SrcRegion.OffsetY + (int32_t)blitDesc.SrcRegion.Height,
        blitDesc.SrcRegion.OffsetZ + (int32_t)(blitDesc.SrcRegion.Depth == 0 ? 1 : blitDesc.SrcRegion.Depth)
    };

    // ---- Dst ----
    region.dstSubresource.aspectMask = vkDst->GetAspectFlags();
    region.dstSubresource.mipLevel = blitDesc.DstMipIndex;
    region.dstSubresource.baseArrayLayer = blitDesc.DstArraySlice;
    region.dstSubresource.layerCount = blitDesc.LayerCount;

    region.dstOffsets[0] = {
        blitDesc.DstRegion.OffsetX,
        blitDesc.DstRegion.OffsetY,
        blitDesc.DstRegion.OffsetZ
    };

    region.dstOffsets[1] = {
        blitDesc.DstRegion.OffsetX + (int32_t)blitDesc.DstRegion.Width,
        blitDesc.DstRegion.OffsetY + (int32_t)blitDesc.DstRegion.Height,
        blitDesc.DstRegion.OffsetZ + (int32_t)(blitDesc.DstRegion.Depth == 0 ? 1 : blitDesc.DstRegion.Depth)
    };

    // =========================
    // Map the RHI filter to Vulkan.
    // =========================
    VkFilter vkFilter = VK_FILTER_LINEAR;
    switch (blitDesc.Filter)
    {
    case ERHIFilter::Nearest:
        vkFilter = VK_FILTER_NEAREST;
        break;
    case ERHIFilter::Linear:
        vkFilter = VK_FILTER_LINEAR;
        break;
    default:
        vkFilter = VK_FILTER_LINEAR;
        break;
    }

    // =========================
    // Record the blit command.
    // =========================
    auto commandBuffer = _commandBufferManager->GetActiveCommandBuffer();

    VKFunc::CmdBlitImage(
        commandBuffer->GetHandle(),
        srcImage,
        srcImageLayout,
        dstImage,
        dstImageLayout,
        1,
        &region,
        vkFilter);
}


VulkanComputeContext* VulkanComputeContext::CastFrom(RHIComputeContext* context)
{
    return dynamic_cast<VulkanComputeContext*>(context);
}

VulkanComputeContext::VulkanComputeContext(VulkanDevice* device, VulkanQueue* queue)
    : VulkanCommandContext(device, queue)
{
    m_PendingCompute = new VulkanPendingComputeState(device, this);
    m_LooseUniformDataUploader = new VulkanLooseUniformDataUploader(device);

}

VulkanComputeContext::~VulkanComputeContext()
{
    delete m_PendingCompute;
    delete m_LooseUniformDataUploader;
}

void VulkanComputeContext::SetBatchedShaderParameters(RHIComputeShader* shader, const RHIBatchedShaderParameters& parameter)
{
    if (!m_PendingCompute)
    {
        return;
    }

    auto shaderType = shader->GetShaderType();

    for (const auto& uniformParam : parameter.UniformParameters)
    {
        const uint8_t* valuePtr = parameter.Data.data() + uniformParam.Offset;
        if (shaderType == RHI::ERHIShaderFrequency::Compute)
        {
            m_PendingCompute->SetShaderParameter(shaderType, uniformParam.BufferIndex, uniformParam.BaseIndex, uniformParam.Size, valuePtr);
        }
    }

    for (const auto& resourceParam : parameter.ResourceParameters)
    {
        if (shaderType != RHI::ERHIShaderFrequency::Compute)
        {
            continue;
        }

        switch (resourceParam.Type)
        {
        case RHIShaderResourceParameter::EType::Texture:
            m_PendingCompute->SetTexture(shaderType, resourceParam.Index, resourceParam.ArrayIndex, static_cast<VulkanTexture*>(resourceParam.GetResourceAs<RHITexture>()));
            break;
        case RHIShaderResourceParameter::EType::SRV:
            m_PendingCompute->SetSRV(shaderType, resourceParam.Index, resourceParam.ArrayIndex, static_cast<VulkanShaderResourceView*>(resourceParam.GetResourceAs<RHIShaderResourceView>()));
            break;
        case RHIShaderResourceParameter::EType::UAV:
            m_PendingCompute->SetUAV(shaderType, resourceParam.Index, resourceParam.ArrayIndex, static_cast<VulkanUnorderedAccessView*>(resourceParam.GetResourceAs<RHIUnorderedAccessView>()));
            break;
        case RHIShaderResourceParameter::EType::Sampler:
            m_PendingCompute->SetSampler(shaderType, resourceParam.Index, resourceParam.ArrayIndex, static_cast<VulkanSampler*>(resourceParam.GetResourceAs<RHISampler>()));
            break;
        case RHIShaderResourceParameter::EType::UniformBuffer:
            m_PendingCompute->SetUniformBuffer(shaderType, resourceParam.Index, resourceParam.ArrayIndex, static_cast<VulkanBuffer*>(resourceParam.GetResourceAs<RHIBuffer>()));
            break;
        default:
            break;
        }
    }
}

void VulkanComputeContext::SetComputePipelineState(RHIComputePipelineState* pipelineState)
{
    auto vkPipeline = static_cast<VulkanComputePipelineState*>(pipelineState);
    if (m_PendingCompute)
    {
        m_PendingCompute->SetPipeline(vkPipeline);
    }
}

void VulkanComputeContext::Dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
{
    auto commandBuffer = _commandBufferManager->GetActiveCommandBuffer();
    if (m_PendingCompute && m_PendingCompute->HasPipeline())
    {
        m_PendingCompute->PrepareForDispatch(commandBuffer);
        VKFunc::CmdDispatch(commandBuffer->GetHandle(), groupCountX, groupCountY, groupCountZ);
    }
}

void VulkanComputeContext::InnerBegin()
{
    if (m_PendingCompute) {
        m_PendingCompute->Reset();
    }
}

VulkanGraphicContext* VulkanGraphicContext::CastFrom(RHI::RHIGraphicContex* context)
{
    return dynamic_cast<VulkanGraphicContext*>(context);
}

VulkanGraphicContext::VulkanGraphicContext(VulkanDevice* device, VulkanQueue* queue)
    : VulkanCommandContext(device, queue)
{
    m_PendingGfx = new VulkanPendingGfxState(device, this);
    m_PendingRayTracing = new VulkanPendingRayTracingState(device, this);
    m_LooseUniformDataUploader = new VulkanLooseUniformDataUploader(device);
}

VulkanGraphicContext::~VulkanGraphicContext()
{
    delete m_PendingGfx;
    delete m_PendingRayTracing;
    delete m_LooseUniformDataUploader;
}

void VulkanGraphicContext::SetBatchedShaderParameters(RHIGraphicShader* shader, const RHIBatchedShaderParameters& parameter)
{
    if (!m_PendingGfx)
    {
        return;
    }

    auto shaderType = shader->GetShaderType();

    for (const auto& uniformParam : parameter.UniformParameters)
    {
        const uint8_t* valuePtr = parameter.Data.data() + uniformParam.Offset;
        switch (shaderType)
        {
        case RHI::ERHIShaderFrequency::Vertex:
        case RHI::ERHIShaderFrequency::Fragment:
        case RHI::ERHIShaderFrequency::Geometry:
        case RHI::ERHIShaderFrequency::Mesh:
        case RHI::ERHIShaderFrequency::Task:
            m_PendingGfx->SetShaderParameter(shaderType, uniformParam.BufferIndex, uniformParam.BaseIndex, uniformParam.Size, valuePtr);
            break;
        default:
            break;
        }
    }

    for (const auto& resourceParam : parameter.ResourceParameters)
    {
        const bool isGraphicsStage =
            shaderType == RHI::ERHIShaderFrequency::Vertex
            || shaderType == RHI::ERHIShaderFrequency::Fragment
            || shaderType == RHI::ERHIShaderFrequency::Geometry
            || shaderType == RHI::ERHIShaderFrequency::Mesh
            || shaderType == RHI::ERHIShaderFrequency::Task;

        if (!isGraphicsStage)
        {
            continue;
        }

        switch (resourceParam.Type)
        {
        case RHIShaderResourceParameter::EType::Texture:
            m_PendingGfx->SetTexture(shaderType, resourceParam.Index, resourceParam.ArrayIndex, static_cast<VulkanTexture*>(resourceParam.GetResourceAs<RHITexture>()));
            break;
        case RHIShaderResourceParameter::EType::SRV:
            m_PendingGfx->SetSRV(shaderType, resourceParam.Index, resourceParam.ArrayIndex, static_cast<VulkanShaderResourceView*>(resourceParam.GetResourceAs<RHIShaderResourceView>()));
            break;
        case RHIShaderResourceParameter::EType::UAV:
            m_PendingGfx->SetUAV(shaderType, resourceParam.Index, resourceParam.ArrayIndex, static_cast<VulkanUnorderedAccessView*>(resourceParam.GetResourceAs<RHIUnorderedAccessView>()));
            break;
        case RHIShaderResourceParameter::EType::Sampler:
            m_PendingGfx->SetSampler(shaderType, resourceParam.Index, resourceParam.ArrayIndex, static_cast<VulkanSampler*>(resourceParam.GetResourceAs<RHISampler>()));
            break;
        case RHIShaderResourceParameter::EType::UniformBuffer:
            m_PendingGfx->SetUniformBuffer(shaderType, resourceParam.Index, resourceParam.ArrayIndex, static_cast<VulkanBuffer*>(resourceParam.GetResourceAs<RHIBuffer>()));
            break;
        default:
            break;
        }
    }
}

void VulkanGraphicContext::SetStreamSource(uint32_t streamIndex, RHIBuffer* vertexBuffer, uint32_t offset)
{
    VulkanBuffer* vulkanVertexBuffer = dynamic_cast<VulkanBuffer*>(vertexBuffer);
    VkDeviceSize deviceOffset = offset;
    if (m_PendingGfx && m_PendingGfx->HasPipeline())
    {
        m_PendingGfx->SetVertexStream(streamIndex, vulkanVertexBuffer->GetHandle(), deviceOffset);
    }
}

void VulkanGraphicContext::SetGraphicPipelineState(RHIGraphicsPipelineState* pipelineState)
{
    auto vkPipeline = static_cast<VulkanGraphicsPipelineState*>(pipelineState);
    if (m_PendingGfx)
    {
        m_PendingGfx->SetPipeline(vkPipeline);
    }
}

void VulkanGraphicContext::SetViewport(float x, float y, float w, float h, float minDepth, float maxDepth)
{
    VkViewport vkViewport = {};
    vkViewport.x = static_cast<float>(x);
    vkViewport.y = static_cast<float>(y);
    vkViewport.width = static_cast<float>(w);
    vkViewport.height = static_cast<float>(h);
    vkViewport.minDepth = minDepth;
    vkViewport.maxDepth = maxDepth;
    if (m_PendingGfx && m_PendingGfx->HasPipeline())
    {
        m_PendingGfx->SetViewport(vkViewport);
    }
}

void VulkanGraphicContext::SetScissor(int32_t x, int32_t y, uint32_t w, uint32_t h)
{
    VkRect2D scissorRect = {};
    scissorRect.offset = { x, y };
    scissorRect.extent = { w, h };
    if (m_PendingGfx && m_PendingGfx->HasPipeline())
    {
        m_PendingGfx->SetScissor(scissorRect);
    }
}

void VulkanGraphicContext::Draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance)
{
    auto commandBuffer = _commandBufferManager->GetActiveCommandBuffer();

    if (m_PendingGfx && m_PendingGfx->HasPipeline())
    {
        m_PendingGfx->PrepareForDraw(commandBuffer);
        VKFunc::CmdDraw(commandBuffer->GetHandle(), vertexCount, instanceCount, firstVertex, firstInstance);
    }
}

void VulkanGraphicContext::DrawIndexed(RHIBuffer* indexBuffer, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance)
{
    auto commandBuffer = _commandBufferManager->GetActiveCommandBuffer();
    if (m_PendingGfx && m_PendingGfx->HasPipeline())
    {
        m_PendingGfx->PrepareForDraw(commandBuffer);
        VulkanBuffer* vulkanIndexBuffer = static_cast<VulkanBuffer*>(indexBuffer);
        VkIndexType indexType = vulkanIndexBuffer->GetDesc().Stride == 4 ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16;
        VKFunc::CmdBindIndexBuffer(commandBuffer->GetHandle(), static_cast<VulkanBuffer*>(indexBuffer)->GetHandle(), 0, indexType);
        VKFunc::CmdDrawIndexed(commandBuffer->GetHandle(), indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
    }
}

void VulkanGraphicContext::SetBatchedShaderParameters(RHIRayTracingShader* shader, const RHIBatchedShaderParameters& parameter)
{
    if (!m_PendingRayTracing)
    {
        return;
    }

    auto shaderType = shader->GetShaderType();

    for (const auto& uniformParam : parameter.UniformParameters)
    {

        const uint8_t* valuePtr = parameter.Data.data() + uniformParam.Offset;
        switch (shaderType)
        {
        case RHI::ERHIShaderFrequency::RayGen:
        case RHI::ERHIShaderFrequency::ClosestHit:
        case RHI::ERHIShaderFrequency::Miss:
        case RHI::ERHIShaderFrequency::AnyHit:
        case RHI::ERHIShaderFrequency::Intersection:
        case RHI::ERHIShaderFrequency::Callable:
            m_PendingRayTracing->SetShaderParameter(shaderType, uniformParam.BufferIndex, uniformParam.BaseIndex, uniformParam.Size, valuePtr);
            break;
        default:
            break;
        }
    }

    for (const auto& resourceParam : parameter.ResourceParameters)
    {
        const bool isRayTracingStage =
            shaderType == RHI::ERHIShaderFrequency::RayGen
            || shaderType == RHI::ERHIShaderFrequency::ClosestHit
            || shaderType == RHI::ERHIShaderFrequency::Miss
            || shaderType == RHI::ERHIShaderFrequency::AnyHit
            || shaderType == RHI::ERHIShaderFrequency::Intersection
            || shaderType == RHI::ERHIShaderFrequency::Callable;

        if (!isRayTracingStage)
        {
            continue;
        }

        switch (resourceParam.Type)
        {
        case RHIShaderResourceParameter::EType::Texture:
            m_PendingRayTracing->SetTexture(shaderType, resourceParam.Index, resourceParam.ArrayIndex, static_cast<VulkanTexture*>(resourceParam.GetResourceAs<RHITexture>()));
            break;
        case RHIShaderResourceParameter::EType::SRV:
            m_PendingRayTracing->SetSRV(shaderType, resourceParam.Index, resourceParam.ArrayIndex, static_cast<VulkanShaderResourceView*>(resourceParam.GetResourceAs<RHIShaderResourceView>()));
            break;
        case RHIShaderResourceParameter::EType::UAV:
            m_PendingRayTracing->SetUAV(shaderType, resourceParam.Index, resourceParam.ArrayIndex, static_cast<VulkanUnorderedAccessView*>(resourceParam.GetResourceAs<RHIUnorderedAccessView>()));
            break;
        case RHIShaderResourceParameter::EType::Sampler:
            m_PendingRayTracing->SetSampler(shaderType, resourceParam.Index, resourceParam.ArrayIndex, static_cast<VulkanSampler*>(resourceParam.GetResourceAs<RHISampler>()));
            break;
        case RHIShaderResourceParameter::EType::UniformBuffer:
            m_PendingRayTracing->SetUniformBuffer(shaderType, resourceParam.Index, resourceParam.ArrayIndex, static_cast<VulkanBuffer*>(resourceParam.GetResourceAs<RHIBuffer>()));
            break;
        case RHIShaderResourceParameter::EType::AccelerationStructure:
            m_PendingRayTracing->SetAccelerationStructure(shaderType, resourceParam.Index, resourceParam.ArrayIndex, static_cast<VulkanRayTracingInstance*>(resourceParam.GetResourceAs<RHIRayTracingInstance>()));
            break;
        default:
            break;
        }
    }
}

void VulkanGraphicContext::SetRayTracingPipelineState(RHIRayTracingPipelineState* pipelineState)
{
    if (!m_PendingRayTracing)
    {
        return;
    }

    auto vkPipeline = dynamic_cast<VulkanRayTracingPipeline*>(pipelineState);
    m_PendingRayTracing->SetPipeline(vkPipeline);
}

void VulkanGraphicContext::BuildAccelerationStructure(RHIRayTracingAccelerationStructure* accelerationStructure)
{
    if (!m_PendingRayTracing || !accelerationStructure)
    {
        return;
    }

    auto commandBuffer = _commandBufferManager->GetActiveCommandBuffer();
    m_PendingRayTracing->BuildAccelerationStructure(commandBuffer, accelerationStructure, false);
}

void VulkanGraphicContext::UpdateAccelerationStructure(RHIRayTracingAccelerationStructure* accelerationStructure)
{
    if (!m_PendingRayTracing || !accelerationStructure)
    {
        return;
    }

    auto commandBuffer = _commandBufferManager->GetActiveCommandBuffer();
    m_PendingRayTracing->BuildAccelerationStructure(commandBuffer, accelerationStructure, true);
}

void VulkanGraphicContext::TraceRays(uint32_t width, uint32_t height, uint32_t depth)
{
    if (!m_PendingRayTracing || !m_PendingRayTracing->HasPipeline())
    {
        return;
    }

    auto commandBuffer = _commandBufferManager->GetActiveCommandBuffer();
    m_PendingRayTracing->PrepareForTraceRays(commandBuffer);

    const auto& raygenShaderBindingTable = m_PendingRayTracing->GetRayGenShaderBindingTableRegion();
    const auto& missShaderBindingTable = m_PendingRayTracing->GetMissShaderBindingTableRegion();
    const auto& hitShaderBindingTable = m_PendingRayTracing->GetHitShaderBindingTableRegion();
    const auto& callableShaderBindingTable = m_PendingRayTracing->GetCallableShaderBindingTableRegion();

    VKFunc::CmdTraceRaysKHR(
        commandBuffer->GetHandle(),
        &raygenShaderBindingTable,
        &missShaderBindingTable,
        &hitShaderBindingTable,
        &callableShaderBindingTable,
        width,
        height,
        depth);
}

void VulkanGraphicContext::InnerBegin()
{
    if (m_PendingGfx) {
        m_PendingGfx->Reset();
    }
    if (m_PendingRayTracing) {
        m_PendingRayTracing->Reset();
    }
}

void VulkanGraphicContext::BeginRenderPass(const RHIRenderPassInfo& renderPassInfo)
{
    auto commandBuffer = _commandBufferManager->GetActiveCommandBuffer();
    _device->GetRenderPassManager()->BeginRenderPass(commandBuffer, renderPassInfo);
}

void VulkanGraphicContext::EndRenderPass()
{
    auto commandBuffer = _commandBufferManager->GetActiveCommandBuffer();
    _device->GetRenderPassManager()->EndRenderPass(commandBuffer);
}

} // namespace WR::RHIVulkan
