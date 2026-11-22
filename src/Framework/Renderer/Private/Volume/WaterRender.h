#pragma once

#include "AtmosphereRender.h"

namespace Renderer
{
    enum class EWaterView : uint32_t { Composite, Normals, Reflection, Refraction, Thickness };

    struct WaterRenderSettings
    {
        Core::Float3 Position{0.0f, 0.45f, 0.0f};
        Core::Float2 HalfExtent{9.0f, 9.0f};
        float YawDegrees = 0.0f;
        float WaveDirectionDegrees = 30.0f;
        float WaveAmplitude = 0.12f;
        float WaveFrequency = 0.42f; // Cycles per world unit.
        float WaveSpeed = 0.8f; // World units per second.
        float NormalStrength = 1.0f;
        Core::Float3 BodyColor{0.018f, 0.13f, 0.17f};
        Core::Float3 Absorption{0.85f, 0.30f, 0.17f};
        float Roughness = 0.15f;
        float ReflectionStrength = 1.0f;
        float RefractionStrength = 1.0f;
        float MaxThickness = 12.0f;
        float TimeSeconds = 0.0f;
        EWaterView View = EWaterView::Composite;
        bool bEnabled = false;
    };

    // Returns view-local depth that includes the water surface for downstream atmosphere and fog.
    RENDERER_API RenderCore::RenderGraphTextureRef AddWaterPass(RenderCore::RenderGraphBuilder& builder,
        const Engine::SceneView& view, RenderCore::RenderGraphTextureRef color, RenderCore::RenderGraphTextureRef opaqueDepth,
        const WaterRenderSettings& settings, const VolumeAtmosphereLight& atmosphere, Engine::SceneInterface* scene);
}
