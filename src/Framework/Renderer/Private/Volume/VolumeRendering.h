#pragma once

#include "Math.hpp"
#include "RenderGraphResource.h"

namespace Engine { class SceneView; class SceneInterface; }
namespace RenderCore { class RenderGraphBuilder; }

namespace Renderer
{
    // Density scales the per-world-unit optical coefficients. Emission is linear radiance per unit length.
    struct VolumeMedium
    {
        float Density = 0.08f;
        Core::Float3 Scattering{0.72f, 0.76f, 0.82f};
        Core::Float3 Absorption{0.18f, 0.18f, 0.18f};
        Core::Float3 Emission{0.0f, 0.0f, 0.0f};
        float Anisotropy = 0.0f;
    };

    enum class EVolumeView : uint32_t
    {
        Composite,
        Scattering,
        Transmittance,
        Density,
        Froxels,
        DirectLighting
    };

    struct VolumeRenderSettings
    {
        VolumeMedium Medium;
        Core::Float3 AmbientRadiance{0.24f, 0.28f, 0.32f};
        float MaxDistance = 45.0f;
        EVolumeView View = EVolumeView::Composite;
        uint32_t DebugSlice = 16;
        uint32_t LightMask = 7; // Directional, Point, Spot in bits 0 through 2.
        bool bEnabled = true;
    };

    // The caller supplies one full-size perspective view and a transferable, linear scene color.
    RENDERER_API void AddVolumePasses(RenderCore::RenderGraphBuilder& builder, const Engine::SceneView& view,
        RenderCore::RenderGraphTextureRef sceneDepth, RenderCore::RenderGraphTextureRef sceneColor,
        const VolumeRenderSettings& settings, Engine::SceneInterface* scene = nullptr);
}
