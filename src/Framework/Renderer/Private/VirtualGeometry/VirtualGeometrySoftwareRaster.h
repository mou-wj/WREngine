#pragma once
#include "VirtualGeometryGPUSelection.h"

namespace Renderer
{
    enum class EVirtualGeometryRasterMode : uint32_t { Hardware, Hybrid, SoftwarePreferred };

    struct VirtualGeometryRasterSettings
    {
        EVirtualGeometryRasterMode Mode = EVirtualGeometryRasterMode::Hardware;
        float SoftwareClusterPixels = 32;
    };

    struct VirtualGeometryRasterRouting
    {
        RenderCore::RenderGraphBufferRef HardwareArguments = nullptr;
        RenderCore::RenderGraphBufferRef Paths = nullptr; // 0: invisible, 1: hardware, 2: software.
        RenderCore::RenderGraphBufferRef Statistics = nullptr; // HW Clusters, SW Clusters, forced HW fallbacks, SW triangles.
    };

    struct VirtualGeometrySoftwareTargets
    {
        RenderCore::RenderGraphBufferRef Depth = nullptr;
        RenderCore::RenderGraphBufferRef Visibility = nullptr;
        Core::Int2 Size;
    };

    VirtualGeometryRasterRouting AddVirtualGeometryRasterRouting(RenderCore::RenderGraphBuilder& builder,
        const Engine::VirtualGeometryResource& resource, const VirtualGeometryGPUSelection& selection,
        const Core::Mat4& localToWorld, const Engine::SceneView& view, const VirtualGeometryRasterSettings& settings);
    VirtualGeometrySoftwareTargets AddVirtualGeometrySoftwareClear(RenderCore::RenderGraphBuilder& builder, Core::Int2 size);
    void AddVirtualGeometrySoftwareRaster(RenderCore::RenderGraphBuilder& builder,
        const Engine::VirtualGeometryResource& resource, const VirtualGeometryRasterRouting& routing,
        const VirtualGeometrySoftwareTargets& targets, const Core::Mat4& localToWorld,
        const Engine::SceneView& view, uint32_t firstDraw, bool writeVisibility);
    void AddVirtualGeometryRasterMerge(RenderCore::RenderGraphBuilder& builder, const Engine::SceneView& view,
        const VirtualGeometrySoftwareTargets& software, RenderCore::RenderGraphTextureRef hardwareVisibility,
        RenderCore::RenderGraphTextureRef hardwareDepth, RenderCore::RenderGraphTextureRef finalVisibility,
        RenderCore::RenderGraphTextureRef finalDepth);
}
