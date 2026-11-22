#pragma once

#include "RHIResource.h"
#include "glad/gl.h"

namespace RHIOpenGL
{
    inline GLenum GetDestinationColorBlendFactor(RHI::ERHIColorBlendMode mode)
    {
        return mode == RHI::ERHIColorBlendMode::Additive ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA;
    }

    GLbitfield ConvertRHIAccessToGLBarrier(RHI::ERHIResourceAccess access);
    [[noreturn]] void ThrowRayTracingUnsupported();
    GLenum ConvertRHICompareOpToGL(RHI::ERHICompareOp op);
    GLenum ConvertRHIFormatToGLInternalFormat(RHI::ERHIFormat format);
    GLenum ConvertRHIFormatToGLFormat(RHI::ERHIFormat format);
    GLenum ConvertRHIFormatToGLType(RHI::ERHIFormat format);
    GLenum GetOpenGLTextureFormat(RHI::ERHIFormat format);
    GLenum GetOpenGLTextureType(RHI::ERHIFormat format);
    GLenum ConvertRHITextureTypeToGLTextureType(RHI::ERHITextureType type);
	void CheckError();
}
