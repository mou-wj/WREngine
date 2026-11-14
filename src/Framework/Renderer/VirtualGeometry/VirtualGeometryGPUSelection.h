#pragma once
#include "RenderGraphBuilder.h"
#include "VirtualGeometryLODSelection.h"

namespace Renderer
{
    struct VirtualGeometryGPUSelection
    {
        RenderCore::RenderGraphBufferRef AppliedGroups = nullptr;
        RenderCore::RenderGraphBufferRef ClusterFlags = nullptr; // bit 0: complete cut; bit 1: visible subset.
        RenderCore::RenderGraphBufferRef VisibleClusters = nullptr; // Compact, unordered; Statistics[1] valid entries.
        RenderCore::RenderGraphBufferRef Statistics = nullptr; // Cut count, visible count, culled count, visible triangles.
        RenderCore::RenderGraphBufferRef IndirectArguments = nullptr; // One 20-byte slot per original Cluster ID.
        uint32_t DependencyLevels = 0;
    };

    RENDERER_API VirtualGeometryGPUSelection AddVirtualGeometryGPUSelectionPass(
        RenderCore::RenderGraphBuilder& builder, const Engine::VirtualGeometryResource& resource,
        const Core::Mat4& localToWorld, const Engine::SceneView& view, float targetPixelError,
        bool frustumCulling = true);
}
