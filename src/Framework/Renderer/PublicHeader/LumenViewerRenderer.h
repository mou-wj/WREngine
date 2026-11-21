#pragma once

#include "RenderGraphBuilder.h"
#include "RenderResource.h"
#include "SceneView.h"
#include <memory>
#include <vector>

namespace Engine { class SceneInterface; }

namespace Renderer
{
    enum class ELumenViewerMode : uint32_t
    {
        Scene,
        SurfaceBaseColor,
        SurfaceWorldNormal,
        SurfaceCoverage,
        SurfaceRoughness,
        SurfaceMetallic,
        SurfaceCardAtlas,
        DirectLightingAtlas,
        ProbePlacement,
        ProbeRayDirections,
        ProbeHitRatio,
        ProbeRadiance,
        IndirectDiffuse,
        RadiosityIndirectAtlas,
        RadiosityIterationDelta,
        WorldProbeAtlas,
        ProbeWorldCacheHits,
        ProbeRawDiffuse,
        ProbeTemporalDiffuse,
        ProbeHistoryValidity,
        ProbeHistoryWeight,
        ProbeReprojectionError,
        ReflectionComposite,
        ReflectionRayDirection,
        ReflectionScreenHit,
        ReflectionSDFHit,
        ReflectionSource,
        ReflectionRoughness,
        ReflectionRadiance,
        ReflectionContribution
    };

    enum class ELumenViewerCardView : uint32_t
    {
        BaseColor,
        WorldNormal,
        Coverage,
        Roughness,
        Metallic
    };

    struct LumenViewerCardInfo
    {
        uint32_t Index = 0;
        uint32_t InstanceID = UINT32_MAX;
        uint32_t AxisIndex = 0;
        Core::Float4 Center{};
        Core::Float4 AxisU{};
        Core::Float4 AxisV{};
        Core::Float4 Normal{};
        Core::Float4 Extent{};
        Core::UInt4 Tile{};
        Core::UInt2 AtlasSize{};
    };

    class LumenViewerRendererState;

    struct LumenViewerRadiositySettings
    {
        uint32_t IterationLimit = 32;
        bool bEnabled = true;
        bool bPaused = false;
        bool bStep = false;
        bool bRestart = false;
    };

    struct LumenViewerWorldCacheSettings
    {
        bool bEnabled = true;
        int UpdateBudget = 24;
    };

    // Builds the Lumen stages into a caller-owned preview target and cache lifetime.
    class RENDERER_API LumenViewerRenderer
    {
    public:
        LumenViewerRenderer();
        ~LumenViewerRenderer();
        LumenViewerRenderer(const LumenViewerRenderer&) = delete;
        LumenViewerRenderer& operator=(const LumenViewerRenderer&) = delete;

        bool AddPasses(RenderCore::RenderGraphBuilder& builder, Engine::SceneInterface* scene,
            const Engine::SceneView& view, RenderCore::RenderTexture* output, ELumenViewerMode mode,
            uint32_t selectedCardIndex = UINT32_MAX, ELumenViewerCardView cardView = ELumenViewerCardView::BaseColor,
            const LumenViewerRadiositySettings& radiosity = {}, const LumenViewerWorldCacheSettings& worldCache = {});
        const std::vector<LumenViewerCardInfo>& GetSurfaceCards() const;
        uint32_t GetRadiosityIteration() const;
        void Reset(RenderCore::RenderTexture* lastOutput = nullptr);

    private:
        std::unique_ptr<LumenViewerRendererState> m_State;
    };
}
