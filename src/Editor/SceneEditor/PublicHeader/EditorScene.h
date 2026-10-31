#pragma once
#include "ViewportClient.h"
#include "Camera.h"
#include "SceneView.h"
#include "EditorTranslationGizmo.h"
#include <memory>
#include <string>
#include <vector>

namespace Engine
{
    class SceneInterface;
    class StaticMeshAsset;
    class MaterialAsset;
    class StaticMeshComponent;
    class LightComponent;
    class SkyLightAsset;
}

namespace Editor
{
    enum class EEditorViewDirection { Orbit, Top, Bottom, Left, Right, Front, Back };

    // Scene-editor session and viewport client; assets still own real resources.
    class SCENEEDITOR_API EditorScene final : public Engine::ViewportClient
    {
    public:
        struct Object
        {
            std::string Name;
            std::unique_ptr<Engine::SceneComponent> Component;
        };
        EditorScene();
        ~EditorScene() override;
        void Initialize();
        void CreateDefaultLights();
        void Shutdown();
        void ClearObjects();
        void AddMesh(const std::shared_ptr<Engine::StaticMeshAsset>& asset);
        bool ApplyMaterialAt(const std::shared_ptr<Engine::MaterialAsset>& material, const Core::Float2& viewportPosition);
        void DuplicateSelected();
        void RemoveSelected();
        void Select(int index);
        int GetSelectedIndex() const { return m_SelectedIndex; }
        Engine::SceneComponent* GetSelected() const;
        Engine::StaticMeshComponent* GetSelectedMesh() const;
        const std::vector<Object>& GetObjects() const { return m_Objects; }
        void NotifySelectedChanged();
        void NotifyMaterialsChanged();
        void SetTranslationTarget(Engine::SceneComponent* component);
        Engine::SceneComponent* GetTranslationTarget() const { return m_TranslationGizmo.GetTarget(); }
        void ClearTranslationHover() { m_TranslationGizmo.ClearHover(); }
        void SetRotationTarget(Engine::SceneComponent* component);
        Engine::SceneComponent* GetRotationTarget() const { return m_RotationGizmo.GetTarget(); }
        void SetScaleTarget(Engine::SceneComponent* component);
        Engine::SceneComponent* GetScaleTarget() const { return m_ScaleGizmo.GetTarget(); }
        void ClearTransformHover();
        void FocusSelected();
        void SetViewDirection(EEditorViewDirection direction);
        EEditorViewDirection GetViewDirection() const { return m_ViewDirection; }
        void Draw(Engine::Viewport* viewport) override;
        bool OnMouseButton(const SlateCore::MouseButtonEvent& event) override;
        bool OnMouseMove(const SlateCore::MouseMoveEvent& event) override;
        bool OnMouseWheel(const SlateCore::MouseWheelEvent& event) override;
        bool OnFocusLost() override;
        void OnViewportResized(int width, int height) override;
    private:
        void UpdateCamera();
        void WaitForLastDraw();
        void ClearTransformTargets();
        void EndTransformDrag();
        bool IsTransformDragging() const;
        Engine::Viewport* m_Viewport = nullptr;
        std::unique_ptr<Engine::SceneInterface> m_Scene;
        std::vector<Object> m_Objects;
        std::shared_ptr<Engine::SkyLightAsset> m_SkyAsset;
        Engine::Camera m_Camera;
        Engine::SceneViewFamily m_ViewFamily;
        EditorTranslationGizmo m_TranslationGizmo;
        EditorRotationGizmo m_RotationGizmo;
        EditorScaleGizmo m_ScaleGizmo;
        int m_SelectedIndex = -1;
        uint32_t m_NextObjectId = 1;
        float m_Distance = 3.4641f;
        float m_Yaw = -2.3561945f;
        float m_Pitch = -0.6154797f;
        Core::Float3 m_OrbitUp{0,1,0};
        int m_PressedAxis = -1;
        Core::Float2 m_AxisPressPosition;
        Core::Float3 m_AxisStartOffset;
        Core::Float3 m_AxisStartUp;
        bool m_bAxisDragging = false;
        EEditorViewDirection m_ViewDirection = EEditorViewDirection::Orbit;
        bool m_bRotating = false;
        bool m_bPanning = false;
        bool m_bSceneDirty = false;
    };
}
