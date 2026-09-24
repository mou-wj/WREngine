#include "OpenGLResource.h"
#include <cstdlib>
#include <cstdio>

namespace RHIOpenGL
{
    OpenGLTexture::OpenGLTexture(const RHI::RHITextureDesc& desc)
        : RHI::RHITexture(desc)
    {
        glGenTextures(1, &TextureHandle);
        glBindTexture(GL_TEXTURE_2D, TextureHandle);

        const GLenum internalFormat = ConvertRHIFormatToGLInternalFormat(desc.Format);
        const GLenum glFormat = ConvertRHIFormatToGLFormat(desc.Format);
        const GLenum glType = ConvertRHIFormatToGLType(desc.Format);

        glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, desc.Width, desc.Height, 0, glFormat, glType, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    OpenGLTexture::~OpenGLTexture()
    {
        if (TextureHandle != 0)
        {
            glDeleteTextures(1, &TextureHandle);
            TextureHandle = 0;
        }
    }

    OpenGLBuffer::OpenGLBuffer(const RHI::RHIBufferDesc& desc)
        : RHI::RHIBuffer(desc)
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
    }

    OpenGLBuffer::~OpenGLBuffer()
    {
        if (BufferHandle != 0)
        {
            glDeleteBuffers(1, &BufferHandle);
            BufferHandle = 0;
        }
    }

    OpenGLShaderResourceView::OpenGLShaderResourceView(RHI::RHIViewableResource* Resource,const RHI::RHITexSRVCreateInfo& Desc)
        : RHI::RHIShaderResourceView(Resource), IsTextureView(true), TexSRVDesc(Desc)
    {
        
    }
    OpenGLShaderResourceView::OpenGLShaderResourceView(RHI::RHIViewableResource* Resource,const RHI::RHIBufferSRVCreateInfo& Desc) : RHI::RHIShaderResourceView(Resource), IsTextureView(false), BufferSRVDesc(Desc)
    {
    }
    bool OpenGLShaderResourceView::IsTexture() {
        return IsTextureView;
    }
    bool OpenGLShaderResourceView::IsBuffer() {
        return !IsTextureView;
    }
    OpenGLUnorderedAccessView::OpenGLUnorderedAccessView(RHI::RHIViewableResource* Resource , const RHI::RHITexUAVCreateInfo& Desc)
        : RHI::RHIUnorderedAccessView(Resource),IsTextureView(true),TexUAVDesc(Desc)
    {
    }
    OpenGLUnorderedAccessView::OpenGLUnorderedAccessView(RHI::RHIViewableResource* Resource , const RHI::RHIBufferUAVCreateInfo& Desc)
        : RHI::RHIUnorderedAccessView(Resource),IsTextureView(false), BufferUAVDesc(Desc)
    {
    }
    bool OpenGLUnorderedAccessView::IsTexture() {
        return IsTextureView;
    }
    bool OpenGLUnorderedAccessView::IsBuffer() {
        return !IsTextureView;
    }

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
            return false;
        }

        return true;
    }
    void OpenGLShaderBase::FillOpenGLShaderResourceLayout() {
        for (auto binding : Reflection.Resources) {
            ShaderResourceLayout.Bindings[binding.Binding].Count = binding.Count;
            ShaderResourceLayout.Bindings[binding.Binding].Type = binding.Type;
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
            glDeleteShader(ShaderHandle);
            ShaderHandle = 0;
        }
    }

    OpenGLVertexShader::OpenGLVertexShader()
        : RHI::RHIVertexShader()
    {
        ShaderHandle = glCreateShader(GL_VERTEX_SHADER);
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
            glDeleteShader(ShaderHandle);
            ShaderHandle = 0;
        }
    }

    OpenGLFragmentShader::OpenGLFragmentShader()
        : RHI::RHIFragmentShader()
    {
        ShaderHandle = glCreateShader(GL_FRAGMENT_SHADER);
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
            glDeleteShader(ShaderHandle);
            ShaderHandle = 0;
        }
    }

    OpenGLComputeShader::OpenGLComputeShader()
        : RHI::RHIComputeShader()
    {
        ShaderHandle = glCreateShader(GL_COMPUTE_SHADER);
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
            glDeleteShader(ShaderHandle);
            ShaderHandle = 0;
        }
    }

    OpenGLGeometryShader::OpenGLGeometryShader()
        : RHI::RHIGeometryShader()
    {
        ShaderHandle = glCreateShader(GL_GEOMETRY_SHADER);
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
            glDeleteShader(ShaderHandle);
            ShaderHandle = 0;
        }
    }

    OpenGLTessControlShader::OpenGLTessControlShader()
        : RHI::RHITessControlShader()
    {
        ShaderHandle = glCreateShader(GL_TESS_CONTROL_SHADER);
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
            glDeleteShader(ShaderHandle);
            ShaderHandle = 0;
        }
    }

    OpenGLTessEvalShader::OpenGLTessEvalShader()
        : RHI::RHITessEvalShader()
    {
        ShaderHandle = glCreateShader(GL_TESS_EVALUATION_SHADER);
    }

    OpenGLSampler::OpenGLSampler(const RHI::RHISamplerDesc& desc)
        : RHI::RHISampler(desc)
    {
        glGenSamplers(1, &SamplerHandle);
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
            glDeleteSamplers(1, &SamplerHandle);
            SamplerHandle = 0;
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
