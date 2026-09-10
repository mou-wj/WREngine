#pragma once

#include "RHIResource.h"
#include "glad/gl.h"

namespace RHIOpenGL
{
    GLenum ConvertRHIFormatToGLInternalFormat(RHI::ERHIFormat format);
    GLenum ConvertRHIFormatToGLFormat(RHI::ERHIFormat format);
    GLenum ConvertRHIFormatToGLType(RHI::ERHIFormat format);
    GLenum GetOpenGLTextureFormat(RHI::ERHIFormat format);
    GLenum GetOpenGLTextureType(RHI::ERHIFormat format);
	void CheckError();
}
