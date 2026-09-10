#pragma once

#include "RHICommandContex.h"
#include "OpenGLQueue.h"
#include <functional>
#include <unordered_map>
#include <vector>

namespace RHIOpenGL
{
    class OpenGLCommandContext : public virtual RHI::RHIContextBase
    {
    public:
        explicit OpenGLCommandContext(OpenGLQueue* inQueue = nullptr);
        ~OpenGLCommandContext() override = default;

        void Begin() override;
        void End() override;
        void BeginTransitions(std::vector<const RHI::RHITransition*> transitions) override;
        void EndTransitions(std::vector<const RHI::RHITransition*> transitions) override;
        void CopyTexture(RHI::RHITexture* src, RHI::RHITexture* dst, const RHI::RHICopyTextureDesc& copyDesc) override;
        void BlitTexture(RHI::RHITexture* src, RHI::RHITexture* dst, const RHI::RHIBlitTextureDesc& blitDesc) override;

        OpenGLQueue* GetQueue() const { return Queue; }
        const std::vector<std::function<void()>>& GetNativeCommands() const { return NativeCommands; }  
    protected:
        std::vector<std::function<void()>> NativeCommands;
        void UploadUniformBufferData(const RHI::RHIBatchedShaderParameters& parameter);
        OpenGLQueue* Queue = nullptr;
        std::unordered_map<uint32_t, GLuint> UniformBufferCache;
    };

    class OpenGLComputeContext : public OpenGLCommandContext, public RHI::RHIComputeContex
    {
    public:
        explicit OpenGLComputeContext(OpenGLQueue* inQueue = nullptr);
        ~OpenGLComputeContext() override = default;

        void SetComputePipelineState(RHI::RHIComputePipelineState* pipelineState) override;
        void SetBatchedShaderParameters(RHI::RHIComputeShader* shader, const RHI::RHIBatchedShaderParameters& parameter) override;
        void Dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) override;

    private:
        RHI::RHIComputePipelineState* CurrentPipelineState = nullptr;
    };

    class OpenGLGraphicContext : public OpenGLCommandContext, public RHI::RHIGraphicContex
    {
    public:
        explicit OpenGLGraphicContext(OpenGLQueue* inQueue = nullptr);
        ~OpenGLGraphicContext() override;

        void SetBatchedShaderParameters(RHI::RHIGraphicShader* shader, const RHI::RHIBatchedShaderParameters& parameter) override;
        void SetStreamSource(uint32_t streamIndex, RHI::RHIBuffer* vertexBuffer, uint32_t offset) override;
        void SetGraphicPipelineState(RHI::RHIGraphicsPipelineState* pipelineState) override;
        void SetViewport(float x, float y, float w, float h, float minDepth, float maxDepth) override;
        void SetScissor(int32_t x, int32_t y, uint32_t w, uint32_t h) override;
        void Draw(uint32_t vertexCount, uint32_t instanceCount = 1, uint32_t firstVertex = 0, uint32_t firstInstance = 0) override;
        void DrawIndexed(RHI::RHIBuffer* indexBuffer, uint32_t indexCount, uint32_t instanceCount = 1, uint32_t firstIndex = 0, int32_t vertexOffset = 0, uint32_t firstInstance = 0) override;
        void BeginRenderPass(const RHI::RHIRenderPassInfo& renderPassInfo) override;
        void EndRenderPass() override;
        void SetBatchedShaderParameters(RHI::RHIRayTracingShader* shader, const RHI::RHIBatchedShaderParameters& parameter) override;
        void SetRayTracingPipelineState(RHI::RHIRayTracingPipelineState* pipelineState) override;
        void BuildAccelerationStructure(RHI::RHIRayTracingAccelerationStructure* accelerationStructure) override;
        void UpdateAccelerationStructure(RHI::RHIRayTracingAccelerationStructure* accelerationStructure) override;
        void TraceRays(uint32_t width, uint32_t height, uint32_t depth = 1) override;

    private:
        RHI::RHIGraphicsPipelineState* CurrentPipelineState = nullptr;
        RHI::RHIBuffer* BoundVertexBuffers[8] = {};
        OpenGLFramebuffer* CurrentFramebuffer;
        GLuint CurrentVertexArray = 0;
        GLenum CurrentPrimitiveTopology = GL_TRIANGLES;
    };
}
