#include "EditorVolumeViewer.h"
#include "../../../Framework/Renderer/Private/Volume/VolumeRendering.h"
#include "Camera.h"
#include "EngineGlobal.h"
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
#include <cmath>
#include <vector>

namespace Editor
{
    class VolumeViewerPreview final : public Engine::RenderTarget
    {
    public:
        ~VolumeViewerPreview() { Reset(); }

        RenderCore::RenderTexture* GetRenderTarget() override { return m_Color.get(); }

        void Reset()
        {
            WaitForDraw();
            m_Color.reset();
            if (m_Scene)
            {
                for (const auto& mesh : m_Meshes)
                {
                    m_Scene->RemovePrimitive(mesh.get());
                }
                m_Scene->RemoveLight(m_Directional.get());
                m_Scene->FlushPendingUpdates();
                m_Meshes.clear();
                m_Assets.clear();
                m_Directional.reset();
                m_Scene.reset();
            }
        }

        void ResetCamera()
        {
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
            if (!m_Color || m_Color->GetRHI()->GetDesc().Width != width || m_Color->GetRHI()->GetDesc().Height != height)
            {
                WaitForDraw();
                RHI::RHITextureDesc desc{};
                desc.Width = width;
                desc.Height = height;
                desc.Format = RHI::ERHIFormat::R32G32B32A32_Float;
                desc.Usage = RHI::ERHITextureCreateFlag::RenderTarget | RHI::ERHITextureCreateFlag::ShaderResource |
                    RHI::ERHITextureCreateFlag::UAV | RHI::ERHITextureCreateFlag::TransferSrc;
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
            view.FrameIndex = m_FrameIndex++;
            view.FrameTimeSeconds = ImGui::GetTime();
            view.Flags = Engine::VF_EnableShadows;
            Engine::SceneViewFamily family;
            family.Scene = m_Scene.get();
            family.RenderTarget = this;
            family.AddView(std::move(view));
            family.BuildAllViews();
            Engine::GetRenderModuleInstance()->BeginRender(&family);
        }

        Renderer::VolumeMedium Medium;

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
            m_Directional->SetIntensity(3.0f);
            m_Directional->SetCastShadow(true);
            m_Scene->AddLight(m_Directional.get());
            m_Scene->FlushPendingUpdates();
            ResetCamera();
        }

        std::unique_ptr<Engine::SceneInterface> m_Scene;
        std::vector<std::shared_ptr<Engine::StaticMeshAsset>> m_Assets;
        std::vector<std::unique_ptr<Engine::StaticMeshComponent>> m_Meshes;
        std::unique_ptr<Engine::DirectionalLightComponent> m_Directional;
        std::unique_ptr<RenderCore::RenderTexture> m_Color;
        Engine::Camera m_Camera;
        Core::Float3 m_Focus{0, 0.8f, 0};
        float m_Yaw = 0.62f;
        float m_Pitch = 0.31f;
        float m_Distance = 12.0f;
        uint64_t m_FrameIndex = 0;
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

    void EditorVolumeViewer::Draw()
    {
        if (!m_bOpen)
        {
            return;
        }
        ImGui::SetNextWindowSize({1000, 650}, ImGuiCond_FirstUseEver);
        m_bVisible = ImGui::Begin("Volume Viewer", &m_bOpen);
        const auto position = ImGui::GetWindowPos();
        const auto windowSize = ImGui::GetWindowSize();
        m_WindowGeometry = {position.x, position.y, windowSize.x, windowSize.y};
        if (m_bOpen && m_bVisible)
        {
            if (ImGui::Button("Reset camera"))
            {
                m_Preview->ResetCamera();
            }
            ImGui::SameLine();
            ImGui::TextDisabled("Right drag: orbit   Middle drag: pan   Wheel: zoom");
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
