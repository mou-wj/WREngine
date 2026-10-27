#include "Widgets/ImCommonWidgets.h"
#include "imgui.h"
#include <utility>
#include <algorithm>
#include <cmath>

namespace ImGUISlate
{
    ImSplitterWidget::ImSplitterWidget(std::string id, bool bVertical)
        : m_Id(std::move(id)), m_bVertical(bVertical) {}
    void ImSplitterWidget::SetOnDragged(std::function<void(float)> callback) { m_OnDragged = std::move(callback); }
    void ImSplitterWidget::Draw()
    {
        const auto geometry = GetGeometry();
        if (GetVisibility() != SlateCore::EVisibility::Visible || geometry.Width <= 0 || geometry.Height <= 0) return;
        ImGui::SetNextWindowPos({geometry.X, geometry.Y}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({geometry.Width, geometry.Height}, ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, {1, 1});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
        const auto flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing;
        if (ImGui::Begin(m_Id.c_str(), nullptr, flags))
        {
            ImGui::InvisibleButton("Drag", {geometry.Width, geometry.Height});
            const bool hovered = ImGui::IsItemHovered(), active = ImGui::IsItemActive();
            if (hovered || active) ImGui::SetMouseCursor(m_bVertical ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS);
            const auto color = active ? ImGuiCol_SeparatorActive : hovered ? ImGuiCol_SeparatorHovered : ImGuiCol_Separator;
            ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImGui::GetColorU32(color));
            const float position = m_bVertical ? ImGui::GetIO().MousePos.x : ImGui::GetIO().MousePos.y;
            if (ImGui::IsItemActivated()) m_LastDragPosition = position;
            if (active)
            {
                const float delta = position - m_LastDragPosition;
                m_LastDragPosition = position;
                // The callback may resize a viewport; run before scene/UI drawing.
                if (delta != 0 && m_OnDragged) m_OnDragged(delta);
            }
        }
        ImGui::End();
        ImGui::PopStyleVar(4);
    }

    ImPanelWidget::ImPanelWidget(std::string title) : m_Title(std::move(title)) {}
    void ImPanelWidget::Draw()
    {
        if (GetVisibility() != SlateCore::EVisibility::Visible) return;
        const auto& geometry = GetGeometry();
        if (geometry.Width <= 0 || geometry.Height <= 0) return;
        ImGui::SetNextWindowPos({geometry.X, geometry.Y}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({geometry.Width, geometry.Height}, ImGuiCond_Always);
        const auto flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse;
        if (ImGui::Begin(m_Title.c_str(), nullptr, flags)) DrawContent();
        ImGui::End();
    }
    void ImListWidget::SetItems(std::vector<std::string> items)
    {
        m_Items = std::move(items);
        if (m_SelectedIndex >= static_cast<int>(m_Items.size())) m_SelectedIndex = -1;
    }
    void ImListWidget::SetSelectedIndex(int index)
    {
        m_SelectedIndex = index >= 0 && index < static_cast<int>(m_Items.size()) ? index : -1;
    }
    void ImListWidget::SetOnSelectionChanged(std::function<void(int)> callback)
    { m_OnSelectionChanged = std::move(callback); }
    void ImListWidget::Draw()
    {
        if (GetVisibility() != SlateCore::EVisibility::Visible) return;
        ImGui::PushID(this);
        for (int index = 0; index < static_cast<int>(m_Items.size()); ++index)
        {
            ImGui::PushID(index);
            if (ImGui::Selectable(m_Items[index].c_str(), index == m_SelectedIndex))
            {
                m_SelectedIndex = index;
                if (m_OnSelectionChanged) m_OnSelectionChanged(index);
            }
            ImGui::PopID();
        }
        ImGui::PopID();
    }
    void ImGridWidget::SetItems(std::vector<ImGridItem> items) { m_Items = std::move(items); }
    void ImGridWidget::SetSelectedId(std::string id) { m_SelectedId = std::move(id); }
    void ImGridWidget::SetTileWidth(float width) { m_TileWidth = std::clamp(width, 72.0f, 240.0f); }
    void ImGridWidget::SetOnSelectionChanged(std::function<void(const std::string&, bool)> callback)
    { m_OnSelectionChanged = std::move(callback); }
    void ImGridWidget::SetOnItemActions(std::function<void(const std::string&)> callback)
    { m_OnItemActions = std::move(callback); }
    void ImGridWidget::Draw()
    {
        if (GetVisibility() != SlateCore::EVisibility::Visible) return;
        ImGui::PushID(this);
        const int columns = std::max(1, static_cast<int>(ImGui::GetContentRegionAvail().x / (m_TileWidth + 8)));
        if (ImGui::BeginTable("Grid", columns, ImGuiTableFlags_SizingStretchSame))
        {
            for (const auto& item : m_Items)
            {
                ImGui::TableNextColumn();
                ImGui::PushID(item.Id.c_str());
                const auto origin = ImGui::GetCursorScreenPos();
                const float width = std::max(16.0f, ImGui::GetContentRegionAvail().x);
                const float height = m_TileWidth + 30;
                ImGui::InvisibleButton("Card", {width, height});
                const bool hovered = ImGui::IsItemHovered();
                if (ImGui::IsItemClicked())
                {
                    m_SelectedId = item.Id;
                    if (m_OnSelectionChanged) m_OnSelectionChanged(item.Id, ImGui::IsMouseDoubleClicked(0));
                }
                auto* draw = ImGui::GetWindowDrawList();
                const ImVec2 maximum{origin.x + width, origin.y + height};
                draw->AddRectFilled(origin, maximum, ImGui::GetColorU32(hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg), 5);
                if (m_SelectedId == item.Id) draw->AddRect(origin, maximum, ImGui::GetColorU32(ImGuiCol_CheckMark), 5.0f, 2.0f);
                const float radius = std::min(width - 16, m_TileWidth - 18) * 0.5f;
                const ImVec2 center{origin.x + width * 0.5f, origin.y + m_TileWidth * 0.5f};
                const auto color = ImGui::ColorConvertFloat4ToU32({item.Color[0], item.Color[1], item.Color[2], 1});
                const ImVec2 low{center.x - radius, center.y - radius * 0.7f};
                const ImVec2 high{center.x + radius, center.y + radius * 0.7f};
                if (item.Icon == EImGridIcon::ColorSphere)
                {
                    // UI swatches/icons do not allocate asset textures or a PBR preview.
                    for (int ring = 24; ring >= 1; --ring)
                    {
                        const float ratio = ring / 24.0f;
                        const float light = 0.35f + 0.65f * std::sqrt(std::max(0.0f, 1 - ratio * ratio));
                        draw->AddCircleFilled(center, std::max(1.0f, radius * ratio), ImGui::ColorConvertFloat4ToU32(
                            {item.Color[0] * light, item.Color[1] * light, item.Color[2] * light, 1}), 32);
                    }
                }
                else if (item.Icon == EImGridIcon::Folder)
                {
                    draw->AddRectFilled({low.x, low.y - 5}, {center.x, center.y}, color, 3);
                    draw->AddRectFilled(low, high, color, 4);
                    draw->AddLine({low.x + 3, low.y + 6}, {high.x - 3, low.y + 6}, ImGui::GetColorU32(ImGuiCol_Text), 1);
                }
                else if (item.Icon == EImGridIcon::Image)
                {
                    draw->AddRect(low, high, color, 3.0f, 2.0f);
                    draw->AddCircleFilled({high.x - radius * 0.4f, low.y + radius * 0.35f}, radius * 0.12f, color);
                    draw->AddTriangleFilled({low.x + 4, high.y - 4}, {center.x - radius * 0.3f, center.y - radius * 0.2f}, {center.x + radius * 0.4f, high.y - 4}, color);
                    draw->AddTriangleFilled({center.x, high.y - 4}, {center.x + radius * 0.35f, center.y}, {high.x - 4, high.y - 4}, color);
                }
                else if (item.Icon == EImGridIcon::Mesh)
                {
                    const ImVec2 top{center.x, low.y}, left{low.x, center.y - radius * 0.2f}, right{high.x, center.y - radius * 0.2f};
                    const ImVec2 middle{center.x, center.y + radius * 0.2f}, bottom{center.x, high.y};
                    draw->AddQuad(top, right, middle, left, color, 2);
                    draw->AddQuad(left, middle, bottom, {left.x, center.y + radius * 0.4f}, color, 2);
                    draw->AddQuad(middle, right, {right.x, center.y + radius * 0.4f}, bottom, color, 2);
                }
                else
                {
                    draw->AddRect({low.x + radius * 0.3f, low.y}, {high.x - radius * 0.3f, high.y}, color, 3.0f, 2.0f);
                    for (int line = 0; line < 3; ++line)
                        draw->AddLine({low.x + radius * 0.5f, center.y + line * 6 - 6}, {high.x - radius * 0.5f, center.y + line * 6 - 6}, color, 2);
                }
                draw->PushClipRect(origin, maximum, true);
                draw->AddText({origin.x + 6, origin.y + m_TileWidth}, ImGui::GetColorU32(ImGuiCol_Text), item.Label.c_str());
                draw->AddText({origin.x + 6, origin.y + m_TileWidth + ImGui::GetTextLineHeight()}, ImGui::GetColorU32(ImGuiCol_TextDisabled), item.Subtitle.c_str());
                draw->PopClipRect();
                if (m_OnItemActions) m_OnItemActions(item.Id);
                if (hovered) ImGui::SetTooltip("%s\n%s", item.Label.c_str(), item.Id.c_str());
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::PopID();
    }

    bool ImPropertyControls::Text(const char* label, std::string& value, const char* hint)
    {
        // ImGui asks the owner to resize before replacing its UTF-8 buffer.
        auto resize = [](ImGuiInputTextCallbackData* data) {
            auto& text = *static_cast<std::string*>(data->UserData);
            text.resize(static_cast<size_t>(data->BufTextLen));
            data->Buf = text.data();
            return 0;
        };
        return ImGui::InputTextWithHint(label, hint ? hint : "", value.data(), value.capacity() + 1,
                                       ImGuiInputTextFlags_CallbackResize, resize, &value);
    }
    bool ImPropertyControls::Scalar(const char* label, float& value, float speed, float minimum, float maximum)
    { return ImGui::DragFloat(label, &value, speed, minimum, maximum); }
    bool ImPropertyControls::Vector3(const char* label, float* value, float speed)
    { return ImGui::DragFloat3(label, value, speed); }
    bool ImPropertyControls::Vector4(const char* label, std::array<float, 4>& value)
    { return ImGui::DragFloat4(label, value.data(), 0.01f); }
    bool ImPropertyControls::Color(const char* label, std::array<float, 4>& value)
    { return ImGui::ColorEdit4(label, value.data()); }
    bool ImPropertyControls::Boolean(const char* label, bool& value)
    { return ImGui::Checkbox(label, &value); }
    bool ImPropertyControls::Choice(const char* label, int& index, const char* const* items, int count)
    { return ImGui::Combo(label, &index, items, count); }
}
