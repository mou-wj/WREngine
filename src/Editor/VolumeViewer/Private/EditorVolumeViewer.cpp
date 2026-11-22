#include "EditorVolumeViewer.h"
#include "../../../Framework/Renderer/Private/Volume/AtmosphereRender.h"
#include "../../../Framework/Renderer/Private/Volume/VolumeRendering.h"
#include "../../../Framework/Renderer/Private/Volume/WaterRender.h"
#include "Camera.h"
#include "EngineGlobal.h"
#include "EnvironmentLightResourceManager.h"
#include "LightComponent.h"
#include "RHIApi.h"
#include "SceneInterface.h"
#include "SceneView.h"
#include "Shape.h"
#include "StaticMeshAsset.h"
#include "StaticMeshComponent.h"
#include "Viewport.h"
#include "imgui.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <vector>

namespace Editor
{
    namespace
    {
        void DrawMediumControls(Renderer::VolumeMedium& medium)
        {
            ImGui::ColorEdit3("Scattering coefficient", &medium.Scattering.X, ImGuiColorEditFlags_Float);
            ImGui::ColorEdit3("Absorption coefficient", &medium.Absorption.X, ImGuiColorEditFlags_Float);
            ImGui::ColorEdit3("Emission per unit", &medium.Emission.X, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
            ImGui::SliderFloat("Anisotropy", &medium.Anisotropy, -0.95f, 0.95f, "%.2f");
        }
    }

    class VolumeViewerPreview final : public Engine::RenderTarget
    {
    public:
        VolumeViewerPreview()
        {
            Settings.bSpatialReconstruction = true;
            Renderer::LocalVolume box;
            box.Position = {-0.8f, 0.8f, -0.6f};
            box.RotationDegrees = {0, 24, 0};
            box.HalfExtent = {2.3f, 1.0f, 2.6f};
            box.Medium.Density = 0.28f;
            box.DensityField.Mode = Renderer::EVolumeDensityMode::Noise;
            box.DensityField.Velocity = {0.04f, 0.01f, 0.02f};
            Settings.LocalVolumes.push_back(box);

            Renderer::LocalVolume sphere;
            sphere.Shape = Renderer::EVolumeShape::Sphere;
            sphere.Position = {1.5f, 0.9f, 0.7f};
            sphere.HalfExtent = {2.1f, 1.3f, 1.6f};
            sphere.Medium.Density = 0.32f;
            sphere.Medium.Scattering = {0.32f, 0.58f, 0.86f};
            sphere.DensityField.Mode = Renderer::EVolumeDensityMode::Texture;
            sphere.DensityField.Velocity = {-0.03f, 0.01f, 0.04f};
            Settings.LocalVolumes.push_back(sphere);
        }
        ~VolumeViewerPreview() { Reset(); }

        RenderCore::RenderTexture* GetRenderTarget() override { return m_Color.get(); }

        void Reset()
        {
            WaitForDraw();
            m_History.Release();
            m_Color.reset();
            m_DemoDensity.reset();
            if (m_Scene)
            {
                for (const auto& mesh : m_Meshes)
                {
                    m_Scene->RemovePrimitive(mesh.get());
                }
                m_Scene->RemoveLight(m_Directional.get());
                m_Scene->RemoveLight(m_Point.get());
                m_Scene->RemoveLight(m_Spot.get());
                m_Scene->RemoveLight(m_Sky.get());
                m_Scene->FlushPendingUpdates();
                m_Meshes.clear();
                m_Assets.clear();
                m_Directional.reset();
                m_Point.reset();
                m_Spot.reset();
                m_Sky.reset();
                m_Scene.reset();
                Engine::EnvironmentLightResourceManager::Get().ReleaseUnused();
            }
        }

        void ResetCamera()
        {
            m_History.Invalidate();
            m_Yaw = 0.62f;
            m_Pitch = 0.31f;
            m_Distance = 12.0f;
            m_Focus = {0, 0.8f, 0};
        }

        void UpdateCamera(const ImGuiIO& input, bool bHovered)
        {
            const Core::Float3 orbit = OrbitDirection();
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            {
                m_bOrbiting = bHovered;
            }
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
            {
                m_bPanning = bHovered;
            }
            m_bOrbiting = m_bOrbiting && ImGui::IsMouseDown(ImGuiMouseButton_Right);
            m_bPanning = m_bPanning && ImGui::IsMouseDown(ImGuiMouseButton_Middle);
            if (m_bOrbiting)
            {
                m_Yaw = std::remainder(m_Yaw - input.MouseDelta.x * 0.008f, 6.2831853f);
                m_Pitch = std::clamp(m_Pitch + input.MouseDelta.y * 0.008f, -1.5f, 1.5f);
            }
            if (m_bPanning)
            {
                const auto right = Core::Normalize(Core::Cross(Core::Float3{0, 1, 0}, orbit));
                const auto up = Core::Cross(orbit, right);
                m_Focus = m_Focus + (right * -input.MouseDelta.x + up * input.MouseDelta.y) * (0.002f * m_Distance);
            }
            if (bHovered && input.MouseWheel != 0.0f)
            {
                m_Distance = std::clamp(m_Distance * std::exp(-input.MouseWheel * 0.16f), 1.0f, 80.0f);
            }
        }

        void Render(uint32_t width, uint32_t height)
        {
            if (!m_Scene)
            {
                InitializeScene();
            }
            const float elevation = Core::DegToRad(SunElevation);
            const float azimuth = Core::DegToRad(SunAzimuth);
            const Core::Float3 toSun{std::cos(elevation) * std::sin(azimuth), std::sin(elevation),
                std::cos(elevation) * std::cos(azimuth)};
            const auto lightDirection = Atmosphere.bEnabled ? toSun * -1.0f : Core::Normalize(Core::Float3{0.5f, -1.0f, -0.6f});
            if (m_Directional->GetIntensity() != LightIntensities.X || m_Point->GetIntensity() != LightIntensities.Y ||
                m_Spot->GetIntensity() != LightIntensities.Z ||
                (m_Directional->GetDirection() - lightDirection).LengthSquared() > 1e-8f)
            {
                WaitForDraw();
                m_Directional->SetIntensity(LightIntensities.X);
                m_Directional->SetDirection(lightDirection);
                m_Point->SetIntensity(LightIntensities.Y);
                m_Spot->SetIntensity(LightIntensities.Z);
                m_Scene->FlushPendingUpdates();
            }
            if (!m_Color || m_Color->GetRHI()->GetDesc().Width != width || m_Color->GetRHI()->GetDesc().Height != height)
            {
                WaitForDraw();
                RHI::RHITextureDesc desc{};
                desc.Width = width;
                desc.Height = height;
                desc.Format = RHI::ERHIFormat::R32G32B32A32_Float;
                desc.Usage = RHI::ERHITextureCreateFlag::RenderTarget | RHI::ERHITextureCreateFlag::ShaderResource |
                    RHI::ERHITextureCreateFlag::UAV | RHI::ERHITextureCreateFlag::TransferSrc |
                    RHI::ERHITextureCreateFlag::TransferDest;
                m_Color = std::make_unique<RenderCore::RenderTexture>(desc);
                m_Color->InitRHIResource();
            }

            const auto direction = OrbitDirection();
            m_Camera.SetPosition(m_Focus + direction * m_Distance);
            m_Camera.SetTarget(m_Focus);
            m_Camera.SetUp({0, 1, 0});
            m_Camera.SetPerspective(Core::DegToRad(45.0f), float(width) / float(height), 0.1f, 100.0f);
            Engine::SceneView view;
            view.Viewport = {0, 0, int(width), int(height)};
            view.RenderTargetWidth = float(width);
            view.RenderTargetHeight = float(height);
            view.CameraWorldPos = m_Camera.GetPosition();
            view.CameraWorldDir = direction * -1.0f;
            view.NearClip = m_Camera.GetNearPlane();
            view.FarClip = m_Camera.GetFarPlane();
            view.ViewMatrix = m_Camera.GetViewMatrix();
            view.ProjectionMatrix = m_Camera.GetProjectionMatrix();
            view.ViewProjectionMatrix = m_Camera.GetViewProjectionMatrix();
            view.InvViewProjectionMatrix = m_Camera.GetInverseViewProjectionMatrix();
            view.FrameIndex = m_FrameIndex++;
            if (bAnimateDensity)
            {
                m_DensityTime += ImGui::GetIO().DeltaTime;
            }
            view.FrameTimeSeconds = m_DensityTime;
            view.Flags = Engine::VF_EnableShadows;
            auto frameSettings = Settings;
            for (auto& local : frameSettings.LocalVolumes)
            {
                if (local.DensityField.Mode == Renderer::EVolumeDensityMode::Texture && !local.DensityField.Texture)
                {
                    if (!m_DemoDensity)
                    {
                        m_DemoDensity = Renderer::CreateVolumePreviewDensityTexture();
                    }
                    local.DensityField.Texture = m_DemoDensity.get();
                }
            }
            auto water = Water;
            if (water.bEnabled && bAnimateWater)
            {
                m_WaterTime += ImGui::GetIO().DeltaTime;
            }
            water.TimeSeconds = static_cast<float>(m_WaterTime);
            view.DrawExtensions.push_back({[this, volume = std::move(frameSettings), atmosphere = Atmosphere, water](
                RenderCore::RenderGraphBuilder& graph, const Engine::SceneView& sceneView, RenderCore::RenderGraphTextureRef color,
                RenderCore::RenderGraphTextureRef depth)
            {
                if (atmosphere.bEnabled || water.bEnabled)
                {
                    const auto lighting = Renderer::PrepareAtmosphere(graph, atmosphere, m_Scene.get());
                    const bool bAtmosphereDebug = atmosphere.bEnabled && atmosphere.View != Renderer::EAtmosphereView::Composite;
                    if (water.bEnabled && !bAtmosphereDebug)
                    {
                        depth = Renderer::AddWaterPass(graph, sceneView, color, depth, water, lighting, m_Scene.get());
                        if (water.View != Renderer::EWaterView::Composite)
                        {
                            m_History.Invalidate();
                            Stats = {};
                            return;
                        }
                    }
                    Renderer::AddAtmospherePass(graph, sceneView, color, depth, atmosphere, lighting);
                    if (bAtmosphereDebug)
                    {
                        m_History.Invalidate();
                        Stats = {};
                        return;
                    }
                }
                Renderer::AddVolumePasses(graph, sceneView, depth, color, volume, m_Scene.get(), &m_History, &Stats);
            }});
            Engine::SceneViewFamily family;
            family.Scene = m_Scene.get();
            family.RenderTarget = this;
            family.AddView(std::move(view));
            family.BuildAllViews();
            if (bMeasureCompletedFrame)
            {
                WaitForDraw();
            }
            const auto start = std::chrono::steady_clock::now();
            Engine::GetRenderModuleInstance()->BeginRender(&family);
            if (bMeasureCompletedFrame)
            {
                WaitForDraw();
                CompletedFrameMilliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
                bMeasureCompletedFrame = false;
            }
        }

        void SetQualityPreset(bool bOptimized)
        {
            ResetCamera();
            bAnimateDensity = false;
            m_DensityTime = 0;
            Settings.View = Renderer::EVolumeView::Composite;
            Settings.FroxelTileSize = 8;
            Settings.FroxelSliceCount = 32;
            Settings.bSpatialReconstruction = bOptimized;
            Settings.bJitter = false;
            Settings.HistoryWeight = bOptimized ? 0.85f : 0.0f;
            if (!bOptimized)
            {
                m_History.Release();
            }
        }

        Renderer::VolumeRenderSettings Settings;
        Renderer::AtmosphereRenderSettings Atmosphere;
        Renderer::WaterRenderSettings Water;
        Renderer::VolumeRenderStats Stats;
        Core::Float3 LightIntensities{3, 20, 45};
        float SunElevation = 52.0f;
        float SunAzimuth = -40.0f;
        double CompletedFrameMilliseconds = 0;
        bool bMeasureCompletedFrame = false;
        bool bAnimateDensity = false;
        bool bAnimateWater = true;

    private:
        Core::Float3 OrbitDirection() const
        {
            return {std::cos(m_Pitch) * std::sin(m_Yaw), std::sin(m_Pitch), std::cos(m_Pitch) * std::cos(m_Yaw)};
        }

        void WaitForDraw() const
        {
            if (m_Color && RHI::GRHIApi)
            {
                const auto fence = m_Color->GetTracker().GetLastAccessFence();
                if (fence.Value)
                {
                    RHI::GRHIApi->GetQueue(fence.QueueType)->WaitValue(fence.Value);
                }
            }
        }

        void AddMesh(Engine::EBasicMeshShape shape, Core::Float3 location, Core::Float3 scale)
        {
            m_Assets.push_back(Engine::StaticMeshAsset::CreateShape(shape));
            auto mesh = std::make_unique<Engine::StaticMeshComponent>();
            mesh->SetStaticMesh(m_Assets.back()->GetMesh());
            mesh->SetWorldLocation(location);
            mesh->SetLocalScale(scale);
            m_Meshes.push_back(std::move(mesh));
            m_Scene->AddPrimitive(m_Meshes.back().get());
        }

        void InitializeScene()
        {
            m_Scene.reset(Engine::GetRenderModuleInstance()->AllocateScene());
            AddMesh(Engine::EBasicMeshShape::Cube, {0, -0.35f, 0}, {14, 0.6f, 14});
            AddMesh(Engine::EBasicMeshShape::Sphere, {1, 0.85f, 1}, {1.4f, 1.4f, 1.4f});
            AddMesh(Engine::EBasicMeshShape::Cube, {-1.6f, 1.1f, 0}, {0.7f, 2.3f, 0.7f});
            AddMesh(Engine::EBasicMeshShape::Cube, {0.7f, 0.65f, -3}, {1.3f, 1.4f, 1.3f});

            m_Directional = std::make_unique<Engine::DirectionalLightComponent>();
            m_Directional->SetDirection({0.5f, -1.0f, -0.6f});
            m_Directional->SetColor({1.0f, 0.91f, 0.79f});
            m_Directional->SetIntensity(LightIntensities.X);
            m_Directional->SetCastShadow(true);
            m_Scene->AddLight(m_Directional.get());

            m_Point = std::make_unique<Engine::PointLightComponent>();
            m_Point->SetWorldLocation({2.1f, 1.8f, 0.4f});
            m_Point->SetColor({0.4f, 0.65f, 1.0f});
            m_Point->SetIntensity(LightIntensities.Y);
            m_Point->SetAttenuationRadius(6.0f);
            m_Point->SetCastShadow(true);
            m_Scene->AddLight(m_Point.get());

            m_Spot = std::make_unique<Engine::SpotLightComponent>();
            m_Spot->SetWorldLocation({-2.8f, 4.0f, -0.3f});
            m_Spot->SetDirection({0.45f, -1.0f, 0.3f});
            m_Spot->SetColor({1.0f, 0.45f, 0.24f});
            m_Spot->SetIntensity(LightIntensities.Z);
            m_Spot->SetAttenuationRadius(10.0f);
            m_Spot->SetInnerConeAngle(13.0f);
            m_Spot->SetOuterConeAngle(22.0f);
            m_Spot->SetCastShadow(true);
            m_Scene->AddLight(m_Spot.get());
            m_Sky = std::make_unique<Engine::SkyLightComponent>();
            auto& environments = Engine::EnvironmentLightResourceManager::Get();
            environments.ApplyEnvironment(*m_Sky, environments.GetDefaultEnvironment());
            m_Scene->AddLight(m_Sky.get());
            m_Scene->FlushPendingUpdates();
            ResetCamera();
        }

        std::unique_ptr<Engine::SceneInterface> m_Scene;
        std::vector<std::shared_ptr<Engine::StaticMeshAsset>> m_Assets;
        std::vector<std::unique_ptr<Engine::StaticMeshComponent>> m_Meshes;
        std::unique_ptr<Engine::DirectionalLightComponent> m_Directional;
        std::unique_ptr<Engine::PointLightComponent> m_Point;
        std::unique_ptr<Engine::SpotLightComponent> m_Spot;
        std::unique_ptr<Engine::SkyLightComponent> m_Sky;
        std::unique_ptr<RenderCore::RenderTexture> m_Color;
        std::shared_ptr<RenderCore::RenderTexture> m_DemoDensity;
        Renderer::VolumeRenderHistory m_History;
        Engine::Camera m_Camera;
        Core::Float3 m_Focus{0, 0.8f, 0};
        float m_Yaw = 0.62f;
        float m_Pitch = 0.31f;
        float m_Distance = 12.0f;
        uint64_t m_FrameIndex = 0;
        double m_DensityTime = 0;
        double m_WaterTime = 0;
        bool m_bOrbiting = false;
        bool m_bPanning = false;
    };

    EditorVolumeViewer::EditorVolumeViewer() : m_Preview(std::make_unique<VolumeViewerPreview>()) {}
    EditorVolumeViewer::~EditorVolumeViewer() = default;

    void EditorVolumeViewer::Open() { m_bOpen = true; }

    bool EditorVolumeViewer::HitTest(float x, float y) const
    {
        return m_bOpen && m_bVisible && x >= m_WindowGeometry.X && y >= m_WindowGeometry.Y &&
            x < m_WindowGeometry.X + m_WindowGeometry.Width && y < m_WindowGeometry.Y + m_WindowGeometry.Height;
    }

    void EditorVolumeViewer::DrawOptions()
    {
        ImGui::PushItemWidth(140.0f);
        if (ImGui::Button("Reset camera"))
        {
            m_Preview->ResetCamera();
        }
        ImGui::TextDisabled("RMB orbit   MMB pan   Wheel zoom");
        auto& settings = m_Preview->Settings;
        ImGui::Checkbox("Enable volume", &settings.bEnabled);
        const char* views[] = {"Composite", "Scattering", "Transmittance", "Density", "Froxels", "Direct lighting",
            "Light visibility", "Local overlap", "Indirect lighting", "Density sources", "History weight"};
        int mode = static_cast<int>(settings.View);
        ImGui::SetNextItemWidth(170.0f);
        if (ImGui::Combo("View", &mode, views, IM_ARRAYSIZE(views)))
        {
            settings.View = static_cast<Renderer::EVolumeView>(mode);
        }
        ImGui::SliderFloat("Global density", &settings.Medium.Density, 0.0f, 0.4f, "%.3f");
        if (ImGui::CollapsingHeader("Atmosphere"))
        {
            auto& atmosphere = m_Preview->Atmosphere;
            ImGui::Checkbox("Enable atmosphere", &atmosphere.bEnabled);
            ImGui::Checkbox("Aerial perspective", &atmosphere.bAerialPerspective);
            const char* views[] = {"Composite", "In-scattering", "Transmittance"};
            int view = static_cast<int>(atmosphere.View);
            if (ImGui::Combo("Atmosphere view", &view, views, IM_ARRAYSIZE(views)))
            {
                atmosphere.View = static_cast<Renderer::EAtmosphereView>(view);
            }
            ImGui::SliderFloat("Sun elevation", &m_Preview->SunElevation, -20.0f, 90.0f, "%.1f deg");
            ImGui::SliderFloat("Sun azimuth", &m_Preview->SunAzimuth, -180.0f, 180.0f, "%.1f deg");
            ImGui::SliderFloat("Sun intensity", &m_Preview->LightIntensities.X, 0.0f, 10.0f);
            ImGui::SliderFloat("Air density", &atmosphere.Density, 0.0f, 4.0f);
            ImGui::DragFloat("Planet radius (km)", &atmosphere.PlanetRadiusKm, 20.0f, 100.0f, 15000.0f);
            ImGui::DragFloat("Air height (km)", &atmosphere.AtmosphereHeightKm, 1.0f, 10.0f, 150.0f);
            ImGui::SliderFloat("Km / scene unit", &atmosphere.WorldToKm, 0.001f, 1.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
            ImGui::DragFloat3("Rayleigh / km", &atmosphere.RayleighScattering.X, 0.0002f, 0.0f, 1.0f, "%.5f");
            ImGui::DragFloat3("Mie / km", &atmosphere.MieScattering.X, 0.0002f, 0.0f, 1.0f, "%.5f");
            ImGui::DragFloat3("Absorption / km", &atmosphere.Absorption.X, 0.0002f, 0.0f, 1.0f, "%.5f");
        }
        if (ImGui::CollapsingHeader("Water"))
        {
            auto& water = m_Preview->Water;
            ImGui::Checkbox("Enable water", &water.bEnabled);
            ImGui::Checkbox("Animate waves", &m_Preview->bAnimateWater);
            const char* views[] = {"Composite", "Normals", "Reflection", "Refraction", "Thickness"};
            int view = static_cast<int>(water.View);
            if (ImGui::Combo("Water view", &view, views, IM_ARRAYSIZE(views)))
            {
                water.View = static_cast<Renderer::EWaterView>(view);
            }
            ImGui::DragFloat3("Water position", &water.Position.X, 0.05f);
            ImGui::DragFloat2("Water half extent", &water.HalfExtent.X, 0.1f, 0.1f, 80.0f);
            ImGui::SliderFloat("Water yaw", &water.YawDegrees, -180.0f, 180.0f, "%.1f deg");
            ImGui::SliderFloat("Wave direction", &water.WaveDirectionDegrees, -180.0f, 180.0f, "%.1f deg");
            ImGui::SliderFloat("Wave amplitude", &water.WaveAmplitude, 0.0f, 0.5f, "%.3f");
            ImGui::SliderFloat("Wave frequency", &water.WaveFrequency, 0.02f, 2.0f, "%.2f");
            ImGui::SliderFloat("Wave speed", &water.WaveSpeed, 0.0f, 5.0f, "%.2f");
            ImGui::SliderFloat("Normal strength", &water.NormalStrength, 0.0f, 3.0f);
            ImGui::ColorEdit3("Water color", &water.BodyColor.X, ImGuiColorEditFlags_Float);
            ImGui::DragFloat3("Water absorption", &water.Absorption.X, 0.01f, 0.0f, 4.0f);
            ImGui::SliderFloat("Water roughness", &water.Roughness, 0.05f, 1.0f);
            ImGui::SliderFloat("Reflection strength", &water.ReflectionStrength, 0.0f, 1.0f);
            ImGui::SliderFloat("Refraction strength", &water.RefractionStrength, 0.0f, 1.0f);
            ImGui::DragFloat("Depth limit", &water.MaxThickness, 0.1f, 0.1f, 80.0f);
        }
        if (ImGui::CollapsingHeader("Quality and timing"))
        {
            if (ImGui::Button("Reference preset"))
            {
                m_Preview->SetQualityPreset(false);
            }
            ImGui::SameLine();
            if (ImGui::Button("Optimized preset"))
            {
                m_Preview->SetQualityPreset(true);
            }
            if (ImGui::Button("Measure completed frame"))
            {
                m_Preview->bMeasureCompletedFrame = true;
            }
            ImGui::Checkbox("Depth-aware reconstruction", &settings.bSpatialReconstruction);
            ImGui::Checkbox("Froxel jitter", &settings.bJitter);
            ImGui::SliderFloat("History weight", &settings.HistoryWeight, 0.0f, 0.95f, "%.2f");
            int tileSize = int(settings.FroxelTileSize);
            int slices = int(settings.FroxelSliceCount);
            ImGui::SliderInt("Tile pixels", &tileSize, 4, 16);
            ImGui::SliderInt("Depth slices", &slices, 16, 64);
            settings.FroxelTileSize = uint32_t(tileSize);
            settings.FroxelSliceCount = uint32_t(slices);
            const auto& stats = m_Preview->Stats;
            ImGui::Text("Froxels %u x %u x %u", stats.FroxelSize.X, stats.FroxelSize.Y, stats.FroxelSize.Z);
            ImGui::Text("Primary rays %u x %u", stats.IntegrationSize.X, stats.IntegrationSize.Y);
            constexpr double bytesPerMiB = 1024.0 * 1024.0;
            ImGui::TextWrapped("Image payload %.1f MiB transient + %.1f MiB history",
                stats.TransientBytes / bytesPerMiB, stats.HistoryBytes / bytesPerMiB);
            ImGui::TextWrapped("Completed frame %.2f ms (CPU + queue wait)", m_Preview->CompletedFrameMilliseconds);
        }
        if (ImGui::CollapsingHeader("Medium"))
        {
            DrawMediumControls(settings.Medium);
            ImGui::SliderFloat("Range", &settings.MaxDistance, 2.0f, 100.0f, "%.1f");
        }
        if (ImGui::CollapsingHeader("Lights"))
        {
            ImGui::Checkbox("Volume shadows", &settings.bShadows);
            ImGui::Checkbox("Use sky environment", &settings.bUseSkyEnvironment);
            ImGui::SliderFloat("Sky intensity", &settings.EnvironmentIntensity, 0.0f, 5.0f);
            ImGui::CheckboxFlags("Directional contribution", &settings.LightMask, 1u);
            ImGui::CheckboxFlags("Point contribution", &settings.LightMask, 2u);
            ImGui::CheckboxFlags("Spot contribution", &settings.LightMask, 4u);
            ImGui::SliderFloat("Directional intensity", &m_Preview->LightIntensities.X, 0.0f, 10.0f);
            ImGui::SliderFloat("Point intensity", &m_Preview->LightIntensities.Y, 0.0f, 60.0f);
            ImGui::SliderFloat("Spot intensity", &m_Preview->LightIntensities.Z, 0.0f, 100.0f);
        }
        if (ImGui::CollapsingHeader("Local volumes"))
        {
            ImGui::Checkbox("Animate density", &m_Preview->bAnimateDensity);
            if (ImGui::Button("Add box"))
            {
                settings.LocalVolumes.emplace_back();
            }
            ImGui::SameLine();
            if (ImGui::Button("Add sphere"))
            {
                settings.LocalVolumes.emplace_back().Shape = Renderer::EVolumeShape::Sphere;
            }
            for (size_t index = 0; index < settings.LocalVolumes.size(); ++index)
            {
                auto& local = settings.LocalVolumes[index];
                ImGui::PushID(static_cast<int>(index));
                const char* label = local.Shape == Renderer::EVolumeShape::Box ? "Box" : "Sphere";
                const bool bExpanded = ImGui::TreeNode(label);
                bool bRemove = false;
                if (bExpanded)
                {
                    ImGui::DragFloat3("Position", &local.Position.X, 0.05f);
                    ImGui::DragFloat3("Rotation (degrees)", &local.RotationDegrees.X, 1.0f);
                    ImGui::DragFloat3("Half extents", &local.HalfExtent.X, 0.05f, 0.02f, 50.0f);
                    ImGui::DragFloat("Density", &local.Medium.Density, 0.005f, 0.0f, 2.0f);
                    auto& field = local.DensityField;
                    int densityMode = static_cast<int>(field.Mode);
                    const char* densityModes[] = {"Uniform", "Procedural noise", "3D texture"};
                    if (ImGui::Combo("Density source", &densityMode, densityModes, IM_ARRAYSIZE(densityModes)))
                    {
                        field.Mode = static_cast<Renderer::EVolumeDensityMode>(densityMode);
                    }
                    if (field.Mode != Renderer::EVolumeDensityMode::Uniform)
                    {
                        ImGui::DragFloat3("Density offset", &field.Offset.X, 0.01f);
                        ImGui::DragFloat3("Density velocity", &field.Velocity.X, 0.01f);
                        ImGui::DragFloat("Density scale", &field.Scale, 0.01f, 0.0f, 4.0f);
                        ImGui::DragFloat("Density bias", &field.Bias, 0.01f, -1.0f, 1.0f);
                        ImGui::DragFloat("Density threshold", &field.Threshold, 0.01f, 0.0f, 1.0f);
                        if (field.Mode == Renderer::EVolumeDensityMode::Noise)
                        {
                            ImGui::DragFloat("Noise frequency", &field.Frequency, 0.1f, 0.01f, 32.0f);
                        }
                    }
                    DrawMediumControls(local.Medium);
                    bRemove = ImGui::Button("Remove volume");
                    ImGui::TreePop();
                }
                ImGui::PopID();
                if (bRemove)
                {
                    settings.LocalVolumes.erase(settings.LocalVolumes.begin() + index);
                    break;
                }
            }
        }
        if (settings.View == Renderer::EVolumeView::Density || settings.View == Renderer::EVolumeView::Froxels ||
            settings.View == Renderer::EVolumeView::LightVisibility || settings.View == Renderer::EVolumeView::LocalOverlap ||
            settings.View == Renderer::EVolumeView::DensitySources)
        {
            int slice = settings.DebugSlice;
            ImGui::SliderInt("Slice", &slice, 0, int(settings.FroxelSliceCount) - 1);
            settings.DebugSlice = uint32_t(slice);
        }
        ImGui::PopItemWidth();
    }

    void EditorVolumeViewer::Draw()
    {
        if (!m_bOpen)
        {
            return;
        }
        ImGui::SetNextWindowSize({1000, 650}, ImGuiCond_FirstUseEver);
        m_bVisible = ImGui::Begin("Volume Viewer", &m_bOpen, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        const auto position = ImGui::GetWindowPos();
        const auto windowSize = ImGui::GetWindowSize();
        m_WindowGeometry = {position.x, position.y, windowSize.x, windowSize.y};
        if (m_bOpen && m_bVisible)
        {
            const float optionsWidth = std::min(342.0f, ImGui::GetContentRegionAvail().x * 0.38f);
            if (ImGui::BeginChild("Volume options", {optionsWidth, 0}, ImGuiChildFlags_Borders,
                ImGuiWindowFlags_AlwaysVerticalScrollbar | ImGuiWindowFlags_HorizontalScrollbar))
            {
                DrawOptions();
            }
            ImGui::EndChild();
            ImGui::SameLine();

            const auto available = ImGui::GetContentRegionAvail();
            if (available.x >= 16.0f && available.y >= 16.0f)
            {
                const float scale = std::min({1.0f, 1280.0f / available.x, 720.0f / available.y});
                m_Preview->Render(uint32_t(available.x * scale), uint32_t(available.y * scale));
                ImGui::Image(static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(m_Preview->GetRenderTarget())), available);
                m_Preview->UpdateCamera(ImGui::GetIO(), ImGui::IsItemHovered());
            }
        }
        ImGui::End();
        if (!m_bOpen)
        {
            m_Preview->Reset();
        }
    }
}
