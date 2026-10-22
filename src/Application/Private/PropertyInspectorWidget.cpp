#include "PropertyInspectorWidget.h"

#include "imgui.h"

namespace App
{
    PropertyInspectorWidget::PropertyInspectorWidget() = default;

    PropertyInspectorWidget::PropertyInspectorWidget(std::vector<std::pair<std::string, std::string>> properties)
        : m_Properties(std::move(properties))
    {
    }

    void PropertyInspectorWidget::SetComponentProperties(std::vector<std::pair<std::string, std::string>> properties)
    {
        m_Properties = std::move(properties);
    }

    void PropertyInspectorWidget::SetComponentName(const std::string& name)
    {
        m_ComponentName = name;
    }

    void PropertyInspectorWidget::Draw()
    {
        const auto& geometry = GetGeometry();

        ImGui::SetNextWindowPos(ImVec2(geometry.X, geometry.Y), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(geometry.Width, geometry.Height), ImGuiCond_Always);

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove;

        static bool visible = true;
        static float scaleValue = 1.0f;

        if (ImGui::Begin("Property Inspector", nullptr, flags))
        {
            ImGui::TextUnformatted(m_ComponentName.c_str());
            ImGui::Separator();

            if (m_Properties.empty())
            {
                ImGui::TextDisabled("No component selected");
            }
            else
            {
                for (const auto& [key, value] : m_Properties)
                {
                    ImGui::TextUnformatted(key.c_str());
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s", value.c_str());
                }
            }

            ImGui::Separator();
            ImGui::TextDisabled("Component options");
            ImGui::Checkbox("Visible", &visible);
            ImGui::SliderFloat("Scale", &scaleValue, 0.1f, 2.0f);
        }
        ImGui::End();
    }
}
