#pragma once

#include "RenderGraphResource.h"

namespace Engine { class SceneInterface; class SceneView; }
namespace RenderCore { class RenderGraphBuilder; }

namespace Renderer
{
    // effectiveDepth can describe a water surface in addition to the opaque depth attachment.
    RENDERER_API void AddTransparencyPasses(RenderCore::RenderGraphBuilder& builder, Engine::SceneInterface& scene,
        const Engine::SceneView& view, RenderCore::RenderGraphTextureRef sceneColor,
        RenderCore::RenderGraphTextureRef sceneDepth, RenderCore::RenderGraphTextureRef effectiveDepth = nullptr);
}
