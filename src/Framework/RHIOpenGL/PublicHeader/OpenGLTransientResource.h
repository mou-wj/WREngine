#pragma once
#include "RHITransientResource.h"
#include "OpenGLResource.h"

namespace RHIOpenGL
{
    class OpenGLTransientTexture : public RHI::RHITransientTexture
    {
    public:
        explicit OpenGLTransientTexture(const RHI::RHITextureSP& texture);
    };
    using OpenGLTransientTextureSP = std::shared_ptr<OpenGLTransientTexture>;
    class OpenGLTransientBuffer : public RHI::RHITransientBuffer
    {
    public:
        explicit OpenGLTransientBuffer(const RHI::RHIBufferSP& buffer);
    };
    using OpenGLTransientBufferSP = std::shared_ptr<OpenGLTransientBuffer>;

    class OpenGLTransientResourceManager final : public RHI::RHITransientResourceManager
    {
    public:
        OpenGLTransientResourceManager() = default;
        ~OpenGLTransientResourceManager() override;

    protected:
        RHI::RHITextureSP CreateTextureInternal(const RHI::RHITextureDesc& desc) override;
        RHI::RHIBufferSP CreateBufferInternal(const RHI::RHIBufferDesc& desc) override;
        bool CanRecycle(RHI::RHIViewableResource& resource) override;
    };
    using OpenGLTransientResourceManagerSP = std::shared_ptr<OpenGLTransientResourceManager>;
} // namespace RHIOpenGL
