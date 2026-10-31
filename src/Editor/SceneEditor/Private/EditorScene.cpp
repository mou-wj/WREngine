#include "EditorScene.h"
#include "EditorViewAxes.h"
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
#include "BVH.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace Editor
{
    namespace
    {
        constexpr float LightIconRadius = 9;
        struct LightIcon { int ObjectIndex; Core::Float2 Center; float Depth; Engine::ELightType Type; };

        std::vector<LightIcon> BuildLightIcons(const Engine::SceneView& view, const std::vector<EditorScene::Object>& objects)
        {
            std::vector<LightIcon> icons;
            for (size_t i=0; i<objects.size(); ++i)
            {
                const auto* light = dynamic_cast<const Engine::LightComponent*>(objects[i].Component.get());
                if (!light || light->GetLightType()==Engine::ELightType::Sky) continue;
                const auto origin=light->GetWorldLocation(); Core::Float2 center;
                if (!GizmoDrawing::Project(view,origin,center)) continue;
                const auto camera=view.ViewMatrix*std::array<float,4>{origin.X,origin.Y,origin.Z,1};
                icons.push_back({static_cast<int>(i),center,-camera[2],light->GetLightType()});
            }
            // Overlay icons do not depth-test against scene geometry. Nearest
            // icons paint last; picking uses the reverse order and the same diamond.
            std::stable_sort(icons.begin(),icons.end(),[](const auto& a,const auto& b){return a.Depth>b.Depth;});
            return icons;
        }

        void AddLightIcons(RenderCore::RenderGraphBuilder& builder, const Engine::SceneView& view,
            RenderCore::RenderGraphTextureRef color, const std::vector<EditorScene::Object>& objects, int selected)
        {
            std::vector<GizmoDrawing::Triangle> triangles;
            for (const auto& icon : BuildLightIcons(view,objects))
            {
                auto tint=icon.Type==Engine::ELightType::Point ? Core::Float4{0.15f,0.45f,1,1} :
                    (icon.Type==Engine::ELightType::Spot ? Core::Float4{0.2f,0.85f,0.35f,1} : Core::Float4{1,0.65f,0.12f,1});
                if (icon.ObjectIndex==selected) tint={1,1,0,1};
                const auto c=icon.Center;
                const Core::Float2 left{c.X-LightIconRadius,c.Y}, right{c.X+LightIconRadius,c.Y};
                const Core::Float2 top{c.X,c.Y-LightIconRadius}, bottom{c.X,c.Y+LightIconRadius};
                triangles.push_back({left,top,right,tint});triangles.push_back({left,right,bottom,tint});
                const Core::Float4 ink{0.03f,0.03f,0.03f,1};
                GizmoDrawing::Line(triangles,{c.X-3,c.Y},{c.X+3,c.Y},ink,1.5f);
                if (icon.Type==Engine::ELightType::Point)
                    GizmoDrawing::Line(triangles,{c.X,c.Y-3},{c.X,c.Y+3},ink,1.5f);
                else
                {
                    GizmoDrawing::Line(triangles,{c.X,c.Y-3},{c.X+3,c.Y},ink,1.5f);
                    GizmoDrawing::Line(triangles,{c.X,c.Y+3},{c.X+3,c.Y},ink,1.5f);
                }
            }
            GizmoDrawing::AddPass(builder,view,color,triangles,"EditorLightIcons");
        }
    }

    EditorScene::EditorScene() = default;
    EditorScene::~EditorScene() { Shutdown(); }

    void EditorScene::Initialize()
    {
        m_Scene.reset(Engine::GetRenderModuleInstance()->AllocateScene());
        auto& sceneSetting = m_Scene->GetSceneSetting();
        sceneSetting.bDrawGrid = true;
        sceneSetting.bDrawLightHints = true;
        m_SkyAsset = Engine::AssetManager::Get().LoadSync<Engine::SkyLightAsset>(Core::GetProjectDir() + "/resources/pic/DaySkyHDRI046A_1K-TONEMAPPED.jpg");
        m_Camera.SetTarget({0, 0, 0});
        m_Camera.SetPosition({2, 2, 2});
        m_Camera.SetUp({0, 1, 0});
        m_Scene->FlushPendingUpdates();
        std::cout << "[Editor] Scene ready: 0 nodes" << std::endl;
    }

    void EditorScene::CreateDefaultLights()
    {
        const auto addLight = [this](const char* name, std::unique_ptr<Engine::LightComponent> light) {
            auto* component = light.get();
            m_Objects.push_back({name,std::move(light)});
            m_Scene->AddLight(component);
        };

        auto directional = std::make_unique<Engine::DirectionalLightComponent>();
        directional->SetDirection({-1, -1, -1});
        directional->SetColor({1, 1, 1});
        directional->SetIntensity(1);
        directional->SetCastShadow(true);
        addLight("Directional light",std::move(directional));

        auto point = std::make_unique<Engine::PointLightComponent>();
        point->SetWorldLocation({5, 4, 5});
        point->SetColor({1, 0.5f, 0.2f});
        point->SetIntensity(1);
        point->SetAttenuationRadius(10);
        point->SetCastShadow(true);
        addLight("Point light",std::move(point));

        auto spot = std::make_unique<Engine::SpotLightComponent>();
        spot->SetWorldLocation({3, 2, 0});
        spot->SetDirection({-1, -0.5f, 0});
        spot->SetColor({0.2f, 0.5f, 1});
        spot->SetIntensity(1);
        spot->SetAttenuationRadius(15);
        spot->SetInnerConeAngle(20);
        spot->SetOuterConeAngle(45);
        spot->SetCastShadow(true);
        addLight("Spot light",std::move(spot));

        auto sky = std::make_unique<Engine::SkyLightComponent>();
        sky->SetDiffuseIrradiance(m_SkyAsset->GetDiffuseIrradiance());
        sky->SetSpecularIrradiance(m_SkyAsset->GetSpecularPrefilter());
        addLight("Sky light",std::move(sky));
        m_Scene->FlushPendingUpdates();
        m_bSceneDirty = false;
    }

    void EditorScene::Shutdown()
    {
        ClearTransformTargets();
        if (!m_Scene) return;
        m_Viewport = nullptr; // The application has already released its window submissions.
        if (m_bSceneDirty) m_Scene->FlushPendingUpdates();
        for (const auto& object : m_Objects)
            if (auto* light = dynamic_cast<Engine::LightComponent*>(object.Component.get())) m_Scene->RemoveLight(light);
            else if (auto* mesh = dynamic_cast<Engine::StaticMeshComponent*>(object.Component.get())) m_Scene->RemovePrimitive(mesh);
        m_Scene->FlushPendingUpdates();
        m_Objects.clear();
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
        auto component = std::make_unique<Engine::StaticMeshComponent>();
        component->SetStaticMesh(asset->GetMesh());
        auto* mesh = component.get();
        object.Component = std::move(component);
        // Publish ownership before registering the borrowed component pointer.
        m_Objects.push_back(std::move(object));
        m_Scene->AddPrimitive(mesh);
        m_bSceneDirty = true;
        Select(static_cast<int>(m_Objects.size()) - 1);
        std::cout << "[Editor] Added " << m_Objects.back().Name << std::endl;
    }

    void EditorScene::ClearObjects()
    {
        ClearTransformTargets();
        WaitForLastDraw();
        if (m_bSceneDirty) m_Scene->FlushPendingUpdates();
        for (const auto& object : m_Objects)
            if (auto* light = dynamic_cast<Engine::LightComponent*>(object.Component.get())) m_Scene->RemoveLight(light);
            else if (auto* mesh = dynamic_cast<Engine::StaticMeshComponent*>(object.Component.get())) m_Scene->RemovePrimitive(mesh);
        m_Scene->FlushPendingUpdates(); // Consume borrowed pointers before their owners go away.
        m_Objects.clear(); m_SelectedIndex = -1; m_NextObjectId = 1; m_bSceneDirty = false;
        m_Camera.SetTarget({0,0,0}); m_Distance = 3.4641f;
        SetViewDirection(EEditorViewDirection::Orbit);
    }

    int EditorScene::FindObjectIndex(const Engine::SceneComponent* component) const
    {
        for (size_t i=0; i<m_Objects.size(); ++i)
            if (m_Objects[i].Component.get()==component) return static_cast<int>(i);
        return -1;
    }

    void EditorScene::SelectAt(Core::Float2 position)
    {
        const auto* view=m_ViewFamily.GetView(0);
        Core::Ray ray;float maximum;
        if (!m_Scene || !view || !view->MakeRay(position,ray,maximum)) return;
        const auto icons=BuildLightIcons(*view,m_Objects);
        for (auto icon=icons.rbegin(); icon!=icons.rend(); ++icon)
            if (std::abs(position.X-icon->Center.X)+std::abs(position.Y-icon->Center.Y)<=LightIconRadius)
            { Select(icon->ObjectIndex);return; }
        Engine::ScenePickResult result;
        Select(m_Scene->Raycast(ray,maximum,result,Engine::EScenePickFilter::All) ? FindObjectIndex(result.Component) : -1);
    }

    bool EditorScene::ApplyMaterialAt(const std::shared_ptr<Engine::MaterialAsset>& material, const Core::Float2& viewportPosition)
    {
        if (!material || !material->GetMaterial()) throw std::invalid_argument("A valid material asset is required");
        if (material->GetMaterial()->GetBlendMode()!=Engine::EBlendMode::Opaque)
            throw std::runtime_error("The scene renderer currently supports Opaque materials only");
        const auto* view=m_ViewFamily.GetView(0);
        if (!m_Scene || !view) return false;
        Core::Ray ray;float maximum;
        if (!view->MakeRay({viewportPosition.X*view->Viewport.Width,viewportPosition.Y*view->Viewport.Height},ray,maximum)) return false;
        Engine::ScenePickResult result;
        if (!m_Scene->Raycast(ray,maximum,result)) return false;
        const int index=FindObjectIndex(result.Component);
        auto* component=dynamic_cast<Engine::StaticMeshComponent*>(result.Component);
        if (index<0 || !component || result.MaterialIndex<0) return false;
        WaitForLastDraw();
        component->SetMaterial(result.MaterialIndex,material->GetMaterial());
        Select(index);m_Scene->NotifyComponentChanged(component);m_bSceneDirty=true;
        return true;
    }

    void EditorScene::DuplicateSelected()
    {
        auto* selected = GetSelectedMesh();
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
            GetSelectedMesh()->SetMaterial(slot, source->GetAsset()->GetMaterial());
            if (const auto* instance = selected->GetMaterialInstance(slot))
                for (const auto& definition : source->GetParameterDefinitions())
                    if (instance->HasOverride(definition.Name, definition.Type))
                        GetSelectedMesh()->SetMaterialParameter(slot, definition.Name,
                            instance->GetParameter(definition.Name, definition.Type)->GetValueVariant());
        }
        NotifySelectedChanged();
    }

    void EditorScene::RemoveSelected()
    {
        auto* selected = GetSelected();
        if (!selected) return;
        ClearTransformTargets();
        WaitForLastDraw();
        if (m_bSceneDirty) m_Scene->FlushPendingUpdates();
        if (auto* light = dynamic_cast<Engine::LightComponent*>(selected)) m_Scene->RemoveLight(light);
        else if (auto* mesh = dynamic_cast<Engine::StaticMeshComponent*>(selected)) m_Scene->RemovePrimitive(mesh);
        // Pending removal borrows the component: consume it before destroying its owner.
        m_Scene->FlushPendingUpdates();
        m_bSceneDirty = false;
        m_Objects.erase(m_Objects.begin() + m_SelectedIndex);
        Select(m_Objects.empty() ? -1 : std::min(m_SelectedIndex, static_cast<int>(m_Objects.size()) - 1));
        std::cout << "[Editor] Removed node; count=" << m_Objects.size() << std::endl;
    }
    void EditorScene::Select(int index)
    {
        const int selectedIndex = index >= 0 && index < static_cast<int>(m_Objects.size()) ? index : -1;
        if (selectedIndex != m_SelectedIndex) ClearTransformTargets();
        m_SelectedIndex = selectedIndex;
        if (m_Scene) m_Scene->GetSceneSetting().LightHintTarget = dynamic_cast<Engine::LightComponent*>(GetSelected());
    }
    void EditorScene::SetTranslationTarget(Engine::SceneComponent* component)
    {
        if (component && component->GetSceneOwner() != m_Scene.get())
            throw std::invalid_argument("Translation target must belong to the editor scene");
        if (component && m_TranslationGizmo.GetTarget() != component) ClearTransformTargets();
        m_TranslationGizmo.SetTarget(component);
    }
    void EditorScene::SetRotationTarget(Engine::SceneComponent* component)
    {
        if (component && component->GetSceneOwner() != m_Scene.get())
            throw std::invalid_argument("Rotation target must belong to the editor scene");
        if (component && m_RotationGizmo.GetTarget() != component) ClearTransformTargets();
        m_RotationGizmo.SetTarget(component);
    }
    void EditorScene::SetScaleTarget(Engine::SceneComponent* component)
    {
        if (component && component->GetSceneOwner() != m_Scene.get())
            throw std::invalid_argument("Scale target must belong to the editor scene");
        if (component && m_ScaleGizmo.GetTarget() != component) ClearTransformTargets();
        m_ScaleGizmo.SetTarget(component);
    }
    void EditorScene::ClearTransformTargets()
    {
        m_bPickPending = false;
        m_TranslationGizmo.SetTarget(nullptr);
        m_RotationGizmo.SetTarget(nullptr);
        m_ScaleGizmo.SetTarget(nullptr);
    }
    void EditorScene::ClearTransformHover()
    { m_TranslationGizmo.ClearHover(); m_RotationGizmo.ClearHover(); m_ScaleGizmo.ClearHover(); }
    void EditorScene::EndTransformDrag()
    { m_bPickPending=false; m_TranslationGizmo.EndDrag(); m_RotationGizmo.EndDrag(); m_ScaleGizmo.EndDrag(); ClearTransformHover(); }
    bool EditorScene::IsTransformDragging() const
    { return m_TranslationGizmo.IsDragging() || m_RotationGizmo.IsDragging() || m_ScaleGizmo.IsDragging(); }
    Engine::SceneComponent* EditorScene::GetSelected() const
    {
        return m_SelectedIndex >= 0 ? m_Objects[m_SelectedIndex].Component.get() : nullptr;
    }
    Engine::StaticMeshComponent* EditorScene::GetSelectedMesh() const
    { return dynamic_cast<Engine::StaticMeshComponent*>(GetSelected()); }
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
        for (const auto& object : m_Objects)
            if (dynamic_cast<Engine::StaticMeshComponent*>(object.Component.get())) m_Scene->NotifyComponentChanged(object.Component.get());
        m_bSceneDirty = true;
    }
    void EditorScene::FocusSelected()
    {
        if (auto* component = GetSelected())
        {
            EndTransformDrag();
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
        EndTransformDrag();
        m_OrbitUp = {0,1,0};
        m_PressedAxis = -1;
        m_bAxisDragging = false;
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
        view.DrawExtensions.push_back({[this](auto& builder, const auto& sceneView, auto color, auto depth) {
            AddLightIcons(builder,sceneView,color,m_Objects,m_SelectedIndex);
            m_TranslationGizmo.AddPass(builder,sceneView,color);
            m_RotationGizmo.AddPass(builder,sceneView,color);
            m_ScaleGizmo.AddPass(builder,sceneView,color);
        }});
        view.DrawExtensions.push_back({[this](auto& builder, const auto& sceneView, auto color, auto depth) {
            const int hovered=ViewAxes::HitTest(sceneView.ViewMatrix,sceneView.Viewport.Width,sceneView.Viewport.Height,m_ViewportPointer);
            ViewAxes::AddPass(builder,sceneView,color,m_PressedAxis>=0 ? m_PressedAxis : hovered);
        }});
        m_ViewFamily.Scene = m_Scene.get();
        m_ViewFamily.RenderTarget = viewport;
        m_ViewFamily.ClearViews();
        m_ViewFamily.AddView(view);
        m_ViewFamily.BuildAllViews();
        Engine::GetRenderModuleInstance()->BeginRender(&m_ViewFamily);
    }
    bool EditorScene::OnMouseButton(const SlateCore::MouseButtonEvent& event)
    {
        UpdateViewportHover({static_cast<float>(event.X),static_cast<float>(event.Y)});
        const bool pressed = event.Event != SlateCore::EInputEvent::Released;
        if (event.Button == SlateCore::EMouseButton::Left)
        {
            if (!pressed && IsTransformDragging())
            {
                EndTransformDrag();
                if (const auto* view = m_ViewFamily.GetView(0))
                {
                    m_TranslationGizmo.UpdateHover(*view,{static_cast<float>(event.X),static_cast<float>(event.Y)});
                    m_RotationGizmo.UpdateHover(*view,{static_cast<float>(event.X),static_cast<float>(event.Y)});
                    m_ScaleGizmo.UpdateHover(*view,{static_cast<float>(event.X),static_cast<float>(event.Y)});
                }
                return true;
            }
            if (pressed && m_Viewport && !m_bRotating && !m_bPanning)
            {
                m_PressedAxis = ViewAxes::HitTest(m_Camera.GetViewMatrix(),m_Viewport->GetWidth(),m_Viewport->GetHeight(),
                    {static_cast<float>(event.X),static_cast<float>(event.Y)});
                if (m_PressedAxis < 0)
                {
                    const auto* view = m_ViewFamily.GetView(0);
                    const Core::Float2 position{static_cast<float>(event.X),static_cast<float>(event.Y)};
                    if (!view) return false;
                    if (m_TranslationGizmo.BeginDrag(*view,position) ||
                        m_RotationGizmo.BeginDrag(*view,position) || m_ScaleGizmo.BeginDrag(*view,position)) return true;
                    Core::Ray ray; float maximum;
                    if (!view->MakeRay(position,ray,maximum)) return false;
                    m_PickPressPosition=position;m_bPickPending=true;m_bPickMoved=false;
                    return true;
                }
                ClearTransformHover();
                m_AxisPressPosition = {static_cast<float>(event.X),static_cast<float>(event.Y)};
                m_AxisStartOffset = m_Camera.GetPosition()-m_Camera.GetTarget();
                m_AxisStartUp = Core::Normalize(m_Camera.GetUp());
                m_bAxisDragging = false;
                return true;
            }
            if (!pressed && m_PressedAxis >= 0)
            {
                const int endpoint = m_PressedAxis;
                const bool click = !m_bAxisDragging && m_Viewport && endpoint == ViewAxes::HitTest(m_Camera.GetViewMatrix(),
                    m_Viewport->GetWidth(),m_Viewport->GetHeight(),{static_cast<float>(event.X),static_cast<float>(event.Y)});
                m_PressedAxis = -1; m_bAxisDragging = false;
                if (click)
                {
                    const EEditorViewDirection directions[] = {EEditorViewDirection::Right,EEditorViewDirection::Left,
                        EEditorViewDirection::Top,EEditorViewDirection::Bottom,EEditorViewDirection::Front,EEditorViewDirection::Back};
                    SetViewDirection(directions[endpoint]);
                }
                return true;
            }
            if (!pressed && m_bPickPending)
            {
                const Core::Float2 position{static_cast<float>(event.X),static_cast<float>(event.Y)};
                const bool click=!m_bPickMoved && (position-m_PickPressPosition).LengthSquared()<16;
                m_bPickPending=false;
                if (click) SelectAt(position);
                return true;
            }
            return false;
        }
        m_bPickPending=false;
        if (m_PressedAxis >= 0 || IsTransformDragging()) return true;
        ClearTransformHover();
        if (event.Button == SlateCore::EMouseButton::Right) { m_bRotating = pressed; return true; }
        if (event.Button == SlateCore::EMouseButton::Middle) { m_bPanning = pressed; return true; }
        return false;
    }
    bool EditorScene::OnMouseMove(const SlateCore::MouseMoveEvent& event)
    {
        UpdateViewportHover({static_cast<float>(event.X),static_cast<float>(event.Y)});
        if (m_bPickPending)
        {
            if ((m_ViewportPointer-m_PickPressPosition).LengthSquared()>=16) m_bPickMoved=true;
            return true;
        }
        if (IsTransformDragging())
        {
            const auto* view = m_ViewFamily.GetView(0);
            const Core::Float2 position{static_cast<float>(event.X),static_cast<float>(event.Y)};
            Core::Float3 value;
            if (!view) return true;
            if (m_TranslationGizmo.IsDragging() && m_TranslationGizmo.Drag(*view,position,value) &&
                (value-m_TranslationGizmo.GetTarget()->GetWorldLocation()).LengthSquared() > 1e-12f)
            {
                // PrimitiveComponent's dirty override is a placeholder. Publish
                // explicitly, as the node inspector does, after the normal wait.
                WaitForLastDraw();
                m_TranslationGizmo.GetTarget()->SetWorldLocation(value);
                m_Scene->NotifyComponentChanged(m_TranslationGizmo.GetTarget());
                m_bSceneDirty = true;
            }
            else if (m_RotationGizmo.IsDragging() && m_RotationGizmo.Drag(*view,position,value) &&
                (value-m_RotationGizmo.GetTarget()->GetLocalRotation()).LengthSquared() > 1e-12f)
            {
                WaitForLastDraw();
                m_RotationGizmo.GetTarget()->SetLocalRotation(value);
                m_Scene->NotifyComponentChanged(m_RotationGizmo.GetTarget());
                m_bSceneDirty = true;
            }
            else if (m_ScaleGizmo.IsDragging() && m_ScaleGizmo.Drag(*view,position,value) &&
                (value-m_ScaleGizmo.GetTarget()->GetLocalScale()).LengthSquared() > 1e-12f)
            {
                WaitForLastDraw();
                m_ScaleGizmo.GetTarget()->SetLocalScale(value);
                m_Scene->NotifyComponentChanged(m_ScaleGizmo.GetTarget());
                m_bSceneDirty = true;
            }
            return true;
        }
        if (m_PressedAxis >= 0)
        {
            const float dx = event.X-m_AxisPressPosition.X, dy = event.Y-m_AxisPressPosition.Y;
            if (dx*dx+dy*dy >= 16) m_bAxisDragging = true;
            if (m_bAxisDragging)
            {
                // Compose yaw and pitch from the press-time basis. Rotate the up
                // vector with pitch as well, so pole views stay continuous.
                const auto rotate = [](const Core::Float3& vector, const Core::Float3& axis, float angle) {
                    const float cosine = std::cos(angle), sine = std::sin(angle);
                    return vector*cosine + Core::Cross(axis,vector)*sine +
                        axis*(Core::Dot(axis,vector)*(1-cosine));
                };
                const auto yawOffset = rotate(m_AxisStartOffset,m_AxisStartUp,-dx*0.005f);
                const auto right = Core::Normalize(Core::Cross(m_AxisStartUp,yawOffset));
                const auto offset = rotate(yawOffset,right,-dy*0.005f);
                const auto forward = -Core::Normalize(offset);
                m_Yaw = std::atan2(forward.Z,forward.X);
                m_Pitch = std::asin(std::clamp(forward.Y,-1.0f,1.0f));
                m_OrbitUp = rotate(m_AxisStartUp,right,-dy*0.005f);
                m_ViewDirection = EEditorViewDirection::Orbit;
                m_Camera.SetUp(m_OrbitUp);
                m_Camera.SetPosition(m_Camera.GetTarget()+offset);
            }
            return true;
        }
        if (m_bRotating)
        {
            m_OrbitUp = {0,1,0};
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
        if (const auto* view = m_ViewFamily.GetView(0))
        {
            if (m_Viewport && ViewAxes::HitTest(m_Camera.GetViewMatrix(),m_Viewport->GetWidth(),m_Viewport->GetHeight(),
                {static_cast<float>(event.X),static_cast<float>(event.Y)}) < 0)
            {
                const Core::Float2 position{static_cast<float>(event.X),static_cast<float>(event.Y)};
                m_TranslationGizmo.UpdateHover(*view,position);
                m_RotationGizmo.UpdateHover(*view,position);
                m_ScaleGizmo.UpdateHover(*view,position);
            }
            else ClearTransformHover();
        }
        return false;
    }
    bool EditorScene::OnMouseWheel(const SlateCore::MouseWheelEvent& event)
    {
        m_bPickPending=false;
        if (m_PressedAxis >= 0 || IsTransformDragging()) return true;
        ClearTransformHover();
        m_Distance = std::clamp(m_Distance - event.Delta * 0.25f, 0.2f, 1000.0f);
        UpdateCamera();
        return true;
    }
    bool EditorScene::OnFocusLost()
    {
        m_bPickPending=false;m_ViewportPointer={-1,-1};
        EndTransformDrag();
        m_bRotating = m_bPanning = m_bAxisDragging = false; m_PressedAxis = -1; return true;
    }
    void EditorScene::OnViewportResized(int, int)
    { m_bPickPending=false; m_ViewportPointer={-1,-1}; EndTransformDrag(); }
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
        Core::Float3 up = m_OrbitUp;
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
