#pragma once
#include "Widgets/ImCommonWidgets.h"
#include "MaterialLibrary.h"
#include <functional>
#include <memory>

namespace Engine { class StaticMeshComponent; class MaterialAsset; class TextureAsset; }
namespace Editor
{
    // Queries run during Draw on the UI thread; the host owns the selected objects.
    struct MaterialEditorCallbacks
    {
        std::function<Engine::StaticMeshComponent*()> GetSelectedComponent;
        std::function<std::shared_ptr<Engine::MaterialAsset>()> GetSelectedMaterial;
        std::function<std::shared_ptr<Engine::TextureAsset>()> GetSelectedTexture;
        std::function<void(bool bSharedAsset)> OnMaterialChanged;
    };

    class MATERIALEDITOR_API EditorMaterialPanel final : public ImGUISlate::ImPanelWidget
    {
    public:
        explicit EditorMaterialPanel(MaterialEditorCallbacks callbacks);
        EditorMaterialPanel(MaterialEditorCallbacks callbacks, std::filesystem::path libraryRoot);
        void Draw() override;
        void ShowMaterial(std::shared_ptr<Engine::MaterialAsset> material);
        void SetContentRoot(const std::filesystem::path& root);
        bool OnFilesDropped(const SlateCore::FileDropEvent& event) override;
        bool HitTest(float x, float y) const override;
        void OpenMaterial(const std::filesystem::path& path);
        bool ApplySelectedMaterial();
        std::shared_ptr<Engine::MaterialAsset> GetSelectedMaterial() const { return m_SelectedMaterial; }
        const MaterialLibrary& GetLibrary() const { return m_Library; }
    protected:
        void DrawContent() override;
    private:
        void NotifyChanged(bool bSharedAsset);
        void DrawLibrary();
        void DrawProperties();
        void ProcessImport();
        void DuplicateMaterial(const std::filesystem::path& path);
        MaterialEditorCallbacks m_Callbacks;
        MaterialLibrary m_Library;
        ImGUISlate::ImGridWidget m_Grid;
        std::shared_ptr<Engine::MaterialAsset> m_SelectedMaterial;
        std::shared_ptr<Engine::TextureAsset> m_SelectedTexture;
        std::weak_ptr<Engine::MaterialAsset> m_LastExternalMaterial;
        std::weak_ptr<Engine::TextureAsset> m_LastExternalTexture;
        struct PendingImport
        {
            std::filesystem::path Path;
            std::string Category;
        };
        std::vector<PendingImport> m_PendingImports;
        std::vector<std::string> m_ImportErrors;
        uint32_t m_ImportedCount = 0;
        uint32_t m_ImportTotal = 0;
        std::string m_Category;
        std::string m_Search;
        std::string m_NewName = "NewMaterial";
        int m_ShadingFilter = 0;
        float m_TileWidth = 100;
        bool m_bOpenProperties = false;
        bool m_bOpenLibrary = false;
        bool m_bExpanded = false;
        bool m_bPopupOpen = false;
        SlateCore::WidgetGeometry m_PopupGeometry;
        bool m_bExpandedGeometryValid = false;
        SlateCore::WidgetGeometry m_ExpandedGeometry;
        int m_Slot = 0;
        bool m_bEditAsset = false;
        std::string m_SavePath;
        std::string m_Status;
    };
}
