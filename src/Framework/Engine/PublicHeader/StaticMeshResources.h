
#pragma once
#include "EngineExport.h"
#include <cstdint>
#include <vector>
#include <memory>
#include <utility>
#include <algorithm>
#include "BoxSphereBounds.h"
#include "Material.h"
#include "RenderResource.h"
#include "LocalVertexFactory.h"
#include "DistanceFieldData.h"
namespace Engine {
    // Forward placeholders for shader/texture types. In a real engine, replace
    // these with concrete types or resource handles managed by the RHI/resource system.
    class LocalVertexFactory;

    struct VertexBuffer
    {
        std::vector<float> Vertices;

        std::unique_ptr<RenderCore::RenderBuffer>
            Buffer;

        uint32_t NumComponents = 0;
        bool bValid = false;

        uint64_t GetNumVertices() const
        {
            return NumComponents > 0
                ? Vertices.size() / NumComponents
                : 0;
        }
    };
    

    // Lightweight index buffer: 32-bit indices
    struct ENGINE_API IndexBuffer {
        std::vector<uint32_t> Indices;
        std::unique_ptr<RenderCore::RenderBuffer> Buffer;
        size_t GetNumIndices() const { return Indices.size(); }
    };

    // Section describes a sub-range of the index buffer that uses a specific material.
    struct ENGINE_API SectionInfo {
        // first index in the index buffer (index into IndexBuffer::Indices)
        uint32_t FirstIndex = 0;
        // number of indices in this section
        uint32_t NumIndices = 0;
        // material index into the RenderData's Materials array
        int32_t MaterialIndex = -1;
        int32_t BaseVertexIndex = 0;
    };

    struct StaticMeshVertexBuffers {
        VertexBuffer PositionBuffer;
        VertexBuffer UVBuffer;
        VertexBuffer NormalBuffer;
        VertexBuffer TangentBuffer;
        VertexBuffer ColorBuffer;

    };


    // LOD resource contains one vertex buffer, one index buffer and multiple sections.
    struct ENGINE_API LODResource {
        // Vertex data is type-erased via IVertexData. Use VertexData<T> to supply
        // concrete typed vertex arrays (e.g., VertexData<StaticVertex>).
        StaticMeshVertexBuffers VertexBuffers;
        IndexBuffer IndexBuffer;        // per-LOD index data
        std::vector<SectionInfo> Sections; // sub-mesh sections mapped to materials
        DistanceFieldResource DistanceFieldData;
        // Convenience: query number of sections
        size_t GetNumSections() const { return Sections.size(); }
        // Convenience: get vertex count if VertexDataPtr is set
        std::unique_ptr<
            LocalVertexFactory>
            VertexFactory;

        void InitializeResources();
        void ReleaseResources();
    };


    // FStaticMeshRenderData: top-level render resource container for a StaticMesh.
    // Contains multiple LODs, a material table, and bounds used for culling.
    class ENGINE_API StaticMeshRenderData {
    public:

        StaticMeshRenderData() = default;
        ~StaticMeshRenderData() = default;

        // LOD resources (LOD0 = highest detail)
        std::vector<LODResource> LODResources;

        // Bounds (AABB) for frustum culling and coarse occlusion
        Core::BoxSphereBounds Bounds;


        // ----------------- Minimal public interfaces -----------------
        // Number of LODs available
        size_t GetLODCount() const { return LODResources.size(); }

        // Access a LOD resource (const, read-only for RenderThread)
        // Caller must ensure index < GetLODCount().
        const LODResource& GetLODResource(size_t index) const { return LODResources[index]; }

        // Get bounds for culling
        const Core::BoxSphereBounds& GetBounds() const { return Bounds; }

        

        // Utilities to append resources (used on GameThread during build/load)
        void AddLOD(LODResource&& lod) { LODResources.emplace_back(std::move(lod)); }

        // Intended usage: build on GameThread, then the RenderThread may read these
        // structures concurrently (read-only). Do not modify after submitting to the scene.

    private:
        // Non-copyable to avoid accidental copies of large buffers; allow move if needed.
        StaticMeshRenderData(const StaticMeshRenderData&) = delete;
        StaticMeshRenderData& operator=(const StaticMeshRenderData&) = delete;
    };
} // namespace NSRender