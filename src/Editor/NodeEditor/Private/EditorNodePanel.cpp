#include "EditorNodePanel.h"
#include "SceneComponent.h"
#include "TextureAsset.h"
#include <stdexcept>
#include "imgui.h"
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
    void EditorNodePanel::SetSelectedTextureProvider(std::function<std::shared_ptr<Engine::TextureAsset>()> getTexture)
    {
        m_GetSelectedTexture=std::move(getTexture);
    }
    bool EditorNodePanel::HitTest(float x, float y) const
    {
        return m_GetSelected && m_GetSelected() && ImPanelWidget::HitTest(x, y);
    }
    void EditorNodePanel::DrawContent()
    {
        auto* component=m_GetSelected ? m_GetSelected() : nullptr;
        if (!component) return;
        std::vector<Engine::ComponentProperty> properties;
        component->DescribeProperties(properties);
        using Type=Engine::EComponentPropertyType;
        using Semantic=Engine::EComponentPropertySemantic;
        using Controls=ImGUISlate::ImPropertyControls;
        std::string category;
        bool changed=false;
        for (const auto& property:properties)
        {
            if (!property.Read) throw std::logic_error("Component property has no read callback");
            if (property.Category!=category)
            {
                category=property.Category;
                if (!category.empty()) ImGui::SeparatorText(category.c_str());
            }
            auto value=property.Read();
            ImGui::PushID(property.Id.c_str());
            if (property.Type==Type::Text)
            {
                ImGui::TextWrapped("%s: %s",property.Label.c_str(),std::get<std::string>(value).c_str());
                ImGui::PopID();
                continue;
            }
            const bool writable=!property.bReadOnly && static_cast<bool>(property.Write);
            ImGui::BeginDisabled(!writable);
            bool edited=false;
            const std::function<bool()>* isToolEnabled=nullptr;
            const std::function<void(bool)>* setToolEnabled=nullptr;
            const char* tool=nullptr;
            const char* hint=nullptr;
            switch (property.Semantic)
            {
            case Semantic::Position:
                tool="Move";hint="World axes: X red, Y blue, Z green\nPlanes: XY, XZ, YZ";
                isToolEnabled=&m_IsTranslationGizmoEnabled;setToolEnabled=&m_SetTranslationGizmoEnabled;break;
            case Semantic::Rotation:
                tool="Rotate";hint="Local X/Y/Z rings\nDrag along a ring; the sector shows the angle";
                isToolEnabled=&m_IsRotationGizmoEnabled;setToolEnabled=&m_SetRotationGizmoEnabled;break;
            case Semantic::Scale:
                tool="Scale";hint="Local axes: X red, Y blue, Z green\nDrag outward to enlarge, inward to shrink";
                isToolEnabled=&m_IsScaleGizmoEnabled;setToolEnabled=&m_SetScaleGizmoEnabled;break;
            default:break;
            }
            float buttonWidth=0;
            if (tool)
            {
                const auto& style=ImGui::GetStyle();
                buttonWidth=ImGui::CalcTextSize(tool).x+style.FramePadding.x*2;
                ImGui::SetNextItemWidth(std::max(1.0f,ImGui::GetContentRegionAvail().x-buttonWidth-
                    ImGui::CalcTextSize(property.Label.c_str()).x-style.ItemInnerSpacing.x-style.ItemSpacing.x));
            }
            switch (property.Type)
            {
            case Type::Boolean: edited=Controls::Boolean(property.Label.c_str(),std::get<bool>(value));break;
            case Type::Scalar: edited=Controls::Scalar(property.Label.c_str(),std::get<float>(value),property.Speed,property.Minimum,property.Maximum);break;
            case Type::Vector3: edited=Controls::Vector3(property.Label.c_str(),std::get<Core::Float3>(value).Data.data(),property.Speed);break;
            case Type::Color3: edited=ImGui::ColorEdit3(property.Label.c_str(),std::get<Core::Float3>(value).Data.data());break;
            case Type::TextureAsset:
            {
                const auto& texture=std::get<std::shared_ptr<Engine::TextureAsset>>(value);
                ImGui::TextWrapped("%s: %s",property.Label.c_str(),texture ? texture->GetName().c_str() : "Default");
                const auto selected=m_GetSelectedTexture ? m_GetSelectedTexture() : nullptr;
                ImGui::BeginDisabled(!selected);
                if (ImGui::Button("Use selected texture")) { value=selected; edited=true; }
                ImGui::EndDisabled(); ImGui::SameLine();
                if (ImGui::Button("Reset")) { value=std::shared_ptr<Engine::TextureAsset>{}; edited=true; }
                break;
            }
            default:break;
            }
            if (edited && writable) {
                try {
                    if (property.Write(value)) { changed=true; m_PropertyError.clear(); }
                    else m_PropertyError="Cannot apply " + property.Label;
                }
                catch (const std::exception& error) { m_PropertyError=error.what(); }
            }
            ImGui::EndDisabled();
            if (!property.Tooltip.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("%s",property.Tooltip.c_str());
            if (tool)
            {
                ImGui::SameLine(0,ImGui::GetStyle().ItemSpacing.x);
                const bool available=writable && *isToolEnabled && *setToolEnabled;
                const bool enabled=available && (*isToolEnabled)();
                ImGui::BeginDisabled(!available);
                if (enabled) ImGui::PushStyleColor(ImGuiCol_Button,ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                ImGui::PushID("Tool");
                if (ImGui::Button(tool,{buttonWidth,0})) (*setToolEnabled)(!enabled);
                ImGui::PopID();
                if (enabled) ImGui::PopStyleColor();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s",hint);
                ImGui::EndDisabled();
            }
            ImGui::PopID();
        }
        if (!m_PropertyError.empty()) ImGui::TextWrapped("Property error: %s",m_PropertyError.c_str());
        if (changed && m_OnChanged) m_OnChanged();
    }
}
