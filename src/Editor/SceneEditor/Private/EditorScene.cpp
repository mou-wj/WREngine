#include "EditorScene.h"
#include "AssetManager.h"
#include "LightComponent.h"
#include "EngineGlobal.h"
#include "SceneInterface.h"
#include "StaticMeshComponent.h"
#include "MaterialAsset.h"
#include "PathInfo.h"
#include "Transform.hpp"
#include "RHIApi.h"
#include "RenderResource.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace Editor
{
    EditorScene::EditorScene() = default;
    EditorScene::~EditorScene() { Shutdown(); }

    void EditorScene::Initialize()
    {
        m_Scene.reset(Engine::GetRenderModuleInstance()->AllocateScene());
        m_SkyAsset = Engine::AssetManager::Get().LoadSync<Engine::SkyLightAsset>(Core::GetProjectDir() + "/resources/pic/DaySkyHDRI046A_1K-TONEMAPPED.jpg");

        auto directional = std::make_unique<Engine::DirectionalLightComponent>();
        directional->SetWorldLocation({5, 5, 5});
        directional->SetDirection({-1, -1, -1});
        directional->SetColor({1, 1, 1});
        directional->SetIntensity(1);
        directional->SetCastShadow(true);
        m_Lights.push_back(std::move(directional));

        auto point = std::make_unique<Engine::PointLightComponent>();
        point->SetWorldLocation({5, 4, 5});
        point->SetColor({1, 0.5f, 0.2f});
        point->SetIntensity(1);
        point->SetAttenuationRadius(10);
        point->SetCastShadow(true);
        m_Lights.push_back(std::move(point));

        auto spot = std::make_unique<Engine::SpotLightComponent>();
        spot->SetWorldLocation({3, 2, 0});
        spot->SetDirection({-1, -0.5f, 0});
        spot->SetColor({0.2f, 0.5f, 1});
        spot->SetIntensity(1);
        spot->SetAttenuationRadius(15);
        spot->SetInnerConeAngle(20);
        spot->SetOuterConeAngle(45);
        spot->SetCastShadow(true);
        m_Lights.push_back(std::move(spot));

        auto sky = std::make_unique<Engine::SkyLightComponent>();
        sky->SetDiffuseIrradiance(m_SkyAsset->GetDiffuseIrradiance());
        sky->SetSpecularIrradiance(m_SkyAsset->GetSpecularPrefilter());
        m_Lights.push_back(std::move(sky));
        for (const auto& light : m_Lights) m_Scene->AddLight(light.get());
        m_Scene->FlushPendingUpdates();
        m_bSceneDirty = false;
        m_Camera.SetTarget({0, 0, 0});
        m_Camera.SetPosition({2, 2, 2});
        m_Camera.SetUp({0, 1, 0});
        std::cout << "[Editor] Scene ready: " << m_Objects.size() << " meshes" << std::endl;
    }

    void EditorScene::Shutdown()
    {
        if (!m_Scene) return;
        m_Viewport = nullptr; // The application has already released its window submissions.
        for (const auto& object : m_Objects) m_Scene->RemovePrimitive(object.Component.get());
        for (const auto& light : m_Lights) m_Scene->RemoveLight(light.get());
        m_Scene->FlushPendingUpdates();
        m_Objects.clear();
        m_Lights.clear();
        m_SkyAsset.reset();
        m_Scene.reset();
        m_SelectedIndex = -1;
    }

    void EditorScene::AddMesh(const std::shared_ptr<Engine::StaticMeshAsset>& asset)
    {
        if (!asset || !m_Scene) throw std::invalid_argument("Cannot add a mesh without an asset and scene");
        WaitForLastDraw();
        Object object;
        object.Name = asset->GetName() + " #" + std::to_string(m_NextObjectId++);
        object.Component = std::make_unique<Engine::StaticMeshComponent>();
        object.Component->SetStaticMesh(asset->GetMesh());
        // Publish ownership before registering the borrowed component pointer.
        m_Objects.push_back(std::move(object));
        m_Scene->AddPrimitive(m_Objects.back().Component.get());
        m_bSceneDirty = true;
        Select(static_cast<int>(m_Objects.size()) - 1);
        std::cout << "[Editor] Added " << m_Objects.back().Name << std::endl;
    }

    void EditorScene::ClearObjects()
    {
        WaitForLastDraw();
        for (const auto& object : m_Objects) m_Scene->RemovePrimitive(object.Component.get());
        m_Scene->FlushPendingUpdates(); // Consume borrowed pointers before their owners go away.
        m_Objects.clear(); m_SelectedIndex = -1; m_NextObjectId = 1; m_bSceneDirty = false;
        m_Camera.SetTarget({0,0,0}); m_Distance = 3.4641f;
        SetViewDirection(EEditorViewDirection::Orbit);
    }

    bool EditorScene::ApplyMaterialAt(const std::shared_ptr<Engine::MaterialAsset>& material, const Core::Float2& viewportPosition)
    {
        if (!material || !material->GetMaterial()) throw std::invalid_argument("A valid material asset is required");
        if (material->GetMaterial()->GetBlendMode() != Engine::EBlendMode::Opaque)
            throw std::runtime_error("The scene renderer currently supports Opaque materials only");
        if (!m_Scene || !m_Viewport || !std::isfinite(viewportPosition.X) || !std::isfinite(viewportPosition.Y) ||
            viewportPosition.X < 0 || viewportPosition.X >= 1 || viewportPosition.Y < 0 || viewportPosition.Y >= 1) return false;

        // UV is relative to the last displayed viewport (top-left origin). Use
        // that camera projection before a selection change can resize the view.
        const auto& inverse = m_Camera.GetInverseViewProjectionMatrix();
        auto unproject = [&](float depth) {
            const Core::Float4 clip{2*viewportPosition.X-1, 1-2*viewportPosition.Y, depth, 1};
            Core::Float4 world;
            for (int row=0; row<4; ++row)
                for (int column=0; column<4; ++column) world[row] += inverse(row,column)*clip[column];
            if (std::abs(world.W) < 1e-6f) throw std::runtime_error("Cannot unproject the material drop position");
            return Core::Float3{world.X/world.W, world.Y/world.W, world.Z/world.W};
        };
        const auto origin = unproject(m_Camera.GetDepthRangeMode() == Engine::Camera::ZeroToOne ? 0.0f : -1.0f);
        const auto ray = unproject(1.0f)-origin;
        float nearest = ray.Length();
        if (!std::isfinite(nearest) || nearest <= 0) return false;
        const auto direction = ray/nearest;
        int objectIndex = -1;
        uint32_t materialSlot = 0;
        for (size_t index=0; index<m_Objects.size(); ++index)
        {
            auto* component = m_Objects[index].Component.get();
            auto* mesh = component->GetStaticMesh();
            if (!mesh || !mesh->GetLODCount()) continue;
            const auto& lod = mesh->GetLODResource(0); // Matches StaticMeshDrawBuild's current LOD selection.
            const auto& positions = lod.VertexBuffers.PositionBuffer;
            const auto& indices = lod.IndexBuffer.Indices;
            if (!positions.bValid || positions.NumComponents < 3) continue;
            const auto& transform = component->GetComponentTransform();
            for (const auto& section : lod.Sections)
            {
                if (section.MaterialIndex < 0 || !component->GetMaterial(section.MaterialIndex) ||
                    section.FirstIndex > indices.size() || section.NumIndices > indices.size()-section.FirstIndex) continue;
                for (size_t offset=0; offset+2<section.NumIndices; offset+=3)
                {
                    Core::Float3 vertices[3];
                    bool valid = true;
                    for (int corner=0; corner<3; ++corner)
                    {
                        const int64_t vertex = static_cast<int64_t>(indices[section.FirstIndex+offset+corner])+section.BaseVertexIndex;
                        if (vertex < 0 || static_cast<uint64_t>(vertex) >= positions.GetNumVertices()) { valid = false; break; }
                        const auto start = static_cast<size_t>(vertex)*positions.NumComponents;
                        vertices[corner] = Core::TransformPoint(transform, Core::Float3{positions.Vertices[start],positions.Vertices[start+1],positions.Vertices[start+2]});
                    }
                    if (!valid) continue;
                    // Two-sided Moller-Trumbore intersection in world space:
                    // distances remain comparable under nonuniform/negative scale.
                    const auto edge1 = vertices[1]-vertices[0], edge2 = vertices[2]-vertices[0];
                    const auto p = Core::Cross(direction,edge2);
                    const float determinant = Core::Dot(edge1,p);
                    if (std::abs(determinant) <= 1e-7f*edge1.Length()*edge2.Length()) continue;
                    const auto fromVertex = origin-vertices[0];
                    const float u = Core::Dot(fromVertex,p)/determinant;
                    if (u < 0 || u > 1) continue;
                    const auto q = Core::Cross(fromVertex,edge1);
                    const float v = Core::Dot(direction,q)/determinant;
                    if (v < 0 || u+v > 1) continue;
                    const float distance = Core::Dot(edge2,q)/determinant;
                    if (distance >= 0 && distance < nearest)
                    {
                        nearest = distance; objectIndex = static_cast<int>(index); materialSlot = section.MaterialIndex;
                    }
                }
            }
        }
        if (objectIndex < 0) return false;
        WaitForLastDraw();
        auto* component = m_Objects[objectIndex].Component.get();
        component->SetMaterial(materialSlot, material->GetMaterial());
        Select(objectIndex);
        m_Scene->NotifyComponentChanged(component);
        m_bSceneDirty = true;
        return true;
    }

    void EditorScene::DuplicateSelected()
    {
        auto* selected = GetSelected();
        if (!selected) return;
        auto asset = selected->GetStaticMesh()->GetAsset();
        const auto location = selected->GetLocalLocation() + Core::Float3{1.5f, 0, 0};
        const auto rotation = selected->GetLocalRotation();
        const auto scale = selected->GetLocalScale();
        AddMesh(asset);
        GetSelected()->SetLocalLocation(location);
        GetSelected()->SetLocalRotation(rotation);
        GetSelected()->SetLocalScale(scale);
        for (uint32_t slot = 0; slot < selected->GetNumMaterials(); ++slot)
        {
            auto* source = selected->GetMaterial(slot);
            if (!source) continue;
            GetSelected()->SetMaterial(slot, source->GetAsset()->GetMaterial());
            if (const auto* instance = selected->GetMaterialInstance(slot))
                for (const auto& definition : source->GetParameterDefinitions())
                    if (instance->HasOverride(definition.Name, definition.Type))
                        GetSelected()->SetMaterialParameter(slot, definition.Name,
                            instance->GetParameter(definition.Name, definition.Type)->GetValueVariant());
        }
        NotifySelectedChanged();
    }

    void EditorScene::RemoveSelected()
    {
        auto* selected = GetSelected();
        if (!selected) return;
        WaitForLastDraw();
        m_Scene->RemovePrimitive(selected);
        // Pending removal borrows the component: consume it before destroying its owner.
        m_Scene->FlushPendingUpdates();
        m_bSceneDirty = false;
        m_Objects.erase(m_Objects.begin() + m_SelectedIndex);
        Select(m_Objects.empty() ? -1 : std::min(m_SelectedIndex, static_cast<int>(m_Objects.size()) - 1));
        std::cout << "[Editor] Removed mesh; count=" << m_Objects.size() << std::endl;
    }
    void EditorScene::Select(int index)
    {
        m_SelectedIndex = index >= 0 && index < static_cast<int>(m_Objects.size()) ? index : -1;
    }
    Engine::StaticMeshComponent* EditorScene::GetSelected() const
    {
        return m_SelectedIndex >= 0 ? m_Objects[m_SelectedIndex].Component.get() : nullptr;
    }
    void EditorScene::NotifySelectedChanged()
    {
        if (auto* component = GetSelected())
        {
            WaitForLastDraw();
            m_Scene->NotifyComponentChanged(component);
            m_bSceneDirty = true;
        }
    }
    void EditorScene::NotifyMaterialsChanged()
    {
        WaitForLastDraw();
        for (const auto& object : m_Objects) m_Scene->NotifyComponentChanged(object.Component.get());
        m_bSceneDirty = true;
    }
    void EditorScene::FocusSelected()
    {
        if (auto* component = GetSelected())
        {
            m_Camera.SetTarget(component->GetWorldLocation());
            UpdateCamera();
        }
    }

    void EditorScene::SetViewDirection(EEditorViewDirection direction)
    {
        // Keep the current pivot and distance. These angles also let an orbit drag
        // continue from the chosen axis instead of jumping to the old camera angle.
        constexpr float halfPi = 1.5707963268f;
        switch (direction)
        {
        case EEditorViewDirection::Top: m_Yaw = -halfPi; m_Pitch = -halfPi; break;
        case EEditorViewDirection::Bottom: m_Yaw = -halfPi; m_Pitch = halfPi; break;
        case EEditorViewDirection::Left: m_Yaw = 0; m_Pitch = 0; break;
        case EEditorViewDirection::Right: m_Yaw = 2 * halfPi; m_Pitch = 0; break;
        case EEditorViewDirection::Front: m_Yaw = -halfPi; m_Pitch = 0; break;
        case EEditorViewDirection::Back: m_Yaw = halfPi; m_Pitch = 0; break;
        case EEditorViewDirection::Orbit: m_Yaw = -2.3561945f; m_Pitch = -0.6154797f; break;
        default: throw std::invalid_argument("Unknown editor view direction");
        }
        m_ViewDirection = direction;
        m_bRotating = m_bPanning = false;
        UpdateCamera();
    }

    void EditorScene::Draw(Engine::Viewport* viewport)
    {
        if (!m_Scene || !viewport) return;
        m_Viewport = viewport;
        if (m_bSceneDirty)
        {
            WaitForLastDraw();
            m_Scene->FlushPendingUpdates();
            m_bSceneDirty = false;
        }
        const float aspect = static_cast<float>(viewport->GetWidth()) / std::max(1, viewport->GetHeight());
        m_Camera.SetPerspective(Core::DegToRad(45.0f), aspect, 0.1f, 1000);
        Engine::SceneView view;
        view.CameraWorldPos = m_Camera.GetPosition();
        view.NearClip = m_Camera.GetNearPlane();
        view.FarClip = m_Camera.GetFarPlane();
        view.ViewMatrix = m_Camera.GetViewMatrix();
        view.ProjectionMatrix = m_Camera.GetProjectionMatrix();
        view.ViewProjectionMatrix = view.ProjectionMatrix * view.ViewMatrix;
        view.InvViewProjectionMatrix = Core::Inverse(view.ViewProjectionMatrix);
        view.bDepthRangeZeroToOne = true;
        view.Viewport.Width = viewport->GetWidth();
        view.Viewport.Height = viewport->GetHeight();
        m_ViewFamily.Scene = m_Scene.get();
        m_ViewFamily.RenderTarget = viewport;
        m_ViewFamily.ClearViews();
        m_ViewFamily.AddView(view);
        m_ViewFamily.BuildAllViews();
        Engine::GetRenderModuleInstance()->BeginRender(&m_ViewFamily);
    }
    bool EditorScene::OnMouseButton(const SlateCore::MouseButtonEvent& event)
    {
        const bool pressed = event.Event != SlateCore::EInputEvent::Released;
        if (event.Button == SlateCore::EMouseButton::Right) { m_bRotating = pressed; return true; }
        if (event.Button == SlateCore::EMouseButton::Middle) { m_bPanning = pressed; return true; }
        return false;
    }
    bool EditorScene::OnMouseMove(const SlateCore::MouseMoveEvent& event)
    {
        if (m_bRotating)
        {
            m_ViewDirection = EEditorViewDirection::Orbit;
            m_Yaw += event.DeltaX * 0.005f;
            m_Pitch = std::clamp(m_Pitch - event.DeltaY * 0.005f, -1.5f, 1.5f);
            UpdateCamera();
            return true;
        }
        if (m_bPanning)
        {
            const auto forward = Core::Normalize(m_Camera.GetTarget() - m_Camera.GetPosition());
            const auto right = Core::Normalize(Core::Cross(forward, m_Camera.GetUp()));
            const auto up = Core::Normalize(Core::Cross(right, forward));
            const auto offset = (-right * static_cast<float>(event.DeltaX) + up * static_cast<float>(event.DeltaY)) * (0.005f * m_Distance);
            m_Camera.SetTarget(m_Camera.GetTarget() + offset);
            UpdateCamera();
            return true;
        }
        return false;
    }
    bool EditorScene::OnMouseWheel(const SlateCore::MouseWheelEvent& event)
    {
        m_Distance = std::clamp(m_Distance - event.Delta * 0.25f, 0.2f, 1000.0f);
        UpdateCamera();
        return true;
    }
    bool EditorScene::OnFocusLost() { m_bRotating = m_bPanning = false; return true; }
    void EditorScene::WaitForLastDraw()
    {
        if (!m_Viewport || !m_Viewport->GetRenderTarget()) return;
        // UI sampling updates this tracker at its actual submission. Its completion
        // also covers the scene draw it waited on, before scene buffers are rebuilt.
        const auto fence = m_Viewport->GetRenderTarget()->GetTracker().GetLastAccessFence();
        if (fence.Value) RHI::GRHIApi->GetQueue(fence.QueueType)->WaitValue(fence.Value);
    }
    void EditorScene::UpdateCamera()
    {
        const float cosine = std::cos(m_Pitch);
        Core::Float3 forward{std::cos(m_Yaw) * cosine, std::sin(m_Pitch), std::sin(m_Yaw) * cosine};
        Core::Float3 up{0, 1, 0};
        switch (m_ViewDirection)
        {
        // Exact pole directions need a non-parallel up vector for LookAtRH.
        case EEditorViewDirection::Top: forward = {0, -1, 0}; up = {0, 0, -1}; break;
        case EEditorViewDirection::Bottom: forward = {0, 1, 0}; up = {0, 0, 1}; break;
        case EEditorViewDirection::Left: forward = {1, 0, 0}; break;
        case EEditorViewDirection::Right: forward = {-1, 0, 0}; break;
        case EEditorViewDirection::Front: forward = {0, 0, -1}; break;
        case EEditorViewDirection::Back: forward = {0, 0, 1}; break;
        default: break;
        }
        m_Camera.SetUp(up);
        m_Camera.SetPosition(m_Camera.GetTarget() - forward * m_Distance);
    }
}
