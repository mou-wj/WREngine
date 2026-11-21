#include "LumenViewerRenderer.h"

#include "DefferedSceneRenderer.h"
#include "SceneRenderer.h"
#include "Scene.h"
#include "SceneShaderParameters.h"
#include "GBufferInfo.h"
#include "StaticMeshProcess.h"
#include "LocalVertexFactory.h"
#include "GI/ScreenProbeGather.h"
#include "GI/SurfaceCacheMaterial.h"
#include "GI/SurfaceCacheDirectLighting.h"
#include "GI/SurfaceCacheRadiosity.h"
#include "RHIApi.h"

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
                mode == ELumenViewerMode::IndirectDiffuse;
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
        uint32_t selectedCardIndex, ELumenViewerCardView cardView, const LumenViewerRadiositySettings& radiositySettings)
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
        const auto depthUsage = RHI::ERHITextureCreateFlag::DepthStencil | RHI::ERHITextureCreateFlag::ShaderResource;
        const auto depth = CreateTexture(builder, "LumenViewer.Depth", outputDesc.Width, outputDesc.Height,
            RHI::ERHIFormat::D32_Float, depthUsage);
        const uint32_t maxMipLevel = std::max(1u, static_cast<uint32_t>(std::log2(std::max(outputDesc.Width, outputDesc.Height))));
        const auto pyramidUsage = depthUsage | RHI::ERHITextureCreateFlag::UAV;
        const auto depthPyramid = CreateTexture(builder, "LumenViewer.DepthPyramid", outputDesc.Width,
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
        sceneRenderer->Build(builder);
        Engine::LocalVertexFactoryInstanceManager::Get().UpdateGPUResources();
        scene->UpdateGlobalDistanceFieldIfNeeded();

        if (mode == ELumenViewerMode::Scene)
        {
            return true;
        }

        auto instances = SurfaceCacheMaterial::GatherSceneInstances(*scene);
        std::ranges::sort(instances, {}, &SurfaceCacheMeshInstance::InstanceID);
        if (m_State->Material && !SameInstances(m_State->Material->GetInstances(), instances))
        {
            Reset(output);
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

        const bool bScreenProbe = NeedsScreenProbe(mode);
        const bool bRadiosityView = IsRadiosityView(mode);
        if (bScreenProbe || bRadiosityView || mode == ELumenViewerMode::DirectLightingAtlas)
        {
            const auto lights = SurfaceCacheDirectLighting::GatherSceneLights(*scene);
            if (!m_State->Lighting->IsValidFor(*m_State->Material) || lights != m_State->Lights)
            {
                m_State->Lighting->AddUpdatePasses(builder, *m_State->Material, lights);
                m_State->Lights = lights;
                if (m_State->Radiosity)
                {
                    m_State->Radiosity->Invalidate();
                }
            }
        }
        if (m_State->Radiosity && radiositySettings.bRestart)
        {
            m_State->Radiosity->Invalidate();
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

        if (!bScreenProbe && !bRadiosityView)
        {
            return true;
        }

        const auto direct = m_State->Lighting->Register(builder, *m_State->Material);
        const bool bUseRadiosity = bRadiosityView || (bScreenProbe && radiositySettings.bEnabled);
        if (bUseRadiosity && !m_State->Radiosity)
        {
            m_State->Radiosity = std::make_unique<SurfaceCacheRadiosity>();
        }
        if (bUseRadiosity && (radiositySettings.bStep || (!radiositySettings.bPaused &&
            m_State->Radiosity->GetIteration() < radiositySettings.IterationLimit)))
        {
            m_State->Radiosity->AddIteration(builder, material, direct);
        }
        if (bRadiosityView)
        {
            m_State->Radiosity->AddDebugPass(builder, direct, color, mode == ELumenViewerMode::RadiosityIterationDelta);
            return true;
        }

        ScreenProbeGatherPassInput probeInput;
        probeInput.GBufferA = gbuffers[0];
        probeInput.GBufferB = gbuffers[1];
        probeInput.GBufferC = gbuffers[2];
        probeInput.Depth = depth;
        probeInput.InvViewProj = views.GetViews().front().InvViewProjectionMatrix;
        probeInput.CameraPosition = views.GetViews().front().CameraWorldPos;
        BuildEvnIBLLightParameters(scene, builder, probeInput.Environment);
        probeInput.bHasEnvironment = scene->GetGPUResourceInfo().LightResourceInfo.IBLSpecularTexture != nullptr;
        const auto lighting = bUseRadiosity ? m_State->Radiosity->Register(builder, direct) : direct;
        if (mode == ELumenViewerMode::IndirectDiffuse)
        {
            probeInput.IndirectDiffuseOutput = color;
            probeInput.DebugMode = 4;
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

    uint32_t LumenViewerRenderer::GetRadiosityIteration() const
    {
        return m_State->Radiosity ? m_State->Radiosity->GetIteration() : 0;
    }
}
