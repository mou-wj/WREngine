#pragma once
#include "ImWidget.h"
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Engine { class TextureAsset; class EnvironmentLightResource; }
namespace RenderCore { class RenderTexture; }
namespace Editor
{
    enum class ETextureViewerSource { Asset, EnvironmentDiffuse, EnvironmentSpecular };
    // Keep the actual owner while inspecting a texture; the path is only for a
    // not-yet-loaded file candidate, consumed on confirmation in Tick.
    struct TEXTUREVIEWER_API TextureViewerSource
    {
        std::string Id;
        std::string Name;
        ETextureViewerSource Kind=ETextureViewerSource::Asset;
        std::filesystem::path Path;
        std::shared_ptr<Engine::TextureAsset> Asset;
        std::shared_ptr<const Engine::EnvironmentLightResource> Environment;
        RenderCore::RenderTexture* GetTexture() const;
    };

    class TextureViewerPreview;
    class TEXTUREVIEWER_API EditorTextureViewer final : public ImGUISlate::PopupImWidget
    {
    public:
        explicit EditorTextureViewer(std::function<std::vector<TextureViewerSource>()> getSources);
        ~EditorTextureViewer() override;
        EditorTextureViewer(const EditorTextureViewer&)=delete;
        EditorTextureViewer& operator=(const EditorTextureViewer&)=delete;
        void Open();
        // Apply additions/removals and render previews before GUI records uses.
        void Tick();
        void Reset();
        void Draw() override;
        bool HitTest(float x,float y) const override;
    private:
        struct Image;
        void RefreshSources();
        void DrawList();
        void DrawPicker();
        void DrawCanvas();
        Image* GetSelected();
        std::function<std::vector<TextureViewerSource>()> m_GetSources;
        std::vector<std::unique_ptr<Image>> m_Images;
        std::vector<TextureViewerSource> m_Sources;
        std::vector<std::string> m_PickerSelection;
        std::vector<TextureViewerSource> m_PendingAdd;
        std::vector<uint64_t> m_PendingRemove;
        SlateCore::WidgetGeometry m_PopupGeometry;
        SlateCore::WidgetGeometry m_PickerGeometry;
        std::string m_Search;
        std::string m_Status;
        uint64_t m_Selected=0;
        uint64_t m_NextId=1;
        uint64_t m_DragImage=0;
        double m_Zoom=1;
        double m_PanX=0;
        double m_PanY=0;
        int m_SourceFilter=0;
        bool m_bOpen=false;
        bool m_bVisible=false;
        bool m_bRequestPicker=false;
        bool m_bPickerOpen=false;
        bool m_bPanning=false;
    };
}
