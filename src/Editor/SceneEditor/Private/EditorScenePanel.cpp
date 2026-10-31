#include "EditorScenePanel.h"
#include "EditorScene.h"
#include "StaticMeshComponent.h"
#include "imgui.h"
#include <cmath>
#include <utility>

namespace Editor
{
    using Controls = ImGUISlate::ImPropertyControls;
    void EditorSceneViewportWidget::SetOnResourceDropped(std::function<void(const std::filesystem::path&)> callback)
    { m_OnResourceDropped = std::move(callback); }
    void EditorSceneViewportWidget::SetOnMaterialDropped(std::function<void(const std::filesystem::path&, const Core::Float2&)> callback)
    { m_OnMaterialDropped = std::move(callback); }
    void EditorSceneViewportWidget::Draw()
    {
        const auto& geometry=GetGeometry();
        const auto mouse=ImGui::GetIO().MousePos;
        // This only refreshes hover presentation. Mouse events and capture still
        // follow Window; another GUI window over the viewport suppresses hover.
        if (m_Scene) m_Scene->UpdateViewportHover(ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow) ? Core::Float2{-1,-1} :
            Core::Float2{mouse.x-geometry.X,mouse.y-geometry.Y});
        SlateWidget::Draw();
        const auto* payload = ImGui::GetDragDropPayload();
        if (!payload || (!payload->IsDataType("EDITOR_ASSET_PATH") && !payload->IsDataType("EDITOR_MATERIAL_PATH"))) return;
        const bool material = payload->IsDataType("EDITOR_MATERIAL_PATH");
        if (geometry.Width <= 0 || geometry.Height <= 0) return;
        ImGui::SetNextWindowPos({geometry.X,geometry.Y}); ImGui::SetNextWindowSize({geometry.Width,geometry.Height});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0,0});
        const auto flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
            ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar;
        if (ImGui::Begin("Scene resource drop target", nullptr, flags))
        {
            ImGui::InvisibleButton("Drop resource", {geometry.Width,geometry.Height});
            if (ImGui::BeginDragDropTarget())
            {
                if (const auto* accepted = ImGui::AcceptDragDropPayload(material ? "EDITOR_MATERIAL_PATH" : "EDITOR_ASSET_PATH"))
                    if (accepted->DataSize > 0 && static_cast<const char*>(accepted->Data)[accepted->DataSize-1] == '\0')
                    {
                        const auto path = std::filesystem::u8path(static_cast<const char*>(accepted->Data));
                        if (material && m_OnMaterialDropped)
                        {
                            const auto mouse = ImGui::GetIO().MousePos;
                            m_OnMaterialDropped(path, {(mouse.x-geometry.X)/geometry.Width, (mouse.y-geometry.Y)/geometry.Height});
                        }
                        else if (!material && m_OnResourceDropped) m_OnResourceDropped(path);
                    }
                ImGui::EndDragDropTarget();
            }
        }
        ImGui::End(); ImGui::PopStyleVar();
    }
    EditorViewToolbar::EditorViewToolbar(EditorScene& scene) : m_Scene(scene) {}
    void EditorViewToolbar::Draw()
    {
        const auto& geometry = GetGeometry();
        if (GetVisibility() != SlateCore::EVisibility::Visible || geometry.Width <= 0 || geometry.Height <= 0) return;
        ImGui::SetNextWindowPos({geometry.X, geometry.Y}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({geometry.Width, geometry.Height}, ImGuiCond_Always);
        const auto flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_HorizontalScrollbar;
        if (ImGui::Begin("EditorRibbon", nullptr, flags))
        {
            if (ImGui::BeginTabBar("Ribbon tabs"))
            {
                if (ImGui::BeginTabItem("View"))
                {
                    struct ViewButton { const char* Label; const char* Hint; EEditorViewDirection Direction; };
                    constexpr ViewButton buttons[] = {
                        {"Top\n+Y", "Look down from +Y; screen up is -Z", EEditorViewDirection::Top},
                        {"Bottom\n-Y", "Look up from -Y; screen up is +Z", EEditorViewDirection::Bottom},
                        {"Left\n-X", "Look from -X toward +X", EEditorViewDirection::Left},
                        {"Right\n+X", "Look from +X toward -X", EEditorViewDirection::Right},
                        {"Front\n+Z", "Look from +Z toward -Z", EEditorViewDirection::Front},
                        {"Back\n-Z", "Look from -Z toward +Z", EEditorViewDirection::Back}
                    };
                    ImGui::BeginGroup();
                    for (size_t index = 0; index < std::size(buttons); ++index)
                    {
                        if (index) ImGui::SameLine();
                        const auto& button = buttons[index];
                        const bool selected = m_Scene.GetViewDirection() == button.Direction;
                        if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                        if (ImGui::Button(button.Label, {66, 44})) m_Scene.SetViewDirection(button.Direction);
                        if (selected) ImGui::PopStyleColor();
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", button.Hint);
                    }
                    ImGui::TextDisabled("Standard views");
                    ImGui::EndGroup();
                    ImGui::SameLine(0, 24);
                    ImGui::BeginGroup();
                    if (ImGui::Button("Orbit\n3D", {72, 44})) m_Scene.SetViewDirection(EEditorViewDirection::Orbit);
                    ImGui::SameLine();
                    ImGui::BeginDisabled(!m_Scene.GetSelected());
                    if (ImGui::Button("Focus\nselected", {88, 44})) m_Scene.FocusSelected();
                    ImGui::EndDisabled();
                    ImGui::TextDisabled("Camera | Perspective");
                    ImGui::EndGroup();
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
        }
        ImGui::End();
    }

    EditorScenePanel::EditorScenePanel(EditorScene& scene) : ImPanelWidget("Scene"), m_Scene(scene)
    {
        m_Objects.SetOnSelectionChanged([this](int index) { m_Scene.Select(index); });
    }
    void EditorScenePanel::DrawContent()
    {
        ImGui::TextUnformatted("Scene objects");
        std::vector<std::string> names;
        for (const auto& object : m_Scene.GetObjects()) names.push_back(object.Name);
        m_Objects.SetItems(std::move(names));
        m_Objects.SetSelectedIndex(m_Scene.GetSelectedIndex());
        if (ImGui::BeginChild("Objects", {0, 150}, ImGuiChildFlags_Borders)) m_Objects.Draw();
        ImGui::EndChild();
        ImGui::BeginDisabled(!m_Scene.GetSelectedMesh());
        if (ImGui::Button("Duplicate")) m_Scene.DuplicateSelected();
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!m_Scene.GetSelected());
        if (ImGui::Button("Remove")) m_Scene.RemoveSelected();
        if (ImGui::Button("Focus selected")) m_Scene.FocusSelected();
        ImGui::EndDisabled();
        ImGui::SeparatorText("Viewport controls");
        ImGui::TextWrapped("Left click: select / clear\nLight diamond: select light\nRight drag: orbit\nMiddle drag: pan\nWheel: zoom");
        ImGui::TextDisabled("Scene serialization is not implemented.");
    }
}
