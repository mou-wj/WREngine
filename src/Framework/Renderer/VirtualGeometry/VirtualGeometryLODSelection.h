#pragma once
#include "VirtualGeometryResource.h"
#include "SceneView.h"

namespace Renderer
{
    // Pure CPU selection over immutable Build output. Zero requests the complete leaf cut.
    // Error is projected from the sampled accumulated metric; it is not a strict surface-error bound.
    RENDERER_API std::vector<uint32_t> SelectVirtualGeometryClusters(
        const Engine::VirtualGeometryResource& resource, const Core::Mat4& localToWorld,
        const Engine::SceneView& view, float targetPixelError);
}
