#pragma once
#include "ApplicationBase.h"
#include <memory>
#include <filesystem>
#include <vector>

namespace ImGUISlate { class ImMainWindow; class ImSplitterWidget; class ImWidget; class ImFileDialogWidget; }
namespace SlateCore { class SlateWidget; }
namespace Engine { class SceneViewport; }
namespace Editor
{
    class EditorScene;
    class EditorScenePanel;
    class EditorViewToolbar;
    class EditorAssetPanel;
    class EditorMaterialPanel;
    class EditorNodePanel;

    class EDITOR_API EditorApplication final : public SlateCore::ApplicationBase
    {
    public:
        EditorApplication();
        ~EditorApplication() override;
        bool Initialize() override;
        bool RequestExit() override { return m_bQuit; }
        void Shutdown() override;
        void TickFrame() override;
        void CreateProject(const std::filesystem::path& directory);
        void OpenProject(const std::filesystem::path& directory);
        void CloseProject();
        void ImportResources(const std::vector<std::filesystem::path>& files);
        const std::filesystem::path& GetProjectDirectory() const { return m_ProjectDirectory; }
        size_t GetSceneObjectCount() const;
    private:
        void DrawProjectMenu();
        std::unique_ptr<EditorScene> m_Scene;
        std::unique_ptr<Engine::SceneViewport> m_Viewport;
        std::unique_ptr<SlateCore::SlateWidget> m_ViewportWidget;
        std::unique_ptr<EditorScenePanel> m_ScenePanel;
        std::unique_ptr<EditorViewToolbar> m_ViewToolbar;
        std::unique_ptr<ImGUISlate::ImSplitterWidget> m_LeftSplitter;
        std::unique_ptr<ImGUISlate::ImSplitterWidget> m_RightSplitter;
        std::unique_ptr<ImGUISlate::ImSplitterWidget> m_BottomSplitter;
        std::unique_ptr<EditorAssetPanel> m_AssetPanel;
        std::unique_ptr<EditorMaterialPanel> m_MaterialPanel;
        std::unique_ptr<EditorNodePanel> m_NodePanel;
        std::unique_ptr<ImGUISlate::ImWidget> m_MenuWidget;
        std::unique_ptr<ImGUISlate::ImFileDialogWidget> m_ProjectDialog;
        std::shared_ptr<ImGUISlate::ImMainWindow> m_Window;
        enum class ProjectAction { None, Create, Open, Close };
        ProjectAction m_PendingProjectAction = ProjectAction::None;
        ProjectAction m_DialogAction = ProjectAction::None;
        std::filesystem::path m_ProjectDirectory;
        std::filesystem::path m_PendingProjectDirectory;
        std::filesystem::path m_DroppedMesh;
        std::string m_ProjectError;
        bool m_bQuit = false;
        bool m_bLoggedFrame = false;
    };
}
