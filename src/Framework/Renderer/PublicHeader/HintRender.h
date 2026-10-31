#pragma once
#include "RenderGraphResource.h"

namespace Engine { class SceneView; class LightComponent; }
namespace RenderCore { class RenderGraphBuilder; }

namespace Renderer::HintRender
{
    // One world unit is one metre for editor guides. Height is distance to Y=0.
    // World-space cell spacing for view-dependent grid subdivisions.
    RENDERER_API float GetGridSpacing(float cameraHeight);
    RENDERER_API void AddGridPass(RenderCore::RenderGraphBuilder& builder, const Engine::SceneView& view,
        RenderCore::RenderGraphTextureRef color, RenderCore::RenderGraphTextureRef depth);
    RENDERER_API void AddLightPass(RenderCore::RenderGraphBuilder& builder, const Engine::SceneView& view,
        RenderCore::RenderGraphTextureRef color, RenderCore::RenderGraphTextureRef depth,
        const Engine::LightComponent& light);
}
