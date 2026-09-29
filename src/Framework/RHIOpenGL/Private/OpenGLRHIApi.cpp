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
        PlatformInfo.DepthRange = RHI::EDepthRange::ZeroToOne;
        PlatformInfo.EnableRayTracing = false;
        RHI::GShaderPlatform = RHI::ERHIShaderPlatform::OpenGL;

        // 3. ����ƫ���� (���� Header ֮��ĵ�ַ)
        RHI::G_RHITransition_PrivateDataOffset = 1;

        // 4. �����ܷ����С
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
        return PlatformInfo;
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
        if (!glTexture || !data || region.width == 0 || region.height == 0 || region.depth == 0)
        {
            return;
        }

        const auto& desc = glTexture->GetDesc();
        if (region.mipLevel >= desc.MipLevels ||
            region.xOffset + region.width > desc.Width ||
            region.yOffset + region.height > desc.Height ||
            region.zOffset != 0 || region.depth != 1)
        {
            return;
        }

        // 转发到渲染线程并等待完成：data 指针属于调用方栈帧，异步版无法保证
        // worker 消费时仍然存活。
        const GLuint textureHandle = glTexture->GetHandle();
        const GLenum glFormat = GetOpenGLTextureFormat(desc.Format);
        const GLenum glType = GetOpenGLTextureType(desc.Format);
        EnqueueTaskAndWait([textureHandle, &region, glFormat, glType, data]()
        {
            glBindTexture(GL_TEXTURE_2D, textureHandle);
            glTexSubImage2D(GL_TEXTURE_2D,
                static_cast<GLint>(region.mipLevel),
                static_cast<GLint>(region.xOffset),
                static_cast<GLint>(region.yOffset),
                static_cast<GLsizei>(region.width),
                static_cast<GLsizei>(region.height),
                glFormat,
                glType,
                data);
            glBindTexture(GL_TEXTURE_2D, 0);
        });
    }

    void OpenGLRHIApi::UpdateBuffer(RHI::RHICommandListBase& cmdList, RHI::RHIBuffer* buffer, const void* data, const RHI::RHIUpdateBufferRegion& region)
    {
        (void)cmdList;
        auto* glBuffer = dynamic_cast<OpenGLBuffer*>(buffer);
        if (!glBuffer || !data || region.size == 0 ||
            static_cast<uint64_t>(region.offset) + region.size > glBuffer->GetDesc().Size)
        {
            return;
        }

        // 同 UpdateTexture：data 指针属于调用方，必须等待写入完成再返回。
        const GLuint bufferHandle = glBuffer->GetHandle();
        EnqueueTaskAndWait([bufferHandle, &region, data]()
        {
            glBindBuffer(GL_ARRAY_BUFFER, bufferHandle);
            glBufferSubData(GL_ARRAY_BUFFER,
                static_cast<GLintptr>(region.offset),
                static_cast<GLsizeiptr>(region.size),
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
        const size_t byteCount = static_cast<size_t>(mipWidth) * mipHeight * formatInfo->second.BytesPerPixel;
        void* mappedData = std::malloc(byteCount == 0 ? 1 : byteCount);
        if (!mappedData)
        {
            return nullptr;
        }

        // 读回必须在渲染线程执行并等待完成——返回的指针内容要在返回前填充好。
        const GLuint textureHandle = glTexture->GetHandle();
        const GLenum glFormat = GetOpenGLTextureFormat(desc.Format);
        const GLenum glType = GetOpenGLTextureType(desc.Format);
        EnqueueTaskAndWait([textureHandle, &info, glFormat, glType, mappedData]()
        {
            glBindTexture(GL_TEXTURE_2D, textureHandle);
            glGetTexImage(GL_TEXTURE_2D, static_cast<GLint>(info.MipLevel),
                glFormat, glType, mappedData);
            glBindTexture(GL_TEXTURE_2D, 0);
        });
        return mappedData;
    }

    void* OpenGLRHIApi::MapReadBuffer(RHI::RHICommandListBase& cmdList, RHI::RHIBuffer* buffer, const RHI::RHIReadBufferInfo& info)
    {
        (void)cmdList;
        auto* glBuffer = dynamic_cast<OpenGLBuffer*>(buffer);
        if (!glBuffer || info.offset > glBuffer->GetDesc().Size)
        {
            return nullptr;
        }

        const uint64_t availableSize = glBuffer->GetDesc().Size - info.offset;
        const uint64_t readSize = info.size == 0 ? availableSize : info.size;
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
            glGetBufferSubData(GL_ARRAY_BUFFER, static_cast<GLintptr>(info.offset),
                static_cast<GLsizeiptr>(readSize), mappedData);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
        });
        return mappedData;
    }

    void OpenGLRHIApi::Unmap(void* mappedData)
    {
        std::free(mappedData);
    }

    RHI::RHIShaderResourceViewSP OpenGLRHIApi::CreateTextureShaderResourceView(RHI::RHITexture* Texture, const RHI::RHITexSRVCreateInfo& Desc)
    {
        return std::make_shared<OpenGLShaderResourceView>(Texture,Desc);
    }

    RHI::RHIUnorderedAccessViewSP OpenGLRHIApi::CreateTextureUnorderedAccessView(RHI::RHITexture* Texture, const RHI::RHITexUAVCreateInfo& Desc)
    {
        return std::make_shared<OpenGLUnorderedAccessView>(Texture, Desc);
    }

    RHI::RHIShaderResourceViewSP OpenGLRHIApi::CreateBufferShaderResourceView(RHI::RHIBuffer* Buffer, const RHI::RHIBufferSRVCreateInfo& Desc)
    {
        return std::make_shared<OpenGLShaderResourceView>(Buffer,Desc);
    }

    RHI::RHIUnorderedAccessViewSP OpenGLRHIApi::CreateBufferUnorderedAccessView(RHI::RHIBuffer* Buffer, const RHI::RHIBufferUAVCreateInfo& Desc)
    {
        return std::make_shared<OpenGLUnorderedAccessView>(Buffer,Desc);
    }

    RHI::RHIRayTracingGeometrySP OpenGLRHIApi::CreateRayTracingGeometry(const RHI::RHIRayTracingGeometryDesc& desc)
    {
        return nullptr;
    }

    RHI::RHIRayTracingInstanceSP OpenGLRHIApi::CreateRayTracingInstance(const RHI::RHIRayTracingInstancesDesc& desc)
    {
        return nullptr;
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
        return nullptr;
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
        return nullptr;
    }

    RHI::RHICloseHitShaderSP OpenGLRHIApi::CreateCloseHitShader(const std::vector<char>& shaderSourceCode)
    {
        return nullptr;
    }

    RHI::RHIMissShaderSP OpenGLRHIApi::CreateMissShader(const std::vector<char>& shaderSourceCode)
    {
        return nullptr;
    }

    RHI::RHIAnyHitShaderSP OpenGLRHIApi::CreateAnyHitShader(const std::vector<char>& shaderSourceCode)
    {
        return nullptr;
    }

    RHI::RHIIntersectionShaderSP OpenGLRHIApi::CreateIntersectionShader(const std::vector<char>& shaderSourceCode)
    {
        return nullptr;
    }

    RHI::RHICallableShaderSP OpenGLRHIApi::CreateCallableShader(const std::vector<char>& shaderSourceCode)
    {
        return nullptr;
    }

    RHI::RHISwapchainSP OpenGLRHIApi::CreateSwapchain(void* inWindowHandle, uint32_t w, uint32_t h, RHI::ERHIFormat format)
    {
        return std::make_shared<OpenGLSwapchain>(inWindowHandle, w, h, format);
    }

    RHI::RHISamplerSP OpenGLRHIApi::CreateSampler(const RHI::RHISamplerDesc& desc)
    {
        return std::make_shared<OpenGLSampler>(desc);
    }

    RHI::RHIQueue* OpenGLRHIApi::GetQueue(RHI::EQueueType Type)
    {
		return OpenGLQueueManager::GetInstance().GetQueue(Type);
    }

    RHI::RHIPresentExecutor* OpenGLRHIApi::GetPresentExecutor()
    {
        return &OpenGLQueueManager::GetInstance();
    }

    void OpenGLRHIApi::RHICreateTransition(RHI::RHITransition* Transition, const RHI::RHITransitionCreateInfo& CreateInfo)
    {
    }

    void OpenGLRHIApi::RHIReleaseTransition(RHI::RHITransition* Transition)
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
        bLoaded = true;
    }

    void OpenGLRHIModule::ShutdownModule()
    {
        delete RHI::GRHIApi;
        RHI::GRHIApi = nullptr;
        bLoaded = false;
    }

    bool OpenGLRHIModule::IsLoaded() const
    {
        return bLoaded;
    }

    RHI::RHIApi* OpenGLRHIModule::CreateRHIApi()
    {
        return new OpenGLRHIApi();
    }

    IMPLEMENT_SIMPLE_MODULE(OpenGLRHIModule, "RHIOpenGL");
}
