#pragma once
#include "VirtualGeometryRasterPass.h"

namespace Renderer
{
    class Scene;

    // Append visibility raster and material resolve for one full-target view.
    // Clears sceneDepth and GBuffer A/B/C; does not execute the graph or add lighting.
    // Build/upload geometry and flush scene updates before calling this function.
    // Compute material sampling currently has the limitations documented in renderer_spec.md.
    RENDERER_API std::shared_ptr<VirtualGeometryRasterOutput> AddVirtualGeometryRender(
        RenderCore::RenderGraphBuilder& builder, Scene* scene, const Engine::SceneView& view,
        RenderCore::RenderGraphTextureRef sceneDepth, RenderCore::RenderGraphTextureRef gbufferA,
        RenderCore::RenderGraphTextureRef gbufferB, RenderCore::RenderGraphTextureRef gbufferC);
} // namespace Renderer
