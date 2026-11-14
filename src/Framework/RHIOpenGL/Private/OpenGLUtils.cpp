#include "OpenGLUtils.h"
#include <stdexcept>

namespace RHIOpenGL
{
    GLbitfield ConvertRHIAccessToGLBarrier(RHI::ERHIResourceAccess access)
    {
        using RHI::ERHIResourceAccess;
        GLbitfield bits = 0;
        if (EnumHasAnyFlags(access, ERHIResourceAccess::IndirectArgs))
        {
            bits |= GL_COMMAND_BARRIER_BIT;
        }
        if (EnumHasAnyFlags(access, ERHIResourceAccess::VertexOrIndexBuffer))
        {
            bits |= GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT | GL_ELEMENT_ARRAY_BARRIER_BIT;
        }
        if (EnumHasAnyFlags(access, ERHIResourceAccess::SRV))
        {
            bits |= GL_SHADER_STORAGE_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT | GL_UNIFORM_BARRIER_BIT;
        }
        if (EnumHasAnyFlags(access, ERHIResourceAccess::UAV))
        {
            bits |= GL_SHADER_STORAGE_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT;
        }
        if (EnumHasAnyFlags(access, ERHIResourceAccess::TransferSrc | ERHIResourceAccess::TransferDest | ERHIResourceAccess::CPURead))
        {
            bits |= GL_BUFFER_UPDATE_BARRIER_BIT | GL_TEXTURE_UPDATE_BARRIER_BIT | GL_PIXEL_BUFFER_BARRIER_BIT;
        }
        if (EnumHasAnyFlags(access, ERHIResourceAccess::RenderTargetView | ERHIResourceAccess::DSVRead | ERHIResourceAccess::DSVWrite))
        {
            bits |= GL_FRAMEBUFFER_BARRIER_BIT;
        }
        return bits;
    }

    [[noreturn]] void ThrowRayTracingUnsupported()
    {
        throw std::runtime_error("OpenGLRHI does not support RHI ray tracing pipelines: OpenGL has no standard native ray tracing pipeline, and software BVH traversal/shader lowering is not implemented. Use Vulkan or DirectX12.");
    }
    GLenum ConvertRHICompareOpToGL(RHI::ERHICompareOp op)
    {
        switch (op)
        {
        case RHI::ERHICompareOp::Never:
            return GL_NEVER;
        case RHI::ERHICompareOp::Less:
            return GL_LESS;
        case RHI::ERHICompareOp::Equal:
            return GL_EQUAL;
        case RHI::ERHICompareOp::LessOrEqual:
            return GL_LEQUAL;
        case RHI::ERHICompareOp::Greater:
            return GL_GREATER;
        case RHI::ERHICompareOp::NotEqual:
            return GL_NOTEQUAL;
        case RHI::ERHICompareOp::GreaterOrEqual:
            return GL_GEQUAL;
        case RHI::ERHICompareOp::Always:
            return GL_ALWAYS;
        default:
            throw std::invalid_argument("Invalid RHI depth comparison");
        }
    }

    GLenum ConvertRHIFormatToGLInternalFormat(RHI::ERHIFormat format)
    {
        switch (format)
        {
        case RHI::ERHIFormat::R32G32_UInt:
            return GL_RG32UI;
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
        case RHI::ERHIFormat::R32G32_UInt:
            return GL_RG_INTEGER;
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
        case RHI::ERHIFormat::R32G32_UInt:
            return GL_UNSIGNED_INT;
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
        case RHI::ERHIFormat::R32G32_UInt:
            return GL_RG_INTEGER;
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
        case RHI::ERHIFormat::R32G32_UInt:
            return GL_UNSIGNED_INT;
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
    GLenum ConvertRHITextureTypeToGLTextureType(RHI::ERHITextureType type) {
		switch (type)
		{
		case RHI::ERHITextureType::Texture1D:
			return GL_TEXTURE_1D;
		case RHI::ERHITextureType::Texture2D:
			return GL_TEXTURE_2D;
		case RHI::ERHITextureType::Texture3D:
			return GL_TEXTURE_3D;
		case RHI::ERHITextureType::TextureCube:
			return GL_TEXTURE_CUBE_MAP;
		case RHI::ERHITextureType::Texture2DArray:
			return GL_TEXTURE_2D_ARRAY;
		case RHI::ERHITextureType::TextureCubeArray:
			return GL_TEXTURE_CUBE_MAP_ARRAY;
		default:
			return GL_TEXTURE_2D;
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
