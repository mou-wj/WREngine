#pragma once
#include "ViewportClient.h"
#include "Camera.h"
#include "SceneView.h"
#include <memory>
#include <string>
#include <vector>

namespace Engine
{
    class SceneInterface;
    class StaticMeshAsset;
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
            std::unique_ptr<Engine::StaticMeshComponent> Component;
        };
        EditorScene();
        ~EditorScene() override;
        void Initialize();
        void Shutdown();
        void ClearObjects();
        void AddMesh(const std::shared_ptr<Engine::StaticMeshAsset>& asset);
        void DuplicateSelected();
        void RemoveSelected();
        void Select(int index);
        int GetSelectedIndex() const { return m_SelectedIndex; }
        Engine::StaticMeshComponent* GetSelected() const;
        const std::vector<Object>& GetObjects() const { return m_Objects; }
        void NotifySelectedChanged();
        void NotifyMaterialsChanged();
        void FocusSelected();
        void SetViewDirection(EEditorViewDirection direction);
        EEditorViewDirection GetViewDirection() const { return m_ViewDirection; }
        void Draw(Engine::Viewport* viewport) override;
        bool OnMouseButton(const SlateCore::MouseButtonEvent& event) override;
        bool OnMouseMove(const SlateCore::MouseMoveEvent& event) override;
        bool OnMouseWheel(const SlateCore::MouseWheelEvent& event) override;
        bool OnFocusLost() override;
    private:
        void UpdateCamera();
        void WaitForLastDraw();
        Engine::Viewport* m_Viewport = nullptr;
        std::unique_ptr<Engine::SceneInterface> m_Scene;
        std::vector<Object> m_Objects;
        std::vector<std::unique_ptr<Engine::LightComponent>> m_Lights;
        std::shared_ptr<Engine::SkyLightAsset> m_SkyAsset;
        Engine::Camera m_Camera;
        Engine::SceneViewFamily m_ViewFamily;
        int m_SelectedIndex = -1;
        uint32_t m_NextObjectId = 1;
        float m_Distance = 3.4641f;
        float m_Yaw = -2.3561945f;
        float m_Pitch = -0.6154797f;
        EEditorViewDirection m_ViewDirection = EEditorViewDirection::Orbit;
        bool m_bRotating = false;
        bool m_bPanning = false;
        bool m_bSceneDirty = false;
    };
}
