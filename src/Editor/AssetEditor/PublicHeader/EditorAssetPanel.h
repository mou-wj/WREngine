#pragma once
#include "Widgets/ImCommonWidgets.h"
#include "Widgets/ImFileDialogWidget.h"
#include <filesystem>
#include <functional>
#include <memory>
#include <vector>
#include <map>

namespace Engine { class MaterialAsset; class TextureAsset; class StaticMeshAsset; }
namespace Editor
{
    class ASSETEDITOR_API EditorAssetPanel final : public ImGUISlate::ImPanelWidget
    {
    public:
        using MeshOpenedCallback = std::function<void(const std::shared_ptr<Engine::StaticMeshAsset>&)>;
        using MaterialAssignedCallback = std::function<bool(const std::shared_ptr<Engine::MaterialAsset>&)>;
        EditorAssetPanel();
        explicit EditorAssetPanel(std::filesystem::path contentRoot);
        void SetOnMeshOpened(MeshOpenedCallback callback);
        void SetOnMaterialAssigned(MaterialAssignedCallback callback);
        void SetOnMaterialOpened(std::function<void(const std::shared_ptr<Engine::MaterialAsset>&)> callback);
        void SetContentRoot(const std::filesystem::path& root);
        void QueueImport(const std::vector<std::filesystem::path>& files);
        bool OnFilesDropped(const SlateCore::FileDropEvent& event) override;
        std::shared_ptr<Engine::MaterialAsset> GetSelectedMaterial() const { return m_Material; }
        std::shared_ptr<Engine::TextureAsset> GetSelectedTexture() const { return m_Texture; }
        const std::filesystem::path& GetDirectory() const { return m_Directory; }
        const std::filesystem::path& GetSelectedPath() const { return m_SelectedPath; }
    protected:
        void DrawContent() override;
    private:
        void Refresh();
        void OpenSelected();
        bool NavigateTo(const std::filesystem::path& directory, bool bRecordHistory = true);
        void SelectPath(const std::filesystem::path& path, bool bOpenMaterial = false);
        void DrawTree(const std::filesystem::path& path);
        void DrawResources();
        void DrawStatus();
        std::filesystem::path ImportFile(const std::filesystem::path& source, const std::filesystem::path& directory);
        struct FileEntry
        {
            std::filesystem::path Path;
            bool bDirectory = false;
        };
        const std::vector<FileEntry>& ReadDirectory(const std::filesystem::path& directory);
        bool MatchesTree(const FileEntry& entry);
        ImGUISlate::ImGridWidget m_Grid;
        std::filesystem::path m_Root;
        std::filesystem::path m_Directory;
        std::filesystem::path m_SelectedPath;
        std::map<std::filesystem::path, std::vector<FileEntry>> m_TreeEntries;
        std::vector<std::filesystem::path> m_History;
        size_t m_HistoryIndex = 0;
        std::string m_PathInput;
        std::string m_Filter;
        std::string m_TreeFilter;
        std::string m_LastError;
        int m_TypeFilter = 0;
        float m_TileWidth = 88;
        float m_TreeWidth = 240;
        float m_LastSplitPosition = 0;
        bool m_bRevealDirectory = true;
        std::shared_ptr<Engine::MaterialAsset> m_Material;
        std::shared_ptr<Engine::TextureAsset> m_Texture;
        MeshOpenedCallback m_OnMeshOpened;
        MaterialAssignedCallback m_OnMaterialAssigned;
        std::function<void(const std::shared_ptr<Engine::MaterialAsset>&)> m_OnMaterialOpened;
        ImGUISlate::ImFileDialogWidget m_ImportDialog;
        struct PendingImport { std::filesystem::path Source, Directory; };
        std::vector<PendingImport> m_PendingImports;
    };
}
