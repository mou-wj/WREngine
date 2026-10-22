#include "GlobalDistanceField.h"

#include "GlobalShader.h"
#include "RHIPipelineStateCache.h"
#include <cmath>
using namespace RHI;

namespace Renderer
{


    BEGIN_SHADER_PARAMETER_STRUCT(DistanceFieldMergeParameters)
        SHADER_PARAMETER_RHI_STRUCTURED_BUFFER(DistanceFieldSourceParameters,Source)
        SHADER_PARAMETER_RHI_STRUCTURED_BUFFER(DistanceFieldOutParameters, Output)
        SHADER_PARAMETER(uint32_t, SourceCount)
        SHADER_PARAMETER(uint32_t, OutputCount)
        SHADER_PARAMETER(Core::UInt3, OutputResolution)
        SHADER_PARAMETER(Core::UInt3, InputAtlasResolution)
        
        SHADER_PARAMETER_RHI_TEXTURE(Texture3D<float>, InputSDFTexture)
        SHADER_PARAMETER_RHI_UAV(RWTexture3D<float>, OutputSDFTexture)
        SHADER_PARAMETER_SAMPLER(InputSDFSampler)
    END_SHADER_PARAMETER_STRUCT(DistanceFieldMergeParameters)

    class DistanceFieldMergeCS : public RenderCore::GlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER_TYPE(DistanceFieldMergeCS);

        static bool ShouldCompilePermutation(const RenderCore::ShaderPermutationParameters& parameters)
        {
            return true;
        }

        static void ModifyShaderCompilerEnvironment(const RenderCore::ShaderPermutationParameters& parameters, RenderCore::ShaderCompilerEnvironment& outEnvironment)
        {
        }

        static const RenderCore::ShaderParametersMetadata* GetShaderParameterMetadata()
        {
            return DistanceFieldMergeParameters::GetMetaData();
        }
    };
    IMPLEMENT_GLOBAL_SHADER_TYPE(
        DistanceFieldMergeCS,
        "DistanceFieldMergeCS",
        "/tools/DistanceFieldMergeCS.sf",
        "MainCS",
        RHI::ERHIShaderFrequency::Compute
    );


    bool ExecuteDistanceFieldMergePass(const DistanceFieldMergePassInput& input)
    {
        
        if (!input.InputSDFTexture || !input.OutputSDFTexture)
        {
            return false;
        }


        auto* shader = RenderCore::GShaderMap.GetShader<DistanceFieldMergeCS>(0);
        if (!shader)
        {
            return false;
        }

        auto* computeShader = dynamic_cast<RHI::RHIComputeShader*>(shader->GetRHIShader());
        if (!computeShader)
        {
            return false;
        }

        const auto& inputDesc = input.InputSDFTexture->GetRHI()->GetDesc();
        const auto& outputDesc = input.OutputSDFTexture->GetRHI()->GetDesc();

        RHI::RHITexSRVCreateInfo inputSRVDesc;
        inputSRVDesc.Format = inputDesc.Format;
        inputSRVDesc.ArraySize = inputDesc.ArraySize;

        RHI::RHITexUAVCreateInfo outputUAVDesc;
        outputUAVDesc.Format = outputDesc.Format;
        outputUAVDesc.ArraySize = outputDesc.ArraySize;

        auto* inputSRV = input.InputSDFTexture->GetViewCache().GetOrCreateSRV(input.InputSDFTexture->GetRHI(), inputSRVDesc);
        auto* outputUAV = input.OutputSDFTexture->GetViewCache().GetOrCreateUAV(input.OutputSDFTexture->GetRHI(), outputUAVDesc);

        if (!inputSRV || !outputUAV)
        {
            return false;
        }

        RenderCore::TransitionTextureImmediate(
            RHI::GRHIApi,
            input.InputSDFTexture,
            RHI::ERHIResourceAccess::SRV,
            RHI::EQueueType::Compute);

        RenderCore::TransitionTextureImmediate(
            RHI::GRHIApi,
            input.OutputSDFTexture,
            RHI::ERHIResourceAccess::UAV,
            RHI::EQueueType::Compute);
        
        auto* computeQueue = RHI::GRHIApi->GetQueue(RHI::EQueueType::Compute);
        auto* computeContext = computeQueue->AcquireCommandContext();
        auto* computeContextCasted = dynamic_cast<RHI::RHIComputeContex*>(computeContext);
        if (!computeContextCasted)
        {
            return false;
        }

        RHI::RHIComputeCommandList cmd(computeContextCasted);
        cmd.SetImmediate(true);
        cmd.Begin();





        RHI::RHIComputePipelineStateDesc computeDesc;
        computeDesc.ComputeShader = computeShader;
        auto* pipelineState = RHI::RHIPipelineStateCache::GetOrCreateComputePipelineState(computeDesc);
        cmd.SetComputePipelineState(pipelineState);




        DistanceFieldMergeParameters params;
        RenderCore::TransientBufferDesc bufferDesc;
        bufferDesc.Stride = sizeof(DistanceFieldSourceParameters);
        bufferDesc.Size = input.SourceParams.size() * bufferDesc.Stride;
        bufferDesc.Usage = RHI::ERHIBufferUsageFlag::ShaderResource | RHI::ERHIBufferUsageFlag::TransferDst;
        auto buffer = RenderCore::GTransientResourceAllocator.AllocateBuffer(bufferDesc, 0, 1);

		RenderCore::TransitionResource(GRHIApi, cmd, buffer->GetRHI(), RHI::ERHIResourceAccess::Unknown, RHI::ERHIResourceAccess::TransferDest, RHI::EQueueType::Graphics, RHI::EQueueType::Compute);
		RHIUpdateBufferRegion updateRegion;
        updateRegion.Size = bufferDesc.Size;
        GRHIApi->UpdateBuffer(cmd, buffer->GetRHI(), input.SourceParams.data(), updateRegion);
        RenderCore::TransitionResource(GRHIApi, cmd, buffer->GetRHI(), RHI::ERHIResourceAccess::TransferDest, RHI::ERHIResourceAccess::SRV, RHI::EQueueType::Compute, RHI::EQueueType::Compute);
        
        RHI::RHIBufferSRVCreateInfo srvDesc;
        srvDesc.Stride = sizeof(DistanceFieldSourceParameters);
        srvDesc.NumElements = input.SourceParams.size();
        auto srcsrv = buffer->GetViewCache().GetOrCreateSRV(buffer->GetRHI(), srvDesc);

        bufferDesc.Stride = sizeof(DistanceFieldOutParameters);
        bufferDesc.Size = input.OutputParams.size() * bufferDesc.Stride;
        auto outbuffer = RenderCore::GTransientResourceAllocator.AllocateBuffer(bufferDesc, 0, 1);

        RenderCore::TransitionResource(GRHIApi, cmd, outbuffer->GetRHI(), RHI::ERHIResourceAccess::Unknown, RHI::ERHIResourceAccess::TransferDest, RHI::EQueueType::Graphics, RHI::EQueueType::Compute);
        updateRegion.Size = bufferDesc.Size;
        GRHIApi->UpdateBuffer(cmd, outbuffer->GetRHI(), input.OutputParams.data(), updateRegion);
        RenderCore::TransitionResource(GRHIApi, cmd, outbuffer->GetRHI(), RHI::ERHIResourceAccess::TransferDest, RHI::ERHIResourceAccess::SRV, RHI::EQueueType::Compute, RHI::EQueueType::Compute);


        srvDesc.Stride = sizeof(DistanceFieldOutParameters);
        srvDesc.NumElements = input.OutputParams.size();

        auto outsrv = outbuffer->GetViewCache().GetOrCreateSRV(outbuffer->GetRHI(), srvDesc);
        params.Source = srcsrv;
        params.Output = outsrv;
        params.SourceCount = input.SourceParams.size();
        params.OutputCount = input.OutputParams.size();
        Core::UInt3 outputResolution = Core::UInt3(64, 64, 64);
        params.OutputResolution = outputResolution;
        auto inputSrcDesc = input.InputSDFTexture->GetRHI()->GetDesc();
        Core::UInt3 inputAtlasResolution = Core::UInt3(inputSrcDesc.Width, inputSrcDesc.Height, inputSrcDesc.Depth);
        params.InputAtlasResolution = inputAtlasResolution;
        params.InputSDFTexture = input.InputSDFTexture->GetRHI();
        auto outputTexUAV = input.OutputSDFTexture->GetViewCache().GetOrCreateUAV(input.OutputSDFTexture->GetRHI(), outputUAVDesc);
        params.OutputSDFTexture = outputTexUAV;
        params.InputSDFSampler = RenderCore::GlobalSampler.get();

        SetShaderParameters(cmd, shader, &params);

        const uint32_t groupX = (static_cast<uint32_t>(outputResolution.X)) / 8u;
        const uint32_t groupY = (static_cast<uint32_t>(outputResolution.Y)) / 8u;
        const uint32_t groupZ = (static_cast<uint32_t>(outputResolution.Z)) / 8u;
        cmd.Dispatch(groupX, groupY, groupZ);

        cmd.End();
        RHI::RHIFence fence;
		fence.QueueType = RHI::EQueueType::Compute;
        fence.Value = computeQueue->ExecuteContext(computeContext);
        input.InputSDFTexture->GetTracker().UpdateLastAccessFence(fence);
        input.OutputSDFTexture->GetTracker().UpdateLastAccessFence(fence);
        
        RenderCore::TransitionTextureImmediate(
            RHI::GRHIApi,
            input.OutputSDFTexture,
            RHI::ERHIResourceAccess::SRV,
            RHI::EQueueType::Compute);
        return true;
    }


    GlobalDistanceField::~GlobalDistanceField()
    {
        Release();
    }



    bool GlobalDistanceField::Initialize(
        float inVoxelSize)
    {
        if (m_bIsInitialized) { return true; }
        m_bIsInitialized = true;
        m_Bounds.Min = Core::Float3(-100, -100, -100);
        m_Bounds.Max = Core::Float3(100, 100, 100);
        m_Atlas = std::make_unique<Engine::DistanceFieldAtlas>();
        RHI::ERHITextureCreateFlags usage = RHI::ERHITextureCreateFlag::ShaderResource | RHI::ERHITextureCreateFlag::TransferDest | RHI::ERHITextureCreateFlag::UAV;
        m_Atlas->Initialize(256, 256, 256, 64, usage);
        m_GridSizeX = m_GridSizeY = m_GridSizeZ = 40;
        m_BlockIndexBufferCPU.resize(m_GridSizeX * m_GridSizeY * m_GridSizeZ);
        RHI::RHIBufferDesc bufferDesc;
        bufferDesc.Size = m_GridSizeX * m_GridSizeY * m_GridSizeZ * sizeof(GlobalDistanceFieldBlockIndex);
        bufferDesc.Stride = sizeof(GlobalDistanceFieldBlockIndex);
        bufferDesc.Usage = RHI::ERHIBufferUsageFlag::ShaderResource | RHI::ERHIBufferUsageFlag::TransferDst;
        m_BlockIndexBufferGPU = std::make_unique<RenderCore::RenderBuffer>(bufferDesc);
		m_BlockIndexBufferGPU->InitRHIResource();
        RHI::RHIBufferSRVCreateInfo srvInfo;
        srvInfo.Stride = sizeof(GlobalDistanceFieldBlockIndex);
        srvInfo.NumElements = m_GridSizeX * m_GridSizeY * m_GridSizeZ;

        m_BlockIndexBufferSRV = m_BlockIndexBufferGPU->GetViewCache().GetOrCreateSRV(m_BlockIndexBufferGPU->GetRHI(), srvInfo);

        m_VoxelSize = inVoxelSize;


        m_BlockWorldSize =
            m_Atlas->GetBlockResolution() *
            m_VoxelSize;


        return true;
    }



    void GlobalDistanceField::Release()
    {
        if (m_Atlas)
        {
            for (auto& pair : m_Blocks)
            {
                m_Atlas->Free(
                    pair.second.Allocation);
            }
        }


        m_Blocks.clear();


        m_Atlas = nullptr;
    }



    uint32_t GlobalDistanceField::CalculateBlockId(
        int32_t x,
        int32_t y,
        int32_t z) const
    {
        return
            x +
            y * m_GridSizeX +
            z * m_GridSizeX * m_GridSizeY;
    }



    void GlobalDistanceField::CalculateBlockCoordinate(
        const Core::Float3& position,
        int32_t& x,
        int32_t& y,
        int32_t& z) const
    {
        Core::Float3 delta = position - m_Bounds.Min;

        x =
            static_cast<int32_t>(
                std::floor(delta.X / m_BlockWorldSize));


        y =
            static_cast<int32_t>(
                std::floor(delta.Y / m_BlockWorldSize));


        z =
            static_cast<int32_t>(
                std::floor(delta.Z / m_BlockWorldSize));
    }



    Core::BoxSphereBounds GlobalDistanceField::CalculateBlockBounds(
        int32_t x,
        int32_t y,
        int32_t z) const
    {
        Core::Float3 min(
            x * m_BlockWorldSize,
            y * m_BlockWorldSize,
            z * m_BlockWorldSize);

        min += m_Bounds.Min;

        Core::Float3 max =
            min +
            Core::Float3(
                m_BlockWorldSize,
                m_BlockWorldSize,
                m_BlockWorldSize);



        return Core::BoxSphereBounds(
            Core::AABB(
                min,
                max));
    }


    bool GlobalDistanceField::Allocate(
        const Core::AABB& bounds,
        GlobalDistanceFieldBlockClipMap& outClipMap)
    {
        if (!m_Atlas || bounds.IsEmpty())
        {
            return false;
        }



        int32_t minX;
        int32_t minY;
        int32_t minZ;


        int32_t maxX;
        int32_t maxY;
        int32_t maxZ;



        CalculateBlockCoordinate(
            bounds.Min,
            minX,
            minY,
            minZ);



        CalculateBlockCoordinate(
            bounds.Max,
            maxX,
            maxY,
            maxZ);



        outClipMap.Bounds =
            Core::BoxSphereBounds(bounds);



        outClipMap.MinBlockX = minX;
        outClipMap.MinBlockY = minY;
        outClipMap.MinBlockZ = minZ;



        outClipMap.SizeX =
            maxX - minX + 1;


        outClipMap.SizeY =
            maxY - minY + 1;


        outClipMap.SizeZ =
            maxZ - minZ + 1;



        for (int32_t z = minZ; z <= maxZ; z++)
        {
            for (int32_t y = minY; y <= maxY; y++)
            {
                for (int32_t x = minX; x <= maxX; x++)
                {
                    uint32_t blockId =
                        CalculateBlockId(
                            x,
                            y,
                            z);



                    auto it =
                        m_Blocks.find(blockId);



                    if (it != m_Blocks.end())
                    {
                        outClipMap.BlockIds.push_back(
                            blockId);

                        continue;
                    }



                    GlobalDistanceFieldBlock block;


                    block.BlockId = blockId;
                    block.PageIndex = blockId;

                    block.GridX = x;


                    block.GridY =
                        y;


                    block.GridZ =
                        z;



                    block.Bounds =
                        CalculateBlockBounds(
                            x,
                            y,
                            z).Box;



                    if (!m_Atlas->Allocate(
                        block.Allocation))
                    {
                        return false;
                    }

                    auto& page = m_BlockIndexBufferCPU[blockId];


                    page.Valid = 1;

                    page.AllocationX =
                        block.Allocation.X;

                    page.AllocationY =
                        block.Allocation.Y;

                    page.AllocationZ =
                        block.Allocation.Z;

                    m_Blocks.emplace(
                        blockId,
                        block);



                    outClipMap.BlockIds.push_back(
                        blockId);
                }
            }
        }

        UploadBlockIndexBufferToGPU();

        return !outClipMap.BlockIds.empty();
    }



    void GlobalDistanceField::Release(
        const GlobalDistanceFieldBlockClipMap& clipMap)
    {
        if (!m_Atlas)
        {
            return;
        }



        for (uint32_t blockId : clipMap.BlockIds)
        {
            auto it =
                m_Blocks.find(blockId);



            if (it == m_Blocks.end())
            {
                continue;
            }



            m_Atlas->Free(
                it->second.Allocation);



            m_Blocks.erase(it);
        }
    }



    bool GlobalDistanceField::GetBlock(
        const Core::Float3& position,
        GlobalDistanceFieldBlock& outBlock) const
    {
        int32_t x;
        int32_t y;
        int32_t z;



        CalculateBlockCoordinate(
            position,
            x,
            y,
            z);



        uint32_t blockId =
            CalculateBlockId(
                x,
                y,
                z);



        auto it =
            m_Blocks.find(blockId);



        if (it == m_Blocks.end())
        {
            return false;
        }



        outBlock =
            it->second;


        return true;
    }



    void GlobalDistanceField::GetBlocksInsideBounds(
        const Core::AABB& bounds,
        std::vector<uint32_t>& outBlockIds) const
    {
        outBlockIds.clear();



        int32_t minX;
        int32_t minY;
        int32_t minZ;


        int32_t maxX;
        int32_t maxY;
        int32_t maxZ;



        CalculateBlockCoordinate(
            bounds.Min,
            minX,
            minY,
            minZ);



        CalculateBlockCoordinate(
            bounds.Max,
            maxX,
            maxY,
            maxZ);



        for (int32_t z = minZ; z <= maxZ; z++)
        {
            for (int32_t y = minY; y <= maxY; y++)
            {
                for (int32_t x = minX; x <= maxX; x++)
                {
                    uint32_t blockId =
                        CalculateBlockId(
                            x,
                            y,
                            z);



                    if (m_Blocks.find(blockId)
                        != m_Blocks.end())
                    {
                        outBlockIds.push_back(
                            blockId);
                    }
                }
            }
        }
    }



    const GlobalDistanceFieldBlock* GlobalDistanceField::GetBlock(
        uint32_t blockId) const
    {
        auto it =
            m_Blocks.find(blockId);


        if (it == m_Blocks.end())
        {
            return nullptr;
        }


        return &it->second;
    }



    bool GlobalDistanceField::GetBlockAllocation(
        uint32_t blockId,
        Engine::DistanceFieldAllocation& outAllocation) const
    {
        auto block =
            GetBlock(blockId);


        if (!block)
        {
            return false;
        }


        outAllocation =
            block->Allocation;


        return true;
    }

    void Renderer::GlobalDistanceField::UploadBlockIndexBufferToGPU()
    {
        if (!m_BlockIndexBufferGPU)
        {
            return;
        }


        auto size = m_GridSizeX * m_GridSizeY * m_GridSizeZ * sizeof(GlobalDistanceFieldBlockIndex);
        char zero = 0;
        memset(m_BlockIndexBufferCPU.data(), 0, size);
        for (const auto& [BlockId, Block] : m_Blocks) {
            auto& page = m_BlockIndexBufferCPU[BlockId];
            page.Valid = 1;
            page.AllocationX = Block.Allocation.X;
            page.AllocationY = Block.Allocation.Y;
            page.AllocationZ = Block.Allocation.Z;
        }


        m_BlockIndexBufferGPU->UploadData(m_BlockIndexBufferCPU.data(), size, 0);
    }



}