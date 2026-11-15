#pragma once

#include "EngineExport.h"
#include "BoxSphereBounds.h"
#include "RenderResource.h"
#include <cstdint>
#include <limits>
#include <vector>

namespace Engine
{
    class StaticMesh;
    constexpr uint32_t INVALID_VIRTUAL_GEOMETRY_INDEX = std::numeric_limits<uint32_t>::max();
    constexpr uint32_t VIRTUAL_GEOMETRY_MAX_CLUSTER_TRIANGLES = 128;

    // 几何误差与对应的空间包围球，均使用 Mesh Local Space
    struct VirtualGeometryLODMetric
    {
        Core::BoxSphereBounds Bounds;
        float Error = 0.0f; // 累计几何误差
    };

    // Cluster 是最小几何渲染单元
    struct VirtualGeometryCluster
    {
        uint32_t VertexOffset = 0;
        uint32_t VertexCount = 0;
        uint32_t IndexOffset = 0;
        uint32_t TriangleCount = 0;

        uint32_t GroupIndex = INVALID_VIRTUAL_GEOMETRY_INDEX;           // 当前 Cluster 所属的简化 Group
        uint32_t GeneratingGroupIndex = INVALID_VIRTUAL_GEOMETRY_INDEX; // 生成当前 Cluster 的 Group
        uint32_t MaterialIndex = 0;                                     // INVALID 表示源 Section 未指定材质槽
        uint32_t MipLevel = 0;

        Core::BoxSphereBounds Bounds;
    };

    // Group 定义一次整体替换；Root Group 只含 Child，ParentCount 为零。
    struct VirtualGeometryGroup
    {
        uint32_t ChildClusterOffset = 0;
        uint32_t ChildClusterCount = 0;
        uint32_t ParentClusterOffset = 0;
        uint32_t ParentClusterCount = 0;

        VirtualGeometryLODMetric ChildMetric;
        VirtualGeometryLODMetric ParentMetric;

        uint32_t MipLevel = 0;
        float SimplificationError = 0.0f; // 本次双向表面采样误差；不是严格 Hausdorff 上界
    };

    // CPU 端虚拟几何资源，不直接持有 RHI 对象
    struct VirtualGeometryResource
    {
        std::vector<VirtualGeometryCluster> Clusters;
        std::vector<VirtualGeometryGroup> Groups;

        // Group 的 Cluster 索引范围通过 Offset/Count 引用
        std::vector<uint32_t> GroupChildClusterIndices;
        std::vector<uint32_t> GroupParentClusterIndices;

        // 每个 Cluster 独立拥有顶点区间，Indices 存储 Cluster Local Index
        std::vector<Core::Float3> Positions;
        std::vector<Core::Float3> Normals;
        std::vector<Core::Float4> Tangents;
        std::vector<Core::Float2> TexCoords;
        std::vector<Core::Float4> Colors;
        std::vector<uint32_t> Indices;

        // 顶层 Group，允许多个根节点
        std::vector<uint32_t> RootGroupIndices;

        Core::BoxSphereBounds Bounds;

        std::unique_ptr<RenderCore::RenderBuffer> PositionBuffer;
        std::unique_ptr<RenderCore::RenderBuffer> NormalBuffer;
        std::unique_ptr<RenderCore::RenderBuffer> TangentBuffer;
        std::unique_ptr<RenderCore::RenderBuffer> TexCoordBuffer;
        std::unique_ptr<RenderCore::RenderBuffer> ColorBuffer;
        std::unique_ptr<RenderCore::RenderBuffer> IndexBuffer;
        std::unique_ptr<RenderCore::RenderBuffer> ClusterBuffer;
        std::unique_ptr<RenderCore::RenderBuffer> GroupBuffer;
        std::unique_ptr<RenderCore::RenderBuffer> GroupChildClusterIndexBuffer;
        std::unique_ptr<RenderCore::RenderBuffer> GroupParentClusterIndexBuffer;
        std::unique_ptr<RenderCore::RenderBuffer> RootGroupIndexBuffer;

        uint32_t Version = 1;
        uint32_t StreamingPageSlots = 0; // Zero: original full buffers; nonzero: bounded physical page slots.

        void Clear()
        {
            ReleaseResources();
            Clusters.clear();
            Groups.clear();
            GroupChildClusterIndices.clear();
            GroupParentClusterIndices.clear();
            Positions.clear();
            Normals.clear();
            Tangents.clear();
            TexCoords.clear();
            Colors.clear();
            Indices.clear();
            RootGroupIndices.clear();
            Bounds.SetEmpty();
        }

        bool IsValid() const
        {
            return !Clusters.empty() && !Positions.empty() && !Indices.empty();
        }

        // 同步 CPU 构建 LOD0；异常会清空本资源，不修改 StaticMesh 或创建 RHI 对象。
        ENGINE_API void Build(const StaticMesh* staticMesh);

        // Upload completed CPU data; release retains it for subsequent reinitialization.
        ENGINE_API void InitializeResources();
        ENGINE_API void InitializeStreamingResources(uint32_t pageSlots);
        ENGINE_API void ReleaseResources();

    private:
        void InitializeResourcesInternal(uint32_t pageSlots);
    };

} // namespace Engine
