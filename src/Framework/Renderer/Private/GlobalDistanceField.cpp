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

        static bool ShouldCompilePermutation(const RenderCore::ShaderPermutationParameters& Parameters)
        {
            return true;
        }

        static void ModifyShaderCompilerEnvironment(const RenderCore::ShaderPermutationParameters& Parameters, RenderCore::ShaderCompilerEnvironment& OutEnvironment)
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


    bool ExecuteDistanceFieldMergePass(const DistanceFieldMergePassInput& Input)
    {
        
        if (!Input.InputSDFTexture || !Input.OutputSDFTexture)
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

        const auto& inputDesc = Input.InputSDFTexture->GetRHI()->GetDesc();
        const auto& outputDesc = Input.OutputSDFTexture->GetRHI()->GetDesc();

        RHI::RHITexSRVCreateInfo inputSRVDesc;
        inputSRVDesc.Format = inputDesc.Format;
        inputSRVDesc.ArraySize = inputDesc.ArraySize;

        RHI::RHITexUAVCreateInfo outputUAVDesc;
        outputUAVDesc.Format = outputDesc.Format;
        outputUAVDesc.ArraySize = outputDesc.ArraySize;

        auto* inputSRV = Input.InputSDFTexture->GetViewCache().GetOrCreateSRV(Input.InputSDFTexture->GetRHI(), inputSRVDesc);
        auto* outputUAV = Input.OutputSDFTexture->GetViewCache().GetOrCreateUAV(Input.OutputSDFTexture->GetRHI(), outputUAVDesc);

        if (!inputSRV || !outputUAV)
        {
            return false;
        }

        RenderCore::TransitionTextureImmediate(
            RHI::GRHIApi,
            Input.InputSDFTexture,
            RHI::ERHIResourceAccess::SRV,
            RHI::EQueueType::Compute);

        RenderCore::TransitionTextureImmediate(
            RHI::GRHIApi,
            Input.OutputSDFTexture,
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
        bufferDesc.Size = Input.sourceParams.size() * bufferDesc.Stride;
        bufferDesc.Usage = RHI::ERHIBufferUsageFlag::ShaderResource | RHI::ERHIBufferUsageFlag::TransferDst;
        auto buffer = RenderCore::GTransientResourceAllocator.AllocateBuffer(bufferDesc, 0, 1);

		RenderCore::TransitionResource(GRHIApi, cmd, buffer->GetRHI(), RHI::ERHIResourceAccess::Unknown, RHI::ERHIResourceAccess::TransferDest, RHI::EQueueType::Graphics, RHI::EQueueType::Compute);
		RHIUpdateBufferRegion updateRegion;
        updateRegion.Size = bufferDesc.Size;
        GRHIApi->UpdateBuffer(cmd, buffer->GetRHI(), Input.sourceParams.data(), updateRegion);
        RenderCore::TransitionResource(GRHIApi, cmd, buffer->GetRHI(), RHI::ERHIResourceAccess::TransferDest, RHI::ERHIResourceAccess::SRV, RHI::EQueueType::Compute, RHI::EQueueType::Compute);
        
        RHI::RHIBufferSRVCreateInfo srvDesc;
        srvDesc.Stride = sizeof(DistanceFieldSourceParameters);
        srvDesc.NumElements = Input.sourceParams.size();
        auto srcsrv = buffer->GetViewCache().GetOrCreateSRV(buffer->GetRHI(), srvDesc);

        bufferDesc.Stride = sizeof(DistanceFieldOutParameters);
        bufferDesc.Size = Input.outputParams.size() * bufferDesc.Stride;
        auto outbuffer = RenderCore::GTransientResourceAllocator.AllocateBuffer(bufferDesc, 0, 1);

        RenderCore::TransitionResource(GRHIApi, cmd, outbuffer->GetRHI(), RHI::ERHIResourceAccess::Unknown, RHI::ERHIResourceAccess::TransferDest, RHI::EQueueType::Graphics, RHI::EQueueType::Compute);
        updateRegion.Size = bufferDesc.Size;
        GRHIApi->UpdateBuffer(cmd, outbuffer->GetRHI(), Input.outputParams.data(), updateRegion);
        RenderCore::TransitionResource(GRHIApi, cmd, outbuffer->GetRHI(), RHI::ERHIResourceAccess::TransferDest, RHI::ERHIResourceAccess::SRV, RHI::EQueueType::Compute, RHI::EQueueType::Compute);


        srvDesc.Stride = sizeof(DistanceFieldOutParameters);
        srvDesc.NumElements = Input.outputParams.size();

        auto outsrv = outbuffer->GetViewCache().GetOrCreateSRV(outbuffer->GetRHI(), srvDesc);
        params.Source = srcsrv;
        params.Output = outsrv;
        params.SourceCount = Input.sourceParams.size();
        params.OutputCount = Input.outputParams.size();
        Core::UInt3 OutputResolution = Core::UInt3(64, 64, 64);
        params.OutputResolution = OutputResolution;
        auto inputSrcDesc = Input.InputSDFTexture->GetRHI()->GetDesc();
        Core::UInt3 InputAtlasResolution = Core::UInt3(inputSrcDesc.Width, inputSrcDesc.Height, inputSrcDesc.Depth);
        params.InputAtlasResolution = InputAtlasResolution;
        params.InputSDFTexture = Input.InputSDFTexture->GetRHI();
        auto outputTexUAV = Input.OutputSDFTexture->GetViewCache().GetOrCreateUAV(Input.OutputSDFTexture->GetRHI(), outputUAVDesc);
        params.OutputSDFTexture = outputTexUAV;
        params.InputSDFSampler = RenderCore::GlobalSampler.get();

        SetShaderParameters(cmd, shader, &params);

        const uint32_t groupX = (static_cast<uint32_t>(OutputResolution.X)) / 8u;
        const uint32_t groupY = (static_cast<uint32_t>(OutputResolution.Y)) / 8u;
        const uint32_t groupZ = (static_cast<uint32_t>(OutputResolution.Z)) / 8u;
        cmd.Dispatch(groupX, groupY, groupZ);

        cmd.End();
        RHI::RHIFence fence;
		fence.QueueType = RHI::EQueueType::Compute;
        fence.Value = computeQueue->ExecuteContext(computeContext);
        Input.InputSDFTexture->GetTracker().UpdateLastAccessFence(fence);
        Input.OutputSDFTexture->GetTracker().UpdateLastAccessFence(fence);
        
        RenderCore::TransitionTextureImmediate(
            RHI::GRHIApi,
            Input.OutputSDFTexture,
            RHI::ERHIResourceAccess::SRV,
            RHI::EQueueType::Compute);
        return true;
    }


    GlobalDistanceField::~GlobalDistanceField()
    {
        Release();
    }



    bool GlobalDistanceField::Initialize(
        float InVoxelSize)
    {
        if (IsInitialized) { return true; }
        IsInitialized = true;
        Bounds.Min = Core::Float3(-100, -100, -100);
        Bounds.Max = Core::Float3(100, 100, 100);
        Atlas = std::make_unique<Engine::DistanceFieldAtlas>();
        RHI::ERHITextureCreateFlags usage = RHI::ERHITextureCreateFlag::ShaderResource | RHI::ERHITextureCreateFlag::TransferDest | RHI::ERHITextureCreateFlag::UAV;
        Atlas->Initialize(256, 256, 256, 64, usage);
        GridSizeX = GridSizeY = GridSizeZ = 40;
        BlockIndexBufferCPU.resize(GridSizeX * GridSizeY * GridSizeZ);
        RHI::RHIBufferDesc bufferDesc;
        bufferDesc.Size = GridSizeX * GridSizeY * GridSizeZ * sizeof(GlobalDistanceFieldBlockIndex);
        bufferDesc.Stride = sizeof(GlobalDistanceFieldBlockIndex);
        bufferDesc.Usage = RHI::ERHIBufferUsageFlag::ShaderResource | RHI::ERHIBufferUsageFlag::TransferDst;
        BlockIndexBufferGPU = std::make_unique<RenderCore::RenderBuffer>(bufferDesc);
		BlockIndexBufferGPU->InitRHIResource();
        RHI::RHIBufferSRVCreateInfo srvInfo;
        srvInfo.Stride = sizeof(GlobalDistanceFieldBlockIndex);
        srvInfo.NumElements = GridSizeX * GridSizeY * GridSizeZ;

        BlockIndexBufferSRV = BlockIndexBufferGPU->GetViewCache().GetOrCreateSRV(BlockIndexBufferGPU->GetRHI(), srvInfo);

        VoxelSize = InVoxelSize;


        BlockWorldSize =
            Atlas->GetBlockResolution() *
            VoxelSize;


        return true;
    }



    void GlobalDistanceField::Release()
    {
        if (Atlas)
        {
            for (auto& Pair : Blocks)
            {
                Atlas->Free(
                    Pair.second.Allocation);
            }
        }


        Blocks.clear();


        Atlas = nullptr;
    }



    uint32_t GlobalDistanceField::CalculateBlockId(
        int32_t X,
        int32_t Y,
        int32_t Z) const
    {
        return
            X +
            Y * GridSizeX +
            Z * GridSizeX * GridSizeY;
    }



    void GlobalDistanceField::CalculateBlockCoordinate(
        const Core::Float3& Position,
        int32_t& X,
        int32_t& Y,
        int32_t& Z) const
    {
        Core::Float3 delta = Position - Bounds.Min;

        X =
            static_cast<int32_t>(
                std::floor(delta.X / BlockWorldSize));


        Y =
            static_cast<int32_t>(
                std::floor(delta.Y / BlockWorldSize));


        Z =
            static_cast<int32_t>(
                std::floor(delta.Z / BlockWorldSize));
    }



    Core::BoxSphereBounds GlobalDistanceField::CalculateBlockBounds(
        int32_t X,
        int32_t Y,
        int32_t Z) const
    {
        Core::Float3 Min(
            X * BlockWorldSize,
            Y * BlockWorldSize,
            Z * BlockWorldSize);

        Min += Bounds.Min;

        Core::Float3 Max =
            Min +
            Core::Float3(
                BlockWorldSize,
                BlockWorldSize,
                BlockWorldSize);



        return Core::BoxSphereBounds(
            Core::AABB(
                Min,
                Max));
    }


    bool GlobalDistanceField::Allocate(
        const Core::AABB& Bounds,
        GlobalDistanceFieldBlockClipMap& OutClipMap)
    {
        if (!Atlas || Bounds.IsEmpty())
        {
            return false;
        }



        int32_t MinX;
        int32_t MinY;
        int32_t MinZ;


        int32_t MaxX;
        int32_t MaxY;
        int32_t MaxZ;



        CalculateBlockCoordinate(
            Bounds.Min,
            MinX,
            MinY,
            MinZ);



        CalculateBlockCoordinate(
            Bounds.Max,
            MaxX,
            MaxY,
            MaxZ);



        OutClipMap.Bounds =
            Core::BoxSphereBounds(Bounds);



        OutClipMap.MinBlockX = MinX;
        OutClipMap.MinBlockY = MinY;
        OutClipMap.MinBlockZ = MinZ;



        OutClipMap.SizeX =
            MaxX - MinX + 1;


        OutClipMap.SizeY =
            MaxY - MinY + 1;


        OutClipMap.SizeZ =
            MaxZ - MinZ + 1;



        for (int32_t Z = MinZ; Z <= MaxZ; Z++)
        {
            for (int32_t Y = MinY; Y <= MaxY; Y++)
            {
                for (int32_t X = MinX; X <= MaxX; X++)
                {
                    uint32_t BlockId =
                        CalculateBlockId(
                            X,
                            Y,
                            Z);



                    auto It =
                        Blocks.find(BlockId);



                    if (It != Blocks.end())
                    {
                        OutClipMap.BlockIds.push_back(
                            BlockId);

                        continue;
                    }



                    GlobalDistanceFieldBlock Block;


                    Block.BlockId = BlockId;
                    Block.PageIndex = BlockId;

                    Block.GridX = X;


                    Block.GridY =
                        Y;


                    Block.GridZ =
                        Z;



                    Block.Bounds =
                        CalculateBlockBounds(
                            X,
                            Y,
                            Z).Box;



                    if (!Atlas->Allocate(
                        Block.Allocation))
                    {
                        return false;
                    }

                    auto& page = BlockIndexBufferCPU[BlockId];


                    page.Valid = 1;

                    page.AllocationX =
                        Block.Allocation.X;

                    page.AllocationY =
                        Block.Allocation.Y;

                    page.AllocationZ =
                        Block.Allocation.Z;

                    Blocks.emplace(
                        BlockId,
                        Block);



                    OutClipMap.BlockIds.push_back(
                        BlockId);
                }
            }
        }

        UploadBlockIndexBufferToGPU();

        return !OutClipMap.BlockIds.empty();
    }



    void GlobalDistanceField::Release(
        const GlobalDistanceFieldBlockClipMap& ClipMap)
    {
        if (!Atlas)
        {
            return;
        }



        for (uint32_t BlockId : ClipMap.BlockIds)
        {
            auto It =
                Blocks.find(BlockId);



            if (It == Blocks.end())
            {
                continue;
            }



            Atlas->Free(
                It->second.Allocation);



            Blocks.erase(It);
        }
    }



    bool GlobalDistanceField::GetBlock(
        const Core::Float3& Position,
        GlobalDistanceFieldBlock& OutBlock) const
    {
        int32_t X;
        int32_t Y;
        int32_t Z;



        CalculateBlockCoordinate(
            Position,
            X,
            Y,
            Z);



        uint32_t BlockId =
            CalculateBlockId(
                X,
                Y,
                Z);



        auto It =
            Blocks.find(BlockId);



        if (It == Blocks.end())
        {
            return false;
        }



        OutBlock =
            It->second;


        return true;
    }



    void GlobalDistanceField::GetBlocksInsideBounds(
        const Core::AABB& Bounds,
        std::vector<uint32_t>& OutBlockIds) const
    {
        OutBlockIds.clear();



        int32_t MinX;
        int32_t MinY;
        int32_t MinZ;


        int32_t MaxX;
        int32_t MaxY;
        int32_t MaxZ;



        CalculateBlockCoordinate(
            Bounds.Min,
            MinX,
            MinY,
            MinZ);



        CalculateBlockCoordinate(
            Bounds.Max,
            MaxX,
            MaxY,
            MaxZ);



        for (int32_t Z = MinZ; Z <= MaxZ; Z++)
        {
            for (int32_t Y = MinY; Y <= MaxY; Y++)
            {
                for (int32_t X = MinX; X <= MaxX; X++)
                {
                    uint32_t BlockId =
                        CalculateBlockId(
                            X,
                            Y,
                            Z);



                    if (Blocks.find(BlockId)
                        != Blocks.end())
                    {
                        OutBlockIds.push_back(
                            BlockId);
                    }
                }
            }
        }
    }



    const GlobalDistanceFieldBlock* GlobalDistanceField::GetBlock(
        uint32_t BlockId) const
    {
        auto It =
            Blocks.find(BlockId);


        if (It == Blocks.end())
        {
            return nullptr;
        }


        return &It->second;
    }



    bool GlobalDistanceField::GetBlockAllocation(
        uint32_t BlockId,
        Engine::DistanceFieldAllocation& OutAllocation) const
    {
        auto Block =
            GetBlock(BlockId);


        if (!Block)
        {
            return false;
        }


        OutAllocation =
            Block->Allocation;


        return true;
    }

    void Renderer::GlobalDistanceField::UploadBlockIndexBufferToGPU()
    {
        if (!BlockIndexBufferGPU)
        {
            return;
        }


        auto size = GridSizeX * GridSizeY * GridSizeZ * sizeof(GlobalDistanceFieldBlockIndex);
        char zero = 0;
        memset(BlockIndexBufferCPU.data(), 0, size);
        for (const auto& [BlockId, Block] : Blocks) {
            auto& page = BlockIndexBufferCPU[BlockId];
            page.Valid = 1;
            page.AllocationX = Block.Allocation.X;
            page.AllocationY = Block.Allocation.Y;
            page.AllocationZ = Block.Allocation.Z;
        }


        BlockIndexBufferGPU->UploadData(BlockIndexBufferCPU.data(), size, 0);
    }



}