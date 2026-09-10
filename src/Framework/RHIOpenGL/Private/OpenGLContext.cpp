#include "OpenGLContext.h"
#include "OpenGLPipelineState.h"
#include "OpenGLResource.h"
#include "OpenGLRenderTarget.h"
#include "glad/gl.h"

namespace RHIOpenGL
{
    void OpenGLCommandContext::UploadUniformBufferData(const RHI::RHIBatchedShaderParameters& parameter)
    {
        OpenGLCommandContext& context = *this;
        if (parameter.UniformParameters.empty() || parameter.Data.empty())
        {
            return;
        }

        std::unordered_map<uint32_t, size_t> maxSizes;
        for (const auto& uniformParam : parameter.UniformParameters)
        {
            if (uniformParam.Offset + uniformParam.Size > parameter.Data.size())
            {
                continue;
            }

            const size_t requiredSize = static_cast<size_t>(uniformParam.Offset) + static_cast<size_t>(uniformParam.Size);
            auto it = maxSizes.find(uniformParam.BufferIndex);
            if (it == maxSizes.end() || requiredSize > it->second)
            {
                maxSizes[uniformParam.BufferIndex] = requiredSize;
            }
        }

        for (const auto& [bufferIndex, totalSize] : maxSizes)
        {
            GLuint bufferHandle = 0;
            auto found = context.UniformBufferCache.find(bufferIndex);
            if (found == context.UniformBufferCache.end())
            {
                glGenBuffers(1, &bufferHandle);
                context.UniformBufferCache[bufferIndex] = bufferHandle;
            }
            else
            {
                bufferHandle = found->second;
            }

            glBindBuffer(GL_UNIFORM_BUFFER, bufferHandle);
            glBufferData(GL_UNIFORM_BUFFER, static_cast<GLsizeiptr>(totalSize), parameter.Data.data(), GL_DYNAMIC_DRAW);
            glBindBufferRange(GL_UNIFORM_BUFFER, bufferIndex, bufferHandle, 0, static_cast<GLintptr>(totalSize));
        }
    }

    OpenGLCommandContext::OpenGLCommandContext(OpenGLQueue* inQueue)
        : Queue(inQueue)
    {
    }

    void OpenGLCommandContext::Begin()
    {
        NativeCommands.clear();
    }

    void OpenGLCommandContext::End()
    {
        // no-op: command list is recorded in NativeCommands and will be executed later
    }

    void OpenGLCommandContext::BeginTransitions(std::vector<const RHI::RHITransition*> transitions)
    {
        (void)transitions;
    }

    void OpenGLCommandContext::EndTransitions(std::vector<const RHI::RHITransition*> transitions)
    {
        (void)transitions;
    }

    void OpenGLCommandContext::CopyTexture(RHI::RHITexture* src, RHI::RHITexture* dst, const RHI::RHICopyTextureDesc& copyDesc)
    {
        NativeCommands.emplace_back([src, dst, copyDesc]()
        {
            auto* glSrc = dynamic_cast<OpenGLTexture*>(src);
            auto* glDst = dynamic_cast<OpenGLTexture*>(dst);
            if (!glSrc || !glDst || copyDesc.LayerCount == 0 ||
                copyDesc.SrcRegion.Width == 0 || copyDesc.SrcRegion.Height == 0 || copyDesc.SrcRegion.Depth == 0)
            {
                return;
            }

            const auto& srcDesc = glSrc->GetDesc();
            const auto& dstDesc = glDst->GetDesc();
            if (copyDesc.SrcMipIndex >= srcDesc.MipLevels || copyDesc.DstMipIndex >= dstDesc.MipLevels ||
                copyDesc.SrcArraySlice != 0 || copyDesc.DstArraySlice != 0 ||
                copyDesc.SrcRegion.OffsetX < 0 || copyDesc.SrcRegion.OffsetY < 0 || copyDesc.SrcRegion.OffsetZ < 0 ||
                copyDesc.DstRegion.OffsetX < 0 || copyDesc.DstRegion.OffsetY < 0 || copyDesc.DstRegion.OffsetZ < 0)
            {
                return;
            }

            glCopyImageSubData(glSrc->GetHandle(), GL_TEXTURE_2D,
                static_cast<GLint>(copyDesc.SrcMipIndex), copyDesc.SrcRegion.OffsetX,
                copyDesc.SrcRegion.OffsetY, copyDesc.SrcRegion.OffsetZ,
                glDst->GetHandle(), GL_TEXTURE_2D,
                static_cast<GLint>(copyDesc.DstMipIndex), copyDesc.DstRegion.OffsetX,
                copyDesc.DstRegion.OffsetY, copyDesc.DstRegion.OffsetZ,
                static_cast<GLsizei>(copyDesc.SrcRegion.Width),
                static_cast<GLsizei>(copyDesc.SrcRegion.Height),
                static_cast<GLsizei>(copyDesc.SrcRegion.Depth));
        });
    }

    void OpenGLCommandContext::BlitTexture(RHI::RHITexture* src, RHI::RHITexture* dst, const RHI::RHIBlitTextureDesc& blitDesc)
    {
        NativeCommands.emplace_back([src, dst, blitDesc]()
        {
            auto* glSrc = dynamic_cast<OpenGLTexture*>(src);
            auto* glDst = dynamic_cast<OpenGLTexture*>(dst);
            if (!glSrc || !glDst || blitDesc.LayerCount == 0 ||
                blitDesc.SrcRegion.Width == 0 || blitDesc.SrcRegion.Height == 0 ||
                blitDesc.DstRegion.Width == 0 || blitDesc.DstRegion.Height == 0)
            {
                return;
            }

            GLuint readFramebuffer = 0;
            GLuint drawFramebuffer = 0;
            glGenFramebuffers(1, &readFramebuffer);
            glGenFramebuffers(1, &drawFramebuffer);

            glBindFramebuffer(GL_READ_FRAMEBUFFER, readFramebuffer);
            glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                glSrc->GetHandle(), static_cast<GLint>(blitDesc.SrcMipIndex));
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, drawFramebuffer);
            glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                glDst->GetHandle(), static_cast<GLint>(blitDesc.DstMipIndex));

            const GLenum filter = blitDesc.Filter == RHI::ERHIFilter::Nearest ? GL_NEAREST : GL_LINEAR;
            glBlitFramebuffer(
                blitDesc.SrcRegion.OffsetX, blitDesc.SrcRegion.OffsetY,
                blitDesc.SrcRegion.OffsetX + static_cast<int32_t>(blitDesc.SrcRegion.Width),
                blitDesc.SrcRegion.OffsetY + static_cast<int32_t>(blitDesc.SrcRegion.Height),
                blitDesc.DstRegion.OffsetX, blitDesc.DstRegion.OffsetY,
                blitDesc.DstRegion.OffsetX + static_cast<int32_t>(blitDesc.DstRegion.Width),
                blitDesc.DstRegion.OffsetY + static_cast<int32_t>(blitDesc.DstRegion.Height),
                GL_COLOR_BUFFER_BIT, filter);

            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glDeleteFramebuffers(1, &readFramebuffer);
            glDeleteFramebuffers(1, &drawFramebuffer);
        });
    }

    OpenGLComputeContext::OpenGLComputeContext(OpenGLQueue* inQueue)
        : OpenGLCommandContext(inQueue)
    {
    }

    void OpenGLComputeContext::SetComputePipelineState(RHI::RHIComputePipelineState* pipelineState)
    {
        NativeCommands.emplace_back([this, pipelineState]()
        {
            auto* glPipeline = dynamic_cast<OpenGLComputePipelineState*>(pipelineState);
            if (!glPipeline)
            {
                return;
            }

            CurrentPipelineState = pipelineState;
            glUseProgram(glPipeline->GetProgramHandle());
        });
    }

    void OpenGLComputeContext::SetBatchedShaderParameters(RHI::RHIComputeShader* shader, const RHI::RHIBatchedShaderParameters& parameter)
    {
        NativeCommands.emplace_back([this, shader, parameter]()
        {
            (void)shader;
            UploadUniformBufferData(parameter);

            for (const auto& resourceParam : parameter.ResourceParameters)
            {
                switch (resourceParam.Type)
                {
                case RHI::RHIShaderResourceParameter::EType::Texture:
                case RHI::RHIShaderResourceParameter::EType::SRV:
                {
                    auto* texture = dynamic_cast<OpenGLTexture*>(resourceParam.GetResourceAs<RHI::RHITexture>());
                    if (texture)
                    {
                        glBindTextureUnit(resourceParam.Index, texture->GetHandle());
                        glBindSampler(resourceParam.Index, 0);
                    }
                    break;
                }
                case RHI::RHIShaderResourceParameter::EType::Sampler:
                {
                    auto* sampler = dynamic_cast<OpenGLSampler*>(resourceParam.GetResourceAs<RHI::RHISampler>());
                    if (sampler)
                    {
                        glBindSampler(resourceParam.Index, sampler->GetHandle());
                    }
                    break;
                }
                case RHI::RHIShaderResourceParameter::EType::UniformBuffer:
                {
                    auto* buffer = dynamic_cast<OpenGLBuffer*>(resourceParam.GetResourceAs<RHI::RHIBuffer>());
                    if (buffer)
                    {
                        glBindBufferBase(GL_UNIFORM_BUFFER, resourceParam.Index, buffer->GetHandle());
                    }
                    break;
                }
                default:
                    break;
                }
            }
        });
    }

    void OpenGLComputeContext::Dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
    {
        NativeCommands.emplace_back([groupCountX, groupCountY, groupCountZ]()
        {
            glDispatchCompute(groupCountX, groupCountY, groupCountZ);
        });
    }

    OpenGLGraphicContext::OpenGLGraphicContext(OpenGLQueue* inQueue)
        : OpenGLCommandContext(inQueue),CurrentFramebuffer(new OpenGLFramebuffer())
    {
        
    }
    OpenGLGraphicContext::~OpenGLGraphicContext() {

    }
    void OpenGLGraphicContext::SetBatchedShaderParameters(RHI::RHIGraphicShader* shader, const RHI::RHIBatchedShaderParameters& parameter)
    {
        NativeCommands.emplace_back([this, shader, parameter]()
        {
            (void)shader;
            UploadUniformBufferData(parameter);

            for (const auto& resourceParam : parameter.ResourceParameters)
            {
                switch (resourceParam.Type)
                {
                case RHI::RHIShaderResourceParameter::EType::Texture:
                case RHI::RHIShaderResourceParameter::EType::SRV:
                {
                    auto* texture = dynamic_cast<OpenGLTexture*>(resourceParam.GetResourceAs<RHI::RHITexture>());
                    if (texture)
                    {
                        glBindTextureUnit(resourceParam.Index, texture->GetHandle());
                        glBindSampler(resourceParam.Index, 0);
                    }
                    break;
                }
                case RHI::RHIShaderResourceParameter::EType::Sampler:
                {
                    auto* sampler = dynamic_cast<OpenGLSampler*>(resourceParam.GetResourceAs<RHI::RHISampler>());
                    if (sampler)
                    {
                        glBindSampler(resourceParam.Index, sampler->GetHandle());
                    }
                    break;
                }
                case RHI::RHIShaderResourceParameter::EType::UniformBuffer:
                {
                    auto* buffer = dynamic_cast<OpenGLBuffer*>(resourceParam.GetResourceAs<RHI::RHIBuffer>());
                    if (buffer)
                    {
                        glBindBufferBase(GL_UNIFORM_BUFFER, resourceParam.Index, buffer->GetHandle());
                    }
                    break;
                }
                default:
                    break;
                }
            }
        });
    }

    void OpenGLGraphicContext::SetStreamSource(uint32_t streamIndex, RHI::RHIBuffer* vertexBuffer, uint32_t offset)
    {
        NativeCommands.emplace_back([this, streamIndex, vertexBuffer, offset]()
        {
            if (streamIndex >= 8)
            {
                return;
            }

            BoundVertexBuffers[streamIndex] = vertexBuffer;
            if (CurrentVertexArray == 0)
            {
                glGenVertexArrays(1, &CurrentVertexArray);
            }

            glBindVertexArray(CurrentVertexArray);

            if (vertexBuffer)
            {
                auto* glBuffer = dynamic_cast<OpenGLBuffer*>(vertexBuffer);
                if (glBuffer)
                {
                    glBindBuffer(GL_ARRAY_BUFFER, glBuffer->GetHandle());
                    glEnableVertexAttribArray(streamIndex);
                    glVertexAttribPointer(streamIndex, 3, GL_FLOAT, GL_FALSE, 0, reinterpret_cast<void*>(static_cast<uintptr_t>(offset)));
                }
            }
        });
    }

    void OpenGLGraphicContext::SetGraphicPipelineState(RHI::RHIGraphicsPipelineState* pipelineState)
    {
        NativeCommands.emplace_back([this, pipelineState]()
        {
            auto* glPipeline = dynamic_cast<OpenGLGraphicsPipelineState*>(pipelineState);
            if (!glPipeline)
            {
                return;
            }

            CurrentPipelineState = pipelineState;
            if (CurrentVertexArray == 0)
            {
                glGenVertexArrays(1, &CurrentVertexArray);
            }

            glBindVertexArray(CurrentVertexArray);
            glUseProgram(glPipeline->GetProgramHandle());

            const auto& desc = pipelineState->GetDesc();
            switch (desc.primitiveTopology)
            {
            case RHI::EPrimitiveTopology::PointList:
                CurrentPrimitiveTopology = GL_POINTS;
                break;
            case RHI::EPrimitiveTopology::LineList:
                CurrentPrimitiveTopology = GL_LINES;
                break;
            case RHI::EPrimitiveTopology::LineStrip:
                CurrentPrimitiveTopology = GL_LINE_STRIP;
                break;
            case RHI::EPrimitiveTopology::TriangleStrip:
                CurrentPrimitiveTopology = GL_TRIANGLE_STRIP;
                break;
            default:
                CurrentPrimitiveTopology = GL_TRIANGLES;
                break;
            }

            if (desc.rasterizerState)
            {
                const auto& rasterizer = desc.rasterizerState->GetDesc();
                if (rasterizer.cullMode == RHI::ERHICullMode::Back)
                {
                    glEnable(GL_CULL_FACE);
                    glCullFace(GL_BACK);
                }
                else if (rasterizer.cullMode == RHI::ERHICullMode::Front)
                {
                    glEnable(GL_CULL_FACE);
                    glCullFace(GL_FRONT);
                }
                else
                {
                    glDisable(GL_CULL_FACE);
                }

                if (rasterizer.frontFace == RHI::ERHIFrontFace::Clockwise)
                {
                    glFrontFace(GL_CW);
                }
                else
                {
                    glFrontFace(GL_CCW);
                }
            }
            else
            {
                glDisable(GL_CULL_FACE);
            }

            if (desc.depthStencilState)
            {
                const auto& depthStencil = desc.depthStencilState->GetDesc();
                if (depthStencil.depthTestEnable)
                {
                    glEnable(GL_DEPTH_TEST);
                    glDepthFunc(GL_LESS);
                }
                else
                {
                    glDisable(GL_DEPTH_TEST);
                }

                if (depthStencil.depthWriteEnable)
                {
                    glDepthMask(GL_TRUE);
                }
                else
                {
                    glDepthMask(GL_FALSE);
                }
            }
            else
            {
                glDisable(GL_DEPTH_TEST);
            }

            if (desc.colorBlendState && !desc.colorBlendState->GetDesc().attachments.empty())
            {
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            }
            else
            {
                glDisable(GL_BLEND);
            }
        });
    }

    void OpenGLGraphicContext::SetViewport(float x, float y, float w, float h, float minDepth, float maxDepth)
    {
        NativeCommands.emplace_back([x, y, w, h, minDepth, maxDepth]()
        {
            glViewport(static_cast<GLint>(x), static_cast<GLint>(y), static_cast<GLsizei>(w), static_cast<GLsizei>(h));
            glDepthRangef(minDepth, maxDepth);
        });
    }

    void OpenGLGraphicContext::SetScissor(int32_t x, int32_t y, uint32_t w, uint32_t h)
    {
        NativeCommands.emplace_back([x, y, w, h]()
        {
            glScissor(x, y, static_cast<GLsizei>(w), static_cast<GLsizei>(h));
        });
    }

    void OpenGLGraphicContext::Draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance)
    {
        NativeCommands.emplace_back([this, vertexCount, instanceCount, firstVertex, firstInstance]()
        {
            if (CurrentVertexArray != 0)
            {
                glBindVertexArray(CurrentVertexArray);
            }

            glDrawArrays(CurrentPrimitiveTopology, static_cast<GLint>(firstVertex), static_cast<GLsizei>(vertexCount * instanceCount));
            (void)firstInstance;
        });
    }

    void OpenGLGraphicContext::DrawIndexed(RHI::RHIBuffer* indexBuffer, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance)
    {
        NativeCommands.emplace_back([this, indexBuffer, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance]()
        {
            if (CurrentVertexArray != 0)
            {
                glBindVertexArray(CurrentVertexArray);
            }

            auto* glIndexBuffer = dynamic_cast<OpenGLBuffer*>(indexBuffer);
            if (!glIndexBuffer)
            {
                return;
            }

            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, glIndexBuffer->GetHandle());
            const GLenum indexType = glIndexBuffer->GetDesc().Stride == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT;
            glDrawElements(CurrentPrimitiveTopology, static_cast<GLsizei>(indexCount * instanceCount), indexType, reinterpret_cast<void*>(static_cast<uintptr_t>(firstIndex * static_cast<uint32_t>(glIndexBuffer->GetDesc().Stride))));
            (void)vertexOffset;
            (void)firstInstance;
        });
    }

    void OpenGLGraphicContext::BeginRenderPass(const RHI::RHIRenderPassInfo& renderPassInfo)
    {
        NativeCommands.emplace_back([this, renderPassInfo]()
        {
            CurrentFramebuffer->Create();
            CurrentFramebuffer->Reset();
            CurrentFramebuffer->Bind();

            const auto& targets = renderPassInfo.RenderTargets;
            GLenum drawBuffers[8] = {};
            uint32_t colorCount = 0;

            for (uint8_t i = 0; i < targets.NumColorAttachments; ++i)
            {
                const auto& colorTarget = targets.ColorAttachments[i];
                if (!colorTarget.Texture)
                {
                    continue;
                }

                auto* glColorTexture = dynamic_cast<OpenGLTexture*>(colorTarget.Texture);
                if (!glColorTexture)
                {
                    continue;
                }

                CurrentFramebuffer->AttachColorTexture(glColorTexture->GetHandle(), i, GL_TEXTURE_2D);
                drawBuffers[colorCount++] = GL_COLOR_ATTACHMENT0 + i;
            }

            if (colorCount > 0)
            {
                glDrawBuffers(colorCount, drawBuffers);
            }

            if (targets.DepthStencil.Texture)
            {
                auto* glDepthTexture = dynamic_cast<OpenGLTexture*>(targets.DepthStencil.Texture);
                if (glDepthTexture)
                {
                    CurrentFramebuffer->AttachDepthTexture(glDepthTexture->GetHandle(), GL_TEXTURE_2D);
                }
            }

            if (renderPassInfo.RenderArea.Width > 0 && renderPassInfo.RenderArea.Height > 0)
            {
                glViewport(renderPassInfo.RenderArea.X, renderPassInfo.RenderArea.Y,
                    renderPassInfo.RenderArea.Width, renderPassInfo.RenderArea.Height);
            }

            const GLenum framebufferStatus = CurrentFramebuffer->CheckStatus();
            if (framebufferStatus != GL_FRAMEBUFFER_COMPLETE)
            {
                std::fprintf(stderr, "[OpenGLRHI] Framebuffer incomplete: 0x%x\n", framebufferStatus);
            }

            glEnable(GL_SCISSOR_TEST);
            glScissor(0, 0, renderPassInfo.RenderTargets.Dimensions.x, renderPassInfo.RenderTargets.Dimensions.y);

            if (targets.NumColorAttachments > 0)
            {
                glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT);
            }

            if (targets.DepthStencil.Texture)
            {
                glClearDepth(1.0f);
                glClear(GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
            }
        });
    }

    void OpenGLGraphicContext::EndRenderPass()
    {
        NativeCommands.emplace_back([this]()
        {
            CurrentFramebuffer->Unbind();
            glDisable(GL_SCISSOR_TEST);

            if (CurrentVertexArray != 0)
            {
                glBindVertexArray(0);
            }
        });
    }

    void OpenGLGraphicContext::SetBatchedShaderParameters(RHI::RHIRayTracingShader* shader, const RHI::RHIBatchedShaderParameters& parameter)
    {
        (void)shader;
        (void)parameter;
    }

    void OpenGLGraphicContext::SetRayTracingPipelineState(RHI::RHIRayTracingPipelineState* pipelineState)
    {
        (void)pipelineState;
    }

    void OpenGLGraphicContext::BuildAccelerationStructure(RHI::RHIRayTracingAccelerationStructure* accelerationStructure)
    {
        (void)accelerationStructure;
    }

    void OpenGLGraphicContext::UpdateAccelerationStructure(RHI::RHIRayTracingAccelerationStructure* accelerationStructure)
    {
        (void)accelerationStructure;
    }

    void OpenGLGraphicContext::TraceRays(uint32_t width, uint32_t height, uint32_t depth)
    {
        (void)width;
        (void)height;
        (void)depth;
    }
}
