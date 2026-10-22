#pragma once

#include "RHIResource.h"
#include "RHIShaderParameter.h"
#include "RHITransition.h"

namespace RHI
{
    using RHICmdBuffer = uint64_t;

    class RHI_API RHIContextBase
    {
    public:
        virtual ~RHIContextBase() = default;
        virtual void Begin() = 0;
        virtual void End() = 0;
        virtual void BeginTransitions(std::vector<const RHITransition*> transitions) = 0;
        virtual void EndTransitions(std::vector<const RHITransition*> transitions) = 0;
        virtual void CopyTexture(RHITexture* src, RHITexture* dst, const RHICopyTextureDesc& copyDesc) = 0;
        // 新增：BlitTexture接口
        virtual void BlitTexture(RHITexture* src, RHITexture* dst, const RHIBlitTextureDesc& blitDesc) = 0;
    };

    class RHI_API RHIComputeContex : public virtual RHIContextBase
    {
    public:
        virtual ~RHIComputeContex() = default;
        // ========================
        // 计算命令接口。
        // ========================
        virtual void SetComputePipelineState(RHIComputePipelineState* pipelineState) = 0;
        virtual void SetBatchedShaderParameters(RHIComputeShader* shader, const RHIBatchedShaderParameters& parameter) = 0;
        virtual void Dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) = 0;
    };

    class RHI_API RHIGraphicContex : public virtual RHIContextBase
    {
    public:
        virtual ~RHIGraphicContex() = default;
        virtual void SetBatchedShaderParameters(RHIGraphicShader* shader, const RHIBatchedShaderParameters& parameter) = 0;

        // ========================
        // 图形命令接口。
        // ========================
        virtual void SetStreamSource(uint32_t streamIndex, RHIBuffer* vertexBuffer, uint32_t offset) = 0;
        virtual void SetGraphicPipelineState(RHIGraphicsPipelineState* pipelineState) = 0;
        virtual void SetViewport(float x, float y, float w, float h, float minDepth, float maxDepth) = 0;
        virtual void SetScissor(int32_t x, int32_t y, uint32_t w, uint32_t h) = 0;
        virtual void Draw(uint32_t vertexCount, uint32_t instanceCount = 1,
            uint32_t firstVertex = 0, uint32_t firstInstance = 0) = 0;
        virtual void DrawIndexed(RHIBuffer* indexBuffer, uint32_t indexCount,
            uint32_t instanceCount = 1, uint32_t firstIndex = 0,
            int32_t vertexOffset = 0, uint32_t firstInstance = 0) = 0;

        virtual void BeginRenderPass(const RHIRenderPassInfo& renderPassInfo) = 0;
        virtual void EndRenderPass() = 0;

        // ========================
        // 光线追踪命令接口。
        // ========================
        virtual void SetBatchedShaderParameters(RHIRayTracingShader* shader, const RHIBatchedShaderParameters& parameter) = 0;
        virtual void SetRayTracingPipelineState(
            RHIRayTracingPipelineState* pipelineState) = 0;

        virtual void BuildAccelerationStructure(
            RHIRayTracingAccelerationStructure* accelerationStructure) = 0;


        virtual void UpdateAccelerationStructure(
            RHIRayTracingAccelerationStructure* accelerationStructure) = 0;


        virtual void TraceRays(
            uint32_t width,
            uint32_t height,
            uint32_t depth = 1) = 0;
    };


    // Backward-compatible aliases.
    using RHIComputeContext = RHIComputeContex;



    using RHIContextBaseSP = std::shared_ptr<RHIContextBase>;
    using RHIComputeContextSP = std::shared_ptr<RHIComputeContext>;
    using RHIGraphicContextSP = std::shared_ptr<RHIGraphicContex>;



enum class ERHIPipelineStage
{
    None = 0,
    TopOfPipe,
    DrawIndirect,
    VertexInput,
    VertexShader,
    FragmentShader,
    EarlyFragmentTests,
    LateFragmentTests,
    ColorAttachmentOutput,
    ComputeShader,
    Transfer,
    BottomOfPipe,
    AllCommands
};

class RHISyncPoint {
public:
    virtual ~RHISyncPoint() = default;

    // 获取当前 GPU 已经执行到的数值（用于 CPU 端的进度查询或 GC）
    virtual uint64_t GetCurrentValue() = 0;

    // CPU 端阻塞等待直到达到某个值
    virtual void Wait(uint64_t value, uint64_t timeoutNs = UINT64_MAX) = 0;

    // 获取所属队列类型
    EQueueType GetQueueType() const { return _type; }

protected:
    EQueueType _type;
};

struct RHIWaitInfo {
    // 依赖哪一个同步点（即哪个队列）
    RHISyncPoint* SyncPoint = nullptr;
    
    //如果SyncPoint为空，则依赖队列的当前进度
    EQueueType QueueType = EQueueType::Graphics;

    // 依赖该同步点的哪一个具体进度
    uint64_t Value = 0;
    
    // 在本队列的哪个阶段开始等待
    ERHIPipelineStage WaitStage = ERHIPipelineStage::AllCommands;
};

class RHISwapchain {
public:
    virtual ~RHISwapchain() = default;

    struct RHISwapchainSlot {
    RHITexture* Texture;    // 物理资源
    RHISyncPoint* ReadySync; // 准入证
    };

    // 关键：获取当前帧可以写入的纹理（不透明的 RHITexture）
    virtual RHISwapchainSlot AcquireNextSlot() = 0;
    
    // 窗口缩放处理
    virtual void Resize(uint32_t width, uint32_t height) = 0;

protected:

};

// Present capability, separate from queue submission.
// Implement on objects that own a present-capable surface (e.g. a graphics queue).
class RHI_API RHIPresentExecutor
{
public:
    virtual ~RHIPresentExecutor() = default;
    virtual void Present(RHISwapchain* swapchain, const RHIWaitInfo& waitInfo) = 0;
};


class RHIQueue {
public:
    virtual ~RHIQueue() = default;

    virtual EQueueType GetType() const  = 0;

    template<typename ContextType>
    ContextType* AcquireCastedCommandContext() {
		RHIContextBase* baseCtx = AcquireCommandContext();
		return dynamic_cast<ContextType*>(baseCtx);
    };

    virtual RHIContextBase* AcquireCommandContext() = 0;
    virtual RHIContextBase* ReleaseCommandContext(RHIContextBase* context) = 0;

    // 提交指令包，并返回完成点与可供后续等待的依赖对象
    virtual uint64_t ExecuteContext(RHIContextBase* context) = 0;

    // 批量提交并处理跨队列等待
    virtual uint64_t ExecuteContext(const std::vector<RHIContextBase*>& cmds,
                               const std::vector<RHIWaitInfo>& waitInfos) = 0;
    virtual void WaitValue(uint64_t fenceValue) = 0;
    // 强制刷新硬件队列
    virtual void WaitIdle() = 0;

	virtual uint64_t GetCurrentTimelineValue() = 0;

    virtual RHISyncPoint* GetSyncPoint() = 0;
private:
};

using RHISwapchainSP = std::shared_ptr<RHISwapchain>;


} // namespace WR::RHI