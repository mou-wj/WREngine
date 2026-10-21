#include "OpenGLUtils.h"
#include "OpenGLContext.h"
#include "OpenGLPipelineState.h"
#include "OpenGLResource.h"
#include "OpenGLRenderTarget.h"
#include "RHIUtils.h"
#include "glad/gl.h"

namespace RHIOpenGL
{
    namespace
    {
        struct VertexAttribFormatInfo
        {
            GLint ComponentCount = 0;
            GLenum ComponentType = GL_FLOAT;
            GLboolean Normalized = GL_FALSE;
            bool IntegerAttribute = false;
            bool IsValid = false;
        };

        VertexAttribFormatInfo GetVertexAttribFormatInfo(RHI::ERHIFormat format)
        {
            switch (format)
            {
            case RHI::ERHIFormat::R8_UNorm:
                return { 1, GL_UNSIGNED_BYTE, GL_TRUE, false, true };
            case RHI::ERHIFormat::R8G8B8A8_UNorm:
            case RHI::ERHIFormat::B8G8R8A8_UNorm:
                return { 4, GL_UNSIGNED_BYTE, GL_TRUE, false, true };
            case RHI::ERHIFormat::R16G16_Float:
                return { 2, GL_HALF_FLOAT, GL_FALSE, false, true };
            case RHI::ERHIFormat::R16G16B16A16_Float:
                return { 4, GL_HALF_FLOAT, GL_FALSE, false, true };
            case RHI::ERHIFormat::R16_UInt:
                return { 1, GL_UNSIGNED_SHORT, GL_FALSE, true, true };
            case RHI::ERHIFormat::R32_UInt:
                return { 1, GL_UNSIGNED_INT, GL_FALSE, true, true };
            case RHI::ERHIFormat::R32_Float:
                return { 1, GL_FLOAT, GL_FALSE, false, true };
            case RHI::ERHIFormat::R32G32_Float:
                return { 2, GL_FLOAT, GL_FALSE, false, true };
            case RHI::ERHIFormat::R32G32B32_Float:
                return { 3, GL_FLOAT, GL_FALSE, false, true };
            case RHI::ERHIFormat::R32G32B32A32_Float:
                return { 4, GL_FLOAT, GL_FALSE, false, true };
            default:
                return {};
            }
        }

        RHI::ERenderTargetLoadOp GetLoadOp(RHI::ERenderTargetActions actions)
        {
            const uint8_t load = static_cast<uint8_t>(actions) >> static_cast<uint8_t>(RHI::ERenderTargetActions::LoadOpShift);
            switch (load)
            {
            case static_cast<uint8_t>(RHI::ERenderTargetLoadOp::DontCare):
                return RHI::ERenderTargetLoadOp::DontCare;
            case static_cast<uint8_t>(RHI::ERenderTargetLoadOp::Load):
                return RHI::ERenderTargetLoadOp::Load;
            case static_cast<uint8_t>(RHI::ERenderTargetLoadOp::Clear):
                return RHI::ERenderTargetLoadOp::Clear;
            default:
                return RHI::ERenderTargetLoadOp::DontCare;
            }
        }
    }

    void OpenGLGraphicContext::ApplyVertexDescState(const RHI::RHIGraphicsPipelineStateDesc& desc)
    {
        GLint maxAttribCount = 0;
        glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &maxAttribCount);

        for (GLint attribIndex = 0; attribIndex < maxAttribCount; ++attribIndex)
        {
            glDisableVertexAttribArray(static_cast<GLuint>(attribIndex));
            glVertexAttribDivisor(static_cast<GLuint>(attribIndex), 0);
        }

        if (!desc.vertexDescState)
        {
            bVertexLayoutDirty = false;
            return;
        }

        const auto& vertexDesc = desc.vertexDescState->GetDesc();
        std::unordered_map<uint32_t, RHI::RHIVertexBindingDesc> bindingDescs;
        for (const auto& binding : vertexDesc.bindings)
        {
            bindingDescs[binding.binding] = binding;
        }

        for (const auto& attribute : vertexDesc.attributes)
        {
            if (attribute.binding >= 8 || static_cast<GLint>(attribute.location) >= maxAttribCount)
            {
                continue;
            }

            const auto formatInfo = GetVertexAttribFormatInfo(attribute.format);
            if (!formatInfo.IsValid)
            {
                continue;
            }

            auto* vertexBuffer = dynamic_cast<OpenGLBuffer*>(BoundVertexBuffers[attribute.binding]);
            if (!vertexBuffer)
            {
                continue;
            }

            uint32_t stride = 0;
            auto bindingIt = bindingDescs.find(attribute.binding);
            if (bindingIt != bindingDescs.end())
            {
                stride = bindingIt->second.stride;
            }

            if (stride == 0)
            {
                stride = static_cast<uint32_t>(RHI::GetFormatSize(attribute.format));
            }

            glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer->GetHandle());
            glEnableVertexAttribArray(attribute.location);

            const uintptr_t byteOffset = static_cast<uintptr_t>(BoundVertexBufferOffsets[attribute.binding]) +
                static_cast<uintptr_t>(attribute.offset);

            if (formatInfo.IntegerAttribute)
            {
                glVertexAttribIPointer(
                    attribute.location,
                    formatInfo.ComponentCount,
                    formatInfo.ComponentType,
                    static_cast<GLsizei>(stride),
                    reinterpret_cast<void*>(byteOffset));
            }
            else
            {
                glVertexAttribPointer(
                    attribute.location,
                    formatInfo.ComponentCount,
                    formatInfo.ComponentType,
                    formatInfo.Normalized,
                    static_cast<GLsizei>(stride),
                    reinterpret_cast<void*>(byteOffset));
            }

            const GLuint divisor =
                (bindingIt != bindingDescs.end() && bindingIt->second.inputRate == RHI::ERHIInputRate::PerInstance)
                ? 1u
                : 0u;
            glVertexAttribDivisor(attribute.location, divisor);
        }

        bVertexLayoutDirty = false;
    }

    void OpenGLGraphicContext::ApplyPrimitiveTopologyState(const RHI::RHIGraphicsPipelineStateDesc& desc)
    {
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
    }

    void OpenGLGraphicContext::ApplyRasterizerState(const RHI::RHIGraphicsPipelineStateDesc& desc)
    {
        if (!desc.rasterizerState)
        {
            glDisable(GL_CULL_FACE);
            return;
        }

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

    void OpenGLGraphicContext::ApplyDepthStencilState(const RHI::RHIGraphicsPipelineStateDesc& desc)
    {
        if (!desc.depthStencilState)
        {
            glDisable(GL_DEPTH_TEST);
            return;
        }

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

    void OpenGLGraphicContext::ApplyColorBlendState(const RHI::RHIGraphicsPipelineStateDesc& desc)
    {
        if (desc.colorBlendState && !desc.colorBlendState->GetDesc().attachments.empty())
        {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }
        else
        {
            glDisable(GL_BLEND);
        }
    }

    void OpenGLCommandContext::UploadUniformBufferData(const RHI::RHIBatchedShaderParameters& parameter)
    {
        if (parameter.UniformParameters.empty() || parameter.Data.empty())
        {
            return;
        }

        // 1) 按 BufferIndex (= GL UBO binding) 统计 cbuffer 大小 = max(BaseIndex + Size)
        std::unordered_map<uint32_t, uint32_t> cbufferSizes;
        for (const auto& uniformParam : parameter.UniformParameters)
        {
            if (uniformParam.Offset > parameter.Data.size() ||
                uniformParam.Size > parameter.Data.size() - uniformParam.Offset)
            {
                continue;
            }
            if (uniformParam.BaseIndex + uniformParam.Size < uniformParam.BaseIndex)
            {
                continue; // uint32 overflow
            }
            const uint32_t end = uniformParam.BaseIndex + uniformParam.Size;
            auto it = cbufferSizes.find(uniformParam.BufferIndex);
            if (it == cbufferSizes.end() || end > it->second)
            {
                cbufferSizes[uniformParam.BufferIndex] = end;
            }
        }
        if (cbufferSizes.empty())
        {
            return;
        }

        // 2) 对每个 binding 让 ring buffer cache 准备一段
        for (const auto& kv : cbufferSizes)
        {
            UBOCache.EnsureRange(kv.first, kv.second);
        }

        // 3) 按 BaseIndex 写入各 member
        for (const auto& uniformParam : parameter.UniformParameters)
        {
            if (uniformParam.Offset > parameter.Data.size() ||
                uniformParam.Size > parameter.Data.size() - uniformParam.Offset)
            {
                continue;
            }
            UBOCache.WriteSlice(uniformParam.BufferIndex,
                uniformParam.BaseIndex,
                parameter.Data.data() + uniformParam.Offset,
                uniformParam.Size);
        }

        // 4) 把段绑到 GL（ring buffer 优势：单 GL buffer 跨多个 binding range 表达）
        for (const auto& kv : cbufferSizes)
        {
            UBOCache.Bind(kv.first);
        }
    }

    void OpenGLCommandContext::ApplyShaderParameters(const RHI::RHIBatchedShaderParameters& parameter)
    {
        UploadUniformBufferData(parameter);

        for (const auto& resourceParam : parameter.ResourceParameters)
        {
            
            const uint32_t slot = resourceParam.Index + resourceParam.ArrayIndex;
            switch (resourceParam.Type)
            {
            case RHI::RHIShaderResourceParameter::EType::Texture:
            {
                auto* texture = dynamic_cast<OpenGLTexture*>(resourceParam.GetResourceAs<RHI::RHITexture>());
                glBindTextureUnit(slot, texture ? texture->GetHandle() : 0u);
                glBindSampler(slot, 0u); // 清掉早前残留的 sampler
                break;
            }
            case RHI::RHIShaderResourceParameter::EType::SRV:
            {
                auto* srv = dynamic_cast<OpenGLShaderResourceView*>(resourceParam.GetResourceAs<RHI::RHIShaderResourceView>());
                if (srv == nullptr) break;
                if (srv->IsTexture())
                {
                    // mip/array 切片已由 glTextureView 在 view 创建时确定；
                    // 这里只决定是否再清掉配对的 sampler。
                    auto* view = srv->GetTextureView();
                    glBindTextureUnit(slot, view ? view->GetHandle() : 0u);
                    glBindSampler(slot, 0u);
                }
                // buffer SRV — 描述符层信息，GL 无需显式绑定
                break;
            }
            case RHI::RHIShaderResourceParameter::EType::Sampler:
            {
                auto* sampler = dynamic_cast<OpenGLSampler*>(resourceParam.GetResourceAs<RHI::RHISampler>());
                glBindSampler(slot, sampler ? sampler->GetHandle() : 0u);
                break;
            }
            case RHI::RHIShaderResourceParameter::EType::UniformBuffer:
            {
                auto* buffer = dynamic_cast<OpenGLBuffer*>(resourceParam.GetResourceAs<RHI::RHIBuffer>());
                if (buffer == nullptr)
                {
                    break; // StructNested 路径：Resource 为空，跳过 bind
                }
                glBindBufferBase(GL_UNIFORM_BUFFER, slot, buffer->GetHandle());
                break;
            }
            case RHI::RHIShaderResourceParameter::EType::UAV:
            {
                auto* uav = dynamic_cast<OpenGLUnorderedAccessView*>(resourceParam.GetResourceAs<RHI::RHIUnorderedAccessView>());
                if (uav == nullptr) break;
                if (uav->IsTexture())
                {
                    auto* view = uav->GetTextureView();
                    if (view != nullptr)
                    {
                        const auto& info = uav->GetTexUAVDesc();
                        GLuint handle = view->GetHandle();
						glBindImageTextures(slot, 1, &handle);
                        //glBindImageTexture(slot,
                        //                  view->GetHandle(),
                        //                  0,
                        //                  GL_FALSE,
                        //                  0,
                        //                  GL_READ_WRITE,
                        //                  ConvertRHIFormatToGLFormat(info.Format));
                    }
                }
                // buffer UAV — 描述符层信息，GL 无需显式绑定
                break;
            }
            case RHI::RHIShaderResourceParameter::EType::AccelerationStructure:
            default:
                break;
            }
        }
    }

    OpenGLCommandContext::OpenGLCommandContext(OpenGLQueue* inQueue)
        : Queue(inQueue)
    {
    }

    void OpenGLCommandContext::Begin()
    {
        NativeCommands.clear();
        UBOCache.Reset();
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
            UBOCache.Reset();

            auto* glPipeline = dynamic_cast<OpenGLComputePipelineState*>(pipelineState);
            if (!glPipeline)
            {
                return;
            }

            CurrentPipelineState = pipelineState;
            glUseProgram(glPipeline->GetProgramHandle());

            // 收集 CS 的 UBO 声明大小，作为后续 EnsureRange 的下界；只记 hint
            // 不分配 GPU 资源。pipeline 切换时已经 Reset，所以这里覆盖干净。
            if (auto* csBase = dynamic_cast<OpenGLShaderBase*>(glPipeline->GetDesc().computeShader))
            {
                for (const auto& kv : csBase->GetShaderResourceLayout().UniformBufferSizes)
                {
                    UBOCache.HintUniformBufferSize(kv.first, kv.second);
                }
            }
        });
    }

    void OpenGLComputeContext::SetBatchedShaderParameters(RHI::RHIComputeShader* shader, const RHI::RHIBatchedShaderParameters& parameter)
    {
        NativeCommands.emplace_back([this, shader, parameter]()
        {
            RHI::RHIBatchedShaderParameters parameterCopy = parameter;
            (void)shader; // shader 类型当前未被绑定逻辑使用
            ApplyShaderParameters(parameterCopy);
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
            (void)shader; // shader 类型当前未被绑定逻辑使用
            ApplyShaderParameters(parameter);
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
            BoundVertexBufferOffsets[streamIndex] = offset;
            bVertexLayoutDirty = true;
        });
    }

    void OpenGLGraphicContext::SetGraphicPipelineState(RHI::RHIGraphicsPipelineState* pipelineState)
    {
        NativeCommands.emplace_back([this, pipelineState]()
        {
            UBOCache.Reset();

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
            ApplyVertexDescState(desc);
            ApplyPrimitiveTopologyState(desc);
            ApplyRasterizerState(desc);
            ApplyDepthStencilState(desc);
            ApplyColorBlendState(desc);

            // 收集 VS + FS 的 UBO 声明大小。HintUniformBufferSize 内部取 max，
            // 所以同一 binding 在两 stage 各自反射出不同大小时以较大者为准——
            // 这与 GL 一 program 共享 UBO 槽位的语义一致（同一 binding 两 stage
            // 必须都看得见数据，段大小取大值才能覆盖两者期望）。
            if (auto* vsBase = dynamic_cast<OpenGLShaderBase*>(desc.shaderStages.vertexShader))
            {
                for (const auto& kv : vsBase->GetShaderResourceLayout().UniformBufferSizes)
                {
                    UBOCache.HintUniformBufferSize(kv.first, kv.second);
                }
            }
            if (auto* fsBase = dynamic_cast<OpenGLShaderBase*>(desc.shaderStages.fragmentShader))
            {
                for (const auto& kv : fsBase->GetShaderResourceLayout().UniformBufferSizes)
                {
                    UBOCache.HintUniformBufferSize(kv.first, kv.second);
                }
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

            if (bVertexLayoutDirty && CurrentPipelineState)
            {
                ApplyVertexDescState(CurrentPipelineState->GetDesc());
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

            if (bVertexLayoutDirty && CurrentPipelineState)
            {
                ApplyVertexDescState(CurrentPipelineState->GetDesc());
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

            bool hasColorClear = false;
            float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
            for (uint8_t i = 0; i < targets.NumColorAttachments; ++i)
            {
                const auto& colorTarget = targets.ColorAttachments[i];
                if (!colorTarget.Texture)
                {
                    continue;
                }

                if (GetLoadOp(colorTarget.Actions) != RHI::ERenderTargetLoadOp::Clear)
                {
                    continue;
                }

                hasColorClear = true;
                if (colorTarget.ClearBinding.Binding == RHI::RHIClearValueBinding::ClearValueBinding::Color)
                {
                    clearColor[0] = colorTarget.ClearBinding.Color[0];
                    clearColor[1] = colorTarget.ClearBinding.Color[1];
                    clearColor[2] = colorTarget.ClearBinding.Color[2];
                    clearColor[3] = colorTarget.ClearBinding.Color[3];
                }
                break;
            }

            if (hasColorClear)
            {
                glClearColor(clearColor[0], clearColor[1], clearColor[2], clearColor[3]);
                glClear(GL_COLOR_BUFFER_BIT);
            }

            if (targets.DepthStencil.Texture && GetLoadOp(targets.DepthStencil.Actions) == RHI::ERenderTargetLoadOp::Clear)
            {
                double clearDepthValue = 1.0;
                int clearStencilValue = 0;
                if (targets.DepthStencil.ClearBinding.Binding == RHI::RHIClearValueBinding::ClearValueBinding::DepthStencil)
                {
                    clearDepthValue = static_cast<double>(targets.DepthStencil.ClearBinding.Depth);
                    clearStencilValue = static_cast<int>(targets.DepthStencil.ClearBinding.Stencil);
                }

                glClearDepth(clearDepthValue);
                glClearStencil(clearStencilValue);
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
        ThrowRayTracingUnsupported();
    }

    void OpenGLGraphicContext::SetRayTracingPipelineState(RHI::RHIRayTracingPipelineState* pipelineState)
    {
        (void)pipelineState;
        ThrowRayTracingUnsupported();
    }

    void OpenGLGraphicContext::BuildAccelerationStructure(RHI::RHIRayTracingAccelerationStructure* accelerationStructure)
    {
        (void)accelerationStructure;
        ThrowRayTracingUnsupported();
    }

    void OpenGLGraphicContext::UpdateAccelerationStructure(RHI::RHIRayTracingAccelerationStructure* accelerationStructure)
    {
        (void)accelerationStructure;
        ThrowRayTracingUnsupported();
    }

    void OpenGLGraphicContext::TraceRays(uint32_t width, uint32_t height, uint32_t depth)
    {
        (void)width;
        (void)height;
        (void)depth;
        ThrowRayTracingUnsupported();
    }
}
