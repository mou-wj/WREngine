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

        GLuint GetHandle() const { return m_TextureHandle; }
        void SetHandle(GLuint handle) { m_TextureHandle = handle; }

    private:
        GLuint m_TextureHandle = 0;
    };
    using OpenGLTextureSP = std::shared_ptr<OpenGLTexture>;

    // =========================================================================
    // OpenGLTextureView
    // -----------------------------------------------------------------------------
    // 把一个 GL_TEXTURE_*_VIEW 创建出来的"视图纹理对象"包装成一个 RAII 句柄。
    // 持有：
    //   - ViewHandle         —— 调用 glGenTextures + glTextureView 得到的 GL 纹理名
    //   - SourceTexture*     —— 所观察的源纹理（不持有所有权，由外层管理生命周期）
    //   - ViewDesc           —— 视图中段（mip / array / format / target）信息
    // 它同时支持 SRV 路径（glBindTextureUnit）和 UAV 路径（glBindImageTexture）：
    // 调用方拿到 GetHandle() 后再按用途分别绑 texture/image unit。
    // =========================================================================
    class OpenGLTextureView
    {
    public:
        struct Desc
        {
            GLenum Target = GL_TEXTURE_2D;
            GLenum InternalFormat = GL_RGBA8;
            uint32_t FirstMipSlice = 0;
            uint32_t MipCount = 1;
            uint32_t FirstArraySlice = 0;
            uint32_t ArraySize = 1;
        };

        OpenGLTextureView(OpenGLTexture* source, const Desc& viewDesc);
        ~OpenGLTextureView();

        OpenGLTextureView(const OpenGLTextureView&) = delete;
        OpenGLTextureView& operator=(const OpenGLTextureView&) = delete;

        GLuint GetHandle() const { return m_ViewHandle; }
        OpenGLTexture* GetSource() const { return m_SourceTexture; }
        const Desc& GetDesc() const { return m_ViewInfo; }

    private:
        OpenGLTexture* m_SourceTexture = nullptr;
        GLuint m_ViewHandle = 0;
        Desc m_ViewInfo{};
    };

    class OpenGLBuffer : public RHI::RHIBuffer
    {
    public:
        explicit OpenGLBuffer(const RHI::RHIBufferDesc& desc);
        ~OpenGLBuffer() override;

        GLuint GetHandle() const { return m_BufferHandle; }
        void SetHandle(GLuint handle) { m_BufferHandle = handle; }

    private:
        GLuint m_BufferHandle = 0;
    };
    using OpenGLBufferSP = std::shared_ptr<OpenGLBuffer>;

    class OpenGLShaderResourceView : public RHI::RHIShaderResourceView
    {
    public:
        explicit OpenGLShaderResourceView(RHI::RHIViewableResource* resource, const RHI::RHITexSRVCreateInfo& desc);
        explicit OpenGLShaderResourceView(RHI::RHIViewableResource* resource, const RHI::RHIBufferSRVCreateInfo& desc);
        ~OpenGLShaderResourceView() override = default;
        bool IsTexture();
        bool IsBuffer();
        // 纹理视图情形下返回 OpenGLTextureView（持有 glTextureView 句柄）；
        // buffer 视图情形下返回 nullptr。绑定阶段取 GL handle 用 GetTextureView()->GetHandle()。
        OpenGLTextureView* GetTextureView() const { return m_TextureView.get(); }
        const RHI::RHITexSRVCreateInfo& GetTexSRVDesc() const { return m_TexSRVDesc; }
        const RHI::RHIBufferSRVCreateInfo& GetBufferSRVDesc() const { return m_BufferSRVDesc; }

    private:
        std::unique_ptr<OpenGLTextureView> m_TextureView;
        RHI::RHITexSRVCreateInfo m_TexSRVDesc;
        RHI::RHIBufferSRVCreateInfo m_BufferSRVDesc;
        bool m_bIsTextureView = false;
    };
    using OpenGLShaderResourceViewSP = std::shared_ptr<OpenGLShaderResourceView>;

    class OpenGLUnorderedAccessView : public RHI::RHIUnorderedAccessView
    {
    public:
        explicit OpenGLUnorderedAccessView(RHI::RHIViewableResource* resource, const RHI::RHITexUAVCreateInfo& desc);
        explicit OpenGLUnorderedAccessView(RHI::RHIViewableResource* resource, const RHI::RHIBufferUAVCreateInfo& desc);
        ~OpenGLUnorderedAccessView() override = default;
        bool IsTexture();
        bool IsBuffer();
        OpenGLTextureView* GetTextureView() const { return m_TextureView.get(); }
        const RHI::RHITexUAVCreateInfo& GetTexUAVDesc() const { return m_TexUAVDesc; }
        const RHI::RHIBufferUAVCreateInfo& GetBufferUAVDesc() const { return m_BufferUAVDesc; }
    private:
        std::unique_ptr<OpenGLTextureView> m_TextureView;
        RHI::RHITexUAVCreateInfo m_TexUAVDesc;
        RHI::RHIBufferUAVCreateInfo m_BufferUAVDesc;
        bool m_bIsTextureView = false;
    };
    using OpenGLUnorderedAccessViewSP = std::shared_ptr<OpenGLUnorderedAccessView>;
    using EGLSLShaderResourceType = RenderCore::GLSLCompiledBinaryResultPacker::EGLSLShaderResourceType;
    struct OpenGLShaderResourceBindingInfo{
        EGLSLShaderResourceType Type = EGLSLShaderResourceType::Texture;
        uint32_t Count= 1;
    };
    struct OpenGLShaderResourceLayout {
        // Texture / Sampler / Image / SSBO 等描述符资源
        std::unordered_map<uint32_t, OpenGLShaderResourceBindingInfo> Bindings;
        // 单独的 UBO 大小表（key = GL UBO binding），与 Bindings 分开，
        // 因为 UBO 没有 Count/ArrayIndex，体积也是描述符以外的属性。
        // 数据来源：GLSLCompiledBinaryResultPacker::Header::UniformBuffers[i].Size
        // （SPIRV-Cross `get_declared_struct_size`，即 cbuffer 声明大小）。
        std::unordered_map<uint32_t, uint32_t> UniformBufferSizes;
        bool IsValid(uint32_t binding, EGLSLShaderResourceType type,uint32_t innerIndex){
            if(Bindings.find(binding) == Bindings.end()){
                return false;
            }
            if(Bindings[binding].Type != type ||innerIndex>= Bindings[binding].Count){
                return false;
            }
            return true;
        }
        // 没记录时返回 0。
        uint32_t GetUniformBufferSize(uint32_t binding) const
        {
            auto it = UniformBufferSizes.find(binding);
            return (it == UniformBufferSizes.end()) ? 0u : it->second;
        }
    };

    class OpenGLShaderBase{
    public:
        RenderCore::GLSLCompiledBinaryResultPacker::Header GetShaderReflection() const { return _reflection; }
        bool CompileOpenGLShader(GLuint shaderHandle, const std::vector<char>& source, const char* shaderName);
        const OpenGLShaderResourceLayout& GetShaderResourceLayout() const { return _shaderResourceLayout; }
    protected:
        void FillOpenGLShaderResourceLayout();
        OpenGLShaderResourceLayout _shaderResourceLayout;
        RenderCore::GLSLCompiledBinaryResultPacker::Header _reflection;
    };

    class OpenGLVertexShader : public RHI::RHIVertexShader,public OpenGLShaderBase
    {
    public:
        OpenGLVertexShader();
        ~OpenGLVertexShader() override;

        bool Compile(const std::vector<char>& source);
        GLuint GetHandle() const { return m_ShaderHandle; }
        void SetHandle(GLuint handle) { m_ShaderHandle = handle; }

    private:
        GLuint m_ShaderHandle = 0;
        std::vector<char> m_SourceCode;
    };
    using OpenGLVertexShaderSP = std::shared_ptr<OpenGLVertexShader>;

    class OpenGLFragmentShader : public RHI::RHIFragmentShader, public OpenGLShaderBase
    {
    public:
        OpenGLFragmentShader();
        ~OpenGLFragmentShader() override;

        bool Compile(const std::vector<char>& source);
        GLuint GetHandle() const { return m_ShaderHandle; }
        void SetHandle(GLuint handle) { m_ShaderHandle = handle; }

    private:
        GLuint m_ShaderHandle = 0;
        std::vector<char> m_SourceCode;
    };
    using OpenGLFragmentShaderSP = std::shared_ptr<OpenGLFragmentShader>;

    class OpenGLComputeShader : public RHI::RHIComputeShader, public OpenGLShaderBase
    {
    public:
        OpenGLComputeShader();
        ~OpenGLComputeShader() override;

        bool Compile(const std::vector<char>& source);
        GLuint GetHandle() const { return m_ShaderHandle; }
        void SetHandle(GLuint handle) { m_ShaderHandle = handle; }

    private:
        GLuint m_ShaderHandle = 0;
        std::vector<char> m_SourceCode;
    };
    using OpenGLComputeShaderSP = std::shared_ptr<OpenGLComputeShader>;

    class OpenGLGeometryShader : public RHI::RHIGeometryShader, public OpenGLShaderBase
    {
    public:
        OpenGLGeometryShader();
        ~OpenGLGeometryShader() override;

        bool Compile(const std::vector<char>& source);
        GLuint GetHandle() const { return m_ShaderHandle; }
        void SetHandle(GLuint handle) { m_ShaderHandle = handle; }

    private:
        GLuint m_ShaderHandle = 0;
        std::vector<char> m_SourceCode;
    };
    using OpenGLGeometryShaderSP = std::shared_ptr<OpenGLGeometryShader>;

    class OpenGLTessControlShader : public RHI::RHITessControlShader, public OpenGLShaderBase
    {
    public:
        OpenGLTessControlShader();
        ~OpenGLTessControlShader() override;

        bool Compile(const std::vector<char>& source);
        GLuint GetHandle() const { return m_ShaderHandle; }
        void SetHandle(GLuint handle) { m_ShaderHandle = handle; }

    private:
        GLuint m_ShaderHandle = 0;
        std::vector<char> m_SourceCode;
    };
    using OpenGLTessControlShaderSP = std::shared_ptr<OpenGLTessControlShader>;

    class OpenGLTessEvalShader : public RHI::RHITessEvalShader, public OpenGLShaderBase
    {
    public:
        OpenGLTessEvalShader();
        ~OpenGLTessEvalShader() override;

        bool Compile(const std::vector<char>& source);
        GLuint GetHandle() const { return m_ShaderHandle; }
        void SetHandle(GLuint handle) { m_ShaderHandle = handle; }

    private:
        GLuint m_ShaderHandle = 0;
        std::vector<char> m_SourceCode;
    };
    using OpenGLTessEvalShaderSP = std::shared_ptr<OpenGLTessEvalShader>;

    class OpenGLSampler : public RHI::RHISampler
    {
    public:
        explicit OpenGLSampler(const RHI::RHISamplerDesc& desc);
        ~OpenGLSampler() override;

        GLuint GetHandle() const { return m_SamplerHandle; }
        void SetHandle(GLuint handle) { m_SamplerHandle = handle; }

    private:
        GLuint m_SamplerHandle = 0;
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

        void* GetMappedData() const { return m_MappedData; }

    private:
        void* m_MappedData = nullptr;
    };
    using OpenGLStagingBufferSP = std::shared_ptr<OpenGLStagingBuffer>;
}
