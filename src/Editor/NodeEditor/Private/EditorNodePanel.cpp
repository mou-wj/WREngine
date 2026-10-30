#include "EditorNodePanel.h"
#include "StaticMeshComponent.h"
#include "StaticMesh.h"
#include "StaticMeshAsset.h"
#include "imgui.h"
#include <cmath>
#include <algorithm>
#include <utility>
namespace Editor
{
    EditorNodePanel::EditorNodePanel(std::function<Engine::StaticMeshComponent*()> getSelected, std::function<void()> onChanged)
        : ImPanelWidget("Node Properties"), m_GetSelected(std::move(getSelected)), m_OnChanged(std::move(onChanged)) {}
    void EditorNodePanel::SetTranslationGizmoCallbacks(std::function<bool()> isEnabled, std::function<void(bool)> setEnabled)
    {
        m_IsTranslationGizmoEnabled = std::move(isEnabled);
        m_SetTranslationGizmoEnabled = std::move(setEnabled);
    }
    void EditorNodePanel::SetRotationGizmoCallbacks(std::function<bool()> isEnabled, std::function<void(bool)> setEnabled)
    {
        m_IsRotationGizmoEnabled = std::move(isEnabled);
        m_SetRotationGizmoEnabled = std::move(setEnabled);
    }
    void EditorNodePanel::SetScaleGizmoCallbacks(std::function<bool()> isEnabled, std::function<void(bool)> setEnabled)
    {
        m_IsScaleGizmoEnabled = std::move(isEnabled);
        m_SetScaleGizmoEnabled = std::move(setEnabled);
    }
    void EditorNodePanel::Draw()
    {
        // Skip Begin/End as well as the contents when there is no selected node.
        if (!m_GetSelected || !m_GetSelected()) return;
        ImPanelWidget::Draw();
    }
    bool EditorNodePanel::HitTest(float x, float y) const
    {
        return m_GetSelected && m_GetSelected() && ImPanelWidget::HitTest(x, y);
    }
    void EditorNodePanel::DrawContent()
    {
        auto* component = m_GetSelected ? m_GetSelected() : nullptr;
        if (!component) return;
        ImGui::TextUnformatted("Static mesh node");
        if (component->GetStaticMesh()) ImGui::TextWrapped("Mesh: %s",component->GetStaticMesh()->GetAsset()->GetName().c_str());
        ImGui::SeparatorText("Local transform");
        auto location = component->GetLocalLocation(), rotation = component->GetLocalRotation(), scale = component->GetLocalScale();
        using Controls = ImGUISlate::ImPropertyControls;
        bool changed = false;
        const auto propertyTool = [&](const char* label, Core::Float3& value, float speed, const char* button,
            const char* hint, const std::function<bool()>& isEnabled, const std::function<void(bool)>& setEnabled)
        {
            const auto& style = ImGui::GetStyle();
            const float buttonWidth = ImGui::CalcTextSize(button).x + style.FramePadding.x*2;
            const float width = ImGui::GetContentRegionAvail().x - buttonWidth -
                ImGui::CalcTextSize(label).x - style.ItemInnerSpacing.x - style.ItemSpacing.x;
            ImGui::SetNextItemWidth(std::max(1.0f,width));
            const bool edited = Controls::Vector3(label,value.Data.data(),speed);
            ImGui::SameLine(0,style.ItemSpacing.x);
            const bool available = isEnabled && setEnabled;
            const bool enabled = available && isEnabled();
            ImGui::BeginDisabled(!available);
            if (enabled) ImGui::PushStyleColor(ImGuiCol_Button,ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            ImGui::PushID(label); // "Scale" input and "Scale" button need distinct IDs.
            if (ImGui::Button(button,{buttonWidth,0})) setEnabled(!enabled);
            ImGui::PopID();
            if (enabled) ImGui::PopStyleColor();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s",hint);
            ImGui::EndDisabled();
            return edited;
        };
        if (propertyTool("Position",location,0.05f,"Move","World axes: X red, Y blue, Z green\nPlanes: XY, XZ, YZ",
            m_IsTranslationGizmoEnabled,m_SetTranslationGizmoEnabled)) { component->SetLocalLocation(location); changed = true; }
        if (propertyTool("Rotation",rotation,0.5f,"Rotate","Local X/Y/Z rings\nDrag along a ring; the sector shows the angle",
            m_IsRotationGizmoEnabled,m_SetRotationGizmoEnabled)) { component->SetLocalRotation(rotation); changed = true; }
        if (propertyTool("Scale",scale,0.01f,"Scale","Local axes: X red, Y blue, Z green\nDrag along the axis: outward to enlarge, inward to shrink",
            m_IsScaleGizmoEnabled,m_SetScaleGizmoEnabled))
        {
            for (int axis=0; axis<3; ++axis) if (std::abs(scale.Data[axis])<0.01f) scale.Data[axis] = scale.Data[axis]<0 ? -0.01f : 0.01f;
            component->SetLocalScale(scale); changed = true;
        }
        if (changed && m_OnChanged) m_OnChanged();
        ImGui::Text("Material slots: %u",component->GetNumMaterials());
    }
}
