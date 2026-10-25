#pragma once

#include "VulkanDevice.h"
#include <vector>
#include <unordered_map>
#include <memory>
#include <string>
#include "VulkanDescriptorSets.h"
#include "VulkanRenderPass.h"
#include "VulkanResource.h"
#include "RHIDefine.h"
#include "VulkanFuncWrapper.h"
#include "HashHelper.hpp"

namespace RHIVulkan{
    struct PipelineLayoutInfo
    {
        struct ShaderResourceParameterLayoutInfo {
            uint32_t SetIndex;
            uint32_t BindingIndex;
            uint32_t DescriptorCount = 1;
        };
        struct ShaderUniformBufferLayoutInfo {
            uint32_t SetIndex;
            uint32_t BindingIndex;
            uint32_t Size;
        };

        // 按着色器频率分组管理 descriptor set layouts
        // key: ERHIShaderFrequency cast to uint32_t, value: descriptor set layouts for that stage
		struct ShaderFrequencyLayoutInfo
		{
            std::vector<ShaderResourceParameterLayoutInfo> ResourceParameterLayouts;
            std::vector<ShaderUniformBufferLayoutInfo> UniformBufferLayouts;
		};

        std::vector<DescriptorSetLayoutInfo> Layouts;//pipeline 全局setlayout
        std::unordered_map<uint32_t, ShaderFrequencyLayoutInfo> SetLayoutsByFrequency;
        VkPushConstantRange PushConstant{};
        bool bHasPushConstant = false;

        // 获取指定着色器频率的 descriptor set layouts
        const ShaderFrequencyLayoutInfo* GetLayoutsForFrequency(RHI::ERHIShaderFrequency frequency) const
        {
            auto it = SetLayoutsByFrequency.find(static_cast<uint32_t>(frequency));
            if (it != SetLayoutsByFrequency.end()) {
                return &it->second;
            }
            return nullptr;
        }

        void AddUniformBufferLayoutForFrequency(RHI::ERHIShaderFrequency frequency, const ShaderUniformBufferLayoutInfo& layout)
		{
			ShaderFrequencyLayoutInfo& freqInfo = SetLayoutsByFrequency[static_cast<uint32_t>(frequency)];
			freqInfo.UniformBufferLayouts.push_back(layout);
		}

        // 获取所有着色器频率的 descriptor set layouts（展平）
        std::vector<DescriptorSetLayoutInfo> GetAllLayouts() const
        {
            return Layouts;
        }

        static uint64_t CalculateHash(const PipelineLayoutInfo& info)
        {
            size_t h = 0;
            // Hash all layouts for this frequency
            for (const auto& layout : info.Layouts)
            {
                HashCombine(h, std::hash<uint64_t>()((uint64_t)DescriptorSetLayoutInfo::CalculateHash(layout)));
            }

            if (info.bHasPushConstant)
            {
                HashCombine(h, info.PushConstant.offset);
                HashCombine(h, info.PushConstant.size);
                HashCombine(h, info.PushConstant.stageFlags);
            }

            return h;
        }
    };

    class VulkanPipelineLayout
    {
    public:

        VulkanPipelineLayout(
            VulkanDevice* device,
            const PipelineLayoutInfo& info)
            : m_Device(device)
            , m_Info(info)
        {
            VkPipelineLayoutCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

            // 获取所有着色器频率的 descriptor set layouts
            std::vector<DescriptorSetLayoutInfo> allLayouts = info.GetAllLayouts();
            createInfo.setLayoutCount = (uint32_t)allLayouts.size();

            std::vector<VkDescriptorSetLayout> layouts;
            for (const auto& layout : allLayouts)
			{
				layouts.push_back(
					m_Device->GetDescriptorSetLayoutManager()->GetOrCreateLayout(layout));
			}
            createInfo.pSetLayouts =
                layouts.data();

            if (info.bHasPushConstant)
            {
                createInfo.pushConstantRangeCount = 1;
                createInfo.pPushConstantRanges = &info.PushConstant;
            }

            VKFunc::CreatePipelineLayout(
                m_Device->GetHandle(),
                &createInfo,
                &m_Layout);
        }

        ~VulkanPipelineLayout()
        {
            if (m_Layout != VK_NULL_HANDLE)
            {
                VKFunc::DestroyPipelineLayout(m_Device->GetHandle(), m_Layout);
            }
                
        }

        VkPipelineLayout GetHandle()
        {
            return m_Layout;
        }
        const PipelineLayoutInfo& GetInfo() const { return m_Info; }

    private:

        VulkanDevice* m_Device;
        VkPipelineLayout m_Layout = VK_NULL_HANDLE;
        PipelineLayoutInfo m_Info;
        friend class VulkanPipelineStateCache;
    };

    class VulkanPipelineLayoutCache
    {
    public:

        VulkanPipelineLayoutCache(VulkanDevice* device)
            : m_Device(device)
        {
        }   
        ~VulkanPipelineLayoutCache() 
        {
			m_Layouts.clear();
        }

        VulkanPipelineLayout* GetOrCreateLayout(
            const PipelineLayoutInfo& info)
        {
            uint64_t hash = PipelineLayoutInfo::CalculateHash(info);

            auto it = m_Layouts.find(hash);
            if (it != m_Layouts.end())
            {
                return it->second.get();
            }

            auto layout =
                std::make_unique<VulkanPipelineLayout>(
                    m_Device,
                    info);

            VulkanPipelineLayout* result = layout.get();

            m_Layouts[hash] = std::move(layout);

            return result;
        }

    private:

        VulkanDevice* m_Device;

        std::unordered_map<
            uint64_t,
            std::unique_ptr<VulkanPipelineLayout>> m_Layouts;
    };



// Pipeline基类
class VulkanPipelineBase {
public:
    explicit VulkanPipelineBase(VulkanDevice* device);
    virtual ~VulkanPipelineBase();

    VkPipeline GetHandle() const { return _pipeline; }
    VulkanPipelineLayout* GetLayout() const { return _pipelineLayout; }

protected:

    VulkanDevice* _device;
    VkPipeline _pipeline;
	VulkanPipelineLayout* _pipelineLayout;
    //VulkanDescriptorSets boundDescriptorSets; // 绑定的描述符集
    VulkanRenderPass* _renderPass;
    friend class VulkanPipelineStateCache;
    
    virtual void CreatePipeline() = 0;
};

// 图形管线
class VulkanGraphicsPipelineState : public VulkanPipelineBase ,public RHIGraphicsPipelineState {
public:
    VulkanGraphicsPipelineState(VulkanDevice* device, const RHIGraphicsPipelineStateDesc& pipelineDesc);
    ~VulkanGraphicsPipelineState();
    void Bind(VulkanCommandBuffer* cmdBuffer);

private:
    PipelineLayoutInfo BuildPipelineLayoutInfo(
        const RHIGraphicsPipelineStateDesc& pipelineDesc);
    VkGraphicsPipelineCreateInfo m_CreateInfo; // Vulkan-specific create info

    void CreatePipeline() override;
    std::vector<VkDynamicState>  m_DynamicStates; // 动态状态列表
};

// 计算管线
class VulkanComputePipelineState : public VulkanPipelineBase ,public RHIComputePipelineState {
public:
    VulkanComputePipelineState(VulkanDevice* device, const RHIComputePipelineStateDesc& pipelineDesc);
    ~VulkanComputePipelineState();
    void Bind(VulkanCommandBuffer* cmdBuffer);

private:
    PipelineLayoutInfo BuildPipelineLayoutInfo(const RHIComputePipelineStateDesc& pipelineDesc);
    VkComputePipelineCreateInfo m_CreateInfo; // Vulkan-specific create info

    void CreatePipeline() override;
};

// 光线追踪管线
class VulkanRayTracingPipeline : public VulkanPipelineBase,public RHIRayTracingPipelineState {
public:
    VulkanRayTracingPipeline(VulkanDevice* device, const RHIRayTracingPipelineStateDesc& pipelineDesc);
    ~VulkanRayTracingPipeline();

    const VkStridedDeviceAddressRegionKHR& GetRayGenShaderBindingTableRegion() const { return m_RayGenShaderBindingTableRegion; }
    const VkStridedDeviceAddressRegionKHR& GetMissShaderBindingTableRegion() const { return m_MissShaderBindingTableRegion; }
    const VkStridedDeviceAddressRegionKHR& GetHitShaderBindingTableRegion() const { return m_HitShaderBindingTableRegion; }
    const VkStridedDeviceAddressRegionKHR& GetCallableShaderBindingTableRegion() const { return m_CallableShaderBindingTableRegion; }

private:
    PipelineLayoutInfo BuildPipelineLayoutInfo(const RHIRayTracingPipelineStateDesc& pipelineDesc);
    VkRayTracingPipelineCreateInfoKHR m_CreateInfo; // Vulkan-specific create info

    VkStridedDeviceAddressRegionKHR m_RayGenShaderBindingTableRegion{};
    VkStridedDeviceAddressRegionKHR m_MissShaderBindingTableRegion{};
    VkStridedDeviceAddressRegionKHR m_HitShaderBindingTableRegion{};
    VkStridedDeviceAddressRegionKHR m_CallableShaderBindingTableRegion{};

    VkBuffer m_ShaderBindingTableBuffer = VK_NULL_HANDLE;
    VulkanAllocation m_ShaderBindingTableAllocation{};
    VkDeviceAddress m_ShaderBindingTableDeviceAddress = 0;

    void CreatePipeline() override;
    // 原生管线创建成功后，查询 group handles 并生成四类 SBT region。
    void CreateShaderBindingTable(uint32_t shaderGroupCount);
};

using VulkanPipelineSP = std::shared_ptr<VulkanPipelineBase>;
using VulkanGraphicsPipelineStateSP = std::shared_ptr<VulkanGraphicsPipelineState>;
using VulkanComputePipelineStateSP = std::shared_ptr<VulkanComputePipelineState>;
using VulkanRayTracingPipelineSP = std::shared_ptr<VulkanRayTracingPipeline>;

}
