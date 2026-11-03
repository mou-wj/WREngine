#include "EditorMeshViewer.h"
#include "MeshViewerPreview.h"
#include "StaticMeshAsset.h"
#include "StaticMesh.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace Editor
{
    EditorMeshViewer::EditorMeshViewer(std::function<std::vector<MeshViewerMesh>()> getMeshes)
        : m_GetMeshes(std::move(getMeshes)), m_Preview(std::make_unique<MeshViewerPreview>()) {}
    EditorMeshViewer::~EditorMeshViewer() = default;

    void EditorMeshViewer::Open(const std::string& preferredNode)
    {
        if (!preferredNode.empty()) m_SelectedNode=preferredNode;
        m_bOpen=true; m_bVisible=true;
    }
    void EditorMeshViewer::Reset()
    {
        m_Preview->Reset(); m_Asset.reset(); m_Meshes.clear();
        m_SelectedNode.clear(); m_Status.clear(); m_Lod=m_RequestedLod=0;
        m_bOpen=m_bVisible=false;
    }
    void EditorMeshViewer::Tick()
    {
        if (!m_bOpen) { if (m_Asset) Reset(); return; }
        m_Meshes=m_GetMeshes ? m_GetMeshes() : std::vector<MeshViewerMesh>{};
        auto selected=std::find_if(m_Meshes.begin(),m_Meshes.end(),[this](const auto& mesh) { return mesh.Name==m_SelectedNode; });
        if (selected==m_Meshes.end() && !m_Meshes.empty()) selected=m_Meshes.begin();
        const auto asset=selected==m_Meshes.end() ? nullptr : selected->Asset;
        m_SelectedNode=selected==m_Meshes.end() ? std::string{} : selected->Name;
        if (asset!=m_Asset) m_RequestedLod=0;
        const auto* mesh=asset ? asset->GetMesh() : nullptr;
        const int lod=mesh && mesh->GetLODCount() ? std::clamp(m_RequestedLod,0,static_cast<int>(mesh->GetLODCount())-1) : 0;
        try
        {
            if (asset!=m_Asset || lod!=m_Lod)
            {
                m_Asset=asset; m_Lod=lod; m_Status.clear();
                m_Preview->SetMesh(asset,lod);
            }
            if (asset && m_Status.empty() && m_bVisible)
                m_Preview->Render(m_PreviewSize,m_bWireframe,m_bShowNormals,m_NormalLength,m_Yaw,m_Pitch,m_Distance);
        }
        catch (const std::exception& error) { m_Status=error.what(); }
    }
    bool EditorMeshViewer::HitTest(float x,float y) const
    {
        const auto& g=m_PopupGeometry;
        return m_bOpen && m_bVisible && x>=g.X && y>=g.Y && x<g.X+g.Width && y<g.Y+g.Height;
    }
    void EditorMeshViewer::Draw()
    {
        if (!m_bOpen) return;
        ImGui::SetNextWindowSize({1000,720},ImGuiCond_FirstUseEver);
        m_bVisible=ImGui::Begin("Mesh Viewer",&m_bOpen);
        const auto position=ImGui::GetWindowPos(), size=ImGui::GetWindowSize();
        m_PopupGeometry={position.x,position.y,size.x,size.y};
        if (m_bVisible)
        {
            if (ImGui::BeginCombo("Scene mesh",m_SelectedNode.empty() ? "No mesh nodes" : m_SelectedNode.c_str()))
            {
                for (const auto& mesh:m_Meshes)
                    if (ImGui::Selectable(mesh.Name.c_str(),mesh.Name==m_SelectedNode)) m_SelectedNode=mesh.Name;
                ImGui::EndCombo();
            }
            if (m_Asset && m_Asset->GetMesh())
            {
                const auto* mesh=m_Asset->GetMesh();
                ImGui::SetNextItemWidth(110);
                if (ImGui::BeginCombo("LOD",std::to_string(m_Lod).c_str()))
                {
                    for (size_t lod=0;lod<mesh->GetLODCount();++lod)
                        if (ImGui::Selectable(std::to_string(lod).c_str(),static_cast<int>(lod)==m_Lod)) m_RequestedLod=static_cast<int>(lod);
                    ImGui::EndCombo();
                }
                ImGui::SameLine(); ImGui::Checkbox("Wireframe",&m_bWireframe);
                ImGui::SameLine(); ImGui::Checkbox("Show normals",&m_bShowNormals);
                ImGui::SameLine(); ImGui::SetNextItemWidth(120);
                ImGui::SliderFloat("Normal length",&m_NormalLength,0.01f,0.3f,"%.2f");
                if (ImGui::Button("Reset view")) { m_Yaw=0.785398f; m_Pitch=0.4f; m_Distance=3.2f; }
                ImGui::SameLine(); ImGui::TextDisabled("Right drag: orbit | Wheel: zoom | Phong + one directional light");
                const auto available=ImGui::GetContentRegionAvail();
                const ImVec2 previewSize{std::clamp(available.x,128.0f,1024.0f),std::clamp(available.y*0.5f,160.0f,380.0f)};
                m_PreviewSize={static_cast<int>(std::clamp(previewSize.x,128.0f,1024.0f)),static_cast<int>(previewSize.y)};
                if (auto* texture=m_Preview->GetTexture(); texture && m_Status.empty())
                {
                    ImGui::Image(static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(texture)),previewSize);
                    if (ImGui::IsItemHovered())
                    {
                        const auto& io=ImGui::GetIO();
                        if (ImGui::IsMouseDragging(ImGuiMouseButton_Right))
                        { m_Yaw-=io.MouseDelta.x*0.01f; m_Pitch=std::clamp(m_Pitch+io.MouseDelta.y*0.01f,-1.5f,1.5f); }
                        m_Distance=std::clamp(m_Distance*std::exp(-io.MouseWheel*0.12f),1.3f,15.0f);
                    }
                }
                if (!m_Status.empty()) ImGui::TextWrapped("Preview: %s",m_Status.c_str());
                if (m_Preview->HasGeneratedNormals()) ImGui::TextDisabled("Missing/invalid normals: generated for preview only; source attributes are unchanged.");
                DrawMeshInfo();
            }
            else ImGui::TextUnformatted("Add a mesh to the scene, then choose its node here.");
        }
        ImGui::End();
    }
    void EditorMeshViewer::DrawMeshInfo()
    {
        const auto* mesh=m_Asset->GetMesh();
        if (static_cast<size_t>(m_Lod)>=mesh->GetLODCount()) return;
        const auto& lod=mesh->GetLODResource(m_Lod);
        const auto& v=lod.VertexBuffers;
        ImGui::Text("Asset: %s",m_Asset->GetName().c_str());
        ImGui::Text("Vertices: %llu | Indices: %zu | Triangles: %zu | Regions: %zu | Materials: %zu | LODs: %zu",
            static_cast<unsigned long long>(v.PositionBuffer.GetNumVertices()),lod.IndexBuffer.Indices.size(),
            lod.IndexBuffer.Indices.size()/3,lod.Sections.size(),mesh->GetNumMaterials(),mesh->GetLODCount());
        const auto& bounds=mesh->GetBounds().Box;
        ImGui::Text("Local bounds: (%.4g, %.4g, %.4g) to (%.4g, %.4g, %.4g)",bounds.Min.X,bounds.Min.Y,bounds.Min.Z,bounds.Max.X,bounds.Max.Y,bounds.Max.Z);
        if (ImGui::BeginTabBar("Mesh details"))
        {
            if (ImGui::BeginTabItem("Vertices"))
            {
                const Engine::VertexBuffer* streams[]={&v.PositionBuffer,&v.NormalBuffer,&v.UVBuffer,&v.TangentBuffer,&v.ColorBuffer};
                const char* labels[]={"Position","Normal","UV","Tangent","Color"};
                for (int column=0;column<5;++column)
                {
                    if (column) ImGui::SameLine();
                    ImGui::Checkbox(labels[column],&m_Columns[column]);
                }
                constexpr auto flags=ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                    ImGuiTableFlags_ScrollX | ImGuiTableFlags_Resizable;
                int columns=1; for (bool enabled:m_Columns) if (enabled) ++columns;
                if (ImGui::BeginTable("Vertex attributes",columns,flags,{0,std::max(80.0f,ImGui::GetContentRegionAvail().y)}))
                {
                    ImGui::TableSetupColumn("Vertex",ImGuiTableColumnFlags_WidthFixed,65);
                    for (int column=0;column<5;++column) if (m_Columns[column])
                        ImGui::TableSetupColumn(labels[column],ImGuiTableColumnFlags_WidthFixed,streams[column]->NumComponents*62.0f+24);
                    ImGui::TableSetupScrollFreeze(1,1); ImGui::TableHeadersRow();
                    uint64_t count=0; for (const auto* stream:streams) count=std::max(count,stream->GetNumVertices());
                    ImGuiListClipper clipper; clipper.Begin(static_cast<int>(std::min<uint64_t>(count,std::numeric_limits<int>::max())));
                    while (clipper.Step()) for (int row=clipper.DisplayStart;row<clipper.DisplayEnd;++row)
                    {
                        ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::Text("%d",row);
                        for (int column=0;column<5;++column) if (m_Columns[column])
                        {
                            ImGui::TableNextColumn(); const auto& stream=*streams[column];
                            if (static_cast<uint64_t>(row)>=stream.GetNumVertices()) { ImGui::TextDisabled("--"); continue; }
                            std::ostringstream value; value.precision(6);
                            for (uint32_t component=0;component<stream.NumComponents;++component)
                            { if (component) value<<", "; value<<stream.Vertices[static_cast<size_t>(row)*stream.NumComponents+component]; }
                            ImGui::TextUnformatted(value.str().c_str());
                        }
                    }
                    ImGui::EndTable();
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Regions"))
            {
                if (ImGui::BeginTable("Mesh regions",6,ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,{0,180}))
                {
                    for (const char* name:{"Region","Material","First index","Indices","Triangles","Base vertex"}) ImGui::TableSetupColumn(name);
                    ImGui::TableHeadersRow();
                    for (size_t index=0;index<lod.Sections.size();++index)
                    {
                        const auto& section=lod.Sections[index]; ImGui::TableNextRow();
                        ImGui::TableNextColumn(); ImGui::Text("%zu",index);
                        ImGui::TableNextColumn(); ImGui::Text("%d",section.MaterialIndex);
                        ImGui::TableNextColumn(); ImGui::Text("%u",section.FirstIndex);
                        ImGui::TableNextColumn(); ImGui::Text("%u",section.NumIndices);
                        ImGui::TableNextColumn(); ImGui::Text("%u",section.NumIndices/3);
                        ImGui::TableNextColumn(); ImGui::Text("%d",section.BaseVertexIndex);
                    }
                    ImGui::EndTable();
                }
                ImGui::EndTabItem();
            }
        ImGui::EndTabBar();
        }
    }
}
