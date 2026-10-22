#include "VulkanPipelineState.h"
#include "VulkanShader.h"

#include "VulkanDescriptorSets.h"
#include "VulkanResource.h"
#include "VulkanRHIUtils.h"
#include "VulkanCommandBuffer.h"
#include "VulkanMemory.h"
#include <vector>

using namespace RHI;

namespace RHIVulkan {

VulkanPipelineBase::VulkanPipelineBase(VulkanDevice* device)
: device(device), pipeline(VK_NULL_HANDLE)
{

}
VulkanPipelineBase::~VulkanPipelineBase() {
    VkDevice deviceHandle = device->GetHandle();
    if (pipeline != VK_NULL_HANDLE) {
        device->EnqueuePipelineForDeletion(pipeline);
        pipeline = VK_NULL_HANDLE;
    }

}



VulkanGraphicsPipelineState::VulkanGraphicsPipelineState(VulkanDevice* device, const RHIGraphicsPipelineStateDesc& pipelineDesc)
    : VulkanPipelineBase(device), RHIGraphicsPipelineState(pipelineDesc) {
    // Convert RHI desc to Vulkan create info
    createInfo = {}; // Initialize createInfo
    createInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    VulkanRenderTargetLayout layout(pipelineDesc.AttachmentDesc);
    auto renderPass = device->GetRenderPassManager()->GetOrCreateRenderPass(layout);
    createInfo.renderPass = renderPass->GetHandle();
    createInfo.subpass = pipelineDesc.AttachmentDesc.SubpassIndex;
	auto layoutInfo = BuildPipelineLayoutInfo(pipelineDesc);
    pipelineLayout = device->GetPipelineLayoutCache()->GetOrCreateLayout(layoutInfo);
    
    CreatePipeline();
}

VulkanGraphicsPipelineState::~VulkanGraphicsPipelineState() {
}

void VulkanGraphicsPipelineState::Bind(VulkanCommandBuffer* cmdBuffer)
{
    VKFunc::CmdBindPipeline(cmdBuffer->GetHandle(), VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
}

PipelineLayoutInfo VulkanGraphicsPipelineState::BuildPipelineLayoutInfo(const RHIGraphicsPipelineStateDesc& pipelineDesc)
{
    PipelineLayoutInfo layoutInfo;

    auto processShader = [&](VulkanRHIShader* shader)
        {
            if (!shader)
                return;

            const auto& header = shader->GetShaderReflection();

            RHI::ERHIShaderFrequency frequency = header.Frequency;
            VkShaderStageFlags stageMask =
                TransformShaderFrequencyToStage(frequency);

            // 获取或创建该着色器频率对应的 ShaderFrequencyLayoutInfo
            uint32_t freqKey = static_cast<uint32_t>(frequency);
            PipelineLayoutInfo::ShaderFrequencyLayoutInfo& freqInfo = 
                layoutInfo.setLayoutsByFrequency[freqKey];

            auto ensureSet = [&](uint32_t set)
                {
                    if (layoutInfo.Layouts.size() <= set)
                    {
                        layoutInfo.Layouts.resize(set + 1);
                    }
                };

            for (const auto& binding : header.DescriptorBindings)
            {
                ensureSet(binding.Set);

                DescriptorSetLayoutInfo& setLayout = layoutInfo.Layouts[binding.Set];

                VkDescriptorType vkType = TransformDescriptorTypeFrom(binding.Type);

                setLayout.AddBinding(
                    binding.Binding,
                    vkType,
                    binding.Count,
                    stageMask);
                PipelineLayoutInfo::ShaderResourceParameterLayoutInfo rpLayout;
                rpLayout.SetIndex = binding.Set;
                rpLayout.BindingIndex = binding.Binding;
                rpLayout.DescriptorCount = binding.Count;
                freqInfo.ResourceParameterLayouts.push_back(rpLayout);

            }

            for (const auto& binding : header.UniformBufferBindings) {
				ensureSet(binding.Set);
                DescriptorSetLayoutInfo& setLayout = layoutInfo.Layouts[binding.Set];
                setLayout.AddBinding(
                    binding.Binding,
                    VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    1,
                    stageMask);
				PipelineLayoutInfo::ShaderUniformBufferLayoutInfo ubLayout;
				ubLayout.SetIndex = binding.Set;
				ubLayout.BindingIndex = binding.Binding;
				ubLayout.Size = binding.Size;
				freqInfo.UniformBufferLayouts.push_back(ubLayout);
            }
           

            if (header.HasPushConstant)
            {
                layoutInfo.hasPushConstant = true;

                layoutInfo.pushConstant.offset = 0;
                layoutInfo.pushConstant.size = header.PushConstant.Size;

                layoutInfo.pushConstant.stageFlags |=
                    stageMask;
            }
        };

    processShader(
        dynamic_cast<VulkanRHIShader*>(pipelineDesc.ShaderStages.VertexShader));

    processShader(
        dynamic_cast<VulkanRHIShader*>(pipelineDesc.ShaderStages.FragmentShader));

    return layoutInfo;
}


void VulkanGraphicsPipelineState::CreatePipeline() {
    // 设置VkGraphicsPipelineCreateInfo的其他成员
    // 这里仅提供一个示例，具体成员需要根据pipelineDesc填充
    VkPipelineVertexInputStateCreateInfo &vertexInputInfo = dynamic_cast<VulkanVertexDescState*>(_desc.VertexDescState)->vertexInputInfo;

    VkPipelineRasterizationStateCreateInfo rasterizer = dynamic_cast<VulkanRasterizerState*>(_desc.RasterizerState)->rasterizerInfo;

    VkPipelineColorBlendStateCreateInfo colorBlending = dynamic_cast<VulkanColorBlendState*>(_desc.ColorBlendState)->colorBlendInfo;

    VkPipelineDepthStencilStateCreateInfo depthStencil = dynamic_cast<VulkanDepthStencilState*>(_desc.DepthStencilState)->depthStencilInfo;

    // 设置管线布局
    createInfo.layout = pipelineLayout->GetHandle();


    
    dynamicStates.push_back(VK_DYNAMIC_STATE_VIEWPORT);
    dynamicStates.push_back(VK_DYNAMIC_STATE_SCISSOR);

    VkPipelineDynamicStateCreateInfo dynamicState = {};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = dynamicStates.size();
    dynamicState.pDynamicStates = dynamicStates.data();


    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = TransformPrimitiveTopology(_desc.PrimitiveTopology);
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;
    viewportState.pScissors = nullptr;
    viewportState.pViewports = nullptr;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = TransformSampleCountFrom(_desc.AttachmentDesc.NumSamples);
    multisample.sampleShadingEnable = VK_FALSE;
    multisample.minSampleShading = 1.0f;
    multisample.pSampleMask = nullptr;
    multisample.alphaToCoverageEnable = VK_FALSE;
    multisample.alphaToOneEnable = VK_FALSE;


        // 设置createInfo的各个成员
    createInfo.pVertexInputState = &vertexInputInfo;
    createInfo.pInputAssemblyState = &inputAssembly;
    createInfo.pViewportState = &viewportState;
    createInfo.pColorBlendState = &colorBlending;
    createInfo.pDepthStencilState = &depthStencil;
    createInfo.pRasterizationState = &rasterizer;
    createInfo.pMultisampleState = &multisample;
    createInfo.pDynamicState = &dynamicState;



    // 设置着色器阶段
	std::vector<RHI::RHIShader*> graphicShaders;
    if (_desc.ShaderStages.VertexShader != nullptr) {
        graphicShaders.push_back(_desc.ShaderStages.VertexShader);
    }
	if (_desc.ShaderStages.FragmentShader != nullptr) {
		graphicShaders.push_back(_desc.ShaderStages.FragmentShader);
	}

    std::vector<VkPipelineShaderStageCreateInfo> shaderStages(graphicShaders.size());

    for (size_t i = 0; i < graphicShaders.size(); ++i) {
        const auto& shader = graphicShaders[i];
        VkPipelineShaderStageCreateInfo shaderStageInfo = {};
        auto vulkanShaderSP = dynamic_cast<VulkanRHIShader*>(shader);
        shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStageInfo.stage = vulkanShaderSP->GetShaderStage();
        shaderStageInfo.module = vulkanShaderSP->GetShaderModule();
        shaderStageInfo.pName = vulkanShaderSP->GetEntryPoint().c_str();
        shaderStages[i] = shaderStageInfo;
    }
    createInfo.stageCount = shaderStages.size();
    createInfo.pStages = shaderStages.data();

    // 创建图形管线
    if (!VKFunc::CreateGraphicsPipelines(device->GetHandle(), VK_NULL_HANDLE, 1, &createInfo, &pipeline)) {
        throw std::runtime_error("无法创建图形管线!");
    }
}


VulkanComputePipelineState::VulkanComputePipelineState(VulkanDevice* device, const RHIComputePipelineStateDesc& pipelineDesc)
    : VulkanPipelineBase(device), RHIComputePipelineState(pipelineDesc) {
    // Convert RHI desc to Vulkan create info
    createInfo = {}; // Initialize createInfo
    createInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    createInfo.flags = 0;
    createInfo.pNext = nullptr;
    createInfo.basePipelineHandle = VK_NULL_HANDLE;
    createInfo.basePipelineIndex = -1;

    // Build layout and cache it
    PipelineLayoutInfo layoutInfo = BuildPipelineLayoutInfo(pipelineDesc);
    pipelineLayout = device->GetPipelineLayoutCache()->GetOrCreateLayout(layoutInfo);
    createInfo.layout = pipelineLayout->GetHandle();

    CreatePipeline();
}

VulkanComputePipelineState::~VulkanComputePipelineState() {
}

void VulkanComputePipelineState::Bind(VulkanCommandBuffer* cmdBuffer)
{
    VKFunc::CmdBindPipeline(cmdBuffer->GetHandle(), VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
}

PipelineLayoutInfo VulkanComputePipelineState::BuildPipelineLayoutInfo(const RHIComputePipelineStateDesc& pipelineDesc)
{
    PipelineLayoutInfo layoutInfo;

    auto vulkanShaderSP = dynamic_cast<VulkanRHIShader*>(pipelineDesc.ComputeShader);
    if (!vulkanShaderSP)
        return layoutInfo;

    const auto& header = vulkanShaderSP->GetShaderReflection();
    RHI::ERHIShaderFrequency frequency = header.Frequency;
    VkShaderStageFlags stageMask = TransformShaderFrequencyToStage(frequency);

    // 获取该着色器频率对应的 ShaderFrequencyLayoutInfo
    uint32_t freqKey = static_cast<uint32_t>(frequency);
    PipelineLayoutInfo::ShaderFrequencyLayoutInfo& freqInfo = 
        layoutInfo.setLayoutsByFrequency[freqKey];

    auto ensureSet = [&](uint32_t set)
        {
            if (layoutInfo.Layouts.size() <= set)
            {
                layoutInfo.Layouts.resize(set + 1);
            }
        };

    for (const auto& binding : header.DescriptorBindings)
    {
        ensureSet(binding.Set);

        DescriptorSetLayoutInfo& setLayout = layoutInfo.Layouts[binding.Set];

        VkDescriptorType vkType = TransformDescriptorTypeFrom(binding.Type);

        setLayout.AddBinding(
            binding.Binding,
            vkType,
            binding.Count,
            stageMask);
        PipelineLayoutInfo::ShaderResourceParameterLayoutInfo rpLayout;
        rpLayout.SetIndex = binding.Set;
        rpLayout.BindingIndex = binding.Binding;
        rpLayout.DescriptorCount = binding.Count;
        freqInfo.ResourceParameterLayouts.push_back(rpLayout);

    }

    for (const auto& binding : header.UniformBufferBindings) {
        ensureSet(binding.Set);
        DescriptorSetLayoutInfo& setLayout = layoutInfo.Layouts[binding.Set];
        setLayout.AddBinding(
            binding.Binding,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ,
            1,
            stageMask);
        PipelineLayoutInfo::ShaderUniformBufferLayoutInfo ubLayout;
        ubLayout.SetIndex = binding.Set;
        ubLayout.BindingIndex = binding.Binding;
        ubLayout.Size = binding.Size;
        freqInfo.UniformBufferLayouts.push_back(ubLayout);
    }

    if (header.HasPushConstant)
    {
        layoutInfo.hasPushConstant = true;

        layoutInfo.pushConstant.offset = 0;
        layoutInfo.pushConstant.size = header.PushConstant.Size;

        layoutInfo.pushConstant.stageFlags = stageMask;
    }

    return layoutInfo;
}


void VulkanComputePipelineState::CreatePipeline() {
    // 设置VkComputePipelineCreateInfo的各个成员
    // 这里仅提供一个示例，具体成员需要根据pipelineDesc填充

    // 设置着色器阶段
    auto vulkanShaderSP = dynamic_cast<VulkanRHIShader*>(_desc.ComputeShader);
    if (!vulkanShaderSP)
        throw std::runtime_error("Compute shader is null for compute pipeline");

    VkPipelineShaderStageCreateInfo shaderStageInfo = {};
    shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStageInfo.stage = vulkanShaderSP->GetShaderStage(); // 计算着色器阶段
    shaderStageInfo.module = vulkanShaderSP->GetShaderModule();
    shaderStageInfo.pName = vulkanShaderSP->GetEntryPoint().c_str();

    createInfo.stage = shaderStageInfo;

    // 创建计算管线
    if (!VKFunc::CreateComputePipelines(device->GetHandle(), VK_NULL_HANDLE, 1, &createInfo, &pipeline)) {
        throw std::runtime_error("无法创建计算管线!");
    }
}

VulkanRayTracingPipeline::VulkanRayTracingPipeline(VulkanDevice* device, const RHIRayTracingPipelineStateDesc& pipelineDesc)
    : VulkanPipelineBase(device), RHIRayTracingPipelineState(pipelineDesc) {
    // Convert RHI desc to Vulkan create info
    createInfo = {}; // Initialize createInfo
    createInfo.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
    createInfo.flags = 0;
    createInfo.pNext = nullptr;
    createInfo.pLibraryInfo = nullptr;
    createInfo.pLibraryInterface = nullptr;
    createInfo.pDynamicState = nullptr;
    createInfo.layout = VK_NULL_HANDLE;
    createInfo.basePipelineHandle = VK_NULL_HANDLE;
    createInfo.basePipelineIndex = -1;

    PipelineLayoutInfo layoutInfo = BuildPipelineLayoutInfo(pipelineDesc);
    pipelineLayout = device->GetPipelineLayoutCache()->GetOrCreateLayout(layoutInfo);
    createInfo.layout = pipelineLayout->GetHandle();

    CreatePipeline();
}

VulkanRayTracingPipeline::~VulkanRayTracingPipeline() {
    if (shaderBindingTableBuffer != VK_NULL_HANDLE)
    {
        device->EnqueueBufferForDeletion(shaderBindingTableBuffer);
        shaderBindingTableBuffer = VK_NULL_HANDLE;
    }

    if (shaderBindingTableAllocation.GetMemory() != VK_NULL_HANDLE)
    {
        device->GetMemoryManager()->Free(shaderBindingTableAllocation);
        shaderBindingTableAllocation = VulkanAllocation{};
    }
}

PipelineLayoutInfo VulkanRayTracingPipeline::BuildPipelineLayoutInfo(const RHIRayTracingPipelineStateDesc& pipelineDesc)
{
    PipelineLayoutInfo layoutInfo;

    auto processShader = [&](VulkanRHIShader* shader)
        {
            if (!shader)
                return;

            const auto& header = shader->GetShaderReflection();
            RHI::ERHIShaderFrequency frequency = header.Frequency;
            VkShaderStageFlags stageMask = TransformShaderFrequencyToStage(frequency);

            uint32_t freqKey = static_cast<uint32_t>(frequency);
            PipelineLayoutInfo::ShaderFrequencyLayoutInfo& freqInfo =
                layoutInfo.setLayoutsByFrequency[freqKey];

            auto ensureSet = [&](uint32_t set)
                {
                    if (layoutInfo.Layouts.size() <= set)
                    {
                        layoutInfo.Layouts.resize(set + 1);
                    }
                };

            for (const auto& binding : header.DescriptorBindings)
            {
                ensureSet(binding.Set);

                DescriptorSetLayoutInfo& setLayout = layoutInfo.Layouts[binding.Set];
                VkDescriptorType vkType = TransformDescriptorTypeFrom(binding.Type);

                setLayout.AddBinding(
                    binding.Binding,
                    vkType,
                    binding.Count,
                    stageMask);

                PipelineLayoutInfo::ShaderResourceParameterLayoutInfo rpLayout;
                rpLayout.SetIndex = binding.Set;
                rpLayout.BindingIndex = binding.Binding;
                rpLayout.DescriptorCount = binding.Count;
                freqInfo.ResourceParameterLayouts.push_back(rpLayout);
            }

            for (const auto& binding : header.UniformBufferBindings)
            {
                ensureSet(binding.Set);

                DescriptorSetLayoutInfo& setLayout = layoutInfo.Layouts[binding.Set];
                setLayout.AddBinding(
                    binding.Binding,
                    VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    1,
                    stageMask);

                PipelineLayoutInfo::ShaderUniformBufferLayoutInfo ubLayout;
                ubLayout.SetIndex = binding.Set;
                ubLayout.BindingIndex = binding.Binding;
                ubLayout.Size = binding.Size;
                freqInfo.UniformBufferLayouts.push_back(ubLayout);
            }

            if (header.HasPushConstant)
            {
                layoutInfo.hasPushConstant = true;
                layoutInfo.pushConstant.offset = 0;
                layoutInfo.pushConstant.size = header.PushConstant.Size;
                layoutInfo.pushConstant.stageFlags |= stageMask;
            }
        };

    auto processTable = [&](const std::vector<RHIRayTracingShader*>& shaders)
        {
            for (RHIRayTracingShader* shader : shaders)
            {
                processShader(dynamic_cast<VulkanRHIShader*>(shader));
            }
        };

    processTable(pipelineDesc.RayGenTable);
    processTable(pipelineDesc.MissTable);
    processTable(pipelineDesc.HitGroupTable);
    processTable(pipelineDesc.CallableTable);
    processTable(pipelineDesc.IntersectTable);

    return layoutInfo;
}

void VulkanRayTracingPipeline::CreatePipeline() {
    std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
    std::vector<VkRayTracingShaderGroupCreateInfoKHR> shaderGroups;

    auto appendStage = [&](RHIRayTracingShader* shader) -> uint32_t
        {
            auto vulkanShader = dynamic_cast<VulkanRHIShader*>(shader);
            if (!vulkanShader)
            {
                throw std::runtime_error("Ray tracing shader is null for ray tracing pipeline");
            }

            VkPipelineShaderStageCreateInfo shaderStageInfo = {};
            shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            shaderStageInfo.stage = vulkanShader->GetShaderStage();
            shaderStageInfo.module = vulkanShader->GetShaderModule();
            shaderStageInfo.pName = vulkanShader->GetEntryPoint().c_str();

            shaderStages.push_back(shaderStageInfo);
            return static_cast<uint32_t>(shaderStages.size() - 1);
        };

    auto addGeneralGroup = [&](RHIRayTracingShader* shader)
        {
            VkRayTracingShaderGroupCreateInfoKHR groupInfo = {};
            groupInfo.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
            groupInfo.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
            groupInfo.generalShader = appendStage(shader);
            groupInfo.closestHitShader = VK_SHADER_UNUSED_KHR;
            groupInfo.anyHitShader = VK_SHADER_UNUSED_KHR;
            groupInfo.intersectionShader = VK_SHADER_UNUSED_KHR;
            shaderGroups.push_back(groupInfo);
        };

    for (RHIRayTracingShader* shader : _desc.RayGenTable)
    {
        addGeneralGroup(shader);
    }

    for (RHIRayTracingShader* shader : _desc.MissTable)
    {
        addGeneralGroup(shader);
    }

    for (size_t i = 0; i < _desc.HitGroupTable.size(); ++i)
    {
        VkRayTracingShaderGroupCreateInfoKHR groupInfo = {};
        groupInfo.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
        groupInfo.generalShader = VK_SHADER_UNUSED_KHR;
        groupInfo.anyHitShader = VK_SHADER_UNUSED_KHR;
        groupInfo.closestHitShader = VK_SHADER_UNUSED_KHR;
        groupInfo.intersectionShader = VK_SHADER_UNUSED_KHR;

        RHIRayTracingShader* closestHitShader = _desc.HitGroupTable[i];
        RHIRayTracingShader* intersectionShader =
            i < _desc.IntersectTable.size() ? _desc.IntersectTable[i] : nullptr;

        if (intersectionShader)
        {
            groupInfo.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR;
            groupInfo.intersectionShader = appendStage(intersectionShader);
        }
        else
        {
            groupInfo.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
        }

        if (closestHitShader)
        {
            groupInfo.closestHitShader = appendStage(closestHitShader);
        }

        shaderGroups.push_back(groupInfo);
    }

    for (RHIRayTracingShader* shader : _desc.CallableTable)
    {
        addGeneralGroup(shader);
    }

    if (shaderStages.empty() || shaderGroups.empty())
    {
        throw std::runtime_error("Ray tracing pipeline has no valid shaders or groups");
    }

    createInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
    createInfo.pStages = shaderStages.data();
    createInfo.groupCount = static_cast<uint32_t>(shaderGroups.size());
    createInfo.pGroups = shaderGroups.data();
    createInfo.maxPipelineRayRecursionDepth = 1;

    if (_desc.RayGenTable.size() != 1)
        throw std::runtime_error("The RHI trace command requires exactly one ray-generation shader");
    if (!VKFunc::CreateRayTracingPipelinesKHR(device->GetHandle(), VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &createInfo, &pipeline))
        throw std::runtime_error("Failed to create the ray tracing pipeline");

    VkPhysicalDeviceRayTracingPipelinePropertiesKHR rtProperties{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR };
    VkPhysicalDeviceProperties2 properties{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
    properties.pNext = &rtProperties;
    VKFunc::GetPhysicalDeviceProperties2(device->GetPhysicalDevice(), &properties);
    const auto alignUp = [](VkDeviceSize value, VkDeviceSize alignment) {
        return (value + alignment - 1) / alignment * alignment;
    };
    const VkDeviceSize stride = alignUp(rtProperties.shaderGroupHandleSize, rtProperties.shaderGroupHandleAlignment);
    const VkDeviceSize alignment = rtProperties.shaderGroupBaseAlignment;
    if (!stride || stride > rtProperties.maxShaderGroupStride)
        throw std::runtime_error("Invalid ray tracing shader record stride");
    const uint32_t counts[] = { 1, static_cast<uint32_t>(_desc.MissTable.size()),
        static_cast<uint32_t>(_desc.HitGroupTable.size()), static_cast<uint32_t>(_desc.CallableTable.size()) };
    VkDeviceSize offsets[4]{};
    VkDeviceSize tableSize = 0;
    for (uint32_t i = 0; i < 4; ++i)
    {
        tableSize = alignUp(tableSize, alignment);
        offsets[i] = tableSize;
        tableSize += counts[i] * stride;
    }
    std::vector<uint8_t> handles(shaderGroups.size() * rtProperties.shaderGroupHandleSize);
    if (!VKFunc::GetRayTracingShaderGroupHandlesKHR(device->GetHandle(), pipeline, 0,
        static_cast<uint32_t>(shaderGroups.size()), handles.size(), handles.data()))
        throw std::runtime_error("Failed to retrieve ray tracing shader group handles");

    VkBufferCreateInfo bufferInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bufferInfo.size = tableSize + alignment - 1;
    bufferInfo.usage = VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (!VKFunc::CreateBuffer(device->GetHandle(), &bufferInfo, &shaderBindingTableBuffer))
        throw std::runtime_error("Failed to create the shader binding table buffer");
    VkMemoryRequirements requirements{};
    VKFunc::GetBufferMemoryRequirements(device->GetHandle(), shaderBindingTableBuffer, &requirements);
    if (!device->GetMemoryManager()->Allocate(requirements,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, shaderBindingTableAllocation))
        throw std::runtime_error("Failed to allocate the shader binding table");
    VKFunc::BindBufferMemory(device->GetHandle(), shaderBindingTableBuffer,
        shaderBindingTableAllocation.GetMemory(), shaderBindingTableAllocation.GetOffset());
    VkBufferDeviceAddressInfo addressInfo{ VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
    addressInfo.buffer = shaderBindingTableBuffer;
    const VkDeviceAddress address = VKFunc::GetBufferDeviceAddress(device->GetHandle(), &addressInfo);
    shaderBindingTableDeviceAddress = alignUp(address, alignment);
    auto* mapped = static_cast<uint8_t*>(shaderBindingTableAllocation.GetMappedPointer());
    if (!mapped || !address)
        throw std::runtime_error("The shader binding table has no mapped memory or device address");
    memset(mapped, 0, static_cast<size_t>(bufferInfo.size));
    mapped += shaderBindingTableDeviceAddress - address;
    VkStridedDeviceAddressRegionKHR* regions[] = { &rayGenShaderBindingTableRegion,
        &missShaderBindingTableRegion, &hitShaderBindingTableRegion, &callableShaderBindingTableRegion };
    uint32_t group = 0;
    for (uint32_t table = 0; table < 4; ++table)
    {
        *regions[table] = {};
        if (!counts[table]) continue;
        regions[table]->deviceAddress = shaderBindingTableDeviceAddress + offsets[table];
        regions[table]->stride = stride;
        regions[table]->size = counts[table] * stride;
        for (uint32_t record = 0; record < counts[table]; ++record, ++group)
            memcpy(mapped + offsets[table] + record * stride,
                handles.data() + group * rtProperties.shaderGroupHandleSize, rtProperties.shaderGroupHandleSize);
    }
}

} // namespace WR::RHIVulkan
