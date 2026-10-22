namespace Engine
{
    namespace InstanceDataMgrDetail
    {
        inline RHI::RHIBufferDesc BuildStructuredBufferDesc(uint64_t sizeInBytes, uint32_t stride, const char* debugName)
        {
            RHI::RHIBufferDesc desc;
            desc.Size = std::max<uint64_t>(static_cast<uint64_t>(stride), sizeInBytes);
            desc.Stride = stride;
            desc.InitialQueueType = RHI::EQueueType::Graphics;
            desc.Usage =
                RHI::ERHIBufferUsageFlag::Structured |
                RHI::ERHIBufferUsageFlag::ShaderResource |
                RHI::ERHIBufferUsageFlag::Vertex |
                RHI::ERHIBufferUsageFlag::TransferDst;
            desc.DebugName = debugName;
            return desc;
        }
    }

    template<typename InstanceDataType, typename InstanceIdType>
    InstanceDataBlock<InstanceDataType, InstanceIdType>::InstanceDataBlock(
        ManagerType* inOwner,
        uint32_t inBlockId,
        std::string inDebugName)
        : m_Owner(inOwner)
        , m_BlockId(inBlockId)
        , m_DebugName(std::move(inDebugName))
    {
    }

    template<typename InstanceDataType, typename InstanceIdType>
    uint32_t InstanceDataBlock<InstanceDataType, InstanceIdType>::AddSubParameters(
        const std::vector<InstanceDataType>& inSubParameters)
    {
        if (!m_Owner || inSubParameters.empty())
        {
            return 0;
        }

        std::lock_guard<std::mutex> lock(m_Owner->m_Mutex);
        const uint32_t logicalOffset = static_cast<uint32_t>(m_SubParameters.size());
        m_SubParameters.insert(m_SubParameters.end(), inSubParameters.begin(), inSubParameters.end());
        m_bSubParametersDirty = true;
        return logicalOffset;
    }

    template<typename InstanceDataType, typename InstanceIdType>
    uint32_t InstanceDataBlock<InstanceDataType, InstanceIdType>::AddInstanceIds(
        const std::vector<InstanceIdType>& inInstanceIds)
    {
        if (!m_Owner || inInstanceIds.empty())
        {
            return 0;
        }

        std::lock_guard<std::mutex> lock(m_Owner->m_Mutex);
        const uint32_t logicalOffset = static_cast<uint32_t>(m_InstanceIds.size());
        m_InstanceIds.insert(m_InstanceIds.end(), inInstanceIds.begin(), inInstanceIds.end());
        m_bInstanceIdsDirty = true;
        return logicalOffset;
    }

    template<typename InstanceDataType, typename InstanceIdType>
    bool InstanceDataBlock<InstanceDataType, InstanceIdType>::UpdateSubParameters(
        uint32_t logicalOffset,
        const std::vector<InstanceDataType>& inSubParameters)
    {
        if (!m_Owner || inSubParameters.empty())
        {
            return false;
        }

        std::lock_guard<std::mutex> lock(m_Owner->m_Mutex);
        const uint32_t count = static_cast<uint32_t>(inSubParameters.size());
        const uint32_t requiredSize = logicalOffset + count;
        if (requiredSize > static_cast<uint32_t>(m_SubParameters.size()))
        {
            m_SubParameters.resize(requiredSize);
        }

        std::copy(inSubParameters.begin(), inSubParameters.end(), m_SubParameters.begin() + logicalOffset);
        m_bSubParametersDirty = true;
        return true;
    }

    template<typename InstanceDataType, typename InstanceIdType>
    void InstanceDataBlock<InstanceDataType, InstanceIdType>::UpdateGPUResources()
    {
        if (m_bSubParametersDirty)
        {
            if (m_SubParameters.empty())
            {
                m_InstanceDataBuffer.reset();
            }
            else
            {
                const uint64_t sizeInBytes = static_cast<uint64_t>(m_SubParameters.size()) * sizeof(InstanceDataType);
                if (!m_InstanceDataBuffer || m_InstanceDataBuffer->GetRHI()->GetDesc().Size < sizeInBytes)
                {
                    m_InstanceDataBuffer = std::make_shared<RenderCore::RenderBuffer>(
                        InstanceDataMgrDetail::BuildStructuredBufferDesc(
                            sizeInBytes,
                            sizeof(InstanceDataType),
                            "InstanceDataBuffer"));
                    m_InstanceDataBuffer->InitRHIResource();
                }

                m_InstanceDataBuffer->UploadData(m_SubParameters.data(), static_cast<uint32_t>(sizeInBytes));
                RenderCore::TransitionBufferImmediate(
                    RHI::GRHIApi,
                    m_InstanceDataBuffer.get(),
                    RHI::ERHIResourceAccess::ShadingRateSource,
                    RHI::EQueueType::Graphics);
            }

            m_bSubParametersDirty = false;
        }

        if (m_bInstanceIdsDirty)
        {
            if (m_InstanceIds.empty())
            {
                m_InstanceIdBuffer.reset();
            }
            else
            {
                const uint64_t sizeInBytes = static_cast<uint64_t>(m_InstanceIds.size()) * sizeof(InstanceIdType);
                if (!m_InstanceIdBuffer || m_InstanceIdBuffer->GetRHI()->GetDesc().Size < sizeInBytes)
                {
                    m_InstanceIdBuffer = std::make_shared<RenderCore::RenderBuffer>(
                        InstanceDataMgrDetail::BuildStructuredBufferDesc(
                            sizeInBytes,
                            sizeof(InstanceIdType),
                            "InstanceIdBuffer"));
                    m_InstanceIdBuffer->InitRHIResource();
                }

                m_InstanceIdBuffer->UploadData(m_InstanceIds.data(), static_cast<uint32_t>(sizeInBytes));
                RenderCore::TransitionBufferImmediate(
                    RHI::GRHIApi,
                    m_InstanceIdBuffer.get(),
                    RHI::ERHIResourceAccess::VertexOrIndexBuffer,
                    RHI::EQueueType::Graphics);
            }

            m_bInstanceIdsDirty = false;
        }
    }

    template<typename InstanceDataType, typename InstanceIdType>
    void InstanceDataBlock<InstanceDataType, InstanceIdType>::ClearGPUResources()
    {
        m_SubParameters.clear();
        m_InstanceIds.clear();
        m_InstanceDataBuffer.reset();
        m_InstanceIdBuffer.reset();
        m_bSubParametersDirty = false;
        m_bInstanceIdsDirty = false;
    }

    template<typename InstanceDataType, typename InstanceIdType>
    RenderCore::RenderBuffer* InstanceDataBlock<InstanceDataType, InstanceIdType>::GetInstanceIdBuffer() const
    {
        return m_InstanceIdBuffer.get();
    }

    template<typename InstanceDataType, typename InstanceIdType>
    RHI::RHIShaderResourceView* InstanceDataBlock<InstanceDataType, InstanceIdType>::GetInstanceDataSRV() const
    {
        RHI::RHIBufferSRVCreateInfo createInfo;
        createInfo.Format = RHI::ERHIFormat::Unknown;
        createInfo.NumElements = static_cast<uint32_t>(m_SubParameters.size());
        createInfo.Stride = sizeof(InstanceDataType);
        return m_InstanceDataBuffer
            ? m_InstanceDataBuffer->GetViewCache().GetOrCreateSRV(m_InstanceDataBuffer->GetRHI(), createInfo)
            : nullptr;
    }

    template<typename InstanceDataType, typename InstanceIdType>
    InstanceDataManager<InstanceDataType, InstanceIdType>&
        InstanceDataManager<InstanceDataType, InstanceIdType>::Get()
    {
        static InstanceDataManager instance;
        return instance;
    }

    template<typename InstanceDataType, typename InstanceIdType>
    typename InstanceDataManager<InstanceDataType, InstanceIdType>::BlockRef
        InstanceDataManager<InstanceDataType, InstanceIdType>::CreateInstanceBlock(const std::string& debugName)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        const uint32_t blockId = m_NextBlockId++;
        BlockRef block(new BlockType(this, blockId, debugName));
        m_Blocks.emplace(blockId, block);
        return block;
    }

    template<typename InstanceDataType, typename InstanceIdType>
    void InstanceDataManager<InstanceDataType, InstanceIdType>::RemoveInstanceBlock(uint32_t blockId)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        auto it = m_Blocks.find(blockId);
        if (it == m_Blocks.end())
        {
            return;
        }

        it->second->ClearGPUResources();
        m_Blocks.erase(it);
    }

    template<typename InstanceDataType, typename InstanceIdType>
    void InstanceDataManager<InstanceDataType, InstanceIdType>::RemoveInstanceBlock(const BlockRef& block)
    {
        if (!block)
        {
            return;
        }

        RemoveInstanceBlock(block->GetBlockId());
    }

    template<typename InstanceDataType, typename InstanceIdType>
    void InstanceDataManager<InstanceDataType, InstanceIdType>::BeginUpdateGPUResources()
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        for (auto& pair : m_Blocks)
        {
            if (!pair.second)
            {
                continue;
            }

            pair.second->m_InstanceIds.clear();
            pair.second->m_bInstanceIdsDirty = true;
        }
    }

    template<typename InstanceDataType, typename InstanceIdType>
    void InstanceDataManager<InstanceDataType, InstanceIdType>::UpdateGPUResources()
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        for (auto& pair : m_Blocks)
        {
            if (pair.second)
            {
                pair.second->UpdateGPUResources();
            }
        }
    }

    template<typename InstanceDataType, typename InstanceIdType>
    RenderCore::RenderBuffer* InstanceDataManager<InstanceDataType, InstanceIdType>::GetInstanceIdBuffer(const BlockRef& block) const
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return block ? block->GetInstanceIdBuffer() : nullptr;
    }
}