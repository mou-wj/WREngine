#include "EditorAssetPanel.h"
#include "AssetManager.h"
#include "Material.h"
#include "PathInfo.h"
#include "imgui.h"
#include <algorithm>
#include <cctype>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <fstream>
#include <nlohmann/json.hpp>

namespace Editor
{
    namespace
    {
        namespace fs = std::filesystem;
        std::string PathText(const fs::path& path)
        {
            const auto text = path.generic_u8string();
            return {reinterpret_cast<const char*>(text.data()), text.size()};
        }
        std::string Lower(std::string text)
        {
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
            return text;
        }
        bool IsWithin(const fs::path& path, const fs::path& root)
        {
            const auto relative = path.lexically_relative(root);
            return !relative.empty() && !relative.is_absolute() && *relative.begin() != "..";
        }
        int FileType(const fs::path& path)
        {
            const auto extension = Lower(PathText(path.extension()));
            if (extension == ".glb" || extension == ".gltf") return 1;
            if (extension == ".json") return 2;
            if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".bmp" || extension == ".tga") return 3;
            return 4;
        }
        const char* TypeName(int type)
        {
            constexpr const char* names[] = {"Folder", "Mesh", "Material JSON", "Texture", "File"};
            return names[type];
        }
        std::array<float, 4> TypeColor(int type)
        {
            constexpr std::array<float, 4> colors[] = {{0.85f,0.66f,0.27f,1}, {0.35f,0.7f,0.95f,1},
                {0.4f,0.85f,0.55f,1}, {0.75f,0.5f,0.92f,1}, {0.65f,0.68f,0.73f,1}};
            return colors[type];
        }
    }
    using Controls = ImGUISlate::ImPropertyControls;
    EditorAssetPanel::EditorAssetPanel()
        : EditorAssetPanel(std::filesystem::path(Core::GetProjectDir()) / "resources") {}
    EditorAssetPanel::EditorAssetPanel(std::filesystem::path contentRoot)
        : ImPanelWidget("Assets"), m_Root(contentRoot.empty() ? std::filesystem::path{} : std::filesystem::weakly_canonical(std::filesystem::absolute(contentRoot)))
    {
        m_ImportDialog.Close(); m_ImportDialog.SetTitle("Import resources");
        m_ImportDialog.SetSelectionMode(ImGUISlate::ImFileDialogWidget::SelectionMode::MultipleFiles);
        m_ImportDialog.SetOnConfirm([this](const auto& files) { QueueImport(files); });
        m_Grid.SetOnSelectionChanged([this](const std::string& id, bool activated) {
            SelectPath(std::filesystem::u8path(id),true);
            if (activated) OpenSelected();
        });
        m_Grid.SetOnItemActions([this](const std::string& id) {
            std::error_code error;
            const auto path=std::filesystem::u8path(id);
            if (FileType(path) == 1 && std::filesystem::is_regular_file(path,error) && ImGui::BeginDragDropSource())
            {
                ImGui::SetDragDropPayload("EDITOR_ASSET_PATH",id.c_str(),id.size()+1);
                ImGui::TextUnformatted("Drop mesh into the scene viewport"); ImGui::EndDragDropSource(); return;
            }
            if (ImGui::BeginPopupContextItem("Asset actions"))
            {
                if (ImGui::MenuItem("Open / Add mesh")) { SelectPath(std::filesystem::u8path(id)); OpenSelected(); }
                if (ImGui::MenuItem("Copy resource path"))
                    ImGui::SetClipboardText(("res://" + PathText(std::filesystem::u8path(id).lexically_relative(m_Root))).c_str());
                ImGui::EndPopup();
            }
        });
        if (!m_Root.empty()) NavigateTo(m_Root);
    }
    void EditorAssetPanel::SetOnMaterialOpened(std::function<void(const std::shared_ptr<Engine::MaterialAsset>&)> callback)
    { m_OnMaterialOpened = std::move(callback); }
    void EditorAssetPanel::SetContentRoot(const std::filesystem::path& root)
    {
        m_ImportDialog.Close(); m_PendingImports.clear(); m_Material.reset(); m_Texture.reset();
        m_TreeEntries.clear(); m_History.clear(); m_HistoryIndex = 0; m_SelectedPath.clear(); m_Grid.SetItems({});
        m_Grid.SetSelectedId({}); m_Filter.clear(); m_TreeFilter.clear(); m_PathInput.clear(); m_LastError.clear();
        m_Directory.clear(); m_Root.clear(); m_TypeFilter = 0;
        if (!root.empty()) { m_Root = std::filesystem::weakly_canonical(root); NavigateTo(m_Root); }
    }
    void EditorAssetPanel::QueueImport(const std::vector<std::filesystem::path>& files)
    { if (!m_Root.empty()) for (const auto& file : files) m_PendingImports.push_back({file,m_Directory}); }
    bool EditorAssetPanel::OnFilesDropped(const SlateCore::FileDropEvent& event)
    { if (m_Root.empty() || event.Paths.empty()) return false; QueueImport(event.Paths); return true; }

    std::filesystem::path EditorAssetPanel::ImportFile(const std::filesystem::path& source, const std::filesystem::path& directory)
    {
        namespace fs = std::filesystem; using Json = nlohmann::json;
        if (!fs::is_regular_file(source) || FileType(source)==4) throw std::runtime_error("Import supports GLB/glTF, material JSON and PNG/JPG/BMP/TGA files");
        if (!IsWithin(fs::weakly_canonical(directory),m_Root)) throw std::runtime_error("Import destination is outside project content");
        auto stem = PathText(source.stem());
        for (auto& letter : stem) if (!(static_cast<unsigned char>(letter)<128 && (std::isalnum(static_cast<unsigned char>(letter)) || letter=='-' || letter=='_'))) letter = '_';
        if (stem.empty()) stem = "Resource";
        stem = stem.substr(0,64);
        fs::path destination;
        for (uint32_t index=0;;++index)
        {
            destination = directory / (stem + (index ? "_"+std::to_string(index) : ""));
            std::error_code error;
            if (fs::create_directory(destination,error)) break;
            if (error && !fs::exists(destination)) throw fs::filesystem_error("Cannot create import destination",destination,error);
        }
        try
        {
            const auto extension = Lower(PathText(source.extension()));
            Json data;
            if (extension==".gltf" || extension==".json") { std::ifstream input(source); input >> data; if (!data.is_object()) throw std::runtime_error("Expected an asset JSON object"); }
            if (extension==".glb")
            {
                // Rewriting binary GLB chunks is not an importer capability here.
                // Reject external dependencies rather than silently copying a broken file.
                std::ifstream input(source,std::ios::binary); uint32_t header[5]{}; input.read(reinterpret_cast<char*>(header),sizeof(header));
                if (!input || header[0]!=0x46546C67 || header[1]!=2 || header[4]!=0x4E4F534A || header[3]>fs::file_size(source)-20)
                    throw std::runtime_error("Invalid GLB header");
                std::string json(header[3],'\0'); input.read(json.data(),json.size()); auto description = Json::parse(json);
                for (const auto* block : {"buffers","images"}) if (description.contains(block))
                    for (const auto& entry : description.at(block))
                        if (entry.contains("uri") && !entry.at("uri").get<std::string>().starts_with("data:"))
                            throw std::runtime_error("GLB external dependencies: export an embedded GLB or import glTF instead");
            }
            uint32_t dependencyIndex = 0;
            auto copyDependency = [&](Json& value, bool uri) {
                if (value.is_null()) return;
                auto text = value.get<std::string>();
                if (uri && text.starts_with("data:")) return;
                if (uri && text.find("://")!=std::string::npos) throw std::runtime_error("Only local glTF dependencies can be imported");
                if (uri)
                {
                    std::string decoded;
                    for (size_t i=0;i<text.size();++i)
                        if (text[i]=='%' && i+2<text.size())
                        {
                            auto hex=[](char c)->int { if(c>='0'&&c<='9')return c-'0'; if(c>='a'&&c<='f')return c-'a'+10; if(c>='A'&&c<='F')return c-'A'+10; throw std::runtime_error("Invalid URI encoding"); };
                            decoded += static_cast<char>(hex(text[i+1])*16+hex(text[i+2])); i+=2;
                        }
                        else decoded += text[i];
                    text = std::move(decoded);
                }
                auto path = fs::u8path(text); if (path.is_relative()) path = source.parent_path()/path;
                const auto name = "dependency_"+std::to_string(dependencyIndex++)+Lower(PathText(path.extension()));
                fs::copy_file(path,destination/name,fs::copy_options::none); value = name;
            };
            if (extension==".gltf") for (const auto* block : {"buffers","images"}) if (data.contains(block))
                for (auto& entry : data[block]) if (entry.contains("uri")) copyDependency(entry["uri"],true);
            if (extension==".json" && data.contains("textureParameters"))
                for (auto& [name,value] : data["textureParameters"].items()) copyDependency(value,false);
            const auto result = destination/(stem+extension);
            if (data.is_null()) fs::copy_file(source,result,fs::copy_options::none);
            else { std::ofstream output(result); if (!(output << data.dump(2))) throw std::runtime_error("Cannot write imported asset"); output.close(); if (!output) throw std::runtime_error("Cannot flush imported asset"); }
            return result;
        }
        catch (...) { if (IsWithin(fs::weakly_canonical(destination),m_Root)) fs::remove_all(destination); throw; }
    }
    const std::vector<EditorAssetPanel::FileEntry>& EditorAssetPanel::ReadDirectory(const std::filesystem::path& directory)
    {
        auto found = m_TreeEntries.find(directory);
        if (found != m_TreeEntries.end()) return found->second;
        std::vector<FileEntry> entries;
        for (const auto& entry : std::filesystem::directory_iterator(directory, std::filesystem::directory_options::skip_permission_denied))
        {
            // Do not follow symbolic links into a cycle or outside the project.
            if (entry.is_symlink()) continue;
            if (entry.is_directory() || entry.is_regular_file()) entries.push_back({entry.path(), entry.is_directory()});
        }
        std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
            return a.bDirectory != b.bDirectory ? a.bDirectory : a.Path.filename() < b.Path.filename();
        });
        return m_TreeEntries.emplace(directory, std::move(entries)).first->second;
    }
    bool EditorAssetPanel::NavigateTo(const std::filesystem::path& directory, bool bRecordHistory)
    {
        try
        {
            auto path = std::filesystem::weakly_canonical(directory);
            if (!IsWithin(path, m_Root)) throw std::runtime_error("Directory must stay inside the content root (res://)");
            if (!std::filesystem::is_directory(path)) throw std::runtime_error("Not a directory");
            ReadDirectory(path);
            if (bRecordHistory && (m_History.empty() || m_Directory != path))
            {
                if (!m_History.empty()) m_History.resize(m_HistoryIndex + 1);
                m_History.push_back(path); m_HistoryIndex = m_History.size() - 1;
            }
            m_Directory = std::move(path); m_SelectedPath.clear(); m_Grid.SetSelectedId({});
            const auto relative = m_Directory.lexically_relative(m_Root);
            m_PathInput = "res://" + (relative == "." ? std::string{} : PathText(relative));
            m_bRevealDirectory = true; m_LastError.clear(); return true;
        }
        catch (const std::exception& error) { m_LastError = error.what(); return false; }
    }
    void EditorAssetPanel::Refresh()
    {
        const auto selected = m_SelectedPath;
        m_TreeEntries.clear();
        if (NavigateTo(m_Directory, false) && !selected.empty() && std::filesystem::exists(selected)) SelectPath(selected);
    }
    void EditorAssetPanel::SelectPath(const std::filesystem::path& path, bool bOpenMaterial)
    {
        try
        {
            const auto canonical = std::filesystem::weakly_canonical(path);
            if (!IsWithin(canonical, m_Root) || !std::filesystem::exists(canonical)) throw std::runtime_error("Resource path is missing or outside res://");
            if (canonical.parent_path() != m_Directory && !NavigateTo(canonical.parent_path())) return;
            m_SelectedPath = canonical; m_Grid.SetSelectedId(PathText(canonical));
            m_bRevealDirectory = true; m_LastError.clear();
            if (bOpenMaterial && !std::filesystem::is_directory(canonical) && FileType(canonical)==2) OpenSelected();
        }
        catch (const std::exception& error)
        {
            m_SelectedPath.clear(); m_Grid.SetSelectedId({}); m_LastError = error.what();
        }
    }
    void EditorAssetPanel::OpenSelected()
    {
        if (m_SelectedPath.empty()) return;
        try
        {
            const auto path = m_SelectedPath;
            if (std::filesystem::is_directory(path)) { NavigateTo(path); return; }
            auto& assets = Engine::AssetManager::Get();
            switch (FileType(path))
            {
            case 1:
                if (!m_OnMeshOpened) throw std::runtime_error("No mesh-open handler is attached");
                m_OnMeshOpened(assets.LoadSync<Engine::StaticMeshAsset>(path.generic_string())); break;
            case 2: m_Material = assets.LoadSync<Engine::MaterialAsset>(path.generic_string()); if (m_OnMaterialOpened) m_OnMaterialOpened(m_Material); break;
            case 3: m_Texture = assets.LoadSync<Engine::TextureAsset>(path.generic_string()); break;
            default: throw std::runtime_error("Supported: glb/gltf, material JSON, png/jpg/bmp/tga");
            }
            m_LastError.clear(); std::cout << "[AssetEditor] Opened " << PathText(path) << std::endl;
        }
        catch (const std::exception& error) { m_LastError = error.what(); }
    }
    bool EditorAssetPanel::MatchesTree(const FileEntry& entry)
    {
        if (m_TreeFilter.empty() || Lower(PathText(entry.Path.filename())).find(Lower(m_TreeFilter)) != std::string::npos) return true;
        if (entry.bDirectory)
            for (const auto& child : ReadDirectory(entry.Path)) if (MatchesTree(child)) return true;
        return false;
    }
    void EditorAssetPanel::DrawTree(const std::filesystem::path& path)
    {
        const bool root = path == m_Root;
        auto flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick |
            ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding;
        if (path == m_Directory && m_SelectedPath.empty()) flags |= ImGuiTreeNodeFlags_Selected;
        if (root && m_TreeFilter.empty()) flags |= ImGuiTreeNodeFlags_DefaultOpen;
        if (!m_TreeFilter.empty() || (m_bRevealDirectory && IsWithin(m_Directory, path))) ImGui::SetNextItemOpen(true);
        const auto id = PathText(path), label = root ? "res://" : PathText(path.filename());
        const bool open = ImGui::TreeNodeEx(id.c_str(), flags, "    %s", label.c_str());
        const auto minimum = ImGui::GetItemRectMin();
        const float iconX = minimum.x + ImGui::GetFontSize() + 4;
        ImGui::GetWindowDrawList()->AddRectFilled({iconX,minimum.y+5}, {iconX+13,minimum.y+15}, IM_COL32(95,165,225,255), 2);
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) NavigateTo(path);
        if (open)
        {
            try
            {
                for (const auto& entry : ReadDirectory(path))
                {
                    if (!MatchesTree(entry)) continue;
                    if (entry.bDirectory) { DrawTree(entry.Path); continue; }
                    auto fileFlags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen |
                        ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding;
                    if (entry.Path == m_SelectedPath) fileFlags |= ImGuiTreeNodeFlags_Selected;
                    const auto fileId = PathText(entry.Path);
                    ImGui::TreeNodeEx(fileId.c_str(), fileFlags, "    %s", PathText(entry.Path.filename()).c_str());
                    const auto fileMinimum = ImGui::GetItemRectMin();
                    const auto color = TypeColor(FileType(entry.Path));
                    ImGui::GetWindowDrawList()->AddRectFilled({fileMinimum.x+ImGui::GetFontSize()+5,fileMinimum.y+5},
                        {fileMinimum.x+ImGui::GetFontSize()+14,fileMinimum.y+16}, ImGui::ColorConvertFloat4ToU32({color[0],color[1],color[2],1}), 1);
                    if (ImGui::IsItemClicked()) { SelectPath(entry.Path,true); if (ImGui::IsMouseDoubleClicked(0)) OpenSelected(); }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", fileId.c_str());
                }
            }
            catch (const std::exception& error) { m_LastError = error.what(); ImGui::TextDisabled("Cannot read this directory."); }
            ImGui::TreePop();
        }
    }
    void EditorAssetPanel::DrawResources()
    {
        const char* types[] = {"All types", "Meshes", "Materials", "Textures", "Other files"};
        const float available = ImGui::GetContentRegionAvail().x;
        const bool showSize = available >= 380;
        ImGui::SetNextItemWidth(std::max(50.0f, available - (showSize ? 280 : 120)));
        Controls::Text("##ResourceSearch", m_Filter, "Search resources");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Search the current directory");
        ImGui::SameLine(); ImGui::SetNextItemWidth(105);
        Controls::Choice("##ResourceType", m_TypeFilter, types, 5);
        if (showSize)
        {
            ImGui::SameLine(); ImGui::SetNextItemWidth(90);
            ImGui::SliderFloat("Size", &m_TileWidth, 72, 160, "%.0f");
        }
        std::vector<ImGUISlate::ImGridItem> cards;
        const auto filter = Lower(m_Filter);
        for (const auto& entry : ReadDirectory(m_Directory))
        {
            const int type = entry.bDirectory ? 0 : FileType(entry.Path);
            if (type && m_TypeFilter && m_TypeFilter != type) continue;
            const auto name = PathText(entry.Path.filename());
            if (!filter.empty() && Lower(name).find(filter) == std::string::npos) continue;
            const auto icon = type == 0 ? ImGUISlate::EImGridIcon::Folder : type == 1 ? ImGUISlate::EImGridIcon::Mesh :
                type == 2 ? ImGUISlate::EImGridIcon::ColorSphere : type == 3 ? ImGUISlate::EImGridIcon::Image : ImGUISlate::EImGridIcon::Document;
            cards.push_back({PathText(entry.Path), name, TypeName(type), TypeColor(type), icon});
        }
        if (ImGui::BeginChild("AssetResourceGrid", {0,0}))
        {
            if (cards.empty()) ImGui::TextDisabled("No matching resources.");
            m_Grid.SetItems(std::move(cards)); m_Grid.SetTileWidth(m_TileWidth); m_Grid.Draw();
        }
        ImGui::EndChild();
    }
    void EditorAssetPanel::DrawStatus()
    {
        ImGui::BeginDisabled(m_SelectedPath.empty());
        if (ImGui::Button("Open / Add mesh")) OpenSelected();
        ImGui::EndDisabled(); ImGui::SameLine();
        ImGui::BeginDisabled(!m_Material || !m_OnMaterialAssigned);
        if (ImGui::Button("Assign material"))
        {
            if (!m_OnMaterialAssigned(m_Material)) m_LastError = "Select a mesh before assigning a material";
            else m_LastError.clear();
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered() && m_Material) ImGui::SetTooltip("Loaded material: %s -> mesh slot 0", m_Material->GetName().c_str());
        ImGui::SameLine();
        if (!m_LastError.empty())
        {
            ImGui::TextColored({1,0.5f,0.35f,1}, "Error (hover for details)");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", m_LastError.c_str());
        }
        else
        {
            const auto selected = m_SelectedPath.empty() ? std::string("No selection") : PathText(m_SelectedPath.filename());
            ImGui::TextUnformatted(selected.c_str());
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", m_Texture ? "Texture selection is available in Material Properties." : "Double-click a folder or resource to open it.");
        }
    }
    void EditorAssetPanel::DrawContent()
    {
        if (m_Root.empty()) { ImGui::TextDisabled("Create or open a project."); return; }
        if (!m_PendingImports.empty())
        {
            const auto pending = m_PendingImports.front(); m_PendingImports.erase(m_PendingImports.begin());
            try { const auto path = ImportFile(pending.Source,pending.Directory); m_TreeEntries.clear(); NavigateTo(path.parent_path()); SelectPath(path); }
            catch (const std::exception& error) { m_LastError = error.what(); }
        }
        if (ImGui::Button("Import")) { m_ImportDialog.SetInitialDirectory(m_Root); m_ImportDialog.ClearSelection(); m_ImportDialog.Open(); }
        ImGui::SameLine();
        ImGui::BeginDisabled(m_History.empty() || m_HistoryIndex == 0);
        if (ImGui::Button("<") && NavigateTo(m_History[m_HistoryIndex-1], false)) --m_HistoryIndex;
        ImGui::EndDisabled(); ImGui::SameLine();
        ImGui::BeginDisabled(m_History.empty() || m_HistoryIndex + 1 >= m_History.size());
        if (ImGui::Button(">") && NavigateTo(m_History[m_HistoryIndex+1], false)) ++m_HistoryIndex;
        ImGui::EndDisabled(); ImGui::SameLine();
        ImGui::BeginDisabled(m_Directory == m_Root);
        if (ImGui::Button("Up")) NavigateTo(m_Directory.parent_path());
        ImGui::EndDisabled(); ImGui::SameLine();
        if (ImGui::Button("Refresh")) Refresh();
        ImGui::SameLine(); ImGui::SetNextItemWidth(std::max(40.0f, ImGui::GetContentRegionAvail().x - 45));
        Controls::Text("##ResourcePath", m_PathInput); ImGui::SameLine();
        if (ImGui::Button("Go"))
        {
            auto path = m_PathInput.starts_with("res://") ? m_Root / std::filesystem::u8path(m_PathInput.substr(6)) : std::filesystem::u8path(m_PathInput);
            NavigateTo(path.is_relative() ? m_Directory / path : path);
        }
        const float bodyHeight = std::max(1.0f, ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing());
        const float width = ImGui::GetContentRegionAvail().x;
        const float minTree = std::min(120.0f, width * 0.35f);
        const float maxTree = std::max(minTree, width - std::min(260.0f, width * 0.55f) - 6);
        const float treeWidth = std::clamp(m_TreeWidth, minTree, maxTree);
        if (ImGui::BeginChild("AssetFileSystem", {treeWidth, bodyHeight}, ImGuiChildFlags_Borders))
        {
            ImGui::SetNextItemWidth(-1); Controls::Text("##FileSystemFilter", m_TreeFilter, "Filter files");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Filter the file tree; matching parent folders stay visible");
            if (ImGui::BeginChild("AssetFileTree", {0,0}, 0, ImGuiWindowFlags_HorizontalScrollbar))
                try { DrawTree(m_Root); } catch (const std::exception& error) { m_LastError = error.what(); }
            ImGui::EndChild(); m_bRevealDirectory = false;
        }
        ImGui::EndChild(); ImGui::SameLine(0,0);
        if (ImGui::BeginChild("AssetTreeSplitter", {6,bodyHeight}, 0, ImGuiWindowFlags_NoScrollbar))
        {
            ImGui::InvisibleButton("Drag", {6,bodyHeight});
            if (ImGui::IsItemHovered() || ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            if (ImGui::IsItemActivated()) m_LastSplitPosition = ImGui::GetIO().MousePos.x;
            if (ImGui::IsItemActive())
            {
                m_TreeWidth = std::clamp(treeWidth + ImGui::GetIO().MousePos.x - m_LastSplitPosition, minTree, maxTree);
                m_LastSplitPosition = ImGui::GetIO().MousePos.x;
            }
            ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                ImGui::GetColorU32(ImGui::IsItemActive() ? ImGuiCol_SeparatorActive : ImGui::IsItemHovered() ? ImGuiCol_SeparatorHovered : ImGuiCol_Separator));
        }
        ImGui::EndChild(); ImGui::SameLine(0,0);
        if (ImGui::BeginChild("AssetResourcePane", {0,bodyHeight}, ImGuiChildFlags_Borders))
            try { DrawResources(); } catch (const std::exception& error) { m_LastError = error.what(); }
        ImGui::EndChild();
        if (ImGui::BeginChild("AssetActions", {0,0}, 0, ImGuiWindowFlags_NoScrollbar)) DrawStatus();
        ImGui::EndChild();
        m_ImportDialog.Draw();
    }
    void EditorAssetPanel::SetOnMeshOpened(MeshOpenedCallback callback) { m_OnMeshOpened = std::move(callback); }
    void EditorAssetPanel::SetOnMaterialAssigned(MaterialAssignedCallback callback) { m_OnMaterialAssigned = std::move(callback); }
}
