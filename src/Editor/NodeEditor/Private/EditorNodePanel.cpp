#include "EditorNodePanel.h"
#include "StaticMeshComponent.h"
#include "StaticMesh.h"
#include "StaticMeshAsset.h"
#include "LightComponent.h"
#include "imgui.h"
#include <cmath>
#include <algorithm>
#include <utility>
namespace Editor
{
    EditorNodePanel::EditorNodePanel(std::function<Engine::SceneComponent*()> getSelected, std::function<void()> onChanged)
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
        auto* mesh = dynamic_cast<Engine::StaticMeshComponent*>(component);
        auto* light = dynamic_cast<Engine::LightComponent*>(component);
        const bool sky = light && light->GetLightType() == Engine::ELightType::Sky;
        const bool directional = light && light->GetLightType() == Engine::ELightType::Directional;
        const bool rotatableLight = directional || (light && light->GetLightType() == Engine::ELightType::Spot);
        if (mesh)
        {
            ImGui::TextUnformatted("Static mesh node");
            if (mesh->GetStaticMesh()) ImGui::TextWrapped("Mesh: %s",mesh->GetStaticMesh()->GetAsset()->GetName().c_str());
        }
        else if (light)
        {
            const char* names[] = {"Directional light","Point light","Spot light","Sky light"};
            ImGui::TextUnformatted(names[static_cast<int>(light->GetLightType())]);
        }
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
        // Sky lighting is scene-wide; a component transform has no lighting meaning.
        if (!sky)
        {
            ImGui::SeparatorText("Local transform");
            if (!directional && propertyTool("Position",location,0.05f,"Move","World axes: X red, Y blue, Z green\nPlanes: XY, XZ, YZ",
                m_IsTranslationGizmoEnabled,m_SetTranslationGizmoEnabled)) { component->SetLocalLocation(location); changed = true; }
            if (rotatableLight && propertyTool("Rotation",rotation,0.5f,"Rotate","Rotate the light around its local axes; +X is the light direction",
                m_IsRotationGizmoEnabled,m_SetRotationGizmoEnabled)) { component->SetLocalRotation(rotation); changed = true; }
        }
        if (light)
        {
            // Direction and rotation describe the same light orientation;
            // attenuation range remains independent from component scale.
            ImGui::SeparatorText("Light properties");
            ImGui::BeginDisabled(sky);
            auto color = light->GetColor();
            if (ImGui::ColorEdit3("Color",color.Data.data())) { light->SetColor(color); changed = true; }
            float intensity = light->GetIntensity();
            if (ImGui::DragFloat("Intensity",&intensity,0.05f,0,10000,"%.3f",ImGuiSliderFlags_AlwaysClamp) && std::isfinite(intensity))
            { light->SetIntensity(std::clamp(intensity,0.0f,10000.0f)); changed = true; }
            ImGui::EndDisabled();
            if (sky) ImGui::TextWrapped("Sky color and intensity adjustment is not supported yet.");
            if (!sky)
            {
                bool castShadow = light->IsCastShadow();
                if (ImGui::Checkbox("Cast shadow",&castShadow)) { light->SetCastShadow(castShadow); changed = true; }
            }
            const auto editDirection = [&](auto* directional) {
                auto direction = directional->GetDirection();
                if (Controls::Vector3("Direction",direction.Data.data(),0.01f) &&
                    std::isfinite(direction.X) && std::isfinite(direction.Y) && std::isfinite(direction.Z) &&
                    direction.LengthSquared() > 1e-8f)
                { directional->SetDirection(Core::Normalize(direction)); changed = true; }
            };
            if (auto* directional = dynamic_cast<Engine::DirectionalLightComponent*>(light)) editDirection(directional);
            if (auto* local = dynamic_cast<Engine::LocalLightComponent*>(light))
            {
                float radius = local->GetAttenuationRadius();
                if (ImGui::DragFloat("Range (m)",&radius,0.1f,0.01f,10000,"%.2f",ImGuiSliderFlags_AlwaysClamp) && std::isfinite(radius))
                { local->SetAttenuationRadius(std::clamp(radius,0.01f,10000.0f)); changed = true; }
            }
            if (auto* spot = dynamic_cast<Engine::SpotLightComponent*>(light))
            {
                editDirection(spot);
                float inner = Core::RadToDeg(spot->GetInnerConeAngle()), outer = Core::RadToDeg(spot->GetOuterConeAngle());
                bool coneChanged = ImGui::DragFloat("Inner angle (deg)",&inner,0.25f,0,outer,"%.2f",ImGuiSliderFlags_AlwaysClamp);
                coneChanged |= ImGui::DragFloat("Outer angle (deg)",&outer,0.25f,1,89,"%.2f",ImGuiSliderFlags_AlwaysClamp);
                if (coneChanged && std::isfinite(inner) && std::isfinite(outer))
                {
                    outer = std::clamp(outer,1.0f,89.0f); inner = std::clamp(inner,0.0f,outer);
                    spot->SetInnerConeAngle(inner); spot->SetOuterConeAngle(outer); changed = true;
                }
            }
            if (changed && m_OnChanged) m_OnChanged();
            return;
        }
        if (propertyTool("Rotation",rotation,0.5f,"Rotate","Local X/Y/Z rings\nDrag along a ring; the sector shows the angle",
            m_IsRotationGizmoEnabled,m_SetRotationGizmoEnabled)) { component->SetLocalRotation(rotation); changed = true; }
        if (propertyTool("Scale",scale,0.01f,"Scale","Local axes: X red, Y blue, Z green\nDrag along the axis: outward to enlarge, inward to shrink",
            m_IsScaleGizmoEnabled,m_SetScaleGizmoEnabled))
        {
            for (int axis=0; axis<3; ++axis) if (std::abs(scale.Data[axis])<0.01f) scale.Data[axis] = scale.Data[axis]<0 ? -0.01f : 0.01f;
            component->SetLocalScale(scale); changed = true;
        }
        if (changed && m_OnChanged) m_OnChanged();
        if (mesh) ImGui::Text("Material slots: %u",mesh->GetNumMaterials());
    }
}
