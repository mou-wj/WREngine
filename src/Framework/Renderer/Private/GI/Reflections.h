#pragma once

#include "WorldRadianceCache.h"
#include "../GBufferInfo.h"

namespace Renderer
{
    enum class EReflectionView : uint32_t
    {
        Composite, RayDirection, ScreenHit, SDFHit, Source, Roughness, Radiance, Contribution
    };

    struct ReflectionsInput
    {
        GBufferInputParameters GBuffer{};
        // Direct lighting plus diffuse IBL. Specular IBL and legacy SSR are excluded by the caller.
        RenderCore::RenderGraphTextureRef DeferredColor = nullptr;
        RenderCore::RenderGraphTextureRef Output = nullptr;
        Core::Float4x4 ViewProjection = Core::Float4x4::Identity();
        Core::Float4x4 InverseViewProjection = Core::Float4x4::Identity();
        Core::Float3 CameraPosition{};
        SceneEvnIBLLightParameters Environment{};
        bool bHasEnvironment = false;
        const WorldRadianceCacheParameters* WorldCache = nullptr;
        EReflectionView View = EReflectionView::Composite;
    };

    RENDERER_API void AddReflectionsPasses(RenderCore::RenderGraphBuilder& builder, const ReflectionsInput& input,
        const SurfaceCacheMaterialParameters& material, const SurfaceCacheLightingParameters& lighting);
}
