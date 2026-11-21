#pragma once

#include "SurfaceCacheDirectLighting.h"
#include <array>

namespace Renderer
{
    // Iterations use the same material capture and direct lighting. Invalidate the history
    // when either changes. Submit graphs in construction order and retire GPU uses before destruction.
    class RENDERER_API SurfaceCacheRadiosity
    {
    public:
        SurfaceCacheRadiosity() = default;
        SurfaceCacheRadiosity(const SurfaceCacheRadiosity&) = delete;
        SurfaceCacheRadiosity& operator=(const SurfaceCacheRadiosity&) = delete;

        void Invalidate() { m_Iteration = 0; }
        uint32_t GetIteration() const { return m_Iteration; }
        void AddIteration(RenderCore::RenderGraphBuilder& builder, const SurfaceCacheMaterialParameters& material,
            const SurfaceCacheLightingParameters& direct);
        SurfaceCacheLightingParameters Register(RenderCore::RenderGraphBuilder& builder,
            const SurfaceCacheLightingParameters& direct) const;
        void AddDebugPass(RenderCore::RenderGraphBuilder& builder, const SurfaceCacheLightingParameters& direct,
            RenderCore::RenderGraphTextureRef output, bool bShowDelta) const;
        RenderCore::RenderTexture* GetAtlas() const;

    private:
        RenderCore::RenderGraphTextureRef RegisterAtlas(RenderCore::RenderGraphBuilder& builder, uint32_t index) const;

        std::array<RenderCore::RenderTextureSP, 2> m_Atlases;
        uint32_t m_Iteration = 0;
    };
}
