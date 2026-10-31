#pragma once
#include "RenderGraphResource.h"

namespace Engine { class SceneView; class LightComponent; class LightSceneProxy; }
namespace Core { struct Ray; }
namespace RenderCore { class RenderGraphBuilder; }

namespace Renderer::HintRender
{
    // One world unit is one metre for editor guides. Height is distance to Y=0.
    // World-space cell spacing for view-dependent grid subdivisions.
    RENDERER_API float GetGridSpacing(float cameraHeight);
    // Unit ray, finite world distance; light data comes from the registered proxy.
    // Local lights use range surfaces; directional hints use a 0.05m tolerance.
    RENDERER_API bool RaycastLight(const Engine::LightSceneProxy& light, const Core::Ray& ray,
        float maxDistance, float& outDistance);
    RENDERER_API void AddGridPass(RenderCore::RenderGraphBuilder& builder, const Engine::SceneView& view,
        RenderCore::RenderGraphTextureRef color, RenderCore::RenderGraphTextureRef depth);
    RENDERER_API void AddLightPass(RenderCore::RenderGraphBuilder& builder, const Engine::SceneView& view,
        RenderCore::RenderGraphTextureRef color, RenderCore::RenderGraphTextureRef depth,
        const Engine::LightComponent& light);
}
