#include "AssetBrowserWidget.h"

#include "imgui.h"

namespace App
{
    AssetBrowserWidget::AssetBrowserWidget() = default;

    AssetBrowserWidget::AssetBrowserWidget(std::vector<std::string> assetPaths)
        : m_AssetPaths(std::move(assetPaths))
    {
    }

    void AssetBrowserWidget::SetAssets(std::vector<std::string> assetPaths)
    {
        m_AssetPaths = std::move(assetPaths);
    }

    void AssetBrowserWidget::Draw()
    {
        const auto& geometry = GetGeometry();

        ImGui::SetNextWindowPos(ImVec2(geometry.X, geometry.Y), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(geometry.Width, geometry.Height), ImGuiCond_Always);

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove;

        if (ImGui::Begin("Asset Browser", nullptr, flags))
        {
            ImGui::TextUnformatted("Imported Assets");
            ImGui::Separator();

            if (m_AssetPaths.empty())
            {
                ImGui::TextDisabled("No assets loaded");
            }
            else
            {
                for (const std::string& assetPath : m_AssetPaths)
                {
                    if (ImGui::Selectable(assetPath.c_str()))
                    {
                    }
                }
            }
        }
        ImGui::End();
    }
}
