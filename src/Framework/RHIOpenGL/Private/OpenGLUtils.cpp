#include "OpenGLUtils.h"

namespace RHIOpenGL
{
    GLenum ConvertRHIFormatToGLInternalFormat(RHI::ERHIFormat format)
    {
        switch (format)
        {
        case RHI::ERHIFormat::R8_UNorm:
            return GL_R8;
        case RHI::ERHIFormat::R8G8B8A8_UNorm:
        case RHI::ERHIFormat::B8G8R8A8_UNorm:
            return GL_RGBA8;
        case RHI::ERHIFormat::R16G16_Float:
            return GL_RG16F;
        case RHI::ERHIFormat::R16G16B16A16_Float:
            return GL_RGBA16F;
        case RHI::ERHIFormat::R32_Float:
            return GL_R32F;
        case RHI::ERHIFormat::R32G32_Float:
            return GL_RG32F;
        case RHI::ERHIFormat::R32G32B32A32_Float:
            return GL_RGBA32F;
        case RHI::ERHIFormat::D32_Float:
            return GL_DEPTH_COMPONENT32F;
        case RHI::ERHIFormat::D24_UNorm_S8_UInt:
            return GL_DEPTH24_STENCIL8;
        default:
            return GL_RGBA8;
        }
    }

    GLenum ConvertRHIFormatToGLFormat(RHI::ERHIFormat format)
    {
        switch (format)
        {
        case RHI::ERHIFormat::R8_UNorm:
            return GL_RED;
        case RHI::ERHIFormat::R8G8B8A8_UNorm:
        case RHI::ERHIFormat::B8G8R8A8_UNorm:
            return GL_BGRA;
        case RHI::ERHIFormat::R16G16_Float:
            return GL_RG;
        case RHI::ERHIFormat::R16G16B16A16_Float:
            return GL_RGBA;
        case RHI::ERHIFormat::R32_Float:
            return GL_RED;
        case RHI::ERHIFormat::R32G32_Float:
            return GL_RG;
        case RHI::ERHIFormat::R32G32B32A32_Float:
            return GL_RGBA;
        case RHI::ERHIFormat::D32_Float:
            return GL_DEPTH_COMPONENT;
        case RHI::ERHIFormat::D24_UNorm_S8_UInt:
            return GL_DEPTH_STENCIL;
        default:
            return GL_RGBA;
        }
    }

    GLenum ConvertRHIFormatToGLType(RHI::ERHIFormat format)
    {
        switch (format)
        {
        case RHI::ERHIFormat::R8_UNorm:
        case RHI::ERHIFormat::R8G8B8A8_UNorm:
        case RHI::ERHIFormat::B8G8R8A8_UNorm:
            return GL_UNSIGNED_BYTE;
        case RHI::ERHIFormat::R16G16_Float:
        case RHI::ERHIFormat::R16G16B16A16_Float:
            return GL_HALF_FLOAT;
        case RHI::ERHIFormat::R32_Float:
        case RHI::ERHIFormat::R32G32_Float:
        case RHI::ERHIFormat::R32G32B32A32_Float:
            return GL_FLOAT;
        case RHI::ERHIFormat::D32_Float:
        case RHI::ERHIFormat::D24_UNorm_S8_UInt:
            return GL_UNSIGNED_INT;
        default:
            return GL_UNSIGNED_BYTE;
        }
    }

    GLenum GetOpenGLTextureFormat(RHI::ERHIFormat format)
    {
        switch (format)
        {
        case RHI::ERHIFormat::R8_UNorm:
        case RHI::ERHIFormat::R32_Float:
            return GL_RED;
        case RHI::ERHIFormat::R16G16_Float:
        case RHI::ERHIFormat::R32G32_Float:
            return GL_RG;
        case RHI::ERHIFormat::B8G8R8A8_UNorm:
            return GL_BGRA;
        default:
            return GL_RGBA;
        }
    }

    GLenum GetOpenGLTextureType(RHI::ERHIFormat format)
    {
        switch (format)
        {
        case RHI::ERHIFormat::R8_UNorm:
        case RHI::ERHIFormat::R8G8B8A8_UNorm:
        case RHI::ERHIFormat::B8G8R8A8_UNorm:
            return GL_UNSIGNED_BYTE;
        case RHI::ERHIFormat::R16G16_Float:
        case RHI::ERHIFormat::R16G16B16A16_Float:
            return GL_HALF_FLOAT;
        default:
            return GL_FLOAT;
        }
    }
    void CheckError() {

        while (true)
        {
            GLenum err = glGetError();
            if (err != GL_NO_ERROR) {
                std::cerr << "OpenGL error: " << err << std::endl;
            }
            else {
                break;
            }
        }

    }
}
