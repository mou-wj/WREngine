#pragma once
#include "VirtualGeometryRasterPass.h"

namespace Renderer
{
    class Scene;

    // Append visibility raster and material resolve for one full-target view.
    // Clears sceneDepth and GBuffer A/B/C; does not execute the graph or add lighting.
    // Build/upload geometry and flush scene updates before calling this function.
    // Usage and supported material features are documented in renderer_spec.md.
    // CPU DAG LOD uses a pixel-error budget; zero retains all original leaf Clusters.
    RENDERER_API std::shared_ptr<VirtualGeometryRasterOutput> AddVirtualGeometryRender(
        RenderCore::RenderGraphBuilder& builder, Scene* scene, const Engine::SceneView& view,
        RenderCore::RenderGraphTextureRef sceneDepth, RenderCore::RenderGraphTextureRef gbufferA,
        RenderCore::RenderGraphTextureRef gbufferB, RenderCore::RenderGraphTextureRef gbufferC,
        float targetPixelError = 1.0f);
} // namespace Renderer
