#include "OpenGLResource.h"
#include "OpenGLUtils.h"
#include "OpenGLQueue.h"
#include <cstdlib>
#include <cstdio>

namespace RHIOpenGL
{
    namespace
    {
        // 从源纹理类型 + 视图描述推出 glTextureView 第一个参数 target。
        // 规则：若 ViewType 不是 Derived 优先用显式值；否则从源纹理类型推导。
        GLenum DeriveTextureViewTarget(RHI::ERHITextureType texType, RHI::ERHITextureViewType viewType)
        {
            using RHI::ERHITextureViewType;
            switch (viewType)
            {
                case ERHITextureViewType::TextureView1D:        return GL_TEXTURE_1D;
                case ERHITextureViewType::TextureView3D:        return GL_TEXTURE_3D;
                case ERHITextureViewType::TextureViewCube:      return GL_TEXTURE_CUBE_MAP;
                case ERHITextureViewType::TextureView2DArray:   return GL_TEXTURE_2D_ARRAY;
                case ERHITextureViewType::TextureViewCubeArray: return GL_TEXTURE_CUBE_MAP_ARRAY;
                case ERHITextureViewType::Derived:
                default: break;
            }
            switch (texType)
            {
                case RHI::ERHITextureType::Texture1D:        return GL_TEXTURE_1D;
                case RHI::ERHITextureType::Texture2D:        return GL_TEXTURE_2D;
                case RHI::ERHITextureType::Texture3D:        return GL_TEXTURE_3D;
                case RHI::ERHITextureType::TextureCube:      return GL_TEXTURE_CUBE_MAP;
                case RHI::ERHITextureType::Texture2DArray:   return GL_TEXTURE_2D_ARRAY;
                case RHI::ERHITextureType::TextureCubeArray: return GL_TEXTURE_CUBE_MAP_ARRAY;
                default: return GL_TEXTURE_2D;
            }
        }

        // 从 RHITexSRVCreateInfo/RHITexUAVCreateInfo 构造 OpenGLTextureView::Desc，
        // 并把源 RHI 视图描述也带进来用一次（推 target / internalFormat）。
        OpenGLTextureView::Desc BuildTextureViewDesc(OpenGLTexture* source, const RHI::RHITexSRVCreateInfo& d)
        {
            OpenGLTextureView::Desc out;
            out.InternalFormat = ConvertRHIFormatToGLInternalFormat(d.Format);
            out.FirstMipSlice = d.FirstMipSlice;
            out.MipCount = d.MipCount == 0 ? 1u : d.MipCount;
            out.FirstArraySlice = d.FirstArraySlice;
            out.ArraySize = d.ArraySize == 0 ? 1u : d.ArraySize;
            out.Target = DeriveTextureViewTarget(source->GetDesc().Type, d.ViewType);
            return out;
        }
        OpenGLTextureView::Desc BuildTextureViewDesc(OpenGLTexture* source, const RHI::RHITexUAVCreateInfo& d)
        {
            OpenGLTextureView::Desc out;
            out.InternalFormat = ConvertRHIFormatToGLInternalFormat(d.Format);
            out.FirstMipSlice = d.FirstMipSlice;
            out.MipCount = d.MipCount == 0 ? 1u : d.MipCount;
            out.FirstArraySlice = d.FirstArraySlice;
            out.ArraySize = d.ArraySize == 0 ? 1u : d.ArraySize;
            out.Target = DeriveTextureViewTarget(source->GetDesc().Type, d.ViewType);
            return out;
        }

        template <typename RhiDesc>
        std::unique_ptr<OpenGLTextureView> TryMakeTextureView(RHI::RHIViewableResource* resource, const RhiDesc& desc)
        {
            auto* glTex = dynamic_cast<OpenGLTexture*>(resource);
            if (glTex == nullptr)
            {
                return nullptr; // 非 OpenGLTexture（例如测试用裸 RHITexture）— 此 view 不可用
            }
            OpenGLTextureView::Desc viewDesc = BuildTextureViewDesc(glTex, desc);
            return std::make_unique<OpenGLTextureView>(glTex, viewDesc);
        }
    }

    OpenGLTexture::OpenGLTexture(const RHI::RHITextureDesc& desc)
        : RHI::RHITexture(desc)
    {
        // 主线程不再持有 GL 上下文：GL 调用全部转发给 worker。
        // 构造需要句柄立即可用（swapchain/FBO 挂接、SRV 创建都会读 GetHandle），
        // 所以用 Wait 版——ctor 返回时句柄一定已创建完毕，对调用方语义零变化。
        // 若本 ctor 在 worker 上被调用（未来命令内创建资源），接口自动内联执行。
        EnqueueTaskAndWait([this, &desc]()
        {
            const GLenum internalFormat = ConvertRHIFormatToGLInternalFormat(desc.Format);

            GLenum target = ConvertRHITextureTypeToGLTextureType(desc.Type);


            glCreateTextures(target, 1, &TextureHandle);

            switch (desc.Type)
            {
            case RHI::ERHITextureType::Texture1D:
                glTextureStorage1D(
                    TextureHandle,
                    desc.MipLevels,
                    internalFormat,
                    desc.Width);
                break;

            case RHI::ERHITextureType::Texture2D:
            case RHI::ERHITextureType::TextureCube:
                glTextureStorage2D(
                    TextureHandle,
                    desc.MipLevels,
                    internalFormat,
                    desc.Width,
                    desc.Height);
                break;

            case RHI::ERHITextureType::Texture3D:
            case RHI::ERHITextureType::Texture2DArray:
            case RHI::ERHITextureType::TextureCubeArray:
                glTextureStorage3D(
                    TextureHandle,
                    desc.MipLevels,
                    internalFormat,
                    desc.Width,
                    desc.Height,
                    desc.Depth);
                break;

            default:
                assert(false);
                break;
            }

            glTextureParameteri(TextureHandle, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTextureParameteri(TextureHandle, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        });
    }

    OpenGLTexture::~OpenGLTexture()
    {
        if (TextureHandle != 0)
        {
            // 析构不产生任何返回值，用异步版即可；句柄按值捕获——对象即将消亡，
            // 不能捕获 this。
            const GLuint handle = TextureHandle;
            TextureHandle = 0;
            EnqueueTask([handle]()
            {
                glDeleteTextures(1, &handle);
            });
        }
    }

    OpenGLBuffer::OpenGLBuffer(const RHI::RHIBufferDesc& desc)
        : RHI::RHIBuffer(desc)
    {
        // 同 OpenGLTexture：创建转发 worker 并等待完成。
        EnqueueTaskAndWait([this, &desc]()
        {
            glGenBuffers(1, &BufferHandle);
            glBindBuffer(GL_ARRAY_BUFFER, BufferHandle);
            glBufferData(GL_ARRAY_BUFFER,
                static_cast<GLsizeiptr>(desc.Size == 0 ? 1 : desc.Size),
                nullptr,
                (desc.bCPUAccessible || (desc.Usage & RHI::ERHIBufferUsageFlag::Staging) != RHI::ERHIBufferUsageFlag::None)
                    ? GL_STREAM_DRAW
                    : GL_STATIC_DRAW);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
        });
    }

    OpenGLBuffer::~OpenGLBuffer()
    {
        if (BufferHandle != 0)
        {
            const GLuint handle = BufferHandle;
            BufferHandle = 0;
            EnqueueTask([handle]()
            {
                glDeleteBuffers(1, &handle);
            });
        }
    }

    // ----- OpenGLTextureView -----
    OpenGLTextureView::OpenGLTextureView(OpenGLTexture* source, const Desc& viewDesc)
        : SourceTexture(source), ViewInfo(viewDesc)
    {
        if (SourceTexture == nullptr || SourceTexture->GetHandle() == 0)
        {
            return;
        }
        // SRV/UAV 在主线程创建，GL 调用同样转发 worker；句柄同步可读。
        EnqueueTaskAndWait([this]()
        {
            glGenTextures(1, &ViewHandle);
            glTextureView(ViewHandle,
                          ViewInfo.Target,
                          SourceTexture->GetHandle(),
                          ViewInfo.InternalFormat,
                          ViewInfo.FirstMipSlice,
                          ViewInfo.MipCount,
                          ViewInfo.FirstArraySlice,
                          ViewInfo.ArraySize);
        });
    }

    OpenGLTextureView::~OpenGLTextureView()
    {
        if (ViewHandle != 0)
        {
            const GLuint handle = ViewHandle;
            ViewHandle = 0;
            EnqueueTask([handle]()
            {
                glDeleteTextures(1, &handle);
            });
        }
    }

    // ----- SRV -----
    OpenGLShaderResourceView::OpenGLShaderResourceView(RHI::RHIViewableResource* Resource, const RHI::RHITexSRVCreateInfo& Desc)
        : RHI::RHIShaderResourceView(Resource), TextureView(TryMakeTextureView(Resource, Desc)), IsTextureView(true), TexSRVDesc(Desc)
    {
    }
    OpenGLShaderResourceView::OpenGLShaderResourceView(RHI::RHIViewableResource* Resource, const RHI::RHIBufferSRVCreateInfo& Desc)
        : RHI::RHIShaderResourceView(Resource), IsTextureView(false), BufferSRVDesc(Desc)
    {
    }
    bool OpenGLShaderResourceView::IsTexture() { return IsTextureView; }
    bool OpenGLShaderResourceView::IsBuffer() { return !IsTextureView; }

    // ----- UAV -----
    OpenGLUnorderedAccessView::OpenGLUnorderedAccessView(RHI::RHIViewableResource* Resource, const RHI::RHITexUAVCreateInfo& Desc)
        : RHI::RHIUnorderedAccessView(Resource), TextureView(TryMakeTextureView(Resource, Desc)), IsTextureView(true), TexUAVDesc(Desc)
    {
    }
    OpenGLUnorderedAccessView::OpenGLUnorderedAccessView(RHI::RHIViewableResource* Resource, const RHI::RHIBufferUAVCreateInfo& Desc)
        : RHI::RHIUnorderedAccessView(Resource), IsTextureView(false), BufferUAVDesc(Desc)
    {
    }
    bool OpenGLUnorderedAccessView::IsTexture() { return IsTextureView; }
    bool OpenGLUnorderedAccessView::IsBuffer() { return !IsTextureView; }

    bool OpenGLShaderBase::CompileOpenGLShader(GLuint shaderHandle, const std::vector<char>& packedSource, const char* shaderName)
    {
        if (shaderHandle == 0)
        {
            return false;
        }
        RenderCore::GLSLCompiledBinaryResultPacker packer;
        packer.Depack(packedSource);
        Reflection = packer.DepackedData.HeaderData;
        FillOpenGLShaderResourceLayout();
        auto& source = packer.DepackedData.GLSLCode;
        const char* sourceText = source.empty() ? "" : source.data();
        GLint sourceLength = static_cast<GLint>(source.size());

        // GL 编译转发到渲染线程；worker 内部调用会被任务接口内联执行。
        // Wait 版保证返回时编译结果可查、ShaderHandle 状态已就绪。
        bool bCompiled = false;
        EnqueueTaskAndWait([&]()
        {
            glShaderSource(shaderHandle, 1, &sourceText, sourceLength == 0 ? nullptr : &sourceLength);
            glCompileShader(shaderHandle);

            GLint compileStatus = GL_FALSE;
            glGetShaderiv(shaderHandle, GL_COMPILE_STATUS, &compileStatus);
            if (compileStatus == GL_FALSE)
            {
                GLint infoLogLength = 0;
                glGetShaderiv(shaderHandle, GL_INFO_LOG_LENGTH, &infoLogLength);
                std::vector<char> infoLog(static_cast<size_t>(infoLogLength > 0 ? infoLogLength : 1));
                glGetShaderInfoLog(shaderHandle, static_cast<GLint>(infoLog.size()), nullptr, infoLog.data());
                std::fprintf(stderr, "[OpenGLRHI] Failed to compile %s shader: %s\n", shaderName, infoLog.data());
                bCompiled = false;
                return;
            }

            bCompiled = true;
        });
        return bCompiled;
    }
    void OpenGLShaderBase::FillOpenGLShaderResourceLayout() {
        for (auto binding : Reflection.Resources) {
            ShaderResourceLayout.Bindings[binding.Binding].Count = binding.Count;
            ShaderResourceLayout.Bindings[binding.Binding].Type = binding.Type;
        }
        // UBO 段大小：来自 SPIRV-Cross 的 get_declared_struct_size，等价于 HLSL 里
        // cbuffer 声明大小。对齐由调用方（UniformBufferCache::EnsureRange）按
        // GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT 负责向上取整。
        // 注意：同一 binding 在多 shader（VS+FS）出现时取更大值，由 Context 在
        // pipeline 切换处 HintUniformBufferSize 时统一合并。
        ShaderResourceLayout.UniformBufferSizes.clear();
        for (const auto& ub : Reflection.UniformBuffers)
        {
            auto it = ShaderResourceLayout.UniformBufferSizes.find(ub.Binding);
            if (it == ShaderResourceLayout.UniformBufferSizes.end() || ub.Size > it->second)
            {
                ShaderResourceLayout.UniformBufferSizes[ub.Binding] = ub.Size;
            }
        }
    }
    bool OpenGLVertexShader::Compile(const std::vector<char>& source)
    {
        SourceCode = source;
        return CompileOpenGLShader(ShaderHandle, SourceCode, "vertex");
    }

    OpenGLVertexShader::~OpenGLVertexShader()
    {
        if (ShaderHandle != 0)
        {
            const GLuint handle = ShaderHandle;
            ShaderHandle = 0;
            EnqueueTask([handle]()
            {
                glDeleteShader(handle);
            });
        }
    }

    OpenGLVertexShader::OpenGLVertexShader()
        : RHI::RHIVertexShader()
    {
        EnqueueTaskAndWait([this]()
        {
            ShaderHandle = glCreateShader(GL_VERTEX_SHADER);
        });
    }

    bool OpenGLFragmentShader::Compile(const std::vector<char>& source)
    {
        SourceCode = source;
        return CompileOpenGLShader(ShaderHandle, SourceCode, "fragment");
    }

    OpenGLFragmentShader::~OpenGLFragmentShader()
    {
        if (ShaderHandle != 0)
        {
            const GLuint handle = ShaderHandle;
            ShaderHandle = 0;
            EnqueueTask([handle]()
            {
                glDeleteShader(handle);
            });
        }
    }

    OpenGLFragmentShader::OpenGLFragmentShader()
        : RHI::RHIFragmentShader()
    {
        EnqueueTaskAndWait([this]()
        {
            ShaderHandle = glCreateShader(GL_FRAGMENT_SHADER);
        });
    }

    bool OpenGLComputeShader::Compile(const std::vector<char>& source)
    {
        SourceCode = source;
        return CompileOpenGLShader(ShaderHandle, SourceCode, "compute");
    }

    OpenGLComputeShader::~OpenGLComputeShader()
    {
        if (ShaderHandle != 0)
        {
            const GLuint handle = ShaderHandle;
            ShaderHandle = 0;
            EnqueueTask([handle]()
            {
                glDeleteShader(handle);
            });
        }
    }

    OpenGLComputeShader::OpenGLComputeShader()
        : RHI::RHIComputeShader()
    {
        EnqueueTaskAndWait([this]()
        {
            ShaderHandle = glCreateShader(GL_COMPUTE_SHADER);
        });
    }

    bool OpenGLGeometryShader::Compile(const std::vector<char>& source)
    {
        SourceCode = source;
        return CompileOpenGLShader(ShaderHandle, SourceCode, "geometry");
    }

    OpenGLGeometryShader::~OpenGLGeometryShader()
    {
        if (ShaderHandle != 0)
        {
            const GLuint handle = ShaderHandle;
            ShaderHandle = 0;
            EnqueueTask([handle]()
            {
                glDeleteShader(handle);
            });
        }
    }

    OpenGLGeometryShader::OpenGLGeometryShader()
        : RHI::RHIGeometryShader()
    {
        EnqueueTaskAndWait([this]()
        {
            ShaderHandle = glCreateShader(GL_GEOMETRY_SHADER);
        });
    }

    bool OpenGLTessControlShader::Compile(const std::vector<char>& source)
    {
        SourceCode = source;
        return CompileOpenGLShader(ShaderHandle, SourceCode, "tess control");
    }

    OpenGLTessControlShader::~OpenGLTessControlShader()
    {
        if (ShaderHandle != 0)
        {
            const GLuint handle = ShaderHandle;
            ShaderHandle = 0;
            EnqueueTask([handle]()
            {
                glDeleteShader(handle);
            });
        }
    }

    OpenGLTessControlShader::OpenGLTessControlShader()
        : RHI::RHITessControlShader()
    {
        EnqueueTaskAndWait([this]()
        {
            ShaderHandle = glCreateShader(GL_TESS_CONTROL_SHADER);
        });
    }

    bool OpenGLTessEvalShader::Compile(const std::vector<char>& source)
    {
        SourceCode = source;
        return CompileOpenGLShader(ShaderHandle, SourceCode, "tess eval");
    }

    OpenGLTessEvalShader::~OpenGLTessEvalShader()
    {
        if (ShaderHandle != 0)
        {
            const GLuint handle = ShaderHandle;
            ShaderHandle = 0;
            EnqueueTask([handle]()
            {
                glDeleteShader(handle);
            });
        }
    }

    OpenGLTessEvalShader::OpenGLTessEvalShader()
        : RHI::RHITessEvalShader()
    {
        EnqueueTaskAndWait([this]()
        {
            ShaderHandle = glCreateShader(GL_TESS_EVALUATION_SHADER);
        });
    }

    OpenGLSampler::OpenGLSampler(const RHI::RHISamplerDesc& desc)
        : RHI::RHISampler(desc)
    {
        EnqueueTaskAndWait([this]()
        {
            glGenSamplers(1, &SamplerHandle);
        });
    }

    OpenGLStagingBuffer::~OpenGLStagingBuffer()
    {
        if (MappedData != nullptr)
        {
            std::free(MappedData);
            MappedData = nullptr;
        }
    }

    OpenGLVertexDescState::OpenGLVertexDescState(const RHI::RHIVertexDescStateDesc& desc)
        : RHI::RHIVertexDescState(desc)
    {
    }

    OpenGLRasterizerState::OpenGLRasterizerState(const RHI::RHIRasterizerStateDesc& desc)
        : RHI::RHIRasterizerState(desc)
    {
    }

    OpenGLColorBlendState::OpenGLColorBlendState(const RHI::RHIColorBlendStateDesc& desc)
        : RHI::RHIColorBlendState(desc)
    {
    }

    OpenGLDepthStencilState::OpenGLDepthStencilState(const RHI::RHIDepthStencilStateDesc& desc)
        : RHI::RHIDepthStencilState(desc)
    {
    }

    OpenGLSampler::~OpenGLSampler()
    {
        if (SamplerHandle != 0)
        {
            const GLuint handle = SamplerHandle;
            SamplerHandle = 0;
            EnqueueTask([handle]()
            {
                glDeleteSamplers(1, &handle);
            });
        }
    }

    OpenGLStagingBuffer::OpenGLStagingBuffer(uint32_t size)
        : RHI::RHIStagingBuffer(size)
    {
        MappedData = std::malloc(size == 0 ? 1 : size);
    }

    void* OpenGLStagingBuffer::Map(uint32_t offset, uint32_t numBytes)
    {
        if (!MappedData)
        {
            MappedData = std::malloc((numBytes == 0 ? 1u : numBytes));
        }
        return static_cast<char*>(MappedData) + offset;
    }

    void OpenGLStagingBuffer::Unmap()
    {
        MappedData = nullptr;
    }

}
