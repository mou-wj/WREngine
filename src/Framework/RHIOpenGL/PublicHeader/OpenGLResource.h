#pragma once

#include "RHIResource.h"
#include "glad/gl.h"
#include "ShaderCompiledDataPacker.h"
#include <memory>
#include <vector>

#include "OpenGLUtils.h"

namespace RHIOpenGL
{
    class OpenGLTexture : public RHI::RHITexture
    {
    public:
        explicit OpenGLTexture(const RHI::RHITextureDesc& desc);
        ~OpenGLTexture() override;

        GLuint GetHandle() const { return TextureHandle; }
        void SetHandle(GLuint handle) { TextureHandle = handle; }

    private:
        GLuint TextureHandle = 0;
    };
    using OpenGLTextureSP = std::shared_ptr<OpenGLTexture>;

    class OpenGLBuffer : public RHI::RHIBuffer
    {
    public:
        explicit OpenGLBuffer(const RHI::RHIBufferDesc& desc);
        ~OpenGLBuffer() override;

        GLuint GetHandle() const { return BufferHandle; }
        void SetHandle(GLuint handle) { BufferHandle = handle; }

    private:
        GLuint BufferHandle = 0;
    };
    using OpenGLBufferSP = std::shared_ptr<OpenGLBuffer>;

    class OpenGLShaderResourceView : public RHI::RHIShaderResourceView
    {
    public:
        explicit OpenGLShaderResourceView(RHI::RHIViewableResource* Resource);
        ~OpenGLShaderResourceView() override = default;

        GLuint GetHandle() const { return Handle; }
        void SetHandle(GLuint handle) { Handle = handle; }

    private:
        GLuint Handle = 0;
    };
    using OpenGLShaderResourceViewSP = std::shared_ptr<OpenGLShaderResourceView>;

    class OpenGLUnorderedAccessView : public RHI::RHIUnorderedAccessView
    {
    public:
        explicit OpenGLUnorderedAccessView(RHI::RHIViewableResource* Resource);
        ~OpenGLUnorderedAccessView() override = default;

        GLuint GetHandle() const { return Handle; }
        void SetHandle(GLuint handle) { Handle = handle; }

    private:
        GLuint Handle = 0;
    };
    using OpenGLUnorderedAccessViewSP = std::shared_ptr<OpenGLUnorderedAccessView>;

    class OpenGLShaderBase{
    public:
        RenderCore::GLSLCompiledBinaryResultPacker::Header GetShaderReflection() const { return Reflection; }
        bool CompileOpenGLShader(GLuint shaderHandle, const std::vector<char>& source, const char* shaderName);
    protected:
        RenderCore::GLSLCompiledBinaryResultPacker::Header Reflection;
    };

    class OpenGLVertexShader : public RHI::RHIVertexShader,public OpenGLShaderBase
    {
    public:
        OpenGLVertexShader();
        ~OpenGLVertexShader() override;

        bool Compile(const std::vector<char>& source);
        GLuint GetHandle() const { return ShaderHandle; }
        void SetHandle(GLuint handle) { ShaderHandle = handle; }

    private:
        GLuint ShaderHandle = 0;
        std::vector<char> SourceCode;
    };
    using OpenGLVertexShaderSP = std::shared_ptr<OpenGLVertexShader>;

    class OpenGLFragmentShader : public RHI::RHIFragmentShader, public OpenGLShaderBase
    {
    public:
        OpenGLFragmentShader();
        ~OpenGLFragmentShader() override;

        bool Compile(const std::vector<char>& source);
        GLuint GetHandle() const { return ShaderHandle; }
        void SetHandle(GLuint handle) { ShaderHandle = handle; }

    private:
        GLuint ShaderHandle = 0;
        std::vector<char> SourceCode;
    };
    using OpenGLFragmentShaderSP = std::shared_ptr<OpenGLFragmentShader>;

    class OpenGLComputeShader : public RHI::RHIComputeShader, public OpenGLShaderBase
    {
    public:
        OpenGLComputeShader();
        ~OpenGLComputeShader() override;

        bool Compile(const std::vector<char>& source);
        GLuint GetHandle() const { return ShaderHandle; }
        void SetHandle(GLuint handle) { ShaderHandle = handle; }

    private:
        GLuint ShaderHandle = 0;
        std::vector<char> SourceCode;
    };
    using OpenGLComputeShaderSP = std::shared_ptr<OpenGLComputeShader>;

    class OpenGLGeometryShader : public RHI::RHIGeometryShader, public OpenGLShaderBase
    {
    public:
        OpenGLGeometryShader();
        ~OpenGLGeometryShader() override;

        bool Compile(const std::vector<char>& source);
        GLuint GetHandle() const { return ShaderHandle; }
        void SetHandle(GLuint handle) { ShaderHandle = handle; }

    private:
        GLuint ShaderHandle = 0;
        std::vector<char> SourceCode;
    };
    using OpenGLGeometryShaderSP = std::shared_ptr<OpenGLGeometryShader>;

    class OpenGLTessControlShader : public RHI::RHITessControlShader, public OpenGLShaderBase
    {
    public:
        OpenGLTessControlShader();
        ~OpenGLTessControlShader() override;

        bool Compile(const std::vector<char>& source);
        GLuint GetHandle() const { return ShaderHandle; }
        void SetHandle(GLuint handle) { ShaderHandle = handle; }

    private:
        GLuint ShaderHandle = 0;
        std::vector<char> SourceCode;
    };
    using OpenGLTessControlShaderSP = std::shared_ptr<OpenGLTessControlShader>;

    class OpenGLTessEvalShader : public RHI::RHITessEvalShader, public OpenGLShaderBase
    {
    public:
        OpenGLTessEvalShader();
        ~OpenGLTessEvalShader() override;

        bool Compile(const std::vector<char>& source);
        GLuint GetHandle() const { return ShaderHandle; }
        void SetHandle(GLuint handle) { ShaderHandle = handle; }

    private:
        GLuint ShaderHandle = 0;
        std::vector<char> SourceCode;
    };
    using OpenGLTessEvalShaderSP = std::shared_ptr<OpenGLTessEvalShader>;

    class OpenGLSampler : public RHI::RHISampler
    {
    public:
        explicit OpenGLSampler(const RHI::RHISamplerDesc& desc);
        ~OpenGLSampler() override;

        GLuint GetHandle() const { return SamplerHandle; }
        void SetHandle(GLuint handle) { SamplerHandle = handle; }

    private:
        GLuint SamplerHandle = 0;
    };
    using OpenGLSamplerSP = std::shared_ptr<OpenGLSampler>;

    class OpenGLVertexDescState : public RHI::RHIVertexDescState
    {
    public:
        explicit OpenGLVertexDescState(const RHI::RHIVertexDescStateDesc& desc);
        ~OpenGLVertexDescState() override = default;
    };
    using OpenGLVertexDescStateSP = std::shared_ptr<OpenGLVertexDescState>;

    class OpenGLRasterizerState : public RHI::RHIRasterizerState
    {
    public:
        explicit OpenGLRasterizerState(const RHI::RHIRasterizerStateDesc& desc);
        ~OpenGLRasterizerState() override = default;
    };
    using OpenGLRasterizerStateSP = std::shared_ptr<OpenGLRasterizerState>;

    class OpenGLColorBlendState : public RHI::RHIColorBlendState
    {
    public:
        explicit OpenGLColorBlendState(const RHI::RHIColorBlendStateDesc& desc);
        ~OpenGLColorBlendState() override = default;
    };
    using OpenGLColorBlendStateSP = std::shared_ptr<OpenGLColorBlendState>;

    class OpenGLDepthStencilState : public RHI::RHIDepthStencilState
    {
    public:
        explicit OpenGLDepthStencilState(const RHI::RHIDepthStencilStateDesc& desc);
        ~OpenGLDepthStencilState() override = default;
    };
    using OpenGLDepthStencilStateSP = std::shared_ptr<OpenGLDepthStencilState>;

    class OpenGLStagingBuffer : public RHI::RHIStagingBuffer
    {
    public:
        explicit OpenGLStagingBuffer(uint32_t size);
        ~OpenGLStagingBuffer() ;

        void* Map(uint32_t offset, uint32_t numBytes) override;
        void Unmap() override;

        void* GetMappedData() const { return MappedData; }

    private:
        void* MappedData = nullptr;
    };
    using OpenGLStagingBufferSP = std::shared_ptr<OpenGLStagingBuffer>;
}
