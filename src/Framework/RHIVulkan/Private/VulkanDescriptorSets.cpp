#include "VulkanDescriptorSets.h"
#include "VulkanCommandBuffer.h"
#include "VulkanFuncWrapper.h"
#include <stdexcept>
namespace RHIVulkan {
        //------------------------------------------------------------
        // DescriptorSetLayoutInfo
        //------------------------------------------------------------
    uint64_t DescriptorSetLayoutInfo::CalculateHash(const DescriptorSetLayoutInfo& info)
    {
        uint64_t hash = 0;
        uint64_t h = 14695981039346656037ull;
        for (auto& b : info.Bindings)
        {
            h ^= b.Binding; h *= 1099511628211ull;
            h ^= static_cast<uint64_t>(b.Type); h *= 1099511628211ull;
            h ^= b.Count; h *= 1099511628211ull;
            h ^= b.StageFlags; h *= 1099511628211ull;
        }
        hash = h;
        return hash;
    }

    void DescriptorSetLayoutInfo::AddBinding(uint32_t binding, VkDescriptorType type, uint32_t count, VkShaderStageFlags stageFlags)
    {
        Bindings.push_back({ binding, type, count, stageFlags });
    }

    //------------------------------------------------------------
    // VulkanDescriptorPool
    //------------------------------------------------------------
    VulkanDescriptorPool::VulkanDescriptorPool(VulkanDevice* device, uint32_t maxSets, const std::array<uint32_t, VK_DESCRIPTOR_TYPE_POOL_INDEX_COUNT>& poolSizes)
        : m_Device(device), m_MaxDescriptorSets(maxSets)
    {
        for (size_t i = 0; i < m_PoolSizes.size(); ++i)
            m_PoolSizes[i] = static_cast<float>(poolSizes[i]);

        CreatePool();
    }

    VulkanDescriptorPool::~VulkanDescriptorPool()
    {
        if (m_Pool != VK_NULL_HANDLE)
        {
            VKFunc::DestroyDescriptorPool(m_Device->GetHandle(), m_Pool);
            m_Pool = VK_NULL_HANDLE;
        }
    }

    void VulkanDescriptorPool::CreatePool()
    {
        std::vector<VkDescriptorPoolSize> sizes;
        for (uint32_t i = 0; i < m_PoolSizes.size(); ++i)
        {
            if (m_PoolSizes[i] > 0)
            {
                VkDescriptorPoolSize s{};
                s.type = GetDescriptorTypeByPoolIndex(i);
                s.descriptorCount = static_cast<uint32_t>(m_PoolSizes[i]);
                sizes.push_back(s);
            }
        }

        VkDescriptorPoolCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        info.poolSizeCount = static_cast<uint32_t>(sizes.size());
        info.pPoolSizes = sizes.data();
        info.maxSets = m_MaxDescriptorSets;
        info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

        if (!VKFunc::CreateDescriptorPool(m_Device->GetHandle(), &info, &m_Pool))
        {
            throw std::runtime_error("Failed to create VulkanDescriptorPool");
        }
        m_AllocatedCount = 0;
    }

    bool VulkanDescriptorPool::AllocateDescriptorSet(VkDescriptorSetLayout layout, VkDescriptorSet& outSet)
    {
        if (m_AllocatedCount >= m_MaxDescriptorSets)
            return false;

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_Pool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &layout;
        if (VKFunc::AllocateDescriptorSets(m_Device->GetHandle(), &allocInfo, &outSet))
        {
            ++m_AllocatedCount;
            return true;
        }
        return false;
    }

    void VulkanDescriptorPool::Reset()
    {
        if (m_Pool != VK_NULL_HANDLE)
        {
            VKFunc::ResetDescriptorPool(m_Device->GetHandle(), m_Pool);
            m_AllocatedCount = 0;
        }
    }

    //------------------------------------------------------------
    // TypedDescriptorPool
    //------------------------------------------------------------
    TypedDescriptorPool::TypedDescriptorPool(VulkanDevice* device, VkDescriptorSetLayout layout, const DescriptorSetLayoutInfo& layoutInfo, uint32_t maxSetsPerPool)
        : m_Device(device), m_Layout(layout), m_LayoutInfo(layoutInfo), m_MaxSetsPerPool(maxSetsPerPool)
    {
    }

    TypedDescriptorPool::~TypedDescriptorPool()
    {
		m_Pools.clear();
    }

    VkDescriptorSet TypedDescriptorPool::Allocate()
    {
        for (auto& pool : m_Pools)
        {
            VkDescriptorSet set = VK_NULL_HANDLE;
            if (pool->AllocateDescriptorSet(m_Layout, set))
                return set;
        }

        // 没有可用 pool，创建新的
        std::array<uint32_t, VK_DESCRIPTOR_TYPE_POOL_INDEX_COUNT> poolSizes{};
        for (auto& b : m_LayoutInfo.Bindings)
        {
            const uint32_t poolIndex = GetDescriptorTypePoolIndex(b.Type);
            if (poolIndex != VK_DESCRIPTOR_TYPE_POOL_INVALID_INDEX)
            {
                poolSizes[poolIndex] += b.Count;
            }
        }

        m_Pools.push_back(std::make_unique<VulkanDescriptorPool>(m_Device, m_MaxSetsPerPool, poolSizes));

        VkDescriptorSet set = VK_NULL_HANDLE;
        if (!m_Pools.back()->AllocateDescriptorSet(m_Layout, set))
            throw std::runtime_error("TypedDescriptorPool failed to allocate descriptor set");

        return set;
    }

    //------------------------------------------------------------
    // DescriptorSetLayoutManager
    //------------------------------------------------------------
    VulkanDescriptorSetLayoutManager::VulkanDescriptorSetLayoutManager(VulkanDevice* device)
        : m_Device(device)
    {
    }

    VulkanDescriptorSetLayoutManager::~VulkanDescriptorSetLayoutManager()
    {
        for (auto& pair : m_LayoutMap) {
            VKFunc::DestroyDescriptorSetLayout(m_Device->GetHandle(), pair.second);
        }
        m_LayoutMap.clear();
    }

    VkDescriptorSetLayout VulkanDescriptorSetLayoutManager::GetOrCreateLayout(const DescriptorSetLayoutInfo& info)
    {
		
        uint64_t hash = DescriptorSetLayoutInfo::CalculateHash(info);
        auto it = m_LayoutMap.find(hash);
        if (it != m_LayoutMap.end())
            return it->second;

        std::vector<VkDescriptorSetLayoutBinding> bindings;
        for (auto& b : info.Bindings)
        {
            VkDescriptorSetLayoutBinding binding{};
            binding.binding = b.Binding;
            binding.descriptorType = b.Type;
            binding.descriptorCount = b.Count;
            binding.stageFlags = b.StageFlags;
            binding.pImmutableSamplers = nullptr;
            bindings.push_back(binding);
        }

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        VkDescriptorSetLayout layout;
        if (!VKFunc::CreateDescriptorSetLayout(m_Device->GetHandle(), &layoutInfo, &layout))
            throw std::runtime_error("Failed to create VkDescriptorSetLayout");

        m_LayoutMap[hash] = layout;
        return layout;
    }

    //------------------------------------------------------------
    // DescriptorSetManager
    //------------------------------------------------------------
    VulkanDescriptorSetManager::VulkanDescriptorSetManager(VulkanDevice* device, VulkanDescriptorSetLayoutManager* layoutManager)
        : m_Device(device), m_LayoutCache(layoutManager)
    {
    }

    VulkanDescriptorSetManager::~VulkanDescriptorSetManager()
    {
		m_LayoutDescriptorPoolMap.clear();
    }

    VkDescriptorSet VulkanDescriptorSetManager::GetDescriptorSet(const DescriptorSetLayoutInfo& layoutInfo)
    {
        uint64_t hash = DescriptorSetLayoutInfo::CalculateHash(layoutInfo);

        // 确保 pool 存在
        auto it = m_LayoutDescriptorPoolMap.find(hash);
        if (it == m_LayoutDescriptorPoolMap.end())
        {
            VkDescriptorSetLayout layout = m_LayoutCache->GetOrCreateLayout(layoutInfo);

            m_LayoutDescriptorPoolMap[hash] =
                std::make_unique<TypedDescriptorPool>(m_Device, layout, layoutInfo, 256);

            it = m_LayoutDescriptorPoolMap.find(hash);
        }

        VkDescriptorSet set = VK_NULL_HANDLE;

        auto& allocated = m_LayoutAllocatedSetsMap[hash];

        // 优先复用 FreeSets
        if (!allocated.FreeSets.empty())
        {
            auto freeIt = allocated.FreeSets.begin();
            set = *freeIt;
            allocated.FreeSets.erase(freeIt);
        }
        else
        {
            set = it->second->Allocate();
        }

        allocated.InUseSets.insert(set);

        m_DescriptorSetLayoutHashMap[set] = hash;

        return set;
    }

    void VulkanDescriptorSetManager::BindDescriptorSets(VulkanCommandBuffer* cmdBuffer,VkPipelineLayout layout,VkPipelineBindPoint pipelineBindingPoint, const std::vector<VkDescriptorSet>& descriptorSets,const std::vector<uint32_t>& dynamicOffsets)
    {
        if (descriptorSets.empty())
            return;

        PendingDescriptorSetsInfo info;
        info.CmdBuffer = cmdBuffer;
        info.PendingSets = descriptorSets;
        m_PendingFreeSetInfos.push_back(std::move(info));
        VKFunc::CmdBindDescriptorSets(
            cmdBuffer->GetHandle(),
            pipelineBindingPoint,
            layout,
            0,
            (uint32_t)descriptorSets.size(),
            descriptorSets.data(),
            dynamicOffsets.size(),
            dynamicOffsets.data());
    }

    void VulkanDescriptorSetManager::GarbageCollect()
    {
        auto it = m_PendingFreeSetInfos.begin();

        while (it != m_PendingFreeSetInfos.end())
        {
            VulkanCommandBuffer* cmdBuffer = it->CmdBuffer;

            if (cmdBuffer->GetState() != VulkanCommandBuffer::NeedRecycle)
            {
                ++it;
                continue;
            }

            for (VkDescriptorSet set : it->PendingSets)
            {
                auto hashIt = m_DescriptorSetLayoutHashMap.find(set);
                if (hashIt == m_DescriptorSetLayoutHashMap.end())
                    continue;

                uint64_t hash = hashIt->second;

                auto allocIt = m_LayoutAllocatedSetsMap.find(hash);
                if (allocIt == m_LayoutAllocatedSetsMap.end())
                    continue;

                auto& allocated = allocIt->second;

                auto inUseIt = allocated.InUseSets.find(set);
                if (inUseIt != allocated.InUseSets.end())
                {
                    allocated.InUseSets.erase(inUseIt);
                    allocated.FreeSets.insert(set);
                }
            }

            it = m_PendingFreeSetInfos.erase(it);
        }

    }

} // namespace WR::RHIVulkan
