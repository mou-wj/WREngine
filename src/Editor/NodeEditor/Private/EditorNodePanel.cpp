#include "EditorNodePanel.h"
#include "StaticMeshComponent.h"
#include "StaticMesh.h"
#include "StaticMeshAsset.h"
#include "imgui.h"
#include <cmath>
#include <utility>
namespace Editor
{
    EditorNodePanel::EditorNodePanel(std::function<Engine::StaticMeshComponent*()> getSelected, std::function<void()> onChanged)
        : ImPanelWidget("Node Properties"), m_GetSelected(std::move(getSelected)), m_OnChanged(std::move(onChanged)) {}
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
        if (Controls::Vector3("Position",location.Data.data())) { component->SetLocalLocation(location); changed = true; }
        if (Controls::Vector3("Rotation",rotation.Data.data(),0.5f)) { component->SetLocalRotation(rotation); changed = true; }
        if (Controls::Vector3("Scale",scale.Data.data(),0.01f))
        {
            for (int axis=0; axis<3; ++axis) if (std::abs(scale.Data[axis])<0.01f) scale.Data[axis] = scale.Data[axis]<0 ? -0.01f : 0.01f;
            component->SetLocalScale(scale); changed = true;
        }
        if (changed && m_OnChanged) m_OnChanged();
        ImGui::Text("Material slots: %u",component->GetNumMaterials());
    }
}
