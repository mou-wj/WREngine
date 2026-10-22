#include "VulkanRHIUtils.h"
#include "Log.h"
namespace RHIVulkan {

    VkFormat TransformFormatFrom(ERHIFormat format) {
        switch (format) {
        case ERHIFormat::R8_UNorm: return VK_FORMAT_R8_UNORM;
        case ERHIFormat::R8G8B8A8_UNorm: return VK_FORMAT_R8G8B8A8_UNORM;
        case ERHIFormat::R8G8B8A8_SRGB: return VK_FORMAT_R8G8B8A8_SRGB;
        case ERHIFormat::B8G8R8A8_UNorm: return VK_FORMAT_B8G8R8A8_UNORM;
        case ERHIFormat::B8G8R8A8_SRGB: return VK_FORMAT_B8G8R8A8_SRGB;
        case ERHIFormat::R16G16_Float: return VK_FORMAT_R16G16_SFLOAT;
        case ERHIFormat::R16G16B16A16_Float: return VK_FORMAT_R16G16B16A16_SFLOAT;
        case ERHIFormat::R32_Float: return VK_FORMAT_R32_SFLOAT;
        case ERHIFormat::R32G32_Float: return VK_FORMAT_R32G32_SFLOAT;
        case ERHIFormat::R32G32B32_Float: return VK_FORMAT_R32G32B32_SFLOAT;
        case ERHIFormat::R32G32B32A32_Float: return VK_FORMAT_R32G32B32A32_SFLOAT;
        case ERHIFormat::D24_UNorm_S8_UInt: return VK_FORMAT_D24_UNORM_S8_UINT;
        case ERHIFormat::D32_Float: return VK_FORMAT_D32_SFLOAT;
        case ERHIFormat::R16_UInt: return VK_FORMAT_R16_UINT;
		case ERHIFormat::R32_UInt: return VK_FORMAT_R32_UINT;
        
        default:
            return VK_FORMAT_UNDEFINED; // 未知格式
        }
    }


    VkShaderStageFlagBits TransformShaderStageFrom(ERHIShaderFrequency shaderType) {
        switch (shaderType) {
            case ERHIShaderFrequency::Vertex: return VK_SHADER_STAGE_VERTEX_BIT;
            case ERHIShaderFrequency::Fragment: return VK_SHADER_STAGE_FRAGMENT_BIT;
            case ERHIShaderFrequency::Geometry: return VK_SHADER_STAGE_GEOMETRY_BIT;
            case ERHIShaderFrequency::Compute: return VK_SHADER_STAGE_COMPUTE_BIT;
            case ERHIShaderFrequency::TessControl: return VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
            case ERHIShaderFrequency::TessEvaluation: return VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
            case ERHIShaderFrequency::Mesh: return VK_SHADER_STAGE_MESH_BIT_EXT; // Vulkan扩展
            case ERHIShaderFrequency::Task: return VK_SHADER_STAGE_TASK_BIT_EXT; // Vulkan扩展
            case ERHIShaderFrequency::RayGen: return VK_SHADER_STAGE_RAYGEN_BIT_KHR; // Ray Tracing
            case ERHIShaderFrequency::ClosestHit: return VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR; // Ray Tracing
            case ERHIShaderFrequency::Miss: return VK_SHADER_STAGE_MISS_BIT_KHR; // Ray Tracing
            case ERHIShaderFrequency::AnyHit: return VK_SHADER_STAGE_ANY_HIT_BIT_KHR; // Ray Tracing
            case ERHIShaderFrequency::Intersection: return VK_SHADER_STAGE_INTERSECTION_BIT_KHR; // Ray Tracing
            case ERHIShaderFrequency::Callable: return VK_SHADER_STAGE_CALLABLE_BIT_KHR; // Ray Tracing
            default:
                return static_cast<VkShaderStageFlagBits>(0); // 未知阶段
        }
    }



    VkImageUsageFlags TransformTextureUsageFlagsFrom(ERHITextureCreateFlags flags)
    {
        VkImageUsageFlags usage = 0;

        if (EnumHasAnyFlags(flags, ERHITextureCreateFlag::ShaderResource))
        {
            // sampled image / combined image sampler
            usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
        }

        if (EnumHasAnyFlags(flags, ERHITextureCreateFlag::RenderTarget))
        {
            // color attachment
            usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        }

        if (EnumHasAnyFlags(flags, ERHITextureCreateFlag::UAV))
        {
            // storage image
            usage |= VK_IMAGE_USAGE_STORAGE_BIT;
        }

        if (EnumHasAnyFlags(flags, ERHITextureCreateFlag::DepthStencil))
        {
            usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        }

        if (EnumHasAnyFlags(flags, ERHITextureCreateFlag::TransferSrc))
        {
            usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        }

        if (EnumHasAnyFlags(flags, ERHITextureCreateFlag::TransferDest))
        {
            usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        }

        if (EnumHasAnyFlags(flags, ERHITextureCreateFlag::Presentable))
        {
            // swapchain images *must* have this
            usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        }
        if (EnumHasAnyFlags(flags, ERHITextureCreateFlag::DepthStencil))
        {
            // swapchain images *must* have this
            usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        }
        return usage;
    }
    VkSampleCountFlagBits TransformSampleCountFrom(uint32_t sampleCount)
    {
        switch (sampleCount)
        {
        case 1:  return VK_SAMPLE_COUNT_1_BIT;
        case 2:  return VK_SAMPLE_COUNT_2_BIT;
        case 4:  return VK_SAMPLE_COUNT_4_BIT;
        case 8:  return VK_SAMPLE_COUNT_8_BIT;
        case 16: return VK_SAMPLE_COUNT_16_BIT;
        case 32: return VK_SAMPLE_COUNT_32_BIT;
        case 64: return VK_SAMPLE_COUNT_64_BIT;
        default:
            LOG_ERROR("Invalid sample count: {}", sampleCount);
        }
        return VkSampleCountFlagBits::VK_SAMPLE_COUNT_1_BIT;
    }

    VkImageType TransformImageTypeFrom(ERHITextureType type)
    {
        switch (type)
        {
        case ERHITextureType::Texture1D:
            return VK_IMAGE_TYPE_1D;

        case ERHITextureType::Texture2D:
        case ERHITextureType::Texture2DArray:
        case ERHITextureType::TextureCube:
        case ERHITextureType::TextureCubeArray:
            // Cube / Array 本质都是 2D image
            return VK_IMAGE_TYPE_2D;

        case ERHITextureType::Texture3D:
            return VK_IMAGE_TYPE_3D;

        case ERHITextureType::Unknown:
        default:
            break;
        }
        LOG_ERROR("Invalid texture type: {}", static_cast<int>(type));
        return VK_IMAGE_TYPE_2D;
    }

    VkImageViewType TransformViewTypeFrom(ERHITextureType type) {
        switch (type) {
        case ERHITextureType::Texture1D:
            return VK_IMAGE_VIEW_TYPE_1D;
        case ERHITextureType::Texture2D:
            return VK_IMAGE_VIEW_TYPE_2D;
        case ERHITextureType::Texture3D:
            return VK_IMAGE_VIEW_TYPE_3D;
        case ERHITextureType::TextureCube:
            return VK_IMAGE_VIEW_TYPE_CUBE;
        case ERHITextureType::Texture2DArray:
            return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        case ERHITextureType::TextureCubeArray:
            return VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
        default:
            break;
        }
        LOG_ERROR("Invalid texture type: {}", static_cast<int>(type));
        return VK_IMAGE_VIEW_TYPE_2D; // 默认类型
    }

    VkImageViewType TransformViewTypeFrom(ERHITextureViewType type)
    {
        switch (type) {
        case ERHITextureViewType::TextureView1D:
            return VK_IMAGE_VIEW_TYPE_1D;
        case ERHITextureViewType::TextureView2D:
            return VK_IMAGE_VIEW_TYPE_2D;
        case ERHITextureViewType::TextureView3D:
            return VK_IMAGE_VIEW_TYPE_3D;
        case ERHITextureViewType::TextureViewCube:
            return VK_IMAGE_VIEW_TYPE_CUBE;
        case ERHITextureViewType::TextureView2DArray:
            return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        case ERHITextureViewType::TextureViewCubeArray:
            return VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
        default:
            break;
        }
        LOG_ERROR("Invalid texture type: {}", static_cast<int>(type));
        return VK_IMAGE_VIEW_TYPE_2D; // 默认类型
    }

    VkImageAspectFlags GetImageAspectFlags(VkFormat format)
    {
        switch (format)
        {
            // ---- Depth only ----
        case VK_FORMAT_D16_UNORM:
        case VK_FORMAT_X8_D24_UNORM_PACK32:
        case VK_FORMAT_D32_SFLOAT:
            return VK_IMAGE_ASPECT_DEPTH_BIT;

            // ---- Stencil only ----
        case VK_FORMAT_S8_UINT:
            return VK_IMAGE_ASPECT_STENCIL_BIT;

            // ---- Depth + Stencil ----
        case VK_FORMAT_D16_UNORM_S8_UINT:
        case VK_FORMAT_D24_UNORM_S8_UINT:
        case VK_FORMAT_D32_SFLOAT_S8_UINT:
            return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;

            // ---- Everything else treated as color ----
        default:
            return VK_IMAGE_ASPECT_COLOR_BIT;
        }
    }

    VkDescriptorType TransformDescriptorTypeFrom(RenderCore::SPIRVCompiledBinaryResultPacker::ESPIRVShaderResourceType type)
    {
        using ESPIRVShaderResourceType = RenderCore::SPIRVCompiledBinaryResultPacker::ESPIRVShaderResourceType;

        switch (type)
        {
        case ESPIRVShaderResourceType::Sampler:
            return VK_DESCRIPTOR_TYPE_SAMPLER;

        case ESPIRVShaderResourceType::SampledImage:
            return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE; // 不使用 VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER 根据需求

        case ESPIRVShaderResourceType::StorageImage:
            return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;

        case ESPIRVShaderResourceType::UniformBuffer:
            return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;

        case ESPIRVShaderResourceType::StorageBuffer:
            return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;

        case ESPIRVShaderResourceType::AccelerationStructure:
            return VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;

        default:
            return VK_DESCRIPTOR_TYPE_MAX_ENUM;
        }
    }
    VkShaderStageFlagBits TransformShaderFrequencyToStage(ERHIShaderFrequency frequency)
    {
        switch (frequency)
        {
        case ERHIShaderFrequency::Vertex:         return VK_SHADER_STAGE_VERTEX_BIT;
        case ERHIShaderFrequency::Fragment:       return VK_SHADER_STAGE_FRAGMENT_BIT;
        case ERHIShaderFrequency::Geometry:       return VK_SHADER_STAGE_GEOMETRY_BIT;
        case ERHIShaderFrequency::Compute:        return VK_SHADER_STAGE_COMPUTE_BIT;
        case ERHIShaderFrequency::TessControl:    return VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
        case ERHIShaderFrequency::TessEvaluation: return VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
        case ERHIShaderFrequency::Mesh:           return VK_SHADER_STAGE_MESH_BIT_NV;  // mesh shader extension
        case ERHIShaderFrequency::Task:           return VK_SHADER_STAGE_TASK_BIT_NV;  // task shader extension
        case ERHIShaderFrequency::RayGen:         return VK_SHADER_STAGE_RAYGEN_BIT_KHR;
        case ERHIShaderFrequency::ClosestHit:     return VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
        case ERHIShaderFrequency::Miss:           return VK_SHADER_STAGE_MISS_BIT_KHR;
        case ERHIShaderFrequency::AnyHit:         return VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
        case ERHIShaderFrequency::Intersection:   return VK_SHADER_STAGE_INTERSECTION_BIT_KHR;
        case ERHIShaderFrequency::Callable:       return VK_SHADER_STAGE_CALLABLE_BIT_KHR;

        default:
            return VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM;
        }
    }

    // 辅助函数：将自定义枚举转换为 Vulkan VkPrimitiveTopology
    VkPrimitiveTopology TransformPrimitiveTopology(EPrimitiveTopology topology)
    {
        switch (topology)
        {
        case EPrimitiveTopology::PointList:                 return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        case EPrimitiveTopology::LineList:                  return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        case EPrimitiveTopology::LineStrip:                 return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
        case EPrimitiveTopology::TriangleList:             return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        case EPrimitiveTopology::TriangleStrip:            return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        case EPrimitiveTopology::TriangleFan:              return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
        case EPrimitiveTopology::LineListWithAdjacency:    return VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY;
        case EPrimitiveTopology::LineStripWithAdjacency:   return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY;
        case EPrimitiveTopology::TriangleListWithAdjacency:return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY;
        case EPrimitiveTopology::TriangleStripWithAdjacency:return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY;
        case EPrimitiveTopology::PatchList_1:
        case EPrimitiveTopology::PatchList_2:
        case EPrimitiveTopology::PatchList_3:
        case EPrimitiveTopology::PatchList_4:             return VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;
        default:                                           return VK_PRIMITIVE_TOPOLOGY_MAX_ENUM;
        }
    }
    VkAccessFlags TransformResourceAccess(ERHIResourceAccess access)
    {
        VkAccessFlags result = 0;
        if (EnumHasAnyFlags(access, ERHIResourceAccess::TransferDest | ERHIResourceAccess::ResolveDst)) result |= VK_ACCESS_TRANSFER_WRITE_BIT;
        if (EnumHasAnyFlags(access, ERHIResourceAccess::TransferSrc | ERHIResourceAccess::ResolveSrc)) result |= VK_ACCESS_TRANSFER_READ_BIT;
        if (EnumHasAnyFlags(access, ERHIResourceAccess::SRV)) result |= VK_ACCESS_SHADER_READ_BIT;
        if (EnumHasAnyFlags(access, ERHIResourceAccess::UAV)) result |= VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        if (EnumHasAnyFlags(access, ERHIResourceAccess::BVHRead)) result |= VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
        if (EnumHasAnyFlags(access, ERHIResourceAccess::BVHWrite)) result |= VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        if (EnumHasAnyFlags(access, ERHIResourceAccess::VertexOrIndexBuffer)) result |= VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT;
        if (EnumHasAnyFlags(access, ERHIResourceAccess::IndirectArgs)) result |= VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
        return result;
    }

    VkBufferUsageFlags TransformBufferUsageFlagsFrom(ERHIBufferUsageFlags flags)
    {
        VkBufferUsageFlags usage = 0;

        // 1. 基础类型（显式指定的用途）
        if (EnumHasAnyFlags(flags, ERHIBufferUsageFlag::Vertex))
            usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

        if (EnumHasAnyFlags(flags, ERHIBufferUsageFlag::Index))
            usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;

        if (EnumHasAnyFlags(flags, ERHIBufferUsageFlag::Constant))
            usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

        if (EnumHasAnyFlags(flags, ERHIBufferUsageFlag::Indirect))
            usage |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;

        // 2. 存储/结构化类型 (UAV/SRV 的物理载体)
        if (EnumHasAnyFlags(flags, ERHIBufferUsageFlag::Structured | ERHIBufferUsageFlag::RawBuffer))
            usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

        // 3. 处理 SRV / UAV (重点改进)
        // 如果是 UAV，必须开启 STORAGE 位
        if (EnumHasAnyFlags(flags, ERHIBufferUsageFlag::UnorderedAccess))
        {
            usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
            if (EnumHasAnyFlags(flags, ERHIBufferUsageFlag::Texel))
                usage |= VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT;
        }

        // 如果是 SRV，且不是 Constant，通常也需要作为 Storage 或 Texel 访问
        if (EnumHasAnyFlags(flags, ERHIBufferUsageFlag::ShaderResource))
        {
            usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
            if (EnumHasAnyFlags(flags, ERHIBufferUsageFlag::Texel))
                usage |= VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT;
            // 注意：有些架构下 StructuredBuffer 的 SRV 也要走 STORAGE_BUFFER_BIT
        }

        // 4. 数据传输
        if (EnumHasAnyFlags(flags, ERHIBufferUsageFlag::TransferSrc | ERHIBufferUsageFlag::Staging))
            usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

        if (EnumHasAnyFlags(flags, ERHIBufferUsageFlag::TransferDst | ERHIBufferUsageFlag::Staging))
            usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;

        if (EnumHasAnyFlags(flags,
            ERHIBufferUsageFlag::RawBuffer |
            ERHIBufferUsageFlag::Structured |
            ERHIBufferUsageFlag::ShaderResource |
            ERHIBufferUsageFlag::UnorderedAccess |
            ERHIBufferUsageFlag::Indirect))
        {
            usage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        }
        if (EnumHasAnyFlags(flags,ERHIBufferUsageFlag::AccelerationStructureBuildInput))
        {
            usage |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        }
        return usage;
    }


    VkFilter TransformFilter(ERHIFilter filter)
    {
        switch (filter)
        {
        case ERHIFilter::Nearest:
            return VK_FILTER_NEAREST;

        case ERHIFilter::Linear:
            return VK_FILTER_LINEAR;

        default:
            return VK_FILTER_LINEAR;
        }
    }

    VkSamplerMipmapMode GetMipmapMode(ERHIFilter filter)
    {
        switch (filter)
        {
        case ERHIFilter::Nearest:
            return VK_SAMPLER_MIPMAP_MODE_NEAREST;

        case ERHIFilter::Linear:
            return VK_SAMPLER_MIPMAP_MODE_LINEAR;

        default:
            return VK_SAMPLER_MIPMAP_MODE_LINEAR;
        }
    }

    VkSamplerAddressMode TransformAddressMode(ERHIAddressMode mode)
    {
        switch (mode)
        {
        case ERHIAddressMode::Repeat:
            return VK_SAMPLER_ADDRESS_MODE_REPEAT;

        case ERHIAddressMode::ClampToEdge:
            return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

        case ERHIAddressMode::MirrorClampToEdge:
            return VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE;

        default:
            return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        }
    }

    VkCompareOp TransformCompareOp(ERHICompareOp op)
    {
        switch (op)
        {
        case ERHICompareOp::Never:
            return VK_COMPARE_OP_NEVER;

        case ERHICompareOp::Less:
            return VK_COMPARE_OP_LESS;

        case ERHICompareOp::Equal:
            return VK_COMPARE_OP_EQUAL;

        case ERHICompareOp::LessOrEqual:
            return VK_COMPARE_OP_LESS_OR_EQUAL;

        case ERHICompareOp::Greater:
            return VK_COMPARE_OP_GREATER;

        case ERHICompareOp::NotEqual:
            return VK_COMPARE_OP_NOT_EQUAL;

        case ERHICompareOp::GreaterOrEqual:
            return VK_COMPARE_OP_GREATER_OR_EQUAL;

        case ERHICompareOp::Always:
            return VK_COMPARE_OP_ALWAYS;

        default:
            return VK_COMPARE_OP_LESS_OR_EQUAL;
        }
    }
}