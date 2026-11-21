#include "LumenViewerRenderer.h"

#include "DefferedSceneRenderer.h"
#include "SceneRenderer.h"
#include "Scene.h"
#include "SceneShaderParameters.h"
#include "GBufferInfo.h"
#include "StaticMeshProcess.h"
#include "LocalVertexFactory.h"
#include "GI/ScreenProbeGather.h"
#include "GI/ScreenProbeFilter.h"
#include "GI/Reflections.h"
#include "GI/GIRayTracing.h"
#include "GI/SurfaceCacheMaterial.h"
#include "GI/SurfaceCacheDirectLighting.h"
#include "GI/SurfaceCacheRadiosity.h"
#include "GI/WorldRadianceCache.h"
#include "RHIApi.h"
#include "DrawSkyPass.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace Renderer
{
    class LumenViewerRendererState
    {
    public:
        std::unique_ptr<SurfaceCacheMaterial> Material;
        std::unique_ptr<SurfaceCacheDirectLighting> Lighting;
        std::unique_ptr<SurfaceCacheRadiosity> Radiosity;
        std::unique_ptr<WorldRadianceCache> WorldCache;
        std::unique_ptr<ScreenProbeFilter> ProbeFilter;
        std::unique_ptr<GIRayTracingScene> HardwareScene;
        bool bHardwareActive = false;
        bool bHistoryUsesHardware = false;
        RenderCore::RenderTexture* HistoryEnvironment = nullptr;
        bool bHistoryUsesRadiosity = false;
        bool bHistoryUsesWorldCache = false;
        RenderCore::RenderTexture* Environment = nullptr;
        bool bWorldCacheUsesRadiosity = false;
        std::vector<SurfaceCacheLight> Lights;
        std::vector<LumenViewerCardInfo> Cards;
        Scene* CachedScene = nullptr;
        RenderCore::RenderTexture* LastOutput = nullptr;
        bool bCaptureScheduled = false;
    };

    namespace
    {
        void WaitForTexture(RenderCore::RenderTexture* texture)
        {
            if (!texture || !RHI::GRHIApi)
            {
                return;
            }

            const auto fence = texture->GetTracker().GetLastAccessFence();
            if (fence.Value != 0)
            {
                RHI::GRHIApi->GetQueue(fence.QueueType)->WaitValue(fence.Value);
            }
        }

        RenderCore::RenderGraphTextureRef CreateTexture(RenderCore::RenderGraphBuilder& builder,
            const char* name, uint32_t width, uint32_t height, RHI::ERHIFormat format,
            RHI::ERHITextureCreateFlags usage, uint32_t mipLevels = 1)
        {
            RenderCore::RenderGraphTextureDesc desc;
            desc.Width = width;
            desc.Height = height;
            desc.MipLevels = mipLevels;
            desc.Format = format;
            desc.Usage = usage;
            return builder.CreateTexture(name, desc);
        }

        bool IsRadiosityView(ELumenViewerMode mode)
        {
            return mode == ELumenViewerMode::RadiosityIndirectAtlas || mode == ELumenViewerMode::RadiosityIterationDelta;
        }

        bool NeedsScreenProbe(ELumenViewerMode mode)
        {
            return mode == ELumenViewerMode::ProbePlacement ||
                mode == ELumenViewerMode::ProbeRayDirections ||
                mode == ELumenViewerMode::ProbeHitRatio ||
                mode == ELumenViewerMode::ProbeRadiance ||
                mode == ELumenViewerMode::ProbeWorldCacheHits || mode == ELumenViewerMode::ProbeTraceSource;
        }

        bool NeedsProbeFilter(ELumenViewerMode mode)
        {
            return mode == ELumenViewerMode::IndirectDiffuse ||
                (mode >= ELumenViewerMode::ProbeRawDiffuse && mode <= ELumenViewerMode::ProbeReprojectionError);
        }

        EScreenProbeFilterView GetProbeFilterView(ELumenViewerMode mode)
        {
            switch (mode)
            {
            case ELumenViewerMode::ProbeRawDiffuse: return EScreenProbeFilterView::Raw;
            case ELumenViewerMode::ProbeTemporalDiffuse: return EScreenProbeFilterView::Temporal;
            case ELumenViewerMode::ProbeHistoryValidity: return EScreenProbeFilterView::HistoryValidity;
            case ELumenViewerMode::ProbeHistoryWeight: return EScreenProbeFilterView::HistoryWeight;
            case ELumenViewerMode::ProbeReprojectionError: return EScreenProbeFilterView::ReprojectionError;
            default: return EScreenProbeFilterView::Spatial;
            }
        }

        bool SameInstances(const std::vector<SurfaceCacheMeshInstance>& a, const std::vector<SurfaceCacheMeshInstance>& b)
        {
            return std::ranges::equal(a, b, [](const auto& lhs, const auto& rhs)
            {
                return lhs.InstanceID == rhs.InstanceID && lhs.RenderData == rhs.RenderData &&
                    std::ranges::equal(lhs.LocalToWorld.Elements, rhs.LocalToWorld.Elements) && lhs.Materials == rhs.Materials;
            });
        }
    }

    LumenViewerRenderer::LumenViewerRenderer()
        : m_State(std::make_unique<LumenViewerRendererState>())
    {
    }

    LumenViewerRenderer::~LumenViewerRenderer()
    {
        Reset(m_State ? m_State->LastOutput : nullptr);
    }

    void LumenViewerRenderer::Reset(RenderCore::RenderTexture* lastOutput)
    {
        if (!m_State)
        {
            return;
        }

        WaitForTexture(lastOutput ? lastOutput : m_State->LastOutput);
        m_State->HardwareScene.reset();
        m_State->bHardwareActive = false;
        m_State->ProbeFilter.reset();
        m_State->HistoryEnvironment = nullptr;
        m_State->WorldCache.reset();
        m_State->Environment = nullptr;
        m_State->Radiosity.reset();
        m_State->Lights.clear();
        if (m_State->Lighting)
        {
            m_State->Lighting->Release();
            m_State->Lighting.reset();
        }
        if (m_State->Material)
        {
            m_State->Material->Release();
            m_State->Material.reset();
        }

        m_State->CachedScene = nullptr;
        m_State->Cards.clear();
        m_State->LastOutput = nullptr;
        m_State->bCaptureScheduled = false;
    }

    bool LumenViewerRenderer::AddPasses(RenderCore::RenderGraphBuilder& builder, Engine::SceneInterface* sceneInterface,
        const Engine::SceneView& sourceView, RenderCore::RenderTexture* output, ELumenViewerMode mode,
        uint32_t selectedCardIndex, ELumenViewerCardView cardView, const LumenViewerRadiositySettings& radiositySettings,
        const LumenViewerWorldCacheSettings& worldCacheSettings, bool bUseHardwareTracing)
    {
        if (!sceneInterface || !output || !output->GetRHI())
        {
            return false;
        }

        auto* scene = dynamic_cast<Scene*>(sceneInterface);
        if (!scene)
        {
            return false;
        }

        const auto& outputDesc = output->GetRHI()->GetDesc();
        if (outputDesc.Width == 0 || outputDesc.Height == 0 || outputDesc.Format != RHI::ERHIFormat::R32G32B32A32_Float)
        {
            return false;
        }

        if (m_State->CachedScene && m_State->CachedScene != scene)
        {
            Reset(output);
        }

        m_State->LastOutput = output;
        m_State->bHardwareActive = false;
        const bool bFiltering = NeedsProbeFilter(mode);
        if (!bFiltering && m_State->ProbeFilter)
        {
            m_State->ProbeFilter->Invalidate();
        }
        Engine::SceneView view = sourceView;
        view.Viewport = {0, 0, static_cast<int>(outputDesc.Width), static_cast<int>(outputDesc.Height)};
        view.RenderTargetWidth = static_cast<float>(outputDesc.Width);
        view.RenderTargetHeight = static_cast<float>(outputDesc.Height);
        view.DrawExtensions.clear();
        const float aspect = static_cast<float>(outputDesc.Width) / static_cast<float>(outputDesc.Height);
        view.ProjectionMatrix(0, 0) = view.ProjectionMatrix(1, 1) / aspect;
        view.ViewProjectionMatrix = view.ProjectionMatrix * view.ViewMatrix;
        view.InvViewProjectionMatrix = Core::Inverse(view.ViewProjectionMatrix);

        Engine::SceneViewFamily views;
        views.Scene = scene;
        views.RenderTarget = nullptr;
        views.AddView(std::move(view));
        views.BuildAllViews();

        const auto color = builder.RegisterExternalTexture("LumenViewer.Output", output);
        const bool bReflectionView = mode >= ELumenViewerMode::ReflectionComposite && mode <= ELumenViewerMode::ReflectionHitInstance;
        auto instances = bReflectionView ? SurfaceCacheMaterial::GatherSceneInstances(*scene) : std::vector<SurfaceCacheMeshInstance>{};
        const bool bReflections = bReflectionView && !instances.empty();
        const auto depthUsage = RHI::ERHITextureCreateFlag::DepthStencil | RHI::ERHITextureCreateFlag::ShaderResource;
        const auto depth = CreateTexture(builder, "LumenViewer.Depth", outputDesc.Width, outputDesc.Height,
            RHI::ERHIFormat::D32_Float, depthUsage);
        const uint32_t maxMipLevel = std::max(1u, static_cast<uint32_t>(std::log2(std::max(outputDesc.Width, outputDesc.Height))));
        const auto pyramidUsage = depthUsage | RHI::ERHITextureCreateFlag::UAV;
        const auto depthPyramid = bReflections ? nullptr : CreateTexture(builder, "LumenViewer.DepthPyramid", outputDesc.Width,
            outputDesc.Height, RHI::ERHIFormat::D32_Float, pyramidUsage, maxMipLevel);

        const auto gbufferInfo = CreateGBufferInfo({});
        std::vector<RenderCore::RenderGraphTextureRef> gbuffers;
        gbuffers.reserve(gbufferInfo.NumTargets);
        for (int index = 0; index < gbufferInfo.NumTargets; ++index)
        {
            const auto usage = RHI::ERHITextureCreateFlag::RenderTarget | RHI::ERHITextureCreateFlag::ShaderResource;
            gbuffers.push_back(CreateTexture(builder, ("LumenViewer.GBuffer" + std::to_string(index)).c_str(),
                outputDesc.Width, outputDesc.Height, gbufferInfo.Targets[index].TargetFormat, usage));
        }

        Engine::LocalVertexFactoryInstanceManager::Get().BeginUpdateGPUResources();
        auto sceneRenderer = std::make_shared<DefferedSceneRenderer>();
        sceneRenderer->Scene = scene;
        sceneRenderer->Views = &views;
        sceneRenderer->SceneTextures.SceneColor = color;
        sceneRenderer->SceneTextures.SceneDepth = depth;
        sceneRenderer->SceneTextures.SceneDepthPyramid = depthPyramid;
        sceneRenderer->SceneTextures.GBufferA = gbuffers[0];
        sceneRenderer->SceneTextures.GBufferB = gbuffers[1];
        sceneRenderer->SceneTextures.GBufferC = gbuffers[2];
        sceneRenderer->BuildSceneLightShadowMap(scene, builder);
        sceneRenderer->UploadShadowMapInfo(scene, builder);
        RenderCore::RenderGraphTextureRef reflectionBase = nullptr;
        if (bReflections)
        {
            reflectionBase = sceneRenderer->BuildWithDeferredSpecular(builder);
        }
        else
        {
            sceneRenderer->Build(builder);
        }
        Engine::LocalVertexFactoryInstanceManager::Get().UpdateGPUResources();
        scene->UpdateGlobalDistanceFieldIfNeeded();

        if (bReflections && !reflectionBase)
        {
            return false;
        }

        if (mode == ELumenViewerMode::Scene)
        {
            return true;
        }

        if (!bReflectionView)
        {
            instances = SurfaceCacheMaterial::GatherSceneInstances(*scene);
        }
        std::ranges::sort(instances, {}, &SurfaceCacheMeshInstance::InstanceID);
        if (m_State->Material && !SameInstances(m_State->Material->GetInstances(), instances))
        {
            // Reset waits for the old preview before invalidating resources. Geometry can keep its BLAS.
            auto hardwareScene = std::move(m_State->HardwareScene);
            Reset(output);
            m_State->HardwareScene = std::move(hardwareScene);
            m_State->LastOutput = output;
        }
        if (!m_State->Material)
        {
            if (instances.empty())
            {
                return false;
            }

            m_State->Material = std::make_unique<SurfaceCacheMaterial>();
            m_State->Material->Initialize(instances);
            const auto atlas = m_State->Material->GetBaseColorAtlas()->GetRHI()->GetDesc();
            const auto& cards = m_State->Material->GetCards();
            m_State->Cards.reserve(cards.size());
            for (uint32_t index = 0; index < cards.size(); ++index)
            {
                const auto& card = cards[index];
                LumenViewerCardInfo info;
                info.Index = index;
                info.InstanceID = card.Owner.X;
                info.AxisIndex = card.Owner.Z;
                info.Center = card.Center;
                info.AxisU = card.AxisU;
                info.AxisV = card.AxisV;
                info.Normal = card.AxisN;
                info.Extent = card.Extent;
                info.Tile = card.Tile;
                info.AtlasSize = {atlas.Width, atlas.Height};
                m_State->Cards.push_back(info);
            }
            m_State->Lighting = std::make_unique<SurfaceCacheDirectLighting>();
            m_State->Lighting->Initialize(*m_State->Material);
            m_State->CachedScene = scene;
        }

        SurfaceCacheMaterialParameters material;
        if (!m_State->bCaptureScheduled)
        {
            material = m_State->Material->AddCapturePasses(builder);
            m_State->bCaptureScheduled = true;
        }
        else
        {
            material = m_State->Material->Register(builder);
        }

        const bool bScreenProbe = NeedsScreenProbe(mode) || bFiltering;
        if ((bScreenProbe || bReflections) && bUseHardwareTracing && GIRayTracingScene::IsSupported())
        {
            if (!m_State->HardwareScene)
            {
                m_State->HardwareScene = std::make_unique<GIRayTracingScene>();
            }
            m_State->bHardwareActive = m_State->HardwareScene->Update(instances);
        }
        const bool bRadiosityView = IsRadiosityView(mode);
        const bool bWorldCacheView = mode == ELumenViewerMode::WorldProbeAtlas;
        const bool bTraceReflectionHardware = bReflections && m_State->bHardwareActive;
        SurfaceCachePunctualLightParameters hardwareLights{};
        bool bLightingChanged = false;
        if (bScreenProbe || bRadiosityView || bWorldCacheView || bReflections || mode == ELumenViewerMode::DirectLightingAtlas)
        {
            const auto lights = SurfaceCacheDirectLighting::GatherSceneLights(*scene);
            if (!m_State->Lighting->IsValidFor(*m_State->Material) || lights != m_State->Lights)
            {
                m_State->Lighting->AddUpdatePasses(builder, *m_State->Material, lights, bTraceReflectionHardware ? &hardwareLights : nullptr);
                bLightingChanged = true;
                m_State->Lights = lights;
                if (m_State->Radiosity)
                {
                    m_State->Radiosity->Invalidate();
                }
                if (m_State->WorldCache)
                {
                    m_State->WorldCache->Invalidate();
                }
            }
        }
        if (m_State->Radiosity && radiositySettings.bRestart)
        {
            m_State->Radiosity->Invalidate();
        }
        if (m_State->WorldCache && radiositySettings.bRestart)
        {
            m_State->WorldCache->Invalidate();
        }

        if (mode == ELumenViewerMode::SurfaceBaseColor || mode == ELumenViewerMode::SurfaceWorldNormal ||
            mode == ELumenViewerMode::SurfaceCoverage || mode == ELumenViewerMode::SurfaceRoughness ||
            mode == ELumenViewerMode::SurfaceMetallic)
        {
            const uint32_t debugMode = mode == ELumenViewerMode::SurfaceBaseColor ? 0u :
                mode == ELumenViewerMode::SurfaceWorldNormal ? 1u :
                mode == ELumenViewerMode::SurfaceCoverage ? 2u : mode == ELumenViewerMode::SurfaceRoughness ? 3u : 4u;
            AddSurfaceCacheDebugPass(builder, material, color, debugMode, selectedCardIndex);
            return true;
        }

        if (mode == ELumenViewerMode::SurfaceCardAtlas)
        {
            if (selectedCardIndex < material.CardCount)
            {
                AddSurfaceCacheCardDebugPass(builder, material, selectedCardIndex,
                    static_cast<ESurfaceCardAtlasView>(cardView), color);
            }
            return true;
        }

        if (mode == ELumenViewerMode::DirectLightingAtlas)
        {
            const auto lighting = m_State->Lighting->Register(builder, *m_State->Material);
            AddSurfaceCacheLightingDebugPass(builder, lighting, color);
            return true;
        }

        if (!bScreenProbe && !bRadiosityView && !bWorldCacheView && !bReflections)
        {
            return true;
        }

        const auto direct = m_State->Lighting->Register(builder, *m_State->Material);
        const bool bUseRadiosity = bRadiosityView || radiositySettings.bEnabled;
        if (bUseRadiosity && !m_State->Radiosity)
        {
            m_State->Radiosity = std::make_unique<SurfaceCacheRadiosity>();
        }
        const bool bRadiosityAdvances = bUseRadiosity && (radiositySettings.bStep || (!radiositySettings.bPaused &&
            m_State->Radiosity->GetIteration() < radiositySettings.IterationLimit));
        if (bRadiosityAdvances)
        {
            m_State->Radiosity->AddIteration(builder, material, direct);
        }
        if (bRadiosityView)
        {
            m_State->Radiosity->AddDebugPass(builder, direct, color, mode == ELumenViewerMode::RadiosityIterationDelta);
            return true;
        }

        ScreenProbeGatherPassInput probeInput;
        probeInput.HardwareScene = m_State->bHardwareActive ? m_State->HardwareScene.get() : nullptr;
        probeInput.GBufferA = gbuffers[0];
        probeInput.GBufferB = gbuffers[1];
        probeInput.GBufferC = gbuffers[2];
        probeInput.Depth = depth;
        probeInput.InvViewProj = views.GetViews().front().InvViewProjectionMatrix;
        probeInput.CameraPosition = views.GetViews().front().CameraWorldPos;
        BuildEvnIBLLightParameters(scene, builder, probeInput.Environment);
        const auto& lightResources = scene->GetGPUResourceInfo().LightResourceInfo;
        auto* environment = lightResources.IBLDiffuseTexture ? lightResources.IBLSpecularTexture : nullptr;
        probeInput.bHasEnvironment = environment != nullptr;
        const auto lighting = bUseRadiosity ? m_State->Radiosity->Register(builder, direct) : direct;
        WorldRadianceCacheParameters worldCache{};
        if (bWorldCacheView || worldCacheSettings.bEnabled)
        {
            if (!m_State->WorldCache)
            {
                m_State->WorldCache = std::make_unique<WorldRadianceCache>();
            }
            if (m_State->Environment != environment || m_State->bWorldCacheUsesRadiosity != bUseRadiosity)
            {
                m_State->WorldCache->Invalidate();
                m_State->Environment = environment;
                m_State->bWorldCacheUsesRadiosity = bUseRadiosity;
            }
            worldCache = m_State->WorldCache->Update(builder, probeInput.CameraPosition,
                uint32_t(std::clamp(worldCacheSettings.UpdateBudget, 0, 128)), material, lighting,
                probeInput.Environment, probeInput.bHasEnvironment);
            if (bWorldCacheView)
            {
                WorldRadianceCache::AddDebugPass(builder, worldCache, color);
                return true;
            }
            probeInput.WorldCache = &worldCache;
        }
        if (reflectionBase)
        {
            auto previewView = views.GetViews().front();
            ReflectionsInput reflections;
            reflections.GBuffer.GBufferA = gbuffers[0];
            reflections.GBuffer.GBufferB = gbuffers[1];
            reflections.GBuffer.GBufferC = gbuffers[2];
            reflections.GBuffer.Depth = depth;
            reflections.GBuffer.PointSampler = RenderCore::GlobalNearestSampler.get();
            reflections.DeferredColor = reflectionBase;
            reflections.Output = color;
            reflections.ViewProjection = previewView.ViewProjectionMatrix;
            reflections.InverseViewProjection = previewView.InvViewProjectionMatrix;
            reflections.CameraPosition = previewView.CameraWorldPos;
            reflections.Environment = probeInput.Environment;
            reflections.bHasEnvironment = probeInput.bHasEnvironment;
            reflections.WorldCache = probeInput.WorldCache;
            reflections.HardwareScene = probeInput.HardwareScene;
            if (bTraceReflectionHardware)
            {
                if (!bLightingChanged)
                {
                    hardwareLights = m_State->Lighting->RegisterPunctualLights(builder);
                }
                reflections.HardwareLights = &hardwareLights;
            }
            reflections.View = static_cast<EReflectionView>(uint32_t(mode) - uint32_t(ELumenViewerMode::ReflectionComposite));
            AddReflectionsPasses(builder, reflections, material, lighting);
            if (reflections.View == EReflectionView::Composite && environment)
            {
                AddDrawSkyBoxPass(builder, environment, color, depth, previewView);
            }
            return true;
        }
        if (bFiltering)
        {
            if (!m_State->ProbeFilter)
            {
                m_State->ProbeFilter = std::make_unique<ScreenProbeFilter>();
            }
            if (bLightingChanged || radiositySettings.bRestart || m_State->HistoryEnvironment != environment ||
                m_State->bHistoryUsesRadiosity != bUseRadiosity || m_State->bHistoryUsesWorldCache != worldCacheSettings.bEnabled ||
                m_State->bHistoryUsesHardware != m_State->bHardwareActive)
            {
                m_State->ProbeFilter->Invalidate();
                m_State->HistoryEnvironment = environment;
                m_State->bHistoryUsesRadiosity = bUseRadiosity;
                m_State->bHistoryUsesWorldCache = worldCacheSettings.bEnabled;
                m_State->bHistoryUsesHardware = m_State->bHardwareActive;
            }
            probeInput.SamplePhase = m_State->ProbeFilter->GetSamplePhase();
            auto probes = AddScreenProbeGatherPasses(builder, material, lighting, probeInput);
            const auto& previewView = views.GetViews().front();
            ScreenProbeFilterInput filter;
            filter.RawGI = probes.IndirectDiffuse;
            filter.Output = color;
            filter.GBuffer.GBufferA = probeInput.GBufferA;
            filter.GBuffer.GBufferB = probeInput.GBufferB;
            filter.GBuffer.GBufferC = probeInput.GBufferC;
            filter.GBuffer.Depth = probeInput.Depth;
            filter.GBuffer.PointSampler = RenderCore::GlobalNearestSampler.get();
            filter.ViewProjection = previewView.ViewProjectionMatrix;
            filter.InverseViewProjection = previewView.InvViewProjectionMatrix;
            filter.CameraPosition = previewView.CameraWorldPos;
            filter.View = GetProbeFilterView(mode);
            if (bRadiosityAdvances || (probeInput.WorldCache && m_State->WorldCache->GetUpdatedProbeCount() != 0))
            {
                filter.HistoryWeightLimit = 0.72f;
            }
            m_State->ProbeFilter->AddPasses(builder, filter);
            return true;
        }
        else
        {
            probeInput.DebugOutput = color;
            switch (mode)
            {
            case ELumenViewerMode::ProbePlacement:
                probeInput.DebugMode = 0;
                break;
            case ELumenViewerMode::ProbeRayDirections:
                probeInput.DebugMode = 1;
                break;
            case ELumenViewerMode::ProbeHitRatio:
                probeInput.DebugMode = 2;
                break;
            case ELumenViewerMode::ProbeRadiance:
                probeInput.DebugMode = 3;
                break;
            case ELumenViewerMode::ProbeWorldCacheHits:
                probeInput.DebugMode = 5;
                break;
            case ELumenViewerMode::ProbeTraceSource:
                probeInput.DebugMode = 6;
                break;
            default:
                probeInput.DebugMode = 4;
                break;
            }
        }

        AddScreenProbeGatherPasses(builder, material, lighting, probeInput);
        return true;
    }

    const std::vector<LumenViewerCardInfo>& LumenViewerRenderer::GetSurfaceCards() const
    {
        return m_State->Cards;
    }

    LumenViewerRayTracingInfo LumenViewerRenderer::GetRayTracingInfo() const
    {
        const auto* scene = m_State->HardwareScene.get();
        return {scene ? scene->GetBLASCount() : 0u, scene ? scene->GetInstanceCount() : 0u,
            scene ? scene->GetTLASRevision() : 0u, GIRayTracingScene::IsSupported(), m_State->bHardwareActive};
    }

    uint32_t LumenViewerRenderer::GetRadiosityIteration() const
    {
        return m_State->Radiosity ? m_State->Radiosity->GetIteration() : 0;
    }
}
