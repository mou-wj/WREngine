#pragma once
#include <cstdint>
#include <vector>
#include <memory>
#include <list>
#include "RHIShaderParameter.h"
#include "RHICommandContex.h"

namespace RHI {

    using RHICmdBuffer = uint64_t;

    class RHICommandListBase;
    class RHITransferCommandList;
    class RHIComputeCommandList;
    class RHIGraphicCommandList;
    class RHITransition;

    // -----------------------------
    // 命令基类。
    // -----------------------------
    struct RHI_API RHICommandBase
    {
        virtual ~RHICommandBase() = default;
        virtual void Execute(RHICommandListBase& cmdList) = 0;
    };

    // -----------------------------
    // 分派命令。
    // -----------------------------
    struct RHI_API RHICommandDispatch : public RHICommandBase
    {
        uint32_t X, Y, Z;
        RHICommandDispatch(uint32_t x, uint32_t y, uint32_t z);
        void Execute(RHICommandListBase& cmdList) override;
    };

    struct RHI_API RHICommandDraw : public RHICommandBase
    {
        uint32_t VertexCount, InstanceCount, FirstVertex, FirstInstance;
        RHICommandDraw(uint32_t v, uint32_t i, uint32_t fv, uint32_t fi);
        void Execute(RHICommandListBase& cmdList) override;
    };

    struct RHI_API RHICommandDrawIndexed : public RHICommandBase
    {
        RHIBuffer* IndexBuffer;
        uint32_t IndexCount, FirstIndex, InstanceCount, FirstInstance;
        int32_t VertexOffset;
        RHICommandDrawIndexed(RHIBuffer* indexBuffer, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance);
        void Execute(RHICommandListBase& cmdList) override;
    };

    struct RHI_API RHICommandTraceRays : public RHICommandBase
    {
        uint32_t Width, Height, Depth;
        RHICommandTraceRays(uint32_t w, uint32_t h, uint32_t d);
        void Execute(RHICommandListBase& cmdList) override;
    };

    struct RHI_API RHICommandBegin : public RHICommandBase
    {
        RHICommandBegin();
        void Execute(RHICommandListBase& cmdList) override;
    };
    struct RHI_API RHICommandEnd : public RHICommandBase
    {
        RHICommandEnd();
        void Execute(RHICommandListBase& cmdList) override;
    };
    struct RHI_API RHICommandBeginTransitions : public RHICommandBase
    {
        std::vector<const RHITransition*> Transitions;
        RHICommandBeginTransitions(std::vector<const RHITransition*> inTransitions);
        void Execute(RHICommandListBase& cmdList) override;
    };
    struct RHI_API RHICommandEndTransitions : public RHICommandBase
    {
        std::vector<const RHITransition*> Transitions;
        RHICommandEndTransitions(std::vector<const RHITransition*> inTransitions);
        void Execute(RHICommandListBase& cmdList) override;
    };

    struct RHI_API RHICommandCopyTexture : public RHICommandBase
    {
        RHITexture* Src = nullptr;
        RHITexture* Dst = nullptr;
        RHICopyTextureDesc CopyDesc{};
        RHICommandCopyTexture(RHITexture* src, RHITexture* dst, const RHICopyTextureDesc& copyDesc);
        void Execute(RHICommandListBase& cmdList) override;
    };
    struct RHI_API RHICommandBlitTexture : public RHICommandBase
    {
        RHITexture* Src = nullptr;
        RHITexture* Dst = nullptr;
        RHIBlitTextureDesc BlitDesc{};
        RHICommandBlitTexture(RHITexture* src, RHITexture* dst, const RHIBlitTextureDesc& blitDesc);
        void Execute(RHICommandListBase& cmdList) override;
    };
    struct RHI_API RHICommandUpdateTexture : public RHICommandBase
    {
        RHITexture* Texture;
        const void* Data;
        RHIUpdateTextureRegion Region;
        RHICommandUpdateTexture(RHITexture* src, const void* data, const RHIUpdateTextureRegion& region);
        void Execute(RHICommandListBase& cmdList) override;
    };

    struct RHI_API RHICommandUpdateBuffer : public RHICommandBase
    {
        RHIBuffer* Buffer;
        const void* Data;
        RHIUpdateBufferRegion Region;
        RHICommandUpdateBuffer(RHIBuffer* buffer, const void* data, const RHIUpdateBufferRegion& region);
        void Execute(RHICommandListBase& cmdList) override;
    };

    struct RHI_API RHICommandSetComputePipelineState : public RHICommandBase
    {
        RHIComputePipelineState* PipelineState = nullptr;
        explicit RHICommandSetComputePipelineState(RHIComputePipelineState* pipelineState);
        void Execute(RHICommandListBase& cmdList) override;
    };

    struct RHI_API RHICommandSetComputeShaderParameters : public RHICommandBase
    {
        RHIComputeShader* Shader = nullptr;
        RHIBatchedShaderParameters Parameters{};
        RHICommandSetComputeShaderParameters(RHIComputeShader* shader, const RHIBatchedShaderParameters& parameters);
        void Execute(RHICommandListBase& cmdList) override;
    };

    struct RHI_API RHICommandSetGraphicShaderParameters : public RHICommandBase
    {
        RHIGraphicShader* Shader = nullptr;
        RHIBatchedShaderParameters Parameters{};
        RHICommandSetGraphicShaderParameters(RHIGraphicShader* shader, const RHIBatchedShaderParameters& parameters);
        void Execute(RHICommandListBase& cmdList) override;
    };

    struct RHI_API RHICommandSetRayTracingShaderParameters : public RHICommandBase
    {
        RHIRayTracingShader* Shader = nullptr;
        RHIBatchedShaderParameters Parameters{};
        RHICommandSetRayTracingShaderParameters(RHIRayTracingShader* shader, const RHIBatchedShaderParameters& parameters);
        void Execute(RHICommandListBase& cmdList) override;
    };

    struct RHI_API RHICommandSetStreamSource : public RHICommandBase
    {
        uint32_t StreamIndex = 0;
        RHIBuffer* VertexBuffer;
        uint32_t Offset = 0;
        RHICommandSetStreamSource(uint32_t streamIndex, RHIBuffer* vertexBuffer, uint32_t offset);
        void Execute(RHICommandListBase& cmdList) override;
    };

    struct RHI_API RHICommandSetGraphicPipelineState : public RHICommandBase
    {
        RHIGraphicsPipelineState* PipelineState = nullptr;
        explicit RHICommandSetGraphicPipelineState(RHIGraphicsPipelineState* pipelineState);
        void Execute(RHICommandListBase& cmdList) override;
    };

    struct RHI_API RHICommandSetViewport : public RHICommandBase
    {
        float X = 0.0f;
        float Y = 0.0f;
        float W = 0.0f;
        float H = 0.0f;
        float MinDepth = 0.0f;
        float MaxDepth = 1.0f;
        RHICommandSetViewport(float x, float y, float w, float h, float minDepth, float maxDepth);
        void Execute(RHICommandListBase& cmdList) override;
    };

    struct RHI_API RHICommandSetScissor : public RHICommandBase
    {
        int32_t X = 0;
        int32_t Y = 0;
        uint32_t W = 0;
        uint32_t H = 0;
        RHICommandSetScissor(int32_t x, int32_t y, uint32_t w, uint32_t h);
        void Execute(RHICommandListBase& cmdList) override;
    };

    struct RHI_API RHICommandBeginRenderPass : public RHICommandBase
    {
        RHIRenderPassInfo RenderPassInfo{};
        explicit RHICommandBeginRenderPass(const RHIRenderPassInfo& renderPassInfo);
        void Execute(RHICommandListBase& cmdList) override;
    };

    struct RHI_API RHICommandEndRenderPass : public RHICommandBase
    {
        void Execute(RHICommandListBase& cmdList) override;
    };

    struct RHI_API RHICommandSetRayTracingPipelineState : public RHICommandBase
    {
        RHIRayTracingPipelineState* PipelineState = nullptr;
        explicit RHICommandSetRayTracingPipelineState(RHIRayTracingPipelineState* pipelineState);
        void Execute(RHICommandListBase& cmdList) override;
    };

    struct RHI_API RHICommandBuildAccelerationStructure : public RHICommandBase
    {
        RHIRayTracingAccelerationStructure* AccelerationStructure = nullptr;
        explicit RHICommandBuildAccelerationStructure(RHIRayTracingAccelerationStructure* accelerationStructure);
        void Execute(RHICommandListBase& cmdList) override;
    };

    struct RHI_API RHICommandUpdateAccelerationStructure : public RHICommandBase
    {
        RHIRayTracingAccelerationStructure* AccelerationStructure = nullptr;
        explicit RHICommandUpdateAccelerationStructure(RHIRayTracingAccelerationStructure* accelerationStructure);
        void Execute(RHICommandListBase& cmdList) override;
    };


    // -----------------------------
    // 统一命令列表基类。
    // -----------------------------
    class RHI_API RHICommandListBase
    {
    public:
        explicit RHICommandListBase(RHIContextBase* context);
        virtual ~RHICommandListBase();

        // 记录命令，立即模式下直接执行。
        template<typename T, typename... Args>
        void AddCommand(Args&&... args)
        {
            if (_bImmediate)
            {
                T cmd(std::forward<Args>(args)...);
                cmd.Execute(*this);
            }
            else
            {
                _commands.emplace_back(std::make_shared<T>(std::forward<Args>(args)...));
            }
        }

        // 执行已记录的全部命令。
        virtual void ExecuteAll();
        void Clear();

        void SetImmediate(bool bImmediate);
        bool IsImmediate() const;

        RHIContextBase* GetContext() const;
        // Backward-compatible accessor name.
        RHIContextBase* GetCommandContex() const;

        // -----------------
        // 通用命令接口。
        // -----------------
        void Begin();
        void End();

        void BeginTransitions(std::vector<const RHITransition*> transitions);
        void EndTransitions(std::vector<const RHITransition*> transitions);
        void CopyTexture(RHITexture* src, RHITexture* dst, const RHICopyTextureDesc& copyDesc);
        void BlitTexture(RHITexture* src, RHITexture* dst, const RHIBlitTextureDesc& blitDesc);

    protected:
        bool _bImmediate = false;
        std::list<std::shared_ptr<RHICommandBase>> _commands;
        RHIContextBase* _context = nullptr;

        friend struct RHICommandDispatch;
        friend struct RHICommandDraw;
        friend struct RHICommandTraceRays;
    };
    class RHI_API RHIComputeCommandList : public RHICommandListBase
    {
    public:
        explicit RHIComputeCommandList(RHIComputeContex* context);
        RHIComputeContex* GetComputeContext() const;
        void SetComputePipelineState(RHIComputePipelineState* pipelineState);
        void SetBatchedShaderParameters(RHIComputeShader* shader, const RHIBatchedShaderParameters& batchedShaderParameter);
        void Dispatch(uint32_t x, uint32_t y, uint32_t z);
    };

    class RHI_API RHIGraphicCommandList : public RHICommandListBase
    {
    public:
        explicit RHIGraphicCommandList(RHIGraphicContex* context);
        RHIGraphicContex* GetGraphicContext() const;

        void SetBatchedShaderParameters(RHIGraphicShader* shader, const RHIBatchedShaderParameters& batchedShaderParameter);
        void SetBatchedShaderParameters(RHIRayTracingShader* shader, const RHIBatchedShaderParameters& batchedShaderParameter);
        void SetStreamSource(uint32_t streamIndex, RHIBuffer* vertexBuffer, uint32_t offset);
        void SetGraphicPipelineState(RHIGraphicsPipelineState* pipelineState);
        void SetViewport(float x, float y, float w, float h, float minDepth, float maxDepth);
        void SetScissor(int32_t x, int32_t y, uint32_t w, uint32_t h);
        void Draw(uint32_t vertexCount, uint32_t instanceCount = 1,
            uint32_t firstVertex = 0, uint32_t firstInstance = 0);
        void DrawIndexed(RHIBuffer* indexBuffer, uint32_t indexCount = 1, uint32_t instanceCount = 1, uint32_t firstIndex = 0, int32_t vertexOffset = 0, uint32_t firstInstance = 0);

        void BeginRenderPass(const RHIRenderPassInfo& renderPassInfo);
        void EndRenderPass();
        void SetRayTracingPipelineState(RHIRayTracingPipelineState* pipelineState);
        void BuildAccelerationStructure(RHIRayTracingAccelerationStructure* accelerationStructure);
        void UpdateAccelerationStructure(RHIRayTracingAccelerationStructure* accelerationStructure);
        void TraceRays(uint32_t width, uint32_t height, uint32_t depth = 1);
    };


} // namespace WR::RHI