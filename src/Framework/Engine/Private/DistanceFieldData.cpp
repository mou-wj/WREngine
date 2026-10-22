#include "DistanceFieldData.h"

namespace Engine
{


    DistanceFieldAtlas::~DistanceFieldAtlas()
    {
        Release();
    }



    bool DistanceFieldAtlas::Initialize(
        uint32_t inAtlasResolutionX,
        uint32_t inAtlasResolutionY,
        uint32_t inAtlasResolutionZ,
        uint32_t inBlockResolution,
        RHI::ERHITextureCreateFlags usage)
    {
        Release();


        if (inBlockResolution == 0)
        {
            return false;
        }


        if (inAtlasResolutionX % inBlockResolution != 0 ||
            inAtlasResolutionY % inBlockResolution != 0 ||
            inAtlasResolutionZ % inBlockResolution != 0)
        {
            return false;
        }



        m_AtlasResolutionX = inAtlasResolutionX;
        m_AtlasResolutionY = inAtlasResolutionY;
        m_AtlasResolutionZ = inAtlasResolutionZ;


        m_BlockResolution = inBlockResolution;



        m_BlockCountX =
            m_AtlasResolutionX / m_BlockResolution;


        m_BlockCountY =
            m_AtlasResolutionY / m_BlockResolution;


        m_BlockCountZ =
            m_AtlasResolutionZ / m_BlockResolution;



        uint32_t totalBlocks =
            m_BlockCountX *
            m_BlockCountY *
            m_BlockCountZ;



        m_AllocationMap.resize(
            totalBlocks,
            0);



        m_NextAllocationId = 0;

        m_AtlasTexture = RenderCore::Create3DTexture(inAtlasResolutionX, inAtlasResolutionY, inAtlasResolutionZ, RHI::ERHIFormat::R32_Float, usage , "DistanceFieldAtlas");
        return true;
    }



    void DistanceFieldAtlas::Release()
    {
        m_AllocationMap.clear();


        m_AtlasResolutionX = 0;
        m_AtlasResolutionY = 0;
        m_AtlasResolutionZ = 0;


        m_BlockCountX = 0;
        m_BlockCountY = 0;
        m_BlockCountZ = 0;


        m_NextAllocationId = 0;

        m_AtlasTexture.reset();
    }



    uint32_t DistanceFieldAtlas::CalculateIndex(
        uint32_t x,
        uint32_t y,
        uint32_t z) const
    {
        return x +
            y * m_BlockCountX +
            z * m_BlockCountX * m_BlockCountY;
    }



    bool DistanceFieldAtlas::Allocate(
        DistanceFieldAllocation& outAllocation)
    {
        for (uint32_t z = 0; z < m_BlockCountZ; z++)
        {
            for (uint32_t y = 0; y < m_BlockCountY; y++)
            {
                for (uint32_t x = 0; x < m_BlockCountX; x++)
                {
                    uint32_t index =
                        CalculateIndex(
                            x,
                            y,
                            z);



                    if (m_AllocationMap[index] != 0)
                    {
                        continue;
                    }



                    m_AllocationMap[index] = 1;



                    outAllocation.Id =
                        m_NextAllocationId++;



                    outAllocation.X =
                        x * m_BlockResolution;


                    outAllocation.Y =
                        y * m_BlockResolution;


                    outAllocation.Z =
                        z * m_BlockResolution;



                    outAllocation.SizeX =
                        m_BlockResolution;


                    outAllocation.SizeY =
                        m_BlockResolution;


                    outAllocation.SizeZ =
                        m_BlockResolution;



                    return true;
                }
            }
        }


        return false;
    }



    void DistanceFieldAtlas::Free(
        const DistanceFieldAllocation& allocation)
    {
        if (!allocation.IsValid())
        {
            return;
        }



        uint32_t x =
            allocation.X / m_BlockResolution;


        uint32_t y =
            allocation.Y / m_BlockResolution;


        uint32_t z =
            allocation.Z / m_BlockResolution;



        if (x >= m_BlockCountX ||
            y >= m_BlockCountY ||
            z >= m_BlockCountZ)
        {
            return;
        }



        uint32_t index =
            CalculateIndex(
                x,
                y,
                z);



        m_AllocationMap[index] = 0;
    }



    bool DistanceFieldAtlas::IsAllocated(
        uint32_t id) const
    {
        /*
         * 当前版本:
         *
         * Allocation ID
         * 只用于调试和追踪
         *
         * 没有维护ID->Slot映射
         *
         */


        return id != InvalidDistanceFieldAllocation;
    }
    void DistanceFieldAtlas::UploadData(
        const DistanceFieldAllocation& allocation,
        const float* data) {
        RHI::RHIUpdateTextureRegion uploadReion;
        uploadReion.xOffset = allocation.X;
        uploadReion.yOffset = allocation.Y;
        uploadReion.zOffset = allocation.Z;
        uploadReion.width = allocation.SizeX;
		uploadReion.height = allocation.SizeY;
        uploadReion.depth = allocation.SizeZ;
        m_AtlasTexture->UploadData(data, uploadReion);
    }


}