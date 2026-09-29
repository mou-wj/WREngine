#include "OpenGLRenderTarget.h"
#include "OpenGLQueue.h"

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

        // 本函数可能在主线程（swapchain 构造）和 worker（BeginRenderPass 命令）被调用。
        // 主线程走"转发并等待"；worker 内部调用会被任务接口自动内联执行，行为不变。
        EnqueueTaskAndWait([this]()
        {
            glGenFramebuffers(1, &FramebufferHandle);
        });
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

        // 析构路径可能在主线程（swapchain 销毁）触发，转发到渲染线程删除；
        // worker 已停止时任务接口自动内联执行（此时无 current，同原行为）。
        const GLuint handle = FramebufferHandle;
        FramebufferHandle = 0;
        ColorAttachmentCount = 0;
        HasDepthAttachment = false;
        EnqueueTask([handle]()
        {
            glDeleteFramebuffers(1, &handle);
        });
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
