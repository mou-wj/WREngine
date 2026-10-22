#include "VulkanShader.h"
#include "VulkanRHIUtils.h"
#include "VulkanFuncWrapper.h"
#include <spirv_cross/spirv_cross.hpp>

namespace RHIVulkan {

VulkanRHIShader::VulkanRHIShader(VulkanDevice* device)
    : _device(device)
{
}

VulkanRHIShader::~VulkanRHIShader()
{
    Cleanup();
}

bool VulkanRHIShader::Initialize(const std::vector<char>& packedCode)
{
	RenderCore::SPIRVCompiledBinaryResultPacker packer;
	packer.Depack(packedCode);
	_reflection = packer.DepackedData.Header;
    _entryPoint = packer.DepackedData.Header.EntryPoint;
    auto spirvCode = packer.DepackedData.SpirvCode;
    _shaderStage = TransformShaderStageFrom(packer.DepackedData.Header.Frequency);
    VkShaderModuleCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = spirvCode.size() * sizeof(uint32_t);
    createInfo.pCode = spirvCode.data();

    bool result = VKFunc::CreateShaderModule(_device->GetHandle(), &createInfo,  &_shaderModule);
    return result;
}

void VulkanRHIShader::Cleanup()
{
    if (_shaderModule != VK_NULL_HANDLE && _device)
    {
        VKFunc::DestroyShaderModule(_device->GetHandle(), _shaderModule);
        _shaderModule = VK_NULL_HANDLE;
    }

}


} // namespace WR::RHIVulkan