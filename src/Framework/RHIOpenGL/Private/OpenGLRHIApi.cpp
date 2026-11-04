#include <limits>
#include <stdexcept>
#include "OpenGLUtils.h"
#include "OpenGLRHIApi.h"
#include "OpenGLPlatformSurport.h"
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>


namespace RHIOpenGL
{
    OpenGLRHIApi::~OpenGLRHIApi() {
        Shutdown();
    }

    bool OpenGLRHIApi::Init()
    {
        RHI::GShaderPlatform = RHI::ERHIShaderPlatform::OpenGL;
        m_PlatformInfo.DepthRange = RHI::EDepthRange::ZeroToOne;
        m_PlatformInfo.bEnableRayTracing = false;
        RHI::GShaderPlatform = RHI::ERHIShaderPlatform::OpenGL;

        // 转换数据的占位偏移；当前未创建原生屏障数据。
        RHI::G_RHITransition_PrivateDataOffset = 1;

        // 转换对象的占位大小。
        RHI::G_RHITransition_TotalSize = 2;
        bool initPlatform = InitializePlatformSurport();
        OpenGLQueueManager::GetInstance().StartWorker();

        return initPlatform;
    }

    void OpenGLRHIApi::Shutdown()
    {
        // 先等两条队列把已提交命令全部执行完再停 worker——
        // StopWorker 只 join 线程，不消费队列里剩余的 entry，直接停会
        // 把挂起的命令（含 Present/异步释放任务）整条丢弃。
        if (auto* graphicsQueue = OpenGLQueueManager::GetInstance().GetQueue(RHI::EQueueType::Graphics))
        {
            graphicsQueue->WaitIdle();
        }
        if (auto* computeQueue = OpenGLQueueManager::GetInstance().GetQueue(RHI::EQueueType::Compute))
        {
            computeQueue->WaitIdle();
        }
        OpenGLQueueManager::GetInstance().StopWorker();
        ShutdownPlatformSurport();
    }


    const RHI::RHIPlatformInfo& OpenGLRHIApi::GetPlatformInfo() const
    {
        return m_PlatformInfo;
    }

    RHI::RHITextureSP OpenGLRHIApi::CreateTexture(const RHI::RHITextureDesc& desc)
    {
        return std::make_shared<OpenGLTexture>(desc);
    }

    RHI::RHIBufferSP OpenGLRHIApi::CreateBuffer(const RHI::RHIBufferDesc& desc)
    {
        return std::make_shared<OpenGLBuffer>(desc);
    }

    void OpenGLRHIApi::UpdateTexture(RHI::RHICommandListBase& cmdList, RHI::RHITexture* texture, const void* data, const RHI::RHIUpdateTextureRegion& region)
    {
        (void)cmdList;
        auto* glTexture = dynamic_cast<OpenGLTexture*>(texture);
        if (!glTexture || !data || region.Width == 0 || region.Height == 0 || region.Depth == 0)
        {
            return;
        }

        const auto& desc = glTexture->GetDesc();
        if (region.MipLevel >= desc.MipLevels) throw std::invalid_argument("Texture upload mip is out of range");
        const uint32_t width=std::max(1u,desc.Width>>region.MipLevel), height=std::max(1u,desc.Height>>region.MipLevel);
        const bool layered=desc.Type==RHI::ERHITextureType::Texture2DArray || desc.Type==RHI::ERHITextureType::TextureCube || desc.Type==RHI::ERHITextureType::TextureCubeArray;
        const uint32_t depth=layered ? desc.ArraySize : std::max(1u,desc.Depth>>region.MipLevel);
        const uint32_t z=layered ? region.ArraySlice : region.ZOffset;
        if (uint64_t(region.XOffset)+region.Width>width || uint64_t(region.YOffset)+region.Height>height || uint64_t(z)+region.Depth>depth)
            throw std::invalid_argument("Texture upload region is out of range");
        const GLuint handle=glTexture->GetHandle();
        const GLenum format=GetOpenGLTextureFormat(desc.Format), type=GetOpenGLTextureType(desc.Format);
        // All GL operations stay on the worker; consume the caller's pixels before returning.
        EnqueueTaskAndWait([handle,&region,z,format,type,data,textureType=desc.Type]()
        {
            GLint alignment=4; glGetIntegerv(GL_UNPACK_ALIGNMENT,&alignment); glPixelStorei(GL_UNPACK_ALIGNMENT,1);
            if (textureType==RHI::ERHITextureType::Texture1D)
                glTextureSubImage1D(handle,region.MipLevel,region.XOffset,region.Width,format,type,data);
            else if (textureType==RHI::ERHITextureType::Texture2D)
                glTextureSubImage2D(handle,region.MipLevel,region.XOffset,region.YOffset,region.Width,region.Height,format,type,data);
            else
                glTextureSubImage3D(handle,region.MipLevel,region.XOffset,region.YOffset,z,region.Width,region.Height,region.Depth,format,type,data);
            glPixelStorei(GL_UNPACK_ALIGNMENT,alignment);
        });
    }

    void OpenGLRHIApi::UpdateBuffer(RHI::RHICommandListBase& cmdList, RHI::RHIBuffer* buffer, const void* data, const RHI::RHIUpdateBufferRegion& region)
    {
        (void)cmdList;
        auto* glBuffer = dynamic_cast<OpenGLBuffer*>(buffer);
        if (!glBuffer || !data || region.Size == 0 ||
            static_cast<uint64_t>(region.Offset) + region.Size > glBuffer->GetDesc().Size)
        {
            return;
        }

        // 同 UpdateTexture：data 指针属于调用方，必须等待写入完成再返回。
        const GLuint bufferHandle = glBuffer->GetHandle();
        EnqueueTaskAndWait([bufferHandle, &region, data]()
        {
            glBindBuffer(GL_ARRAY_BUFFER, bufferHandle);
            glBufferSubData(GL_ARRAY_BUFFER,
                static_cast<GLintptr>(region.Offset),
                static_cast<GLsizeiptr>(region.Size),
                data);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
        });
    }

    void* OpenGLRHIApi::MapReadTexture(RHI::RHICommandListBase& cmdList, RHI::RHITexture* texture, const RHI::RHIReadTextureInfo& info)
    {
        (void)cmdList;
        auto* glTexture = dynamic_cast<OpenGLTexture*>(texture);
        if (!glTexture)
        {
            return nullptr;
        }

        const auto& desc = glTexture->GetDesc();
        if (info.MipLevel >= desc.MipLevels || info.ArraySlice >= desc.ArraySize)
        {
            return nullptr;
        }

        const auto formatInfo = RHI::GFormatInfoMap.find(desc.Format);
        if (formatInfo == RHI::GFormatInfoMap.end() || formatInfo->second.BytesPerPixel == 0)
        {
            return nullptr;
        }

        const uint32_t mipWidth = std::max(1u, desc.Width >> info.MipLevel);
        const uint32_t mipHeight = std::max(1u, desc.Height >> info.MipLevel);
        const uint32_t mipDepth=desc.Type==RHI::ERHITextureType::Texture3D ? std::max(1u,desc.Depth>>info.MipLevel) : 1;
        const size_t byteCount=static_cast<size_t>(mipWidth)*mipHeight*mipDepth*formatInfo->second.BytesPerPixel;
        if (byteCount>static_cast<size_t>(std::numeric_limits<GLsizei>::max())) throw std::invalid_argument("Texture readback exceeds GL size limit");
        void* mappedData=std::malloc(byteCount);
        if (!mappedData) throw std::bad_alloc();
        const GLuint handle=glTexture->GetHandle();
        const GLenum format=GetOpenGLTextureFormat(desc.Format), type=GetOpenGLTextureType(desc.Format);
        const uint32_t z=desc.Type==RHI::ERHITextureType::Texture3D ? 0 : info.ArraySlice;
        EnqueueTaskAndWait([=]()
        {
            GLint alignment=4; glGetIntegerv(GL_PACK_ALIGNMENT,&alignment); glPixelStorei(GL_PACK_ALIGNMENT,1);
            glGetTextureSubImage(handle,info.MipLevel,0,0,z,mipWidth,mipHeight,mipDepth,format,type,static_cast<GLsizei>(byteCount),mappedData);
            glPixelStorei(GL_PACK_ALIGNMENT,alignment);
        });
        return mappedData;
    }

    void* OpenGLRHIApi::MapReadBuffer(RHI::RHICommandListBase& cmdList, RHI::RHIBuffer* buffer, const RHI::RHIReadBufferInfo& info)
    {
        (void)cmdList;
        auto* glBuffer = dynamic_cast<OpenGLBuffer*>(buffer);
        if (!glBuffer || info.Offset > glBuffer->GetDesc().Size)
        {
            return nullptr;
        }

        const uint64_t availableSize = glBuffer->GetDesc().Size - info.Offset;
        const uint64_t readSize = info.Size == 0 ? availableSize : info.Size;
        if (readSize == 0 || readSize > availableSize)
        {
            return nullptr;
        }

        void* mappedData = std::malloc(static_cast<size_t>(readSize));
        if (!mappedData)
        {
            return nullptr;
        }

        // 同 MapReadTexture：等待读回完成后再返回指针。
        const GLuint bufferHandle = glBuffer->GetHandle();
        EnqueueTaskAndWait([bufferHandle, &info, readSize, mappedData]()
        {
            glBindBuffer(GL_ARRAY_BUFFER, bufferHandle);
            glGetBufferSubData(GL_ARRAY_BUFFER, static_cast<GLintptr>(info.Offset),
                static_cast<GLsizeiptr>(readSize), mappedData);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
        });
        return mappedData;
    }

    void OpenGLRHIApi::Unmap(void* mappedData)
    {
        std::free(mappedData);
    }

    RHI::RHIShaderResourceViewSP OpenGLRHIApi::CreateTextureShaderResourceView(RHI::RHITexture* texture, const RHI::RHITexSRVCreateInfo& desc)
    {
        return std::make_shared<OpenGLShaderResourceView>(texture,desc);
    }

    RHI::RHIUnorderedAccessViewSP OpenGLRHIApi::CreateTextureUnorderedAccessView(RHI::RHITexture* texture, const RHI::RHITexUAVCreateInfo& desc)
    {
        return std::make_shared<OpenGLUnorderedAccessView>(texture, desc);
    }

    RHI::RHIShaderResourceViewSP OpenGLRHIApi::CreateBufferShaderResourceView(RHI::RHIBuffer* buffer, const RHI::RHIBufferSRVCreateInfo& desc)
    {
        return std::make_shared<OpenGLShaderResourceView>(buffer,desc);
    }

    RHI::RHIUnorderedAccessViewSP OpenGLRHIApi::CreateBufferUnorderedAccessView(RHI::RHIBuffer* buffer, const RHI::RHIBufferUAVCreateInfo& desc)
    {
        return std::make_shared<OpenGLUnorderedAccessView>(buffer,desc);
    }

    RHI::RHIRayTracingGeometrySP OpenGLRHIApi::CreateRayTracingGeometry(const RHI::RHIRayTracingGeometryDesc& desc)
    {
        ThrowRayTracingUnsupported();
    }

    RHI::RHIRayTracingInstanceSP OpenGLRHIApi::CreateRayTracingInstance(const RHI::RHIRayTracingInstancesDesc& desc)
    {
        ThrowRayTracingUnsupported();
    }

    RHI::RHIStagingBufferSP OpenGLRHIApi::CreateStagingBuffer(uint32_t size)
    {
        return std::make_shared<OpenGLStagingBuffer>(size);
    }

    RHI::RHIGraphicsPipelineStateSP OpenGLRHIApi::CreateGraphicsPipelineState(const RHI::RHIGraphicsPipelineStateDesc& desc)
    {
        return std::make_shared<OpenGLGraphicsPipelineState>(desc);
    }

    RHI::RHIComputePipelineStateSP OpenGLRHIApi::CreateComputePipelineState(const RHI::RHIComputePipelineStateDesc& desc)
    {
        return std::make_shared<OpenGLComputePipelineState>(desc);
    }

    RHI::RHIRayTracingPipelineStateSP OpenGLRHIApi::CreateRayTracingsPipelineState(const RHI::RHIRayTracingPipelineStateDesc& desc)
    {
        ThrowRayTracingUnsupported();
    }

    RHI::RHIVertexDescStateSP OpenGLRHIApi::CreateVertexDescState(const RHI::RHIVertexDescStateDesc& desc)
    {
        return std::make_shared<OpenGLVertexDescState>(desc);
    }

    RHI::RHIRasterizerStateSP OpenGLRHIApi::CreateRasterizerState(const RHI::RHIRasterizerStateDesc& desc)
    {
        return std::make_shared<OpenGLRasterizerState>(desc);
    }

    RHI::RHIColorBlendStateSP OpenGLRHIApi::CreateColorBlendState(const RHI::RHIColorBlendStateDesc& desc)
    {
        return std::make_shared<OpenGLColorBlendState>(desc);
    }

    RHI::RHIDepthStencilStateSP OpenGLRHIApi::CreateDepthStencilState(const RHI::RHIDepthStencilStateDesc& desc)
    {
        return std::make_shared<OpenGLDepthStencilState>(desc);
    }

    RHI::RHIVertexShaderSP OpenGLRHIApi::CreateVertexShader(const std::vector<char>& shaderSourceCode)
    {
        auto shader = std::make_shared<OpenGLVertexShader>();
        shader->Compile(shaderSourceCode);
        return shader;
    }

    RHI::RHIFragmentShaderSP OpenGLRHIApi::CreateFragmentShader(const std::vector<char>& shaderSourceCode)
    {
        auto shader = std::make_shared<OpenGLFragmentShader>();
        shader->Compile(shaderSourceCode);
        return shader;
    }

    RHI::RHIComputeShaderSP OpenGLRHIApi::CreateComputeShader(const std::vector<char>& shaderSourceCode)
    {
        auto shader = std::make_shared<OpenGLComputeShader>();
        shader->Compile(shaderSourceCode);
        return shader;
    }

    RHI::RHIGeometryShaderSP OpenGLRHIApi::CreateGeometryShader(const std::vector<char>& shaderSourceCode)
    {
        auto shader = std::make_shared<OpenGLGeometryShader>();
        shader->Compile(shaderSourceCode);
        return shader;
    }

    RHI::RHITessControlShaderSP OpenGLRHIApi::CreateTessControlShader(const std::vector<char>& shaderSourceCode)
    {
        auto shader = std::make_shared<OpenGLTessControlShader>();
        shader->Compile(shaderSourceCode);
        return shader;
    }

    RHI::RHITessEvalShaderSP OpenGLRHIApi::CreateTessEvalShader(const std::vector<char>& shaderSourceCode)
    {
        auto shader = std::make_shared<OpenGLTessEvalShader>();
        shader->Compile(shaderSourceCode);
        return shader;
    }

    RHI::RHIMeshShaderSP OpenGLRHIApi::CreateMeshShader(const std::vector<char>& shaderSourceCode)
    {
        return nullptr;
    }

    RHI::RHITaskShaderSP OpenGLRHIApi::CreateTaskShader(const std::vector<char>& shaderSourceCode)
    {
        return nullptr;
    }

    RHI::RHIRayGenShaderSP OpenGLRHIApi::CreateRayGenShader(const std::vector<char>& shaderSourceCode)
    {
        ThrowRayTracingUnsupported();
    }

    RHI::RHICloseHitShaderSP OpenGLRHIApi::CreateCloseHitShader(const std::vector<char>& shaderSourceCode)
    {
        ThrowRayTracingUnsupported();
    }

    RHI::RHIMissShaderSP OpenGLRHIApi::CreateMissShader(const std::vector<char>& shaderSourceCode)
    {
        ThrowRayTracingUnsupported();
    }

    RHI::RHIAnyHitShaderSP OpenGLRHIApi::CreateAnyHitShader(const std::vector<char>& shaderSourceCode)
    {
        ThrowRayTracingUnsupported();
    }

    RHI::RHIIntersectionShaderSP OpenGLRHIApi::CreateIntersectionShader(const std::vector<char>& shaderSourceCode)
    {
        ThrowRayTracingUnsupported();
    }

    RHI::RHICallableShaderSP OpenGLRHIApi::CreateCallableShader(const std::vector<char>& shaderSourceCode)
    {
        ThrowRayTracingUnsupported();
    }

    RHI::RHISwapchainSP OpenGLRHIApi::CreateSwapchain(void* inWindowHandle, uint32_t w, uint32_t h, RHI::ERHIFormat format)
    {
        return std::make_shared<OpenGLSwapchain>(inWindowHandle, w, h, format);
    }

    RHI::RHISamplerSP OpenGLRHIApi::CreateSampler(const RHI::RHISamplerDesc& desc)
    {
        return std::make_shared<OpenGLSampler>(desc);
    }

    RHI::RHIQueue* OpenGLRHIApi::GetQueue(RHI::EQueueType type)
    {
		return OpenGLQueueManager::GetInstance().GetQueue(type);
    }

    RHI::RHIPresentExecutor* OpenGLRHIApi::GetPresentExecutor()
    {
        return &OpenGLQueueManager::GetInstance();
    }

    void OpenGLRHIApi::RHICreateTransition(RHI::RHITransition* transition, const RHI::RHITransitionCreateInfo& createInfo)
    {
    }

    void OpenGLRHIApi::RHIReleaseTransition(RHI::RHITransition* transition)
    {
    }

    RHI::RHITransientResourceManagerSP OpenGLRHIApi::CreateTransientResourceManager()
    {
        return std::make_shared<OpenGLTransientResourceManager>();
    }

    OpenGLRHIModule::OpenGLRHIModule() = default;
    OpenGLRHIModule::~OpenGLRHIModule() = default;

    void OpenGLRHIModule::StartupModule()
    {
        RHI::GRHIApi = CreateRHIApi();
        RHI::GRHIApi->Init();
        m_bLoaded = true;
    }

    void OpenGLRHIModule::ShutdownModule()
    {
        delete RHI::GRHIApi;
        RHI::GRHIApi = nullptr;
        m_bLoaded = false;
    }

    bool OpenGLRHIModule::IsLoaded() const
    {
        return m_bLoaded;
    }

    RHI::RHIApi* OpenGLRHIModule::CreateRHIApi()
    {
        return new OpenGLRHIApi();
    }

    IMPLEMENT_SIMPLE_MODULE(OpenGLRHIModule, "RHIOpenGL");
}
