#pragma once

#include "RHICommandContex.h"
#include "VulkanDevice.h"
#include "VulkanPipelineState.h"
#include "VulkanPendingPipelineState.h"
#include "VulkanResource.h"
#include "VulkanMemory.h"
#include "RHICommandList.h"
#include "VulkanCommandBuffer.h"
#include "VulkanFuncWrapper.h"

using namespace RHI;

namespace RHIVulkan {
class VulkanDescriptorSet;
class VulkanCommandBufferManager;
class VulkanQueue;

class RHIVULKAN_API VulkanCommandContext : public virtual RHI::RHIContextBase
{
public:
    VulkanCommandContext(VulkanDevice* device, VulkanQueue* queue);
    virtual ~VulkanCommandContext();


    void Begin() override;
    void End() override;
    void BeginTransitions(std::vector<const RHITransition*> transitions) override;
    void EndTransitions(std::vector<const RHITransition*> transitions) override;
    void CopyTexture(RHITexture* src, RHITexture* dst, const RHICopyTextureDesc& copyDesc) override;
    void BlitTexture(RHITexture* src, RHITexture* dst, const RHIBlitTextureDesc& blitDesc) override;

    VulkanQueue* GetQueue() const { return _queue; }
    VulkanCommandBufferManager* GetCommandBufferManager() const { return _commandBufferManager; }
    VulkanDevice* GetDevice() const { return _device; }
    VulkanCommandBuffer* GetRecordedCommandBuffer() const { return _recordedCommandBuffer; }
protected:
    virtual void InnerBegin() {}
    VulkanDevice* _device = nullptr;
    VulkanQueue* _queue = nullptr;
    VulkanCommandBufferManager* _commandBufferManager = nullptr;
    VulkanCommandBuffer* _recordedCommandBuffer = nullptr;
       


};

class RHIVULKAN_API VulkanComputeContext : public VulkanCommandContext, public RHI::RHIComputeContex
{
public:
    static VulkanComputeContext* CastFrom(RHIComputeContext* context);

    VulkanComputeContext(VulkanDevice* device, VulkanQueue* queue);
    ~VulkanComputeContext() override;

    VulkanLooseUniformDataUploader* GetLooseUniformDataUploader() const { return m_LooseUniformDataUploader; }

    void SetBatchedShaderParameters(RHIComputeShader* shader, const RHIBatchedShaderParameters& parameter) override;
    void SetComputePipelineState(RHIComputePipelineState* pipelineState) override;
    void Dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) override;

private:
    void InnerBegin() override;
    friend class VulkanCommandContext;
    VulkanPendingComputeState* m_PendingCompute = nullptr;
    VulkanLooseUniformDataUploader* m_LooseUniformDataUploader = nullptr;
};

class RHIVULKAN_API VulkanGraphicContext : public VulkanCommandContext, public RHI::RHIGraphicContex
{
public:
    static VulkanGraphicContext* CastFrom(RHI::RHIGraphicContex* context);

    VulkanGraphicContext(VulkanDevice* device, VulkanQueue* queue);
    ~VulkanGraphicContext() override;

    VulkanLooseUniformDataUploader* GetLooseUniformDataUploader() const { return m_LooseUniformDataUploader; }

    void SetBatchedShaderParameters(RHIGraphicShader* shader, const RHIBatchedShaderParameters& parameter) override;
    void SetStreamSource(uint32_t streamIndex, RHIBuffer* vertexBuffer, uint32_t offset) override;
    void SetGraphicPipelineState(RHIGraphicsPipelineState* pipelineState) override;
    void SetViewport(float x, float y, float w, float h, float minDepth, float maxDepth) override;
    void SetScissor(int32_t x, int32_t y, uint32_t w, uint32_t h) override;
    void Draw(uint32_t vertexCount, uint32_t instanceCount = 1, uint32_t firstVertex = 0, uint32_t firstInstance = 0) override;
    void DrawIndexed(RHIBuffer* indexBuffer, uint32_t indexCount, uint32_t instanceCount = 1, uint32_t firstIndex = 0, int32_t vertexOffset = 0, uint32_t firstInstance = 0) override;
    void BeginRenderPass(const RHIRenderPassInfo& renderPassInfo) override;
    void EndRenderPass() override;

    void SetBatchedShaderParameters(RHIRayTracingShader* shader, const RHIBatchedShaderParameters& parameter) override;
    void SetRayTracingPipelineState(RHIRayTracingPipelineState* pipelineState) override;

    virtual void BuildAccelerationStructure(RHIRayTracingAccelerationStructure* accelerationStructure) override;


    virtual void UpdateAccelerationStructure(RHIRayTracingAccelerationStructure* accelerationStructure) override;


    virtual void TraceRays(uint32_t width, uint32_t height, uint32_t depth = 1) override;

protected:
private:
    void InnerBegin() override;
    friend class VulkanCommandContext;
    VulkanPendingGfxState* m_PendingGfx = nullptr;
    VulkanPendingRayTracingState* m_PendingRayTracing = nullptr;
    VulkanLooseUniformDataUploader* m_LooseUniformDataUploader = nullptr;
};

struct VulkanCommandUpdateTexture : public RHICommandBase
{
    VulkanTexture* Texture;
    std::shared_ptr<VulkanStagingBuffer> Staging;
    VkBufferImageCopy CopyRegion{};

    VulkanCommandUpdateTexture(VulkanTexture* texture, std::shared_ptr<VulkanStagingBuffer> buffer, VkBufferImageCopy region)
        : Texture(texture), Staging(buffer), CopyRegion(region) {}

    void Execute(RHICommandListBase& cmdList) override {
        auto* vulkanContex = dynamic_cast<VulkanCommandContext*>(cmdList.GetCommandContex());
        VulkanCommandBuffer* cmdBuffer = vulkanContex->GetCommandBufferManager()->GetActiveCommandBuffer();

        VKFunc::CmdCopyBufferToImage(
            cmdBuffer->GetHandle(),
            Staging->GetHandle(),
            Texture->GetImage(),
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1,
            &CopyRegion
        );
        vulkanContex->GetDevice()->GetStagingManager()->ReleaseToCmdBuffer(cmdBuffer, Staging);
    }
};

struct VulkanCommandUpdateBuffer : public RHICommandBase
{
    VulkanBuffer* Buffer;
    std::shared_ptr<VulkanStagingBuffer> Staging;
    VkBufferCopy CopyRegion{};

    VulkanCommandUpdateBuffer(
        VulkanBuffer* buffer,
        std::shared_ptr<VulkanStagingBuffer> stagingBuffer,
        VkBufferCopy region)
        : Buffer(buffer), Staging(stagingBuffer), CopyRegion(region) {
    }

    void Execute(RHICommandListBase& cmdList) override
    {
        auto* vulkanContext = dynamic_cast<VulkanCommandContext*>(cmdList.GetCommandContex());

        VulkanCommandBuffer* cmdBuffer =
            vulkanContext->GetCommandBufferManager()->GetActiveCommandBuffer();

        VKFunc::CmdCopyBuffer(
            cmdBuffer->GetHandle(),
            Staging->GetHandle(),
            Buffer->GetHandle(),
            1,
            &CopyRegion
        );

        vulkanContext->GetDevice()->GetStagingManager()->ReleaseToCmdBuffer(cmdBuffer, Staging);
    }
};

struct VulkanCommandReadTexture : public RHICommandBase
{
    VulkanTexture* Texture;
    std::shared_ptr<VulkanStagingBuffer> Staging;
    VkBufferImageCopy CopyRegion{};

    VulkanCommandReadTexture(
        VulkanTexture* texture,
        std::shared_ptr<VulkanStagingBuffer> buffer,
        VkBufferImageCopy region)
        : Texture(texture)
        , Staging(buffer)
        , CopyRegion(region)
    {
    }

    void Execute(RHICommandListBase& cmdList) override
    {
        auto* vulkanContext =
            dynamic_cast<VulkanCommandContext*>(
                cmdList.GetCommandContex());

        VulkanCommandBuffer* cmdBuffer =
            vulkanContext
            ->GetCommandBufferManager()
            ->GetActiveCommandBuffer();
        VKFunc::CmdCopyImageToBuffer(
            cmdBuffer->GetHandle(),
            Texture->GetImage(),
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            Staging->GetHandle(),
            1,
            &CopyRegion
        );

        vulkanContext
            ->GetDevice()
            ->GetStagingManager()
            ->ReleaseToCmdBuffer(
                cmdBuffer,
                Staging);
    }
};

struct VulkanCommandReadBuffer : public RHICommandBase
{
    VulkanBuffer* Buffer;
    std::shared_ptr<VulkanStagingBuffer> Staging;
    VkBufferCopy CopyRegion{};

    VulkanCommandReadBuffer(
        VulkanBuffer* buffer,
        std::shared_ptr<VulkanStagingBuffer> stagingBuffer,
        VkBufferCopy region)
        : Buffer(buffer)
        , Staging(stagingBuffer)
        , CopyRegion(region)
    {
    }

    void Execute(RHICommandListBase& cmdList) override
    {
        auto* vulkanContext =
            dynamic_cast<VulkanCommandContext*>(
                cmdList.GetCommandContex());

        VulkanCommandBuffer* cmdBuffer =
            vulkanContext
            ->GetCommandBufferManager()
            ->GetActiveCommandBuffer();

        VKFunc::CmdCopyBuffer(
            cmdBuffer->GetHandle(),
            Buffer->GetHandle(),
            Staging->GetHandle(),
            1,
            &CopyRegion
        );

        vulkanContext
            ->GetDevice()
            ->GetStagingManager()
            ->ReleaseToCmdBuffer(
                cmdBuffer,
                Staging);
    }
};

} // namespace WR::RHIVulkan
