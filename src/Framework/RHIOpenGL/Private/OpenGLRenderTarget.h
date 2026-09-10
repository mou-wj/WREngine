#pragma once

#include "glad/gl.h"
#include <vector>
#include <memory>

namespace RHIOpenGL
{
    class OpenGLFramebuffer
    {
    public:
        OpenGLFramebuffer() = default;
        ~OpenGLFramebuffer();

        OpenGLFramebuffer(const OpenGLFramebuffer&) = delete;
        OpenGLFramebuffer& operator=(const OpenGLFramebuffer&) = delete;

        GLuint GetHandle() const { return FramebufferHandle; }

        void Create();
        void Reset();
        void Destroy();
        void Bind() const;
        void Unbind() const;
        void AttachColorTexture(GLuint texture, uint32_t index = 0, GLenum target = GL_TEXTURE_2D);
        void AttachDepthTexture(GLuint texture, GLenum target = GL_TEXTURE_2D);
        void AttachStencilTexture(GLuint texture, GLenum target = GL_TEXTURE_2D);
        GLenum CheckStatus() const;
        bool IsComplete() const;

    private:
        GLuint FramebufferHandle = 0;
        uint32_t ColorAttachmentCount = 0;
        bool HasDepthAttachment = false;
    };
}
