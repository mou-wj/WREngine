#pragma once
#include "ImWidget.h"
#include <array>
#include <string>
#include <vector>

namespace ImGUISlate
{
    // A splitter occupies the gap between regions; the host owns layout changes.
    class IMGUISLATE_API ImSplitterWidget : public ImWidget
    {
    public:
        ImSplitterWidget(std::string id, bool bVertical);
        void SetOnDragged(std::function<void(float)> callback);
        void Draw() override;
    private:
        std::string m_Id;
        bool m_bVertical;
        float m_LastDragPosition = 0;
        std::function<void(float)> m_OnDragged;
    };

    // A logical panel in the current window. It never owns a native surface.
    class IMGUISLATE_API ImPanelWidget : public ImWidget
    {
    public:
        explicit ImPanelWidget(std::string title);
        void Draw() override;
    protected:
        virtual void DrawContent() = 0;
    private:
        std::string m_Title;
    };

    class IMGUISLATE_API ImListWidget : public ImWidget
    {
    public:
        void SetItems(std::vector<std::string> items);
        void SetSelectedIndex(int index);
        int GetSelectedIndex() const { return m_SelectedIndex; }
        void SetOnSelectionChanged(std::function<void(int)> callback);
        void Draw() override;
    private:
        std::vector<std::string> m_Items;
        int m_SelectedIndex = -1;
        std::function<void(int)> m_OnSelectionChanged;
    };

    enum class EImGridIcon { ColorSphere, Folder, Image, Mesh, Document };

    struct ImGridItem
    {
        std::string Id;
        std::string Label;
        std::string Subtitle;
        std::array<float, 4> Color{0.6f, 0.6f, 0.6f, 1};
        EImGridIcon Icon = EImGridIcon::ColorSphere;
    };

    // Grid entries contain display metadata, never Engine or GPU resource owners.
    class IMGUISLATE_API ImGridWidget : public ImWidget
    {
    public:
        void SetItems(std::vector<ImGridItem> items);
        void SetSelectedId(std::string id);
        void SetTileWidth(float width);
        void SetOnSelectionChanged(std::function<void(const std::string&, bool)> callback);
        void SetOnItemActions(std::function<void(const std::string&)> callback);
        void Draw() override;
    private:
        std::vector<ImGridItem> m_Items;
        std::string m_SelectedId;
        float m_TileWidth = 112;
        std::function<void(const std::string&, bool)> m_OnSelectionChanged;
        std::function<void(const std::string&)> m_OnItemActions;
    };

    // Immediate-mode controls: values belong to the caller, not to an editor cache.
    class IMGUISLATE_API ImPropertyControls
    {
    public:
        static bool Text(const char* label, std::string& value, const char* hint = nullptr);
        static bool Scalar(const char* label, float& value, float speed = 0.01f,
                           float minimum = 0, float maximum = 0);
        static bool Vector3(const char* label, float* value, float speed = 0.05f);
        static bool Vector4(const char* label, std::array<float, 4>& value);
        static bool Color(const char* label, std::array<float, 4>& value);
        static bool Boolean(const char* label, bool& value);
        static bool Choice(const char* label, int& index, const char* const* items, int count);
    };
}
