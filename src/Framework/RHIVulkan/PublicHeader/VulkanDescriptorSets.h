#pragma once
#include "VulkanDevice.h"
#include "VulkanFuncWrapper.h"
#include <vector>
#include <stdexcept>
#include <map>
#include <array>
#include <set>
#include <list>
#include <unordered_map>

namespace RHIVulkan {
    static constexpr uint32_t VK_DESCRIPTOR_TYPE_POOL_INDEX_COUNT = 12u;
    static constexpr uint32_t VK_DESCRIPTOR_TYPE_POOL_INVALID_INDEX = 0xFFFFFFFFu;

    inline uint32_t GetDescriptorTypePoolIndex(VkDescriptorType type)
    {
        switch (type)
        {
        case VK_DESCRIPTOR_TYPE_SAMPLER:
            return 0u;
        case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
            return 1u;
        case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
            return 2u;
        case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
            return 3u;
        case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
            return 4u;
        case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
            return 5u;
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
            return 6u;
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
            return 7u;
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
            return 8u;
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
            return 9u;
        case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
            return 10u;
        case VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR:
            return 11u;
        default:
            return VK_DESCRIPTOR_TYPE_POOL_INVALID_INDEX;
        }
    }

    inline VkDescriptorType GetDescriptorTypeByPoolIndex(uint32_t index)
    {
        static const std::array<VkDescriptorType, VK_DESCRIPTOR_TYPE_POOL_INDEX_COUNT> descriptorTypePoolIndexMap = {
            VK_DESCRIPTOR_TYPE_SAMPLER,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,
            VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC,
            VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
            VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
        };

        return index < descriptorTypePoolIndexMap.size() ? descriptorTypePoolIndexMap[index] : VK_DESCRIPTOR_TYPE_MAX_ENUM;
    }
    class VulkanCommandBuffer;
    //------------------------------------------------------------
    // Descriptor Set Binding / Layout
    //------------------------------------------------------------
    struct DescriptorSetBinding
    {
        uint32_t Binding = 0;
        VkDescriptorType Type = VK_DESCRIPTOR_TYPE_MAX_ENUM;
        uint32_t Count = 1;
        VkShaderStageFlags StageFlags = 0;
    };


    //------------------------------------------------------------
    // Layout 描述信息（Hash + Bindings）
    //------------------------------------------------------------
    struct DescriptorSetLayoutInfo
    {
        std::vector<DescriptorSetBinding> Bindings;
        static uint64_t CalculateHash(const DescriptorSetLayoutInfo& info);
        void AddBinding(uint32_t binding, VkDescriptorType type, uint32_t count, VkShaderStageFlags stageFlags);
    };

    //------------------------------------------------------------
    // Layout 句柄 + Info
    //------------------------------------------------------------
    struct DescriptorSetLayoutHandle
    {
        VkDescriptorSetLayout Layout = VK_NULL_HANDLE;
        DescriptorSetLayoutInfo Info;
    };

    //------------------------------------------------------------
    // Vulkan Descriptor Pool 封装
    //------------------------------------------------------------
    class VulkanDescriptorPool
    {
    public:
        VulkanDescriptorPool(VulkanDevice* device, uint32_t maxSets, const std::array<uint32_t, VK_DESCRIPTOR_TYPE_POOL_INDEX_COUNT>& poolSizes);
        ~VulkanDescriptorPool();

        bool AllocateDescriptorSet(VkDescriptorSetLayout layout, VkDescriptorSet& outSet);
        void Reset();

    private:
        void CreatePool();

    private:
        friend class TypedDescriptorPool;
		friend class VulkanDescriptorSetManager;
        VulkanDevice* m_Device;
        uint32_t m_MaxDescriptorSets;
        std::array<float, VK_DESCRIPTOR_TYPE_POOL_INDEX_COUNT> m_PoolSizes{};
        VkDescriptorPool m_Pool = VK_NULL_HANDLE;
        uint32_t m_AllocatedCount = 0;
    };

    class TypedDescriptorPool
    {
    public:
        TypedDescriptorPool(
            VulkanDevice* device,
            VkDescriptorSetLayout layout,
            const DescriptorSetLayoutInfo& layoutInfo,
            uint32_t maxSetsPerPool);
        ~TypedDescriptorPool();

        VkDescriptorSet Allocate();
    private:
        VulkanDevice* m_Device;
        VkDescriptorSetLayout m_Layout;
        DescriptorSetLayoutInfo m_LayoutInfo;
        uint32_t m_MaxSetsPerPool;
        friend class VulkanDescriptorSetManager;
        std::vector<std::unique_ptr<VulkanDescriptorPool>> m_Pools;
    };
 

    //------------------------------------------------------------
    // Layout Cache 管理 VkDescriptorSetLayout
    //------------------------------------------------------------
    class VulkanDescriptorSetLayoutManager
    {
    public:
        VulkanDescriptorSetLayoutManager(VulkanDevice* device);
        ~VulkanDescriptorSetLayoutManager();
        VkDescriptorSetLayout GetOrCreateLayout(const DescriptorSetLayoutInfo& info);

    private:
        VulkanDevice* m_Device;
        std::unordered_map<uint64_t, VkDescriptorSetLayout> m_LayoutMap;
    };



    //------------------------------------------------------------
    // 全局 DescriptorSet Cache 管理器
    //------------------------------------------------------------
    class VulkanDescriptorSetManager
    {
    public:
        VulkanDescriptorSetManager(VulkanDevice* device, VulkanDescriptorSetLayoutManager* layoutManager);
        ~VulkanDescriptorSetManager();

        VkDescriptorSet GetDescriptorSet(const DescriptorSetLayoutInfo& layoutInfo);
        void BindDescriptorSets(VulkanCommandBuffer* cmdBuffer, VkPipelineLayout layout, VkPipelineBindPoint pipelineBindingPoint, const std::vector<VkDescriptorSet>& descriptorSets, const std::vector<uint32_t>& dynamicOffsets = {});
		void GarbageCollect();
        
    private:
        struct PendingDescriptorSetsInfo {
            std::vector<VkDescriptorSet> PendingSets;
            std::vector<uint32_t> PendingDynamicOffsets;
            VulkanCommandBuffer* CmdBuffer = nullptr;
        };
        struct AllocatedSets {
            std::set<VkDescriptorSet> InUseSets;
            std::set<VkDescriptorSet> FreeSets;
        };

        VulkanDevice* m_Device;
        VulkanDescriptorSetLayoutManager* m_LayoutCache;
        std::unordered_map<uint64_t, AllocatedSets> m_LayoutAllocatedSetsMap;
        std::unordered_map<VkDescriptorSet, uint64_t> m_DescriptorSetLayoutHashMap;

        std::list<PendingDescriptorSetsInfo> m_PendingFreeSetInfos;
        std::unordered_map<uint64_t, std::unique_ptr<TypedDescriptorPool>> m_LayoutDescriptorPoolMap;
    };

    class VulkanDescriptorWriter
    {
    public:
        void Reset()
        {
            m_Writes.clear();
            m_ImageInfos.clear();
            m_BufferInfos.clear();
            m_BufferViews.clear();
            m_AccelerationStructureInfos.clear();
            m_AccelerationStructures.clear();
            m_bDirty = false;
        }

        void WriteImage(VkDescriptorSet set,
            uint32_t binding,
            uint32_t element,
            VkDescriptorType type,
            VkImageView view,
            VkImageLayout layout,
            VkSampler sampler = VK_NULL_HANDLE)
        {
            VkDescriptorImageInfo& info = m_ImageInfos.emplace_back();
            info.imageView = view;
            info.imageLayout = layout;
            info.sampler = sampler;

            VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            write.dstSet = set;
            write.dstBinding = binding;
            write.dstArrayElement = element;
            write.descriptorType = type;
            write.descriptorCount = 1;
            write.pImageInfo = &info;

            m_Writes.push_back(write);
            m_bDirty = true;
        }

        void WriteBuffer(VkDescriptorSet set,
            uint32_t binding,
            uint32_t element,
            VkDescriptorType type,
            VkBuffer buffer,
            VkDeviceSize offset,
            VkDeviceSize range)
        {
            VkDescriptorBufferInfo& info = m_BufferInfos.emplace_back();
            info.buffer = buffer;
            info.offset = offset;
            info.range = range;

            VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            write.dstSet = set;
            write.dstBinding = binding;
            write.dstArrayElement = element;
            write.descriptorType = type;
            write.descriptorCount = 1;
            write.pBufferInfo = &info;

            m_Writes.push_back(write);
            m_bDirty = true;
        }
        void WriteTexelBuffer(VkDescriptorSet set,uint32_t binding, uint32_t element, VkDescriptorType type, VkBufferView view)
        {
            VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            write.dstSet = set;
            write.dstBinding = binding;
            write.dstArrayElement = element;
            write.descriptorType = type;
            write.descriptorCount = 1;
            m_BufferViews.push_back(view);
            write.pTexelBufferView = &m_BufferViews.back();
            m_Writes.push_back(write);
            m_bDirty = true;
        }

        void WriteAccelerationStructure(VkDescriptorSet set, uint32_t binding, uint32_t element, VkDescriptorType type, VkAccelerationStructureKHR accelerationStructure)
        {
            VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            write.dstSet = set;
            write.dstBinding = binding;
            write.dstArrayElement = element;
            write.descriptorType = type;
            write.descriptorCount = 1;

            auto& structure = m_AccelerationStructures.emplace_back(accelerationStructure);
            auto& info = m_AccelerationStructureInfos.emplace_back();
            info.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
            info.accelerationStructureCount = 1;
            info.pAccelerationStructures = &structure;
            write.pNext = &info;

            m_Writes.push_back(write);
            m_bDirty = true;
        }

        void Update(VkDevice device)
        {

            VKFunc::UpdateDescriptorSets(device,
                (uint32_t)m_Writes.size(),
                m_Writes.data(),
                0, nullptr);

            m_bDirty = false;
        }

        bool IsDirty() const { return m_bDirty; }

    private:
        std::vector<VkWriteDescriptorSet> m_Writes;
        std::deque<VkDescriptorImageInfo> m_ImageInfos;
        std::deque<VkDescriptorBufferInfo> m_BufferInfos;
        std::deque<VkBufferView> m_BufferViews;
        std::deque<VkWriteDescriptorSetAccelerationStructureKHR> m_AccelerationStructureInfos;
        std::deque<VkAccelerationStructureKHR> m_AccelerationStructures;
        bool m_bDirty = false;
    };


} // namespace WR::RHIVulkan
