#pragma once

#include "EngineExport.h"
#include "DistanceFieldData.h"
#include "TaskPool.h"
#include "BVH.hpp"

#include <unordered_map>
#include <memory>


namespace Engine
{
    struct LODResource;
    struct DistanceFieldTriangle
    {
        Core::Float3 V0;
        Core::Float3 V1;
        Core::Float3 V2;
    };


    struct DistanceFieldTriangleBVHTraits
    {
        static Core::BoxSphereBounds GetBounds(const DistanceFieldTriangle& triangle);


        static Core::Float3 GetCentroid(const DistanceFieldTriangle& triangle);


        static float ClosestDistance(const DistanceFieldTriangle& triangle, const Core::Float3& position);

        static bool RayIntersect(const DistanceFieldTriangle& primitive, const Core::Ray& ray, float& outDistance);

        static bool IntersectVoxel(const DistanceFieldTriangle& triangle, const Core::AABB& voxelBounds);
    };

    using DistanceFieldBVH = Core::BVH<DistanceFieldTriangle, DistanceFieldTriangleBVHTraits>;
    

    class StaticMeshRenderData;




    class ENGINE_API DistanceFieldManager
    {
    public:

        explicit DistanceFieldManager(Core::TaskPool* inTaskPool = nullptr);

        ~DistanceFieldManager();

        void Initialize();

        void Release();


        void BuildMeshDistanceField(
            StaticMeshRenderData& mesh);

		const DistanceFieldAtlas& GetAtlas() { return m_Atlas; }


    private:

        bool BuildCPUData(StaticMeshRenderData& mesh);



        bool BuildTriangles(
            const LODResource& lod,
            std::vector<DistanceFieldTriangle>& outTriangles);



        bool GenerateDistanceCPU(
            DistanceFieldData& data,
            const DistanceFieldBVH& bvh);



        void GenerateDistanceSlice(
            DistanceFieldData& data,
            uint32_t z,
            const DistanceFieldBVH& bvh);

        RenderCore::RenderTextureSP BuildSurfaceMaskTexture(const Core::BoxSphereBounds& bounds, const LODResource& lod);

        RenderCore::RenderTextureSP BuildSDFTexture(RenderCore::RenderTexture* surfaceMask);

    private:

        Core::TaskPool* m_TaskPool = nullptr;
        DistanceFieldAtlas m_Atlas;
        bool m_bInitialized = false;

    };

    ENGINE_API extern DistanceFieldManager GDistanceFieldMgr;
}