#include "DistanceFieldMgr.h"
#include "StaticMeshResources.h"
#include "BVH.hpp"
#include "DistanceFieldProcess.h"
#include <limits>
#include <cmath>
using namespace Core;
namespace Engine
{
    DistanceFieldManager GDistanceFieldMgr;

    Float3 ClosestPointOnTriangle(
        const Float3& p,
        const Float3& a,
        const Float3& b,
        const Float3& c)
    {
        Float3 ab = b - a;
        Float3 ac = c - a;
        Float3 ap = p - a;


        float d1 = Dot(ab, ap);
        float d2 = Dot(ac, ap);


        if (d1 <= 0.0f && d2 <= 0.0f)
        {
            return a;
        }



        Float3 bp = p - b;

        float d3 = Dot(ab, bp);
        float d4 = Dot(ac, bp);


        if (d3 >= 0.0f && d4 <= d3)
        {
            return b;
        }



        float vc = d1 * d4 - d3 * d2;


        if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f)
        {
            float v = d1 / (d1 - d3);

            return a + ab * v;
        }



        Float3 cp = p - c;


        float d5 = Dot(ab, cp);
        float d6 = Dot(ac, cp);



        if (d6 >= 0.0f && d5 <= d6)
        {
            return c;
        }



        float vb = d5 * d2 - d1 * d6;


        if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f)
        {
            float w = d2 / (d2 - d6);

            return a + ac * w;
        }



        float va = d3 * d6 - d5 * d4;


        if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f)
        {
            Float3 bc = c - b;

            float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));

            return b + bc * w;
        }



        float denom = 1.0f / (va + vb + vc);

        float v = vb * denom;

        float w = vc * denom;


        return a + ab * v + ac * w;
    }

    Core::BoxSphereBounds DistanceFieldTriangleBVHTraits::GetBounds(const DistanceFieldTriangle& triangle)
    {
        Core::AABB box;
        box.SetEmpty();
        box.ExpandBy(triangle.V0);
        box.ExpandBy(triangle.V1);
        box.ExpandBy(triangle.V2);
        return Core::BoxSphereBounds(box);
    }


    Core::Float3 DistanceFieldTriangleBVHTraits::GetCentroid(const DistanceFieldTriangle& triangle)
    {
        return Core::Float3((triangle.V0.x + triangle.V1.x + triangle.V2.x) / 3.0f, (triangle.V0.y + triangle.V1.y + triangle.V2.y) / 3.0f, (triangle.V0.z + triangle.V1.z + triangle.V2.z) / 3.0f);
    }


    float DistanceFieldTriangleBVHTraits::ClosestDistance(const DistanceFieldTriangle& triangle, const Core::Float3& position)
    {
        Core::Float3 point = ClosestPointOnTriangle(position, triangle.V0, triangle.V1, triangle.V2);

        Core::Float3 delta = point - position;

        return sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
    }

    bool DistanceFieldTriangleBVHTraits::RayIntersect(const DistanceFieldTriangle& primitive, const Core::Ray& ray, float& outDistance)
    {
        constexpr float epsilon = 1e-6f;

        const Core::Float3 edge1 = primitive.V1 - primitive.V0;
        const Core::Float3 edge2 = primitive.V2 - primitive.V0;

        const Core::Float3 p = Core::Cross(ray.Direction, edge2);

        const float det = Core::Dot(edge1, p);

        if (std::fabs(det) < epsilon)
        {
            return false;
        }

        const float invDet = 1.0f / det;

        const Core::Float3 t = ray.Origin - primitive.V0;

        const float u = Core::Dot(t, p) * invDet;

        if (u < 0.0f || u > 1.0f)
        {
            return false;
        }

        const Core::Float3 q = Core::Cross(t, edge1);

        const float v = Core::Dot(ray.Direction, q) * invDet;

        if (v < 0.0f || u + v > 1.0f)
        {
            return false;
        }

        const float distance = Core::Dot(edge2, q) * invDet;

        if (distance < epsilon)
        {
            return false;
        }

        outDistance = distance;

        return true;
    }

    bool DistanceFieldTriangleBVHTraits::IntersectVoxel(const DistanceFieldTriangle& triangle, const Core::AABB& voxelBounds)
    {
		BoxSphereBounds triangleSphere = GetBounds(triangle);
		return triangleSphere.Box.Intersects(voxelBounds) || voxelBounds.Contains(triangleSphere.Box);
    }
    

    namespace
    {
        constexpr int32_t GLocalSDFResolution = 64;

        void FillTexture3D(RenderCore::RenderTexture* texture, float fillValue)
        {
            if (!texture || !texture->GetRHI())
            {
                return;
            }

            const auto& desc = texture->GetRHI()->GetDesc();
            const size_t voxelCount =
                static_cast<size_t>(desc.Width) *
                static_cast<size_t>(desc.Height) *
                static_cast<size_t>(desc.Depth);
            std::vector<float> initData(voxelCount, fillValue);
            texture->UploadData(initData.data(), 0, 0);
        }

        Core::Float4x4 BuildWorldToVoxel(const Core::AABB& bounds, const Core::Int3& resolution)
        {
            Core::Float4x4 m = Core::Float4x4::Identity();
            if (bounds.IsEmpty())
            {
                return m;
            }

            const float sizeX = CORE_MAX(bounds.Max.x - bounds.Min.x, 1e-4f);
            const float sizeY = CORE_MAX(bounds.Max.y - bounds.Min.y, 1e-4f);
            const float sizeZ = CORE_MAX(bounds.Max.z - bounds.Min.z, 1e-4f);

            const float sx = static_cast<float>(CORE_MAX(1, resolution.x - 1)) / sizeX;
            const float sy = static_cast<float>(CORE_MAX(1, resolution.y - 1)) / sizeY;
            const float sz = static_cast<float>(CORE_MAX(1, resolution.z - 1)) / sizeZ;

            m(0, 0) = sx;
            m(1, 1) = sy;
            m(2, 2) = sz;
            m(0, 3) = -bounds.Min.x * sx;
            m(1, 3) = -bounds.Min.y * sy;
            m(2, 3) = -bounds.Min.z * sz;
            return m;
        }

        std::shared_ptr<RenderCore::RenderBuffer> CreateStructuredBufferFromCPU(
            const void* data,
            uint64_t elementCount,
            uint32_t stride,
            const char* debugName)
        {
            RHI::RHIBufferDesc desc;
            desc.Size = CORE_MAX(1ull, elementCount * static_cast<uint64_t>(stride));
            desc.Stride = stride;
            desc.Usage =
                RHI::ERHIBufferUsageFlag::Structured |
                RHI::ERHIBufferUsageFlag::ShaderResource |
                RHI::ERHIBufferUsageFlag::TransferDst;
            desc.InitialQueueType = RHI::EQueueType::Compute;
            desc.DebugName = debugName;

            auto buffer = std::make_shared<RenderCore::RenderBuffer>(desc);
            buffer->InitRHIResource();
            if (data && elementCount > 0)
            {
                buffer->UploadData(data, static_cast<uint32_t>(elementCount * static_cast<uint64_t>(stride)));
            }
            return buffer;
        }
    }

    DistanceFieldManager::DistanceFieldManager(Core::TaskPool* inTaskPool)
        : m_TaskPool(inTaskPool)
    {
        
    }



    DistanceFieldManager::~DistanceFieldManager()
    {
    }

    void DistanceFieldManager::Initialize()
    {
        if (m_bInitialized) {
            return;
        }
        m_bInitialized = true;
        m_Atlas.Initialize(128, 128, 128, 64);
    }

    void DistanceFieldManager::Release()
    {
        m_bInitialized = false;
		m_Atlas.Release();
    }



    void DistanceFieldManager::BuildMeshDistanceField(StaticMeshRenderData& mesh)
    {
        bool buildInCpu = true;
        if (buildInCpu) {
            BuildCPUData(mesh);
            for (auto& lod : mesh.LODResources) {
                
                if (lod.DistanceFieldData.CPUData.Distance.empty()) {
                    continue;
                }
				auto allocationSuccess = m_Atlas.Allocate(lod.DistanceFieldData.Allocation);
                m_Atlas.UploadData(lod.DistanceFieldData.Allocation, lod.DistanceFieldData.CPUData.Distance.data());
                
            }
        }
        else {
			for (auto& lod : mesh.LODResources) {
				auto surfaceMask = BuildSurfaceMaskTexture(mesh.Bounds, lod);
                
			}
        }
    }



    bool DistanceFieldManager::BuildCPUData(StaticMeshRenderData& mesh)
    {
        const uint32_t resolution = GLocalSDFResolution;


        const Core::AABB& bounds = mesh.Bounds.Box;



        bool res = true;
        for (auto& lod : mesh.LODResources) {
            auto& outData = lod.DistanceFieldData.CPUData;
            outData.Volume.ResolutionX = resolution;
            outData.Volume.ResolutionY = resolution;
            outData.Volume.ResolutionZ = resolution;


            outData.Volume.MinBounds = bounds.Min;
            outData.Volume.MaxBounds = bounds.Max;


            Core::Float3 size = bounds.Max - bounds.Min;


            outData.Volume.VoxelSize = Core::Float3(
                size.x / resolution,
                size.y / resolution,
                size.z / resolution);


            outData.Resize(
                resolution,
                resolution,
                resolution);

            std::vector<DistanceFieldTriangle> triangles;


            if (!BuildTriangles(lod, triangles))
            {
                continue;
            }



            DistanceFieldBVH bvh;

            bvh.Build(triangles);



            res &= GenerateDistanceCPU(
                outData,
                bvh);
        }

        return res;
    }



    bool DistanceFieldManager::BuildTriangles(const LODResource& lod, std::vector<DistanceFieldTriangle>& outTriangles)
    {

        const auto& positions = lod.VertexBuffers.PositionBuffer.Vertices;

        const auto& indices = lod.IndexBuffer.Indices;


        if (positions.empty() || indices.empty())
        {
            return false;
        }



        outTriangles.reserve(indices.size() / 3);



        for (size_t index = 0; index < indices.size(); index += 3)
        {
            uint32_t i0 = indices[index];
            uint32_t i1 = indices[index + 1];
            uint32_t i2 = indices[index + 2];


            DistanceFieldTriangle triangle;


            triangle.V0 = Core::Float3(
                positions[i0 * 3],
                positions[i0 * 3 + 1],
                positions[i0 * 3 + 2]);


            triangle.V1 = Core::Float3(
                positions[i1 * 3],
                positions[i1 * 3 + 1],
                positions[i1 * 3 + 2]);


            triangle.V2 = Core::Float3(
                positions[i2 * 3],
                positions[i2 * 3 + 1],
                positions[i2 * 3 + 2]);


            outTriangles.push_back(triangle);
        }


        return true;
    }



    bool DistanceFieldManager::GenerateDistanceCPU(DistanceFieldData& data, const DistanceFieldBVH& bvh)
    {
        std::vector<Core::TaskHandle> tasks;


        for (uint32_t z = 0; z < data.Volume.ResolutionZ; z++)
        {
            if (m_TaskPool)
            {
                tasks.emplace_back(m_TaskPool->AddTask(
                    [&, z]()
                    {
                        GenerateDistanceSlice(data, z, bvh);
                    }));
            }
            else
            {
                GenerateDistanceSlice(data, z, bvh);
            }
        }



        if (m_TaskPool)
        {
            m_TaskPool->WaitAll(tasks);
        }



        return true;
    }

    Core::Float3 RandomSeed(Core::Float3 center, float radius) {
        float randomx = center.x + radius * (rand() % 1000 / 1000.0f - 0.5f);
        float randomy = center.y + radius * (rand() % 1000 / 1000.0f - 0.5f);
		float randomz = center.z + radius * (rand() % 1000 / 1000.0f - 0.5f);
        return Core::Float3(randomx, randomy, randomz);
    }

    void DistanceFieldManager::GenerateDistanceSlice(DistanceFieldData& data, uint32_t z, const DistanceFieldBVH& bvh)
    {
        const uint32_t sizeX = data.Volume.ResolutionX;
        const uint32_t sizeY = data.Volume.ResolutionY;



        for (uint32_t y = 0; y < sizeY; y++)
        {
            for (uint32_t x = 0; x < sizeX; x++)
            {
                uint32_t index = data.GetIndex(x, y, z);



                Core::Float3 position(
                    data.Volume.MinBounds.x + x * data.Volume.VoxelSize.x,
                    data.Volume.MinBounds.y + y * data.Volume.VoxelSize.y,
                    data.Volume.MinBounds.z + z * data.Volume.VoxelSize.z);



                data.Distance[index] = bvh.ClosestDistance(position);
                float radius = bvh.GetRootNode().Bounds.Sphere.Radius;
				float randomRange = radius * 0.01f;
                Core::Ray hit;
                hit.Origin = position;
                Core::Float3 center = bvh.GetRootNode().Bounds.Box.GetCenter();
				center = RandomSeed(center, randomRange);
                hit.Direction = center - hit.Origin;
                hit.Direction = Core::Normalize(hit.Direction);
                std::set<uint32_t> intersectIds;
                bvh.RayIntersect(hit, intersectIds);
                float positiveDis = data.Distance[index];
                if (intersectIds.size() % 2 == 1) {
                    data.Distance[index] = -positiveDis;
                }
                if (intersectIds.size() == 0) {
                    int i = 10;
                    
                }
                if (intersectIds.size() > 2) {
                    std::set<float> hitDistences;
                    for (auto id : intersectIds) {
                        auto pri = bvh.GetPrimitive(id);
                        float dis = 0;
                        bool sc = DistanceFieldTriangleBVHTraits::RayIntersect(bvh.GetPrimitive(id), hit, dis);
                        if (hitDistences.empty()) {
							hitDistences.insert(dis);
                        }
                        else {
                            bool ignore = false;
                            for (auto rDis : hitDistences) {
                                if (abs(dis - rDis) < 0.00001) {
                                    ignore = true;
                                    break;
                                }
                            }
                            if (!ignore) {
                                hitDistences.insert(dis);
                            }
                        }
                        

                        
                    }
                    if (hitDistences.size() > 2) {
                        data.Distance[index] = -positiveDis;
                    }
                    if (hitDistences.size() % 2 == 1) {
                        data.Distance[index] = -positiveDis;
                    }
                    else {
                        data.Distance[index] = -positiveDis;
                    }
                    
                }
            }
        }
    }
    static Core::Float3 GetVoxelPosition(const DistanceFieldVolumeDesc& volume, uint32_t x, uint32_t y, uint32_t z)
    {
        return Core::Float3(
            volume.MinBounds.x + (x + 0.5f) * volume.VoxelSize.x,
            volume.MinBounds.y + (y + 0.5f) * volume.VoxelSize.y,
            volume.MinBounds.z + (z + 0.5f) * volume.VoxelSize.z
        );
    }
    static Core::AABB GetVoxelBounds(const DistanceFieldVolumeDesc& volume, uint32_t x, uint32_t y, uint32_t z)
    {
        Core::Float3 min(
            volume.MinBounds.x + x * volume.VoxelSize.x,
            volume.MinBounds.y + y * volume.VoxelSize.y,
            volume.MinBounds.z + z * volume.VoxelSize.z
        );

        Core::Float3 max = min + volume.VoxelSize;

        return Core::AABB(min, max);
    }
    RenderCore::RenderTextureSP DistanceFieldManager::BuildSurfaceMaskTexture(const Core::BoxSphereBounds& bounds, const LODResource& lod)
    {
        std::vector<DistanceFieldTriangle> triangles;


        if (!BuildTriangles(lod, triangles))
        {
            return nullptr;
        }



        DistanceFieldBVH bvh;

        bvh.Build(triangles);
        const uint32_t resolution = GLocalSDFResolution;
		std::vector<uint8_t> surfaceMask(resolution * resolution * resolution, 255);
        
        //补充
        DistanceFieldVolumeDesc volume;

        volume.ResolutionX = resolution;
        volume.ResolutionY = resolution;
        volume.ResolutionZ = resolution;

        volume.MinBounds = bounds.Box.Min;
        volume.MaxBounds = bounds.Box.Max;


        Core::Float3 size =
            bounds.Box.Max - bounds.Box.Min;


        volume.VoxelSize =
            Core::Float3(
                size.x / resolution,
                size.y / resolution,
                size.z / resolution);



        constexpr uint32_t blockSize = 8;


        std::vector<Core::TaskHandle> tasks;



        auto processBlock =
            [&](uint32_t bx, uint32_t by, uint32_t bz)
            {
                uint32_t startX = bx * blockSize;
                uint32_t startY = by * blockSize;
                uint32_t startZ = bz * blockSize;


                uint32_t endX = CORE_MIN(startX + blockSize, resolution);
                uint32_t endY = CORE_MIN(startY + blockSize, resolution);
                uint32_t endZ = CORE_MIN(startZ + blockSize, resolution);



                Core::AABB blockBounds(
                    Core::Float3(
                        volume.MinBounds.x + startX * volume.VoxelSize.x,
                        volume.MinBounds.y + startY * volume.VoxelSize.y,
                        volume.MinBounds.z + startZ * volume.VoxelSize.z),
                    Core::Float3(
                        volume.MinBounds.x + endX * volume.VoxelSize.x,
                        volume.MinBounds.y + endY * volume.VoxelSize.y,
                        volume.MinBounds.z + endZ * volume.VoxelSize.z)
                );


                Core::BoxSphereBounds queryBounds(blockBounds);



                std::set<uint32_t> primitiveIds;


                bvh.GetPrimitiveIdsInsideBounds(
                    queryBounds,
                    primitiveIds);



                if (primitiveIds.empty())
                    return;



                for (uint32_t z = startZ; z < endZ; z++)
                {
                    for (uint32_t y = startY; y < endY; y++)
                    {
                        for (uint32_t x = startX; x < endX; x++)
                        {

                            Core::AABB voxelBounds =
                                GetVoxelBounds(
                                    volume,
                                    x, y, z);


                            uint32_t index =
                                z * resolution * resolution +
                                y * resolution +
                                x;


                            for (uint32_t primitiveId : primitiveIds)
                            {

                                const auto& triangle =
                                    bvh.GetPrimitive(
                                        primitiveId);



                                if (DistanceFieldTriangleBVHTraits::IntersectVoxel(
                                    triangle,
                                    voxelBounds))
                                {

                                    surfaceMask[index] =
                                        static_cast<uint8_t>(
                                            primitiveId < 128 ?
                                            primitiveId :
                                            127);


                                    goto NextVoxel;
                                }
                            }


                        NextVoxel:;
                        }
                    }
                }
            };



        uint32_t blockCount =
            (resolution + blockSize - 1) / blockSize;



        for (uint32_t z = 0; z < blockCount; z++)
        {
            for (uint32_t y = 0; y < blockCount; y++)
            {
                for (uint32_t x = 0; x < blockCount; x++)
                {

                    if (m_TaskPool)
                    {
                        tasks.emplace_back(
                            m_TaskPool->AddTask(
                                [&, x, y, z]()
                                {
                                    processBlock(x, y, z);
                                }));
                    }
                    else
                    {
                        processBlock(x, y, z);
                    }

                }
            }
        }



        if (m_TaskPool)
        {
            m_TaskPool->WaitAll(tasks);
        }



		RenderCore::RenderTextureSP surfaceMaskTex = RenderCore::Create3DTexture(
			resolution,
			resolution,
			resolution,
			RHI::ERHIFormat::B8G8R8_UNorm,
            RHI::ERHITextureCreateFlag::ShaderResource | RHI::ERHITextureCreateFlag::TransferDest,
			"SurfaceMaskTest");
		surfaceMaskTex->UploadData(surfaceMask.data(), 0, 0);
        return surfaceMaskTex;
    }

    RenderCore::RenderTextureSP DistanceFieldManager::BuildSDFTexture(RenderCore::RenderTexture* surfaceMask)
    {
        const uint32_t resolution = GLocalSDFResolution;
        RenderCore::RenderTextureSP sdfTex = RenderCore::Create3DTexture(
            resolution,
            resolution,
            resolution,
            RHI::ERHIFormat::B8G8R8_UNorm,
            RHI::ERHITextureCreateFlag::ShaderResource | RHI::ERHITextureCreateFlag::TransferDest,
            "SDFTexture");
        DistanceFieldJumpFlood3DPassInput input;
        input.SurfaceMaskTexture = surfaceMask;
		input.OutputDistanceTexture = sdfTex.get();
        input.GridResolution = Core::Int3(resolution, resolution, resolution);
        ExecuteDistanceFieldJumpFlood3DPass(input);
        return sdfTex;
    }



    


}