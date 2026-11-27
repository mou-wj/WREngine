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
        ReflectionContribution,
        ReflectionHitInstance,
        ProbeTraceSource,
        VoxelLightingSlice,
        ProbeHistoryLength,
        ProbeVariance,
        ProbeComposite
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

    class GIViewerRendererState;

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
        bool bVoxelEnabled = false;
        int VoxelSlice = 3;
    };

    struct LumenViewerRayTracingInfo
    {
        uint32_t BLASCount = 0;
        uint32_t InstanceCount = 0;
        uint64_t TLASRevision = 0;
        bool bSupported = false;
        bool bActive = false;
    };

    struct LumenViewerDenoiserSettings
    {
        bool bUseVariance = false;
        int SpatialIterations = 3;
    };

    // Builds the Lumen stages into a caller-owned preview target and cache lifetime.
    class RENDERER_API GIViewerRenderer
    {
    public:
        GIViewerRenderer();
        ~GIViewerRenderer();
        GIViewerRenderer(const GIViewerRenderer&) = delete;
        GIViewerRenderer& operator=(const GIViewerRenderer&) = delete;

        bool AddPasses(RenderCore::RenderGraphBuilder& builder, Engine::SceneInterface* scene,
            const Engine::SceneView& view, RenderCore::RenderTexture* output, ELumenViewerMode mode,
            uint32_t selectedCardIndex = UINT32_MAX, ELumenViewerCardView cardView = ELumenViewerCardView::BaseColor,
            const LumenViewerRadiositySettings& radiosity = {}, const LumenViewerWorldCacheSettings& worldCache = {},
            bool bUseHardwareTracing = false, const LumenViewerDenoiserSettings& denoiser = {});
        const std::vector<LumenViewerCardInfo>& GetSurfaceCards() const;
        uint32_t GetRadiosityIteration() const;
        LumenViewerRayTracingInfo GetRayTracingInfo() const;
        void Reset(RenderCore::RenderTexture* lastOutput = nullptr);

    private:
        std::unique_ptr<GIViewerRendererState> m_State;
    };
}
