#include "OpenGLTransientResource.h"
#include "OpenGLQueue.h"
#include <stdexcept>

namespace RHIOpenGL
{
    namespace
    {
        void WaitForGPU()
        {
            auto& manager = OpenGLQueueManager::GetInstance();
            if (!manager.IsWorkerThread())
            {
                manager.GetQueue(RHI::EQueueType::Graphics)->WaitIdle();
                manager.GetQueue(RHI::EQueueType::Compute)->WaitIdle();
            }
            manager.EnqueueTaskAndWait([] { glFinish(); });
        }
    } // namespace
    OpenGLTransientTexture::OpenGLTransientTexture(const RHI::RHITextureSP& texture) : RHITransientTexture(texture)
    {
    }
    OpenGLTransientBuffer::OpenGLTransientBuffer(const RHI::RHIBufferSP& buffer) : RHITransientBuffer(buffer)
    {
    }
    OpenGLTransientResourceManager::~OpenGLTransientResourceManager()
    {
        WaitForGPU();
        ClearResources();
        OpenGLQueueManager::GetInstance().EnqueueTaskAndWait([] {});
    }
    RHI::RHITextureSP OpenGLTransientResourceManager::CreateTextureInternal(const RHI::RHITextureDesc& desc)
    {
        auto resource = std::make_shared<OpenGLTexture>(desc);
        if (!resource->GetHandle())
            throw std::runtime_error("Failed to create transient OpenGL texture");
        return resource;
    }
    RHI::RHIBufferSP OpenGLTransientResourceManager::CreateBufferInternal(const RHI::RHIBufferDesc& desc)
    {
        auto resource = std::make_shared<OpenGLBuffer>(desc);
        if (!resource->GetHandle())
            throw std::runtime_error("Failed to create transient OpenGL buffer");
        return resource;
    }
    bool OpenGLTransientResourceManager::CanRecycle(RHI::RHIViewableResource&)
    {
        if (OpenGLQueueManager::GetInstance().IsWorkerThread())
            return false;
        WaitForGPU();
        return true;
    }
} // namespace RHIOpenGL
