#pragma once

#include "EngineExport.h"
#include "RenderResource.h"
#include "Math.hpp"

#include <cstdint>
#include <vector>
#include <memory>

namespace Engine
{

    struct ENGINE_API DistanceFieldVolumeDesc
    {
        uint32_t ResolutionX = 0;
        uint32_t ResolutionY = 0;
        uint32_t ResolutionZ = 0;

        Core::Float3 MinBounds;
        Core::Float3 MaxBounds;

        Core::Float3 VoxelSize;

        uint32_t GetVoxelCount() const { return ResolutionX * ResolutionY * ResolutionZ; }
    };


    struct ENGINE_API DistanceFieldData
    {
        DistanceFieldVolumeDesc Volume;

        std::vector<float> Distance;
        std::vector<uint8_t> SurfaceMask;


        bool IsValid() const
        {
            return Distance.size() == Volume.GetVoxelCount();
        }


        void Resize(uint32_t x, uint32_t y, uint32_t z)
        {
            Volume.ResolutionX = x;
            Volume.ResolutionY = y;
            Volume.ResolutionZ = z;

            Distance.resize(Volume.GetVoxelCount());
            SurfaceMask.resize(Volume.GetVoxelCount());
        }


        uint32_t GetIndex(uint32_t x, uint32_t y, uint32_t z) const
        {
            return x + y * Volume.ResolutionX + z * Volume.ResolutionX * Volume.ResolutionY;
        }


        float GetDistance(uint32_t x, uint32_t y, uint32_t z) const
        {
            return Distance[GetIndex(x, y, z)];
        }


        uint8_t GetSurfaceMask(uint32_t x, uint32_t y, uint32_t z) const
        {
            return SurfaceMask[GetIndex(x, y, z)];
        }
    };

    static constexpr uint32_t InvalidDistanceFieldAllocation = UINT32_MAX;

    /**
     * Distance Field Atlas中的一块物理空间
     *
     * 只描述Texture中的位置
     *
     * 不包含任何World信息
     */
    struct DistanceFieldAllocation
    {
        uint32_t Id = InvalidDistanceFieldAllocation;


        uint32_t X = 0;
        uint32_t Y = 0;
        uint32_t Z = 0;


        uint32_t SizeX = 0;
        uint32_t SizeY = 0;
        uint32_t SizeZ = 0;



        bool IsValid() const
        {
            return Id != InvalidDistanceFieldAllocation;
        }
    };

    /*
     * Runtime GPU resource
     *
     * CPU DistanceFieldData -> GPU Texture3D
     */
    struct ENGINE_API DistanceFieldResource
    {
        DistanceFieldData CPUData;

        DistanceFieldAllocation Allocation;
    };






    /**
     * Distance Field Atlas Allocator
     *
     * 管理Texture3D内部空间分配
     *
     * 不关心:
     *
     * - 世界坐标
     * - Mesh
     * - Scene
     * - Bounds
     *
     * 只负责:
     *
     * Allocation <-> Physical Atlas Space
     */
    class ENGINE_API DistanceFieldAtlas
    {
    public:

        DistanceFieldAtlas() = default;

        ~DistanceFieldAtlas();



    public:

        /**
         * 初始化Atlas空间
         *
         * AtlasResolution:
         *
         * Texture3D尺寸
         *
         * BlockResolution:
         *
         * 最小分配单位
         */
        bool Initialize(
            uint32_t atlasResolutionX,
            uint32_t atlasResolutionY,
            uint32_t atlasResolutionZ,
            uint32_t blockResolution,
            RHI::ERHITextureCreateFlags usage = RHI::ERHITextureCreateFlag::ShaderResource | RHI::ERHITextureCreateFlag::TransferDest);



        void Release();



        /**
         * 分配一个Block空间
         */
        bool Allocate(
            DistanceFieldAllocation& outAllocation);



        /**
         * 释放空间
         */
        void Free(
            const DistanceFieldAllocation& allocation);



        /**
         * 查询Allocation是否有效
         */
        bool IsAllocated(
            uint32_t id) const;


		void UploadData(
			const DistanceFieldAllocation& allocation,
			const float* data);

    public:


        uint32_t GetAtlasResolutionX() const
        {
            return m_AtlasResolutionX;
        }


        uint32_t GetAtlasResolutionY() const
        {
            return m_AtlasResolutionY;
        }


        uint32_t GetAtlasResolutionZ() const
        {
            return m_AtlasResolutionZ;
        }



        uint32_t GetBlockResolution() const
        {
            return m_BlockResolution;
        }

        RenderCore::RenderTexture* GetAtlasTexture() const
        {
            return m_AtlasTexture.get();
        }

    private:


        uint32_t CalculateIndex(
            uint32_t x,
            uint32_t y,
            uint32_t z) const;



    private:


        //---------------------------------
        // Atlas Texture尺寸
        //---------------------------------

        uint32_t m_AtlasResolutionX = 0;
        uint32_t m_AtlasResolutionY = 0;
        uint32_t m_AtlasResolutionZ = 0;



        //---------------------------------
        // 单个Allocation大小
        //---------------------------------

        uint32_t m_BlockResolution = 64;



        //---------------------------------
        // Block数量
        //---------------------------------

        uint32_t m_BlockCountX = 0;
        uint32_t m_BlockCountY = 0;
        uint32_t m_BlockCountZ = 0;



        //---------------------------------
        // 空间占用
        //
        // 0: free
        // 1: used
        //---------------------------------

        std::vector<uint8_t> m_AllocationMap;



        //---------------------------------
        // Allocation ID
        //---------------------------------

        uint32_t m_NextAllocationId = 0;

        RenderCore::RenderTextureSP m_AtlasTexture;
    };


}