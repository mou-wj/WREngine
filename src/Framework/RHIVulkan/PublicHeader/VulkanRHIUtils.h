#pragma once
#include "RHIDefine.h"
#include "ShaderCompiledDataPacker.h"
#include <vulkan/vulkan.h>
using namespace RHI;
namespace RHIVulkan {

    VkFormat TransformFormatFrom(ERHIFormat format);

    VkShaderStageFlagBits TransformShaderStageFrom(ERHIShaderFrequency shaderType) ;

    VkImageUsageFlags TransformTextureUsageFlagsFrom(ERHITextureCreateFlags flags);

	VkSampleCountFlagBits TransformSampleCountFrom(uint32_t sampleCount);

    VkImageType TransformImageTypeFrom(ERHITextureType type);

    VkImageViewType TransformViewTypeFrom(ERHITextureType type);

    VkImageViewType TransformViewTypeFrom(ERHITextureViewType type);

    VkImageAspectFlags GetImageAspectFlags(VkFormat format);

    VkDescriptorType TransformDescriptorTypeFrom(RenderCore::SPIRVCompiledBinaryResultPacker::ESPIRVShaderResourceType type);

	VkShaderStageFlagBits TransformShaderFrequencyToStage(ERHIShaderFrequency frequency);

    VkPrimitiveTopology TransformPrimitiveTopology(EPrimitiveTopology topology);

    VkBufferUsageFlags TransformBufferUsageFlagsFrom(ERHIBufferUsageFlags flags);

    VkAccessFlags TransformResourceAccess(ERHIResourceAccess access);

    VkFilter TransformFilter(ERHIFilter filter);

    VkSamplerMipmapMode GetMipmapMode(ERHIFilter filter);

    VkSamplerAddressMode TransformAddressMode(ERHIAddressMode mode);

    VkCompareOp TransformCompareOp(ERHICompareOp op);

}