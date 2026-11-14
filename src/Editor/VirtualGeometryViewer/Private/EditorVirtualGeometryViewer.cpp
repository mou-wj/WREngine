#include "EditorVirtualGeometryViewer.h"
#include "VirtualGeometryPreview.h"
#include "StaticMeshAsset.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <iostream>

namespace Editor
{
    EditorVirtualGeometryViewer::EditorVirtualGeometryViewer(
        std::function<std::vector<VirtualGeometryViewerMesh>()> getMeshes)
        : m_GetMeshes(std::move(getMeshes)), m_Preview(std::make_unique<VirtualGeometryPreview>()) {}
    EditorVirtualGeometryViewer::~EditorVirtualGeometryViewer() = default;

    void EditorVirtualGeometryViewer::Open(const std::string& preferredNode)
    {
        if (!preferredNode.empty())
        {
            m_SelectedNode = preferredNode;
        }
        m_bOpen = true;
        m_bVisible = true;
    }

    void EditorVirtualGeometryViewer::Reset()
    {
        m_Preview->Reset();
        m_Asset.reset();
        m_Meshes.clear();
        m_SelectedNode.clear();
        m_MipLevel = 0;
        m_SelectedCluster = -1;
        m_bOpen = false;
        m_bVisible = false;
        m_bRebuild = false;
    }

    void EditorVirtualGeometryViewer::Tick()
    {
        if (!m_bOpen)
        {
            if (m_Asset)
            {
                Reset();
            }
            return;
        }
        m_Meshes = m_GetMeshes ? m_GetMeshes() : std::vector<VirtualGeometryViewerMesh>{};
        auto selected = std::find_if(m_Meshes.begin(), m_Meshes.end(), [this](const auto& mesh)
        {
            return mesh.Name == m_SelectedNode;
        });
        if (selected == m_Meshes.end() && !m_Meshes.empty())
        {
            selected = m_Meshes.begin();
        }
        const auto asset = selected == m_Meshes.end() ? nullptr : selected->Asset;
        m_SelectedNode = selected == m_Meshes.end() ? std::string{} : selected->Name;
        if (asset != m_Asset || m_bRebuild)
        {
            m_Preview->Reset();
            m_Preview->SetMesh(asset);
            m_Asset = asset;
            m_MipLevel = 0;
            m_SelectedCluster = -1;
            m_bRebuild = false;
            if (asset)
            {
                std::cout << "[VirtualGeometryViewer] Built " << m_SelectedNode << std::endl;
            }
        }
        if (m_Asset && m_bVisible)
        {
            m_Preview->Render(m_PreviewSize, m_MipLevel, m_bMaterial, m_Yaw, m_Pitch, m_Distance,
                              m_bDynamicLOD, m_TargetPixelError, m_bWireframe);
        }
    }

    bool EditorVirtualGeometryViewer::HitTest(float x, float y) const
    {
        const auto& geometry = m_PopupGeometry;
        return m_bOpen && m_bVisible && x >= geometry.X && y >= geometry.Y &&
               x < geometry.X + geometry.Width && y < geometry.Y + geometry.Height;
    }

    void EditorVirtualGeometryViewer::Draw()
    {
        if (!m_bOpen)
        {
            return;
        }
        ImGui::SetNextWindowSize({1050, 760}, ImGuiCond_FirstUseEver);
        m_bVisible = ImGui::Begin("Virtual Geometry Viewer", &m_bOpen);
        const auto position = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
        m_PopupGeometry = {position.x, position.y, size.x, size.y};
        if (m_bVisible)
        {
            if (ImGui::BeginCombo("Scene mesh", m_SelectedNode.empty() ? "No mesh nodes" : m_SelectedNode.c_str()))
            {
                for (const auto& mesh : m_Meshes)
                {
                    if (ImGui::Selectable(mesh.Name.c_str(), mesh.Name == m_SelectedNode))
                    {
                        m_SelectedNode = mesh.Name;
                    }
                }
                ImGui::EndCombo();
            }
            const auto* geometry = m_Preview->GetGeometry();
            if (geometry && geometry->IsValid())
            {
                uint32_t maxMip = 0;
                for (const auto& cluster : geometry->Clusters)
                {
                    maxMip = std::max(maxMip, cluster.MipLevel);
                }
                ImGui::SetNextItemWidth(120);
                ImGui::Checkbox("Dynamic LOD", &m_bDynamicLOD);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(150);
                ImGui::DragFloat("Target pixel error", &m_TargetPixelError, 0.1f, 0, 64, "%.2f px");
                ImGui::BeginDisabled(m_bDynamicLOD);
                ImGui::SetNextItemWidth(120);
                ImGui::SliderInt("Generated level", &m_MipLevel, 0, static_cast<int>(maxMip));
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::Checkbox("Material + deferred lighting", &m_bMaterial);
                ImGui::SameLine();
                ImGui::Checkbox("Wireframe", &m_bWireframe);
                ImGui::SameLine();
                if (ImGui::Button("Rebuild"))
                {
                    m_bRebuild = true;
                }
                ImGui::TextDisabled(m_bDynamicLOD ? "Atomic Group DAG cut | 0 px: full detail | Sampled error estimate" :
                    "Inspection only: generated levels are not complete DAG cuts.");
                ImGui::TextDisabled("Right drag: orbit | Wheel: zoom | Cluster colors or Visibility -> Resolve -> Deferred lighting");
                const auto available = ImGui::GetContentRegionAvail();
                const ImVec2 imageSize{std::clamp(available.x, 128.0f, 1400.0f),
                                       std::clamp(available.y * 0.58f, 150.0f, 600.0f)};
                m_PreviewSize = {int(imageSize.x), int(imageSize.y)};
                if (auto* texture = m_Preview->GetTexture())
                {
                    ImGui::Image(static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(texture)), imageSize);
                    if (ImGui::IsItemHovered())
                    {
                        const auto& io = ImGui::GetIO();
                        if (ImGui::IsMouseDragging(ImGuiMouseButton_Right))
                        {
                            m_Yaw -= io.MouseDelta.x * 0.01f;
                            m_Pitch = std::clamp(m_Pitch + io.MouseDelta.y * 0.01f, -1.5f, 1.5f);
                        }
                        m_Distance = std::clamp(m_Distance * std::exp(-io.MouseWheel * 0.12f), 1.3f, 15.0f);
                    }
                }
                DrawGeometryInfo();
            }
            else
            {
                ImGui::TextUnformatted("Add a mesh node to the scene, then select it here. Preview builds its own virtual geometry.");
            }
        }
        ImGui::End();
    }

    void EditorVirtualGeometryViewer::DrawGeometryInfo()
    {
        const auto& geometry = *m_Preview->GetGeometry();
        uint32_t count = 0, triangles = 0;
        float error = 0;
        const auto& selected = m_Preview->GetSelectedClusters();
        for (const auto index : selected)
        {
            ++count;
            triangles += geometry.Clusters[index].TriangleCount;
        }
        for (const auto& group : geometry.Groups)
        {
            error = std::max(error, group.ParentMetric.Error);
        }
        ImGui::Text("All clusters: %zu | Groups: %zu | Roots: %zu | Max accumulated error: %.6g",
                    geometry.Clusters.size(), geometry.Groups.size(), geometry.RootGroupIndices.size(), error);
        ImGui::Text("Displayed %s: %u meshlets, %u triangles", m_bDynamicLOD ? "DAG cut" : "generated level", count, triangles);
        const auto flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY;
        if (ImGui::BeginTable("Virtual geometry clusters", 7, flags, {0, 0}))
        {
            for (const char* label : {"Cluster", "Vertices", "Triangles", "Material", "Group", "Generated by", "Radius"})
            {
                ImGui::TableSetupColumn(label);
            }
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();
            for (uint32_t index = 0; index < geometry.Clusters.size(); ++index)
            {
                const auto& cluster = geometry.Clusters[index];
                if (std::find(selected.begin(), selected.end(), index) == selected.end())
                {
                    continue;
                }
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                if (ImGui::Selectable(std::to_string(index).c_str(), m_SelectedCluster == int(index)))
                {
                    m_SelectedCluster = int(index);
                }
                ImGui::TableNextColumn();
                ImGui::Text("%u", cluster.VertexCount);
                ImGui::TableNextColumn();
                ImGui::Text("%u", cluster.TriangleCount);
                ImGui::TableNextColumn();
                ImGui::Text("%u", cluster.MaterialIndex);
                ImGui::TableNextColumn();
                ImGui::Text("%u", cluster.GroupIndex);
                ImGui::TableNextColumn();
                ImGui::Text("%u", cluster.GeneratingGroupIndex);
                ImGui::TableNextColumn();
                ImGui::Text("%.5g", cluster.Bounds.Sphere.Radius);
            }
            ImGui::EndTable();
        }
    }
}
