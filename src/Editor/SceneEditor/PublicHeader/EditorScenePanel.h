#pragma once
#include "Widgets/ImCommonWidgets.h"
#include "SlateWidget.h"
#include "Math.hpp"
#include <filesystem>

namespace Editor
{
    class EditorScene;
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
        void Draw() override;
    private:
        EditorScene& m_Scene;
    };

    class SCENEEDITOR_API EditorScenePanel final : public ImGUISlate::ImPanelWidget
    {
    public:
        explicit EditorScenePanel(EditorScene& scene);
    protected:
        void DrawContent() override;
    private:
        EditorScene& m_Scene;
        ImGUISlate::ImListWidget m_Objects;
    };
}
