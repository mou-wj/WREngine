#pragma once
#include "Widgets/ImCommonWidgets.h"
#include "SlateWidget.h"
#include "Math.hpp"
#include <filesystem>

namespace Editor
{
    class EditorScene;
    enum class EEditorSceneObject;
    class SCENEEDITOR_API EditorSceneViewportWidget final : public SlateCore::SlateWidget
    {
        DECLARE_TYPE_ID_DERIVED_TYPE(EditorSceneViewportWidget, SlateCore::SlateWidget)
    public:
        EditorSceneViewportWidget() = default;
        explicit EditorSceneViewportWidget(EditorScene& scene) : m_Scene(&scene) {}
        void SetOnResourceDropped(std::function<void(const std::filesystem::path&)> callback);
        void SetOnMaterialDropped(std::function<void(const std::filesystem::path&, const Core::Float2&)> callback);
        void Draw() override;
    private:
        EditorScene* m_Scene = nullptr;
        std::function<void(const std::filesystem::path&)> m_OnResourceDropped;
        std::function<void(const std::filesystem::path&, const Core::Float2&)> m_OnMaterialDropped;
    };
    // The host owns placement; view actions remain part of SceneEditor.
    class SCENEEDITOR_API EditorViewToolbar final : public ImGUISlate::ImWidget
    {
    public:
        explicit EditorViewToolbar(EditorScene& scene);
        void SetOnOpenMeshViewer(std::function<void()> callback);
        void SetOnOpenTextureViewer(std::function<void()> callback);
        void Draw() override;
    private:
        EditorScene& m_Scene;
        std::function<void()> m_OnOpenMeshViewer;
        std::function<void()> m_OnOpenTextureViewer;
    };

    class SCENEEDITOR_API EditorScenePanel final : public ImGUISlate::ImPanelWidget
    {
    public:
        explicit EditorScenePanel(EditorScene& scene);
        void SetCreationEnabled(bool enabled) { m_bCreationEnabled = enabled; }
        void SetOnAddObject(std::function<void(EEditorSceneObject)> callback);
        void SetOnDeleteObject(std::function<void(int)> callback);
    protected:
        void DrawContent() override;
    private:
        EditorScene& m_Scene;
        ImGUISlate::ImListWidget m_Objects;
        std::function<void(EEditorSceneObject)> m_OnAddObject;
        std::function<void(int)> m_OnDeleteObject;
        bool m_bCreationEnabled = false;
    };
}
