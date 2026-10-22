
#pragma once

#include "RenderResource.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Engine
{
    class InstanceIdBufferDefferedAccessor
    {
    public:
        virtual ~InstanceIdBufferDefferedAccessor() = default;
        virtual RenderCore::RenderBuffer* GetInstanceIdBuffer() const = 0;
    };

    template<typename InstanceDataType, typename InstanceIdType = uint32_t>
    class InstanceDataManager;

    template<typename InstanceDataType, typename InstanceIdType = uint32_t>
    class InstanceDataBlock : public InstanceIdBufferDefferedAccessor
    {
    public:
        using ManagerType = InstanceDataManager<InstanceDataType, InstanceIdType>;

        uint32_t GetBlockId() const { return m_BlockId; }
        const std::string& GetDebugName() const { return m_DebugName; }

        uint32_t AddSubParameters(const std::vector<InstanceDataType>& inSubParameters);
        uint32_t AddInstanceIds(const std::vector<InstanceIdType>& inInstanceIds);
        bool UpdateSubParameters(uint32_t logicalOffset, const std::vector<InstanceDataType>& inSubParameters);

        uint32_t GetSubParameterCount() const { return static_cast<uint32_t>(m_SubParameters.size()); }
        uint32_t GetInstanceIdCount() const { return static_cast<uint32_t>(m_InstanceIds.size()); }
        RenderCore::RenderBuffer* GetInstanceIdBuffer() const override;
        RHI::RHIShaderResourceView* GetInstanceDataSRV() const;

    private:
        friend class InstanceDataManager<InstanceDataType, InstanceIdType>;

        InstanceDataBlock(ManagerType* inOwner, uint32_t inBlockId, std::string inDebugName);
        void UpdateGPUResources();
        void ClearGPUResources();

        ManagerType* m_Owner = nullptr;
        uint32_t m_BlockId = 0;
        std::string m_DebugName;
        std::vector<InstanceDataType> m_SubParameters;
        std::vector<InstanceIdType> m_InstanceIds;
        std::shared_ptr<RenderCore::RenderBuffer> m_InstanceDataBuffer;
        std::shared_ptr<RenderCore::RenderBuffer> m_InstanceIdBuffer;
        bool m_bSubParametersDirty = false;
        bool m_bInstanceIdsDirty = false;
    };

    template<typename InstanceDataType, typename InstanceIdType>
    class InstanceDataManager
    {
    public:
        using BlockType = InstanceDataBlock<InstanceDataType, InstanceIdType>;
        using BlockRef = std::shared_ptr<BlockType>;

        static InstanceDataManager& Get();

        BlockRef CreateInstanceBlock(const std::string& debugName = std::string());
        void RemoveInstanceBlock(uint32_t blockId);
        void RemoveInstanceBlock(const BlockRef& block);

        void BeginUpdateGPUResources();
        void UpdateGPUResources();

        RenderCore::RenderBuffer* GetInstanceIdBuffer(const BlockRef& block) const;

    private:
        friend class InstanceDataBlock<InstanceDataType, InstanceIdType>;

        mutable std::mutex m_Mutex;
        uint32_t m_NextBlockId = 1;
        std::unordered_map<uint32_t, BlockRef> m_Blocks;
    };
}

#include "InstanceDataMgr.inl"