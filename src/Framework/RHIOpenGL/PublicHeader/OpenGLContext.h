#pragma once

#include "RHICommandContex.h"
#include "OpenGLQueue.h"
#include "glad/gl.h"

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>
#include <algorithm>

namespace RHIOpenGL
{
    // =========================================================================
    // UniformBufferCache
    // -----------------------------------------------------------------------------
    // 一个将多个 GL UBO 绑定共享到 1+ 个大 GPU buffer 不同区间的 ring buffer pool。
    //
    //  - 多个 binding 可以分到同 / 异 chunks 内的不同 offset / size 段；
    //  - 每个段在 GL 一侧用 glBindBufferRange(binding, chunk, offset, size)
    //    绑定到同名 GL_UNIFORM_BUFFER binding，整个 ring 不需要 per-binding buffer；
    //  - pipeline 切换或 context begin 时由调用方调 Reset()，cursor 归零、
    //    binding 映射清空；已分配的 GL chunks 保留以避免每帧 GPU 资源 churn。
    //
    // 不是 OpenGLCommandContext 内部专用，独立可用，方便单测。
    // =========================================================================
    class UniformBufferCache
    {
    public:
        explicit UniformBufferCache(uint32_t chunkCapacity = 64u * 1024u)
            : chunkCapacity_(chunkCapacity ? chunkCapacity : 64u * 1024u)
        {
        }

        ~UniformBufferCache()
        {
            for (GLuint chunk : chunks_)
            {
                if (chunk != 0)
                {
                    glDeleteBuffers(1, &chunk);
                }
            }
            chunks_.clear();
        }

        UniformBufferCache(const UniformBufferCache&) = delete;
        UniformBufferCache& operator=(const UniformBufferCache&) = delete;
        UniformBufferCache(UniformBufferCache&&) noexcept = default;
        UniformBufferCache& operator=(UniformBufferCache&&) noexcept = default;

        // 释放所有 binding 映射与 hint，cursor 归零；GL chunks 保留以备下一轮复用。
        // 调用方在 pipeline 切换 / context begin 时触发。
        void Reset()
        {
            bindings_.clear();
            uniformBufferHints_.clear();
            cursor_ = 0;
        }

        // 记录 binding 的 cbuffer 参考大小（来自 shader 反射的声明大小）。
        // 不分配 GPU 资源；仅作为后续 EnsureRange 的下界，确保段不会因实际数据
        // 偏小而被裁短，导致 GL 端看到的 UBO 长度小于 shader 期望。
        // 同 binding 多次调用取 max；size==0 表示清空该 binding 的 hint。
        void HintUniformBufferSize(uint32_t binding, uint32_t size)
        {
            if (size == 0)
            {
                uniformBufferHints_.erase(binding);
                return;
            }
            auto it = uniformBufferHints_.find(binding);
            if (it == uniformBufferHints_.end() || size > it->second)
            {
                uniformBufferHints_[binding] = size;
            }
        }

        uint32_t GetUniformBufferHint(uint32_t binding) const
        {
            auto it = uniformBufferHints_.find(binding);
            return (it == uniformBufferHints_.end()) ? 0u : it->second;
        }

        // 为 binding 分配（或在已存在且段大小足够时复用）至少 minSize 字节的段。
        // 成功返回 true；minSize 大于 chunk 容量时返回 false。
        bool EnsureRange(uint32_t binding, uint32_t minSize)
        {
            if (minSize == 0 && !HasUniformBufferHint(binding))
            {
                return true;
            }
            EnsureAlignmentQueried();
            // 与 hint 取大值：实际数据可能只有部分成员被 patch，没 patch 的部分
            // GL 端会读到旧值或 UB；如果 hint 大于等于声明大小，永远安全。
            const uint32_t hint = GetUniformBufferHint(binding);
            const uint32_t effectiveMin = std::max(minSize, hint);
            const uint32_t needed = AlignUp(effectiveMin, alignment_);
            if (needed == 0)
            {
                return false;
            }

            auto it = bindings_.find(binding);
            if (it != bindings_.end() && it->second.size >= needed)
            {
                return true; // 缓存命中且容量足够
            }

            if (!EnsureChunkFor(needed))
            {
                return false;
            }

            Entry entry;
            entry.chunkIndex = static_cast<uint32_t>(chunks_.size()) - 1u;
            entry.chunkOffset = cursor_;
            entry.size = needed;
            bindings_[binding] = entry;
            cursor_ += needed;
            return true;
        }

        // 把 size 字节从 data 拷到 binding 关联段内的 dstOffset 位置。
        // dstOffset + size 不能越出该段大小。
        void WriteSlice(uint32_t binding, uint32_t dstOffset, const void* data, uint32_t size)
        {
            if (size == 0)
            {
                return;
            }
            auto it = bindings_.find(binding);
            if (it == bindings_.end())
            {
                return;
            }
            const Entry& e = it->second;
            if (dstOffset > e.size || size > e.size - dstOffset)
            {
                return; // 越界保护
            }
            const GLuint chunk = chunks_[e.chunkIndex];
            glBindBuffer(GL_UNIFORM_BUFFER, chunk);
            glBufferSubData(GL_UNIFORM_BUFFER,
                static_cast<GLintptr>(e.chunkOffset + dstOffset),
                static_cast<GLsizeiptr>(size),
                data);
        }

        // 把 binding 关联段 glBindBufferRange 到 GL_UNIFORM_BUFFER 同名 binding。
        void Bind(uint32_t binding)
        {
            auto it = bindings_.find(binding);
            if (it == bindings_.end())
            {
                return;
            }
            const Entry& e = it->second;
            if (e.chunkIndex >= chunks_.size())
            {
                return;
            }
            const GLuint chunk = chunks_[e.chunkIndex];
            glBindBufferRange(GL_UNIFORM_BUFFER, binding, chunk,
                static_cast<GLintptr>(e.chunkOffset),
                static_cast<GLsizeiptr>(e.size));
        }

        bool HasBinding(uint32_t binding) const
        {
            return bindings_.find(binding) != bindings_.end();
        }

        bool HasUniformBufferHint(uint32_t binding) const
        {
            return uniformBufferHints_.find(binding) != uniformBufferHints_.end();
        }

    private:
        struct Entry
        {
            uint32_t chunkIndex = 0;
            uint32_t chunkOffset = 0;
            uint32_t size = 0;
        };

        static uint32_t AlignUp(uint32_t v, uint32_t a)
        {
            return ((v + a - 1u) / a) * a;
        }

        void EnsureAlignmentQueried()
        {
            if (alignmentQueried_)
            {
                return;
            }
            GLint align = 0;
            glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &align);
            alignment_ = (align > 0) ? static_cast<uint32_t>(align) : 256u;
            alignmentQueried_ = true;
        }

        // 确保当前 chunk（或新 chunk）有至少 neededSize 字节可用。
        // 失败 = neededSize > chunkCapacity_。
        bool EnsureChunkFor(uint32_t neededSize)
        {
            if (neededSize > chunkCapacity_)
            {
                return false;
            }
            if (!chunks_.empty() && cursor_ + neededSize <= chunkCapacity_)
            {
                return true;
            }
            GLuint newChunk = 0;
            glGenBuffers(1, &newChunk);
            glBindBuffer(GL_UNIFORM_BUFFER, newChunk);
            glBufferData(GL_UNIFORM_BUFFER,
                static_cast<GLsizeiptr>(chunkCapacity_),
                nullptr,
                GL_DYNAMIC_DRAW);
            chunks_.push_back(newChunk);
            cursor_ = 0;
            return true;
        }

        uint32_t chunkCapacity_;
        std::vector<GLuint> chunks_;
        uint32_t cursor_ = 0;
        uint32_t alignment_ = 0;
        bool alignmentQueried_ = false;
        std::unordered_map<uint32_t, Entry> bindings_;
        // cbuffer 参考大小（来自 shader 反射的声明大小），pipeline 切换时填入，
        // Reset() 时清空。不参与 GPU 分配，仅作 EnsureRange 的下界。
        std::unordered_map<uint32_t, uint32_t> uniformBufferHints_;
    };

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
        // Compute / Graphic 共用的资源绑定主体：上传 UBO、按 EType 分派
        // Texture / SRV / UAV / Sampler / UniformBuffer。shader 类型
        // （RHIGraphicShader vs RHIComputeShader）当前未被逻辑引用，由派生
        // 类在自己的 SetBatchedShaderParameters 包装一层 lambda 后转发到本函数。
        void ApplyShaderParameters(const RHI::RHIBatchedShaderParameters& parameter);
        OpenGLQueue* Queue = nullptr;
        UniformBufferCache UBOCache;
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
        void ApplyVertexDescState(const RHI::RHIGraphicsPipelineStateDesc& desc);
        void ApplyPrimitiveTopologyState(const RHI::RHIGraphicsPipelineStateDesc& desc);
        void ApplyRasterizerState(const RHI::RHIGraphicsPipelineStateDesc& desc);
        void ApplyDepthStencilState(const RHI::RHIGraphicsPipelineStateDesc& desc);
        void ApplyColorBlendState(const RHI::RHIGraphicsPipelineStateDesc& desc);
        RHI::RHIGraphicsPipelineState* CurrentPipelineState = nullptr;
        RHI::RHIBuffer* BoundVertexBuffers[8] = {};
        uint32_t BoundVertexBufferOffsets[8] = {};
        OpenGLFramebuffer* CurrentFramebuffer;
        GLuint CurrentVertexArray = 0;
        GLenum CurrentPrimitiveTopology = GL_TRIANGLES;
        bool bVertexLayoutDirty = true;
    };
}
