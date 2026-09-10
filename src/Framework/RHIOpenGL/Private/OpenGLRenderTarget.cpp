#include "OpenGLRenderTarget.h"

namespace RHIOpenGL
{
    OpenGLFramebuffer::~OpenGLFramebuffer()
    {
        Destroy();
    }

    void OpenGLFramebuffer::Create()
    {
        if (FramebufferHandle != 0)
        {
            return;
        }

        glGenFramebuffers(1, &FramebufferHandle);
    }

    void OpenGLFramebuffer::Reset()
    {
        if (FramebufferHandle == 0)
        {
            return;
        }

        glBindFramebuffer(GL_FRAMEBUFFER, FramebufferHandle);

        for (uint32_t i = 0; i < ColorAttachmentCount; ++i)
        {
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_TEXTURE_2D, 0, 0);
        }

        if (HasDepthAttachment)
        {
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, 0, 0);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_TEXTURE_2D, 0, 0);
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        ColorAttachmentCount = 0;
        HasDepthAttachment = false;
    }

    void OpenGLFramebuffer::Destroy()
    {
        if (FramebufferHandle == 0)
        {
            return;
        }

        glDeleteFramebuffers(1, &FramebufferHandle);
        FramebufferHandle = 0;
        ColorAttachmentCount = 0;
        HasDepthAttachment = false;
    }

    void OpenGLFramebuffer::Bind() const
    {
        if (FramebufferHandle != 0)
        {
            glBindFramebuffer(GL_FRAMEBUFFER, FramebufferHandle);
        }
    }

    void OpenGLFramebuffer::Unbind() const
    {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void OpenGLFramebuffer::AttachColorTexture(GLuint texture, uint32_t index, GLenum target)
    {
        if (FramebufferHandle == 0)
        {
            Create();
        }

        Bind();
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + index, target, texture, 0);

        if (index >= ColorAttachmentCount)
        {
            ColorAttachmentCount = index + 1;
        }
    }

    void OpenGLFramebuffer::AttachDepthTexture(GLuint texture, GLenum target)
    {
        if (FramebufferHandle == 0)
        {
            Create();
        }

        Bind();
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, target, texture, 0);
        HasDepthAttachment = true;
    }

    void OpenGLFramebuffer::AttachStencilTexture(GLuint texture, GLenum target)
    {
        if (FramebufferHandle == 0)
        {
            Create();
        }

        Bind();
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, target, texture, 0);
        HasDepthAttachment = true;
    }

    GLenum OpenGLFramebuffer::CheckStatus() const
    {
        if (FramebufferHandle == 0)
        {
            return GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT;
        }

        return glCheckFramebufferStatus(GL_FRAMEBUFFER);
    }

    bool OpenGLFramebuffer::IsComplete() const
    {
        return CheckStatus() == GL_FRAMEBUFFER_COMPLETE;
    }
}
