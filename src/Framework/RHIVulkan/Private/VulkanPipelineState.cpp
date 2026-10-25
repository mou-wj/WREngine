#include "VulkanPipelineState.h"
#include "VulkanShader.h"

#include "VulkanDescriptorSets.h"
#include "VulkanResource.h"
#include "VulkanRHIUtils.h"
#include "VulkanCommandBuffer.h"
#include "VulkanMemory.h"
#include <algorithm>
#include <vector>

using namespace RHI;

namespace RHIVulkan {

VulkanPipelineBase::VulkanPipelineBase(VulkanDevice* device)
: _device(device), _pipeline(VK_NULL_HANDLE)
{

}
VulkanPipelineBase::~VulkanPipelineBase() {
    VkDevice deviceHandle = _device->GetHandle();
    if (_pipeline != VK_NULL_HANDLE) {
        _device->EnqueuePipelineForDeletion(_pipeline);
        _pipeline = VK_NULL_HANDLE;
    }

}



VulkanGraphicsPipelineState::VulkanGraphicsPipelineState(VulkanDevice* device, const RHIGraphicsPipelineStateDesc& pipelineDesc)
    : VulkanPipelineBase(device), RHIGraphicsPipelineState(pipelineDesc) {
    // Convert RHI desc to Vulkan create info
    m_CreateInfo = {}; // Initialize createInfo
    m_CreateInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    VulkanRenderTargetLayout layout(pipelineDesc.AttachmentDesc);
    auto renderPass = device->GetRenderPassManager()->GetOrCreateRenderPass(layout);
    m_CreateInfo.renderPass = renderPass->GetHandle();
    m_CreateInfo.subpass = pipelineDesc.AttachmentDesc.SubpassIndex;
	auto layoutInfo = BuildPipelineLayoutInfo(pipelineDesc);
    _pipelineLayout = device->GetPipelineLayoutCache()->GetOrCreateLayout(layoutInfo);
    
    CreatePipeline();
}

VulkanGraphicsPipelineState::~VulkanGraphicsPipelineState() {
}

void VulkanGraphicsPipelineState::Bind(VulkanCommandBuffer* cmdBuffer)
{
    VKFunc::CmdBindPipeline(cmdBuffer->GetHandle(), VK_PIPELINE_BIND_POINT_GRAPHICS, _pipeline);
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
                layoutInfo.SetLayoutsByFrequency[freqKey];

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
           

            if (header.bHasPushConstant)
            {
                layoutInfo.bHasPushConstant = true;

                layoutInfo.PushConstant.offset = 0;
                layoutInfo.PushConstant.size = header.PushConstant.Size;

                layoutInfo.PushConstant.stageFlags |=
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
    VkPipelineVertexInputStateCreateInfo &vertexInputInfo = dynamic_cast<VulkanVertexDescState*>(_desc.VertexDescState)->VertexInputInfo;

    VkPipelineRasterizationStateCreateInfo rasterizer = dynamic_cast<VulkanRasterizerState*>(_desc.RasterizerState)->RasterizerInfo;

    VkPipelineColorBlendStateCreateInfo colorBlending = dynamic_cast<VulkanColorBlendState*>(_desc.ColorBlendState)->ColorBlendInfo;

    VkPipelineDepthStencilStateCreateInfo depthStencil = dynamic_cast<VulkanDepthStencilState*>(_desc.DepthStencilState)->DepthStencilInfo;

    // 设置管线布局
    m_CreateInfo.layout = _pipelineLayout->GetHandle();


    
    m_DynamicStates.push_back(VK_DYNAMIC_STATE_VIEWPORT);
    m_DynamicStates.push_back(VK_DYNAMIC_STATE_SCISSOR);

    VkPipelineDynamicStateCreateInfo dynamicState = {};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = m_DynamicStates.size();
    dynamicState.pDynamicStates = m_DynamicStates.data();


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
    m_CreateInfo.pVertexInputState = &vertexInputInfo;
    m_CreateInfo.pInputAssemblyState = &inputAssembly;
    m_CreateInfo.pViewportState = &viewportState;
    m_CreateInfo.pColorBlendState = &colorBlending;
    m_CreateInfo.pDepthStencilState = &depthStencil;
    m_CreateInfo.pRasterizationState = &rasterizer;
    m_CreateInfo.pMultisampleState = &multisample;
    m_CreateInfo.pDynamicState = &dynamicState;



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
    m_CreateInfo.stageCount = shaderStages.size();
    m_CreateInfo.pStages = shaderStages.data();

    // 创建图形管线
    if (!VKFunc::CreateGraphicsPipelines(_device->GetHandle(), VK_NULL_HANDLE, 1, &m_CreateInfo, &_pipeline)) {
        throw std::runtime_error("无法创建图形管线!");
    }
}


VulkanComputePipelineState::VulkanComputePipelineState(VulkanDevice* device, const RHIComputePipelineStateDesc& pipelineDesc)
    : VulkanPipelineBase(device), RHIComputePipelineState(pipelineDesc) {
    // Convert RHI desc to Vulkan create info
    m_CreateInfo = {}; // Initialize createInfo
    m_CreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    m_CreateInfo.flags = 0;
    m_CreateInfo.pNext = nullptr;
    m_CreateInfo.basePipelineHandle = VK_NULL_HANDLE;
    m_CreateInfo.basePipelineIndex = -1;

    // Build layout and cache it
    PipelineLayoutInfo layoutInfo = BuildPipelineLayoutInfo(pipelineDesc);
    _pipelineLayout = device->GetPipelineLayoutCache()->GetOrCreateLayout(layoutInfo);
    m_CreateInfo.layout = _pipelineLayout->GetHandle();

    CreatePipeline();
}

VulkanComputePipelineState::~VulkanComputePipelineState() {
}

void VulkanComputePipelineState::Bind(VulkanCommandBuffer* cmdBuffer)
{
    VKFunc::CmdBindPipeline(cmdBuffer->GetHandle(), VK_PIPELINE_BIND_POINT_COMPUTE, _pipeline);
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
        layoutInfo.SetLayoutsByFrequency[freqKey];

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

    if (header.bHasPushConstant)
    {
        layoutInfo.bHasPushConstant = true;

        layoutInfo.PushConstant.offset = 0;
        layoutInfo.PushConstant.size = header.PushConstant.Size;

        layoutInfo.PushConstant.stageFlags = stageMask;
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

    m_CreateInfo.stage = shaderStageInfo;

    // 创建计算管线
    if (!VKFunc::CreateComputePipelines(_device->GetHandle(), VK_NULL_HANDLE, 1, &m_CreateInfo, &_pipeline)) {
        throw std::runtime_error("无法创建计算管线!");
    }
}

VulkanRayTracingPipeline::VulkanRayTracingPipeline(VulkanDevice* device, const RHIRayTracingPipelineStateDesc& pipelineDesc)
    : VulkanPipelineBase(device), RHIRayTracingPipelineState(pipelineDesc) {
    // Convert RHI desc to Vulkan create info
    m_CreateInfo = {}; // Initialize createInfo
    m_CreateInfo.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
    m_CreateInfo.flags = 0;
    m_CreateInfo.pNext = nullptr;
    m_CreateInfo.pLibraryInfo = nullptr;
    m_CreateInfo.pLibraryInterface = nullptr;
    m_CreateInfo.pDynamicState = nullptr;
    m_CreateInfo.layout = VK_NULL_HANDLE;
    m_CreateInfo.basePipelineHandle = VK_NULL_HANDLE;
    m_CreateInfo.basePipelineIndex = -1;

    PipelineLayoutInfo layoutInfo = BuildPipelineLayoutInfo(pipelineDesc);
    _pipelineLayout = device->GetPipelineLayoutCache()->GetOrCreateLayout(layoutInfo);
    m_CreateInfo.layout = _pipelineLayout->GetHandle();

    CreatePipeline();
    CreateShaderBindingTable(m_CreateInfo.groupCount);
}

VulkanRayTracingPipeline::~VulkanRayTracingPipeline() {
    if (m_ShaderBindingTableBuffer != VK_NULL_HANDLE)
    {
        _device->EnqueueBufferForDeletion(m_ShaderBindingTableBuffer);
        m_ShaderBindingTableBuffer = VK_NULL_HANDLE;
    }

    if (m_ShaderBindingTableAllocation.GetMemory() != VK_NULL_HANDLE)
    {
        _device->GetMemoryManager()->Free(m_ShaderBindingTableAllocation);
        m_ShaderBindingTableAllocation = VulkanAllocation{};
    }
}

PipelineLayoutInfo VulkanRayTracingPipeline::BuildPipelineLayoutInfo(const RHIRayTracingPipelineStateDesc& pipelineDesc)
{
    PipelineLayoutInfo layoutInfo;
    std::vector<VulkanRHIShader*> processedShaders;

    auto processShader = [&](RHIRayTracingShader* rhiShader)
        {
            if (!rhiShader)
                return;
            auto* shader = dynamic_cast<VulkanRHIShader*>(rhiShader);
            if (!shader)
                throw std::runtime_error("Ray tracing pipeline requires Vulkan shaders");
            // 同一阶段 Shader 可被多个命中组复用，反射布局只处理一次。
            if (std::find(processedShaders.begin(), processedShaders.end(), shader) != processedShaders.end())
                return;
            processedShaders.push_back(shader);

            const auto& header = shader->GetShaderReflection();
            RHI::ERHIShaderFrequency frequency = header.Frequency;
            VkShaderStageFlags stageMask = TransformShaderFrequencyToStage(frequency);

            uint32_t freqKey = static_cast<uint32_t>(frequency);
            PipelineLayoutInfo::ShaderFrequencyLayoutInfo& freqInfo =
                layoutInfo.SetLayoutsByFrequency[freqKey];

            auto addBinding = [&](uint32_t set, uint32_t binding, VkDescriptorType type, uint32_t count)
                {
                    if (layoutInfo.Layouts.size() <= set)
                    {
                        layoutInfo.Layouts.resize(set + 1);
                    }
                    auto& setLayout = layoutInfo.Layouts[set];
                    for (auto& existing : setLayout.Bindings)
                    {
                        if (existing.Binding != binding)
                            continue;
                        // 各阶段共享绑定时合并可见性，不生成重复的 Vulkan binding。
                        if (existing.Type != type || existing.Count != count)
                            throw std::runtime_error("Ray tracing shader descriptor bindings conflict");
                        existing.StageFlags |= stageMask;
                        return;
                    }
                    setLayout.AddBinding(binding, type, count, stageMask);
                };

            for (const auto& binding : header.DescriptorBindings)
            {
                VkDescriptorType vkType = TransformDescriptorTypeFrom(binding.Type);
                addBinding(binding.Set, binding.Binding, vkType, binding.Count);

                PipelineLayoutInfo::ShaderResourceParameterLayoutInfo rpLayout;
                rpLayout.SetIndex = binding.Set;
                rpLayout.BindingIndex = binding.Binding;
                rpLayout.DescriptorCount = binding.Count;
                freqInfo.ResourceParameterLayouts.push_back(rpLayout);
            }

            for (const auto& binding : header.UniformBufferBindings)
            {
                addBinding(binding.Set, binding.Binding, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1);

                PipelineLayoutInfo::ShaderUniformBufferLayoutInfo ubLayout;
                ubLayout.SetIndex = binding.Set;
                ubLayout.BindingIndex = binding.Binding;
                ubLayout.Size = binding.Size;
                freqInfo.UniformBufferLayouts.push_back(ubLayout);
            }

            if (header.bHasPushConstant)
            {
                layoutInfo.bHasPushConstant = true;
                layoutInfo.PushConstant.offset = 0;
                layoutInfo.PushConstant.size = std::max<uint32_t>(layoutInfo.PushConstant.size, header.PushConstant.Size);
                layoutInfo.PushConstant.stageFlags |= stageMask;
            }
        };

    auto processTable = [&](const std::vector<RHIRayTracingShader*>& shaders)
        {
            for (RHIRayTracingShader* shader : shaders)
            {
                processShader(shader);
            }
        };

    processTable(pipelineDesc.RayGenTable);
    processTable(pipelineDesc.MissTable);
    for (const auto& hitGroup : pipelineDesc.HitGroupTable)
    {
        processShader(hitGroup.ClosestHitShader);
        processShader(hitGroup.AnyHitShader);
        processShader(hitGroup.IntersectionShader);
    }
    processTable(pipelineDesc.CallableTable);

    return layoutInfo;
}

void VulkanRayTracingPipeline::CreatePipeline() {
    // stages 保存程序入口；groups 的各 Shader 字段引用 stages 下标，而非 group 下标。
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
            // 0 是有效的 stage 下标；未使用的字段必须显式设置为 VK_SHADER_UNUSED_KHR。
            groupInfo.closestHitShader = VK_SHADER_UNUSED_KHR;
            groupInfo.anyHitShader = VK_SHADER_UNUSED_KHR;
            groupInfo.intersectionShader = VK_SHADER_UNUSED_KHR;
            shaderGroups.push_back(groupInfo);
        };

    // SBT 按同样的 RayGen / Miss / Hit / Callable 顺序复制各组的 handle。
    for (RHIRayTracingShader* shader : _desc.RayGenTable)
    {
        addGeneralGroup(shader);
    }

    for (RHIRayTracingShader* shader : _desc.MissTable)
    {
        addGeneralGroup(shader);
    }

    for (const auto& hitGroup : _desc.HitGroupTable)
    {
        VkRayTracingShaderGroupCreateInfoKHR groupInfo = {};
        groupInfo.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
        groupInfo.generalShader = VK_SHADER_UNUSED_KHR;
        groupInfo.anyHitShader = VK_SHADER_UNUSED_KHR;
        groupInfo.closestHitShader = VK_SHADER_UNUSED_KHR;
        groupInfo.intersectionShader = VK_SHADER_UNUSED_KHR;

        if (hitGroup.IntersectionShader)
        {
            // 程序化几何由 Intersection 求交；三角形组使用原生三角形求交流程。
            groupInfo.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR;
            groupInfo.intersectionShader = appendStage(hitGroup.IntersectionShader);
        }
        else
        {
            groupInfo.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
        }

        if (hitGroup.ClosestHitShader)
        {
            groupInfo.closestHitShader = appendStage(hitGroup.ClosestHitShader);
        }
        if (hitGroup.AnyHitShader)
        {
            groupInfo.anyHitShader = appendStage(hitGroup.AnyHitShader);
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

    m_CreateInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
    m_CreateInfo.pStages = shaderStages.data();
    m_CreateInfo.groupCount = static_cast<uint32_t>(shaderGroups.size());
    m_CreateInfo.pGroups = shaderGroups.data();
    m_CreateInfo.maxPipelineRayRecursionDepth = 1;

    if (_desc.RayGenTable.size() != 1)
        throw std::runtime_error("The RHI trace command requires exactly one ray-generation shader");
    if (!VKFunc::CreateRayTracingPipelinesKHR(_device->GetHandle(), VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &m_CreateInfo, &_pipeline))
        throw std::runtime_error("Failed to create the ray tracing pipeline");
}

void VulkanRayTracingPipeline::CreateShaderBindingTable(uint32_t shaderGroupCount)
{
    const auto& rtProperties = _device->GetRayTracingPipelineProperties();
    const auto alignUp = [](VkDeviceSize value, VkDeviceSize alignment) {
        return (value + alignment - 1) / alignment * alignment;
    };
    // record 的字节步长按 HandleAlignment 对齐，各表的起始地址按 BaseAlignment 对齐。
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
    // handle 属于已创建的 pipeline group；只分配 SBT Buffer 而不写入 handle 无法完成程序选择。
    std::vector<uint8_t> handles(static_cast<size_t>(shaderGroupCount) * rtProperties.shaderGroupHandleSize);
    if (!VKFunc::GetRayTracingShaderGroupHandlesKHR(_device->GetHandle(), _pipeline, 0,
        shaderGroupCount, handles.size(), handles.data()))
        throw std::runtime_error("Failed to retrieve ray tracing shader group handles");

    VkBufferCreateInfo bufferInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    // 预留实际 Buffer 设备地址向上对齐时所需的额外空间。
    bufferInfo.size = tableSize + alignment - 1;
    bufferInfo.usage = VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (!VKFunc::CreateBuffer(_device->GetHandle(), &bufferInfo, &m_ShaderBindingTableBuffer))
        throw std::runtime_error("Failed to create the shader binding table buffer");
    VkMemoryRequirements requirements{};
    VKFunc::GetBufferMemoryRequirements(_device->GetHandle(), m_ShaderBindingTableBuffer, &requirements);
    if (!_device->GetMemoryManager()->Allocate(requirements,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_ShaderBindingTableAllocation))
        throw std::runtime_error("Failed to allocate the shader binding table");
    VKFunc::BindBufferMemory(_device->GetHandle(), m_ShaderBindingTableBuffer,
        m_ShaderBindingTableAllocation.GetMemory(), m_ShaderBindingTableAllocation.GetOffset());
    VkBufferDeviceAddressInfo addressInfo{ VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
    addressInfo.buffer = m_ShaderBindingTableBuffer;
    const VkDeviceAddress address = VKFunc::GetBufferDeviceAddress(_device->GetHandle(), &addressInfo);
    m_ShaderBindingTableDeviceAddress = alignUp(address, alignment);
    auto* mapped = static_cast<uint8_t*>(m_ShaderBindingTableAllocation.GetMappedPointer());
    if (!mapped || !address)
        throw std::runtime_error("The shader binding table has no mapped memory or device address");
    memset(mapped, 0, static_cast<size_t>(bufferInfo.size));
    // CPU 写入位置必须与传给 GPU 的对齐后起始地址保持相同偏移。
    mapped += m_ShaderBindingTableDeviceAddress - address;
    VkStridedDeviceAddressRegionKHR* regions[] = { &m_RayGenShaderBindingTableRegion,
        &m_MissShaderBindingTableRegion, &m_HitShaderBindingTableRegion, &m_CallableShaderBindingTableRegion };
    uint32_t group = 0;
    for (uint32_t table = 0; table < 4; ++table)
    {
        // 空表不指向占位记录；例如没有 Callable 时保持 address/stride/size 全零。
        *regions[table] = {};
        if (!counts[table]) continue;
        regions[table]->deviceAddress = m_ShaderBindingTableDeviceAddress + offsets[table];
        regions[table]->stride = stride;
        regions[table]->size = counts[table] * stride;
        for (uint32_t record = 0; record < counts[table]; ++record, ++group)
            memcpy(mapped + offsets[table] + record * stride,
                handles.data() + group * rtProperties.shaderGroupHandleSize, rtProperties.shaderGroupHandleSize);
    }
}

} // namespace WR::RHIVulkan
