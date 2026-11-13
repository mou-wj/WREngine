#pragma once
#include "VirtualGeometryRasterPass.h"

namespace Renderer
{
    class Scene;

    struct VirtualGeometryMaterialResolveOutput
    {
        RenderCore::RenderGraphTextureRef GBufferA = nullptr;
        RenderCore::RenderGraphTextureRef GBufferB = nullptr;
        RenderCore::RenderGraphTextureRef GBufferC = nullptr;
    };

    RENDERER_API VirtualGeometryMaterialResolveOutput AddVirtualGeometryMaterialResolvePass(
        RenderCore::RenderGraphBuilder& builder, const Engine::SceneView& view,
        const std::shared_ptr<VirtualGeometryRasterOutput>& visibility, RenderCore::RenderGraphTextureRef gbufferA,
        RenderCore::RenderGraphTextureRef gbufferB, RenderCore::RenderGraphTextureRef gbufferC);

    RENDERER_API void AddVirtualGeometryDeferredShadingPass(RenderCore::RenderGraphBuilder& builder,
                                                            const Engine::SceneView& view, Scene* scene,
                                                            const VirtualGeometryMaterialResolveOutput& material,
                                                            RenderCore::RenderGraphTextureRef depth,
                                                            RenderCore::RenderGraphTextureRef target);
} // namespace Renderer
