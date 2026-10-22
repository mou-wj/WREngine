#include "SceneHierarchyWidget.h"

#include "imgui.h"

namespace App
{
    SceneHierarchyWidget::SceneHierarchyWidget() = default;

    SceneHierarchyWidget::SceneHierarchyWidget(std::vector<std::string> sceneObjects)
        : m_SceneObjects(std::move(sceneObjects))
    {
        if (!m_SceneObjects.empty())
        {
            m_SelectedObject = m_SceneObjects.front();
        }
    }

    void SceneHierarchyWidget::SetSceneObjects(std::vector<std::string> sceneObjects)
    {
        m_SceneObjects = std::move(sceneObjects);
        if (!m_SceneObjects.empty() && m_SelectedObject.empty())
        {
            m_SelectedObject = m_SceneObjects.front();
        }
    }

    void SceneHierarchyWidget::SetSelectedObject(const std::string& objectName)
    {
        m_SelectedObject = objectName;
    }

    void SceneHierarchyWidget::Draw()
    {
        const auto& geometry = GetGeometry();

        ImGui::SetNextWindowPos(ImVec2(geometry.X, geometry.Y), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(geometry.Width, geometry.Height), ImGuiCond_Always);

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove;

        if (ImGui::Begin("Scene Hierarchy", nullptr, flags))
        {
            if (!m_SceneObjects.empty())
            {
                for (const std::string& objectName : m_SceneObjects)
                {
                    const bool isSelected = (m_SelectedObject == objectName);
                    if (ImGui::Selectable(objectName.c_str(), isSelected, ImGuiSelectableFlags_AllowDoubleClick))
                    {
                        m_SelectedObject = objectName;
                    }
                }
            }
            else
            {
                ImGui::TextDisabled("No scene objects");
            }
        }
        ImGui::End();
    }
}
