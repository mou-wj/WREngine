#pragma once
#include "ApplicationBase.h"
#include <memory>
#include <filesystem>
#include <vector>
#include <optional>

namespace ImGUISlate { class ImMainWindow; class ImSplitterWidget; class ImWidget; class ImFileDialogWidget; }
namespace SlateCore { class SlateWidget; }
namespace Engine { class SceneViewport; class StaticMeshAsset; class IAsset; }
namespace Editor
{
    class EditorScene;
    enum class EEditorSceneObject;
    class EditorScenePanel;
    class EditorViewToolbar;
    class EditorAssetPanel;
    class EditorMaterialPanel;
    class EditorNodePanel;
    class EditorMeshViewer;
    class EditorTextureViewer;
    struct TextureViewerSource;

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
        void SaveProject();
        void SaveProjectAs(const std::filesystem::path& directory);
        void ImportResources(const std::vector<std::filesystem::path>& files);
        const std::filesystem::path& GetProjectDirectory() const { return m_ProjectDirectory; }
        size_t GetSceneObjectCount() const;
    private:
        void DrawProjectMenu();
        void ResetProjectPanels();
        std::vector<std::shared_ptr<Engine::IAsset>> CollectProjectAssets() const;
        std::vector<TextureViewerSource> CollectTextureViewerSources() const;
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
        std::unique_ptr<EditorMeshViewer> m_MeshViewer;
        std::unique_ptr<EditorTextureViewer> m_TextureViewer;
        std::unique_ptr<ImGUISlate::ImWidget> m_MenuWidget;
        std::unique_ptr<ImGUISlate::ImFileDialogWidget> m_ProjectDialog;
        std::shared_ptr<ImGUISlate::ImMainWindow> m_Window;
        enum class ProjectAction { None, Create, Open, Close, Save, SaveAs, Exit };
        ProjectAction m_PendingProjectAction = ProjectAction::None;
        ProjectAction m_DialogAction = ProjectAction::None;
        ProjectAction m_ConfirmAction = ProjectAction::None;
        std::filesystem::path m_ConfirmDirectory;
        bool m_bShowUnsaved = false;
        bool m_bActionConfirmed = false;
        bool m_bSaveBeforeAction = false;
        std::filesystem::path m_ProjectDirectory;
        std::filesystem::path m_PendingProjectDirectory;
        std::optional<EEditorSceneObject> m_PendingObject;
        int m_PendingDeleteObject = -1;
        std::filesystem::path m_DroppedMesh;
        std::shared_ptr<Engine::StaticMeshAsset> m_PendingMeshAsset;
        std::filesystem::path m_DroppedMaterial;
        Core::Float2 m_DroppedMaterialPosition;
        std::string m_ProjectError;
        bool m_bQuit = false;
        bool m_bLoggedFrame = false;
    };
}
