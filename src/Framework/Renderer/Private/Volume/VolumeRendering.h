#pragma once

#include "Math.hpp"
#include "RenderGraphResource.h"
#include <memory>
#include <vector>

namespace Engine { class SceneView; class SceneInterface; struct SceneViewDrawExtension; }
namespace RenderCore { class RenderGraphBuilder; class RenderTexture; }

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

    enum class EVolumeShape : uint32_t { Box, Sphere };
    enum class EVolumeDensityMode : uint32_t { Uniform, Noise, Texture };

    struct VolumeDensityField
    {
        EVolumeDensityMode Mode = EVolumeDensityMode::Uniform;
        // Borrowed RHI resource. Its owner must retain it through the draw's last GPU access.
        RenderCore::RenderTexture* Texture = nullptr;
        Core::Float3 Offset{0, 0, 0};
        Core::Float3 Velocity{0, 0, 0};
        float Scale = 1.0f;
        float Bias = 0.0f;
        float Threshold = 0.0f;
        float Frequency = 3.0f;
    };

    struct LocalVolume
    {
        EVolumeShape Shape = EVolumeShape::Box;
        Core::Float3 Position{0, 0, 0};
        Core::Float3 RotationDegrees{0, 0, 0};
        Core::Float3 HalfExtent{1, 1, 1};
        VolumeMedium Medium;
        VolumeDensityField DensityField;
    };

    enum class EVolumeView : uint32_t
    {
        Composite,
        Scattering,
        Transmittance,
        Density,
        Froxels,
        DirectLighting,
        LightVisibility,
        LocalOverlap,
        IndirectLighting,
        DensitySources
    };

    struct VolumeRenderSettings
    {
        VolumeMedium Medium;
        std::vector<LocalVolume> LocalVolumes;
        Core::Float3 AmbientRadiance{0.24f, 0.28f, 0.32f};
        float MaxDistance = 45.0f;
        float EnvironmentIntensity = 1.0f;
        EVolumeView View = EVolumeView::Composite;
        uint32_t DebugSlice = 16;
        uint32_t LightMask = 7; // Directional, Point, Spot in bits 0 through 2.
        bool bEnabled = true;
        bool bShadows = true;
        bool bUseSkyEnvironment = true;
    };

    // The caller supplies one full-size perspective view and a transferable, linear scene color.
    RENDERER_API void AddVolumePasses(RenderCore::RenderGraphBuilder& builder, const Engine::SceneView& view,
        RenderCore::RenderGraphTextureRef sceneDepth, RenderCore::RenderGraphTextureRef sceneColor,
        const VolumeRenderSettings& settings, Engine::SceneInterface* scene = nullptr);

    // Optional attachment for a caller which owns the scene and its perspective view.
    RENDERER_API Engine::SceneViewDrawExtension MakeVolumeDrawExtension(VolumeRenderSettings settings, Engine::SceneInterface* scene);
    RENDERER_API std::shared_ptr<RenderCore::RenderTexture> CreateVolumePreviewDensityTexture();
}
