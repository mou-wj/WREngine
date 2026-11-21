#pragma once

#include "SurfaceCacheMaterial.h"

namespace Renderer
{
    // Viewer-owned snapshot. The owner retires the dependent preview output fence before destruction.
    class RENDERER_API GIRayTracingScene
    {
    public:
        GIRayTracingScene();
        ~GIRayTracingScene();
        static bool IsSupported();
        bool Update(const std::vector<SurfaceCacheMeshInstance>& instances);
        uint32_t GetBLASCount() const;
        uint32_t GetInstanceCount() const;
        uint64_t GetTLASRevision() const;

        RenderCore::RenderGraphBufferRef AddTracePasses(RenderCore::RenderGraphBuilder& builder,
            const SurfaceCacheMaterialParameters& material, RenderCore::RenderGraphBufferRef queries, uint32_t count);

    private:
        struct State;
        std::unique_ptr<State> m_State;
    };

    // Screen Probe and Reflections supply unfiltered nearest-hit queries (mode 1) or skip slots (mode 2).
    RenderCore::RenderGraphBufferRef AddGITracePasses(RenderCore::RenderGraphBuilder& builder,
        const SurfaceCacheMaterialParameters& material, RenderCore::RenderGraphBufferRef queries, uint32_t count,
        GIRayTracingScene* hardwareScene);
}
