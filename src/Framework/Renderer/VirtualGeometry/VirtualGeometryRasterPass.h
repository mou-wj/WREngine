#pragma once
#include "RenderGraphBuilder.h"
#include "SceneView.h"
#include "StaticMeshResources.h"
#include "Material.h"

namespace Renderer
{
    struct VirtualGeometryInstance
    {
        std::shared_ptr<Engine::StaticMeshRenderData> RenderData;
        Core::Mat4 LocalToWorld = Core::Mat4::Identity();
        uint32_t InstanceID = 0;
        std::vector<std::shared_ptr<const Engine::MaterialRenderProxy>> Materials;
    };

    BEGIN_SHADER_PARAMETER_STRUCT(VirtualGeometryDrawMetadata)
    SHADER_PARAMETER(uint32_t, InstanceID)
    SHADER_PARAMETER(uint32_t, ClusterID)
    SHADER_PARAMETER(uint32_t, ResourceIndex)
    SHADER_PARAMETER(uint32_t, InstanceIndex)
    END_SHADER_PARAMETER_STRUCT(VirtualGeometryDrawMetadata)

    struct VirtualGeometryRasterOutput
    {
        RenderCore::RenderGraphTextureRef VirtualGeometryVisibilityTexture = nullptr;
        RenderCore::RenderGraphTextureRef VirtualGeometryDepthTexture = nullptr;
        RenderCore::RenderGraphBufferSRVRef DrawMetadata = nullptr;
        std::vector<VirtualGeometryDrawMetadata> Draws;
        std::vector<std::shared_ptr<Engine::StaticMeshRenderData>> Resources;
        std::vector<VirtualGeometryInstance> Instances;
    };

    RENDERER_API std::shared_ptr<VirtualGeometryRasterOutput> AddVirtualGeometryRasterPass(
        RenderCore::RenderGraphBuilder& builder, const Engine::SceneView& view,
        const std::vector<VirtualGeometryInstance>& instances, uint32_t width, uint32_t height);
    // Reuse and clear the caller's D32 scene depth instead of creating another depth texture.
    RENDERER_API std::shared_ptr<VirtualGeometryRasterOutput> AddVirtualGeometryRasterPass(
        RenderCore::RenderGraphBuilder& builder, const Engine::SceneView& view,
        const std::vector<VirtualGeometryInstance>& instances, RenderCore::RenderGraphTextureRef sceneDepth);
    RENDERER_API void AddVirtualGeometryVisibilityDebugPass(RenderCore::RenderGraphBuilder& builder,
                                                            const std::shared_ptr<VirtualGeometryRasterOutput>& output,
                                                            RenderCore::RenderGraphTextureRef target,
                                                            const Engine::SceneView& view);
} // namespace Renderer
