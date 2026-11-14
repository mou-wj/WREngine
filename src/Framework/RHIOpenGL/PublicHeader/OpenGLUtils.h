#pragma once

#include "RHIResource.h"
#include "glad/gl.h"

namespace RHIOpenGL
{
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
