#include "EditorAssetPanel.h"
#include "AssetManager.h"
#include "imgui.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <utility>

namespace Editor
{
    namespace
    {
        namespace fs=std::filesystem;
        using Json=nlohmann::json;
        std::string Text(const fs::path& path)
        { const auto value=path.generic_u8string(); return {reinterpret_cast<const char*>(value.data()),value.size()}; }
        bool Within(const fs::path& path,const fs::path& root)
        { const auto relative=path.lexically_relative(root); return !relative.empty() && !relative.is_absolute() && *relative.begin()!=".."; }
        fs::path Remap(const fs::path& path,const fs::path& source,const fs::path& destination)
        { return path==source ? destination : Within(path,source) ? destination/path.lexically_relative(source) : path; }
        void ValidateName(const std::string& name)
        {
            if (name.empty() || name=="." || name==".." || name.back()=='.' || name.back()==' ' ||
                name.find_first_of("/\\:*?\"<>|")!=std::string::npos ||
                std::any_of(name.begin(),name.end(),[](unsigned char c){return c<32;}))
                throw std::runtime_error("Enter a single file/folder name without reserved characters or trailing dots/spaces");
            auto stem=name.substr(0,name.find('.'));
            std::transform(stem.begin(),stem.end(),stem.begin(),[](unsigned char c){return static_cast<char>(std::toupper(c));});
            if (stem=="CON" || stem=="PRN" || stem=="AUX" || stem=="NUL" ||
                (stem.size()==4 && (stem.starts_with("COM") || stem.starts_with("LPT")) && stem[3]>='1' && stem[3]<='9'))
                throw std::runtime_error("This name is reserved by the filesystem");
        }
        std::string DecodeUri(const std::string& value)
        {
            std::string result;
            const auto hex=[](char c)->int { if(c>='0'&&c<='9')return c-'0'; if(c>='a'&&c<='f')return c-'a'+10; if(c>='A'&&c<='F')return c-'A'+10; throw std::runtime_error("Invalid local glTF URI"); };
            for (size_t i=0;i<value.size();++i) {
                if (value[i]=='%') { if(i+2>=value.size()) throw std::runtime_error("Invalid local glTF URI"); result+=char(hex(value[i+1])*16+hex(value[i+2])); i+=2; }
                else result+=value[i];
            }
            return result;
        }
        std::string EncodeUri(const std::string& value)
        {
            constexpr char hex[]="0123456789ABCDEF"; std::string result;
            for (unsigned char c:value) {
                if ((c>='a'&&c<='z') || (c>='A'&&c<='Z') || (c>='0'&&c<='9') || c=='/' || c=='.' || c=='-' || c=='_' || c=='~') result+=char(c);
                else { result+='%'; result+=hex[c>>4]; result+=hex[c&15]; }
            }
            return result;
        }
        template<class Callback> void References(Json& data,bool gltf,Callback callback)
        {
            if (!data.is_object()) return;
            if (gltf) {
                for (const auto* block:{"buffers","images"}) if (data.contains(block) && data[block].is_array())
                    for (auto& entry:data[block]) if (entry.is_object() && entry.contains("uri") && entry["uri"].is_string()) callback(entry["uri"],true);
            } else if (data.contains("textureParameters") && data["textureParameters"].is_object())
                for (auto& [name,value]:data["textureParameters"].items()) if (value.is_string()) callback(value,false);
        }
        struct DocumentEdit { fs::path Before,After; std::string Original,Updated; };
        std::vector<DocumentEdit> CollectEdits(const fs::path& root,const fs::path& source,const fs::path& destination)
        {
            std::vector<DocumentEdit> edits;
            for (auto it=fs::recursive_directory_iterator(root);it!=fs::recursive_directory_iterator{};++it) {
                if (it->is_symlink() || !Within(fs::weakly_canonical(it->path()),root)) { it.disable_recursion_pending(); continue; }
                if (!it->is_regular_file()) continue;
                auto extension=Text(it->path().extension());
                std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
                if (extension!=".json" && extension!=".gltf") continue;
                std::ifstream input(it->path(),std::ios::binary); if (!input) throw std::runtime_error("Cannot inspect references in "+Text(it->path()));
                DocumentEdit edit{it->path(),destination.empty() ? it->path() : Remap(it->path(),source,destination)};
                edit.Original={std::istreambuf_iterator<char>(input),{}};
                auto data=Json::parse(edit.Original,nullptr,false); if (data.is_discarded()) continue;
                bool changed=false;
                References(data,extension==".gltf",[&](Json& value,bool uri) {
                    const auto text=value.get<std::string>();
                    if (text.empty() || (uri && (text.starts_with("data:") || text.find("://")!=std::string::npos))) return;
                    auto target=fs::u8path(uri ? DecodeUri(text) : text);
                    target=fs::weakly_canonical(target.is_absolute() ? target : edit.Before.parent_path()/target);
                    if (destination.empty()) {
                        if (!Within(edit.Before,source) && Within(target,source))
                            throw std::runtime_error("Still referenced by "+Text(edit.Before.lexically_relative(root))+"; remove or change that dependency before deleting");
                        return;
                    }
                    const auto relocated=Remap(target,source,destination);
                    if (relocated==target && edit.Before.parent_path()==edit.After.parent_path()) return;
                    const auto relative=relocated.lexically_relative(edit.After.parent_path());
                    const auto rewritten=relative.empty() ? Text(relocated) : Text(relative);
                    value=uri ? EncodeUri(rewritten) : rewritten; changed=true;
                });
                if (changed) { edit.Updated=data.dump(2)+'\n'; edits.push_back(std::move(edit)); }
            }
            return edits;
        }
        void Write(const fs::path& path,const std::string& bytes)
        {
            std::ofstream output(path,std::ios::binary|std::ios::trunc);
            if (!output || !output.write(bytes.data(),static_cast<std::streamsize>(bytes.size()))) throw std::runtime_error("Cannot write "+Text(path));
            output.close(); if (!output) throw std::runtime_error("Cannot flush "+Text(path));
        }
    }

    void EditorAssetPanel::DrawDirectoryDropTarget(const std::filesystem::path& directory)
    {
        if (ImGui::BeginDragDropTarget()) {
            for (const auto* type:{"EDITOR_CONTENT_PATH","EDITOR_ASSET_PATH","EDITOR_MATERIAL_PATH"})
                if (const auto* payload=ImGui::AcceptDragDropPayload(type)) {
                    const auto* bytes=static_cast<const char*>(payload->Data);
                    if (payload->DataSize>1 && bytes[payload->DataSize-1]=='\0') {
                        const auto source=std::filesystem::u8path(bytes);
                        m_PendingFileOperation={EFileOperation::Move,source,directory/source.filename()};
                    }
                }
            ImGui::EndDragDropTarget();
        }
    }
    void EditorAssetPanel::DrawFileItemActions(const std::filesystem::path& path,bool bDirectory)
    {
        const auto id=Text(path); ImGui::PushID(id.c_str());
        if (path!=m_Root && ImGui::BeginDragDropSource()) {
            auto extension=Text(path.extension());
            std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
            const char* type=bDirectory ? "EDITOR_CONTENT_PATH" : extension==".glb" || extension==".gltf" ? "EDITOR_ASSET_PATH" : extension==".json" ? "EDITOR_MATERIAL_PATH" : "EDITOR_CONTENT_PATH";
            ImGui::SetDragDropPayload(type,id.c_str(),id.size()+1);
            ImGui::Text("Move %s to a folder",Text(path.filename()).c_str()); ImGui::EndDragDropSource();
        }
        if (bDirectory) DrawDirectoryDropTarget(path);
        if (ImGui::BeginPopupContextItem("Content actions")) {
            if (ImGui::MenuItem("Copy resource path")) ImGui::SetClipboardText(("res://"+Text(path.lexically_relative(m_Root))).c_str());
            if (bDirectory && ImGui::MenuItem("New folder")) {
                m_FileDialogOperation=EFileOperation::CreateDirectory; m_FileDialogPath=path; m_FileName="New Folder"; m_bRequestFileDialog=true;
            }
            if (path!=m_Root) {
                if (ImGui::MenuItem("Rename")) { m_FileDialogOperation=EFileOperation::Move; m_FileDialogPath=path; m_FileName=Text(path.filename()); m_bRequestFileDialog=true; }
                if (ImGui::MenuItem("Delete")) { m_FileDialogOperation=EFileOperation::Delete; m_FileDialogPath=path; m_bRequestFileDialog=true; }
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    void EditorAssetPanel::DrawDirectoryMenu(const std::filesystem::path& directory)
    {
        if (ImGui::BeginPopupContextWindow("Content background",ImGuiPopupFlags_MouseButtonRight|ImGuiPopupFlags_NoOpenOverItems)) {
            if (ImGui::MenuItem("New folder")) { m_FileDialogOperation=EFileOperation::CreateDirectory; m_FileDialogPath=directory; m_FileName="New Folder"; m_bRequestFileDialog=true; }
            ImGui::EndPopup();
        }
    }
    void EditorAssetPanel::DrawFileOperationDialog()
    {
        if (m_FileDialogOperation==EFileOperation::None) return;
        if (m_bRequestFileDialog) { ImGui::OpenPopup("Content file operation"); m_bRequestFileDialog=false; m_LastError.clear(); }
        if (ImGui::BeginPopupModal("Content file operation",nullptr,ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoSavedSettings)) {
            const bool deleting=m_FileDialogOperation==EFileOperation::Delete;
            ImGui::TextUnformatted(deleting ? "Delete files from disk?" : m_FileDialogOperation==EFileOperation::Move ? "Rename resource" : "Create folder");
            ImGui::TextWrapped("%s",Text(m_FileDialogPath.lexically_relative(m_Root)).c_str());
            if (deleting) ImGui::TextUnformatted("Folder contents will also be deleted. Scene resource copies remain.");
            else { ImGui::SetNextItemWidth(320); ImGUISlate::ImPropertyControls::Text("Name",m_FileName); }
            if (!m_LastError.empty()) ImGui::TextWrapped("%s",m_LastError.c_str());
            if (ImGui::Button(deleting ? "Delete files" : "OK")) {
                try {
                    if (!deleting) ValidateName(m_FileName);
                    const auto destination=deleting ? std::filesystem::path{} :
                        (m_FileDialogOperation==EFileOperation::CreateDirectory ? m_FileDialogPath : m_FileDialogPath.parent_path())/std::filesystem::u8path(m_FileName);
                    m_PendingFileOperation={m_FileDialogOperation,m_FileDialogPath,destination};
                    m_FileDialogOperation=EFileOperation::None; ImGui::CloseCurrentPopup();
                } catch (const std::exception& error) { m_LastError=error.what(); }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) { m_FileDialogOperation=EFileOperation::None; ImGui::CloseCurrentPopup(); }
            ImGui::EndPopup();
        }
    }
    void EditorAssetPanel::ProcessFileOperation()
    {
        if (m_PendingFileOperation.Type==EFileOperation::None) return;
        const auto operation=std::exchange(m_PendingFileOperation,{});
        try {
            if (operation.Type==EFileOperation::Move) MoveFilePath(operation.Source,operation.Destination);
            else if (operation.Type==EFileOperation::Delete) DeleteFilePath(operation.Source);
            else {
                const auto parent=std::filesystem::weakly_canonical(operation.Source);
                if (!Within(parent,m_Root) || !std::filesystem::is_directory(parent)) throw std::runtime_error("Folder destination must stay inside res://");
                ValidateName(Text(operation.Destination.filename()));
                const auto destination=parent/operation.Destination.filename();
                if (!std::filesystem::create_directory(destination)) throw std::runtime_error("A file or folder with that name already exists");
                m_TreeEntries.clear(); NavigateTo(parent); SelectPath(destination);
            }
            m_LastError.clear();
        } catch (const std::exception& error) { m_LastError=error.what(); }
    }
    void EditorAssetPanel::MoveFilePath(const std::filesystem::path& input,const std::filesystem::path& output)
    {
        const auto source=std::filesystem::weakly_canonical(input);
        const auto destination=std::filesystem::weakly_canonical(output.parent_path())/output.filename();
        if (!Within(source,m_Root) || source==m_Root || !Within(destination,m_Root) || std::filesystem::is_symlink(input)) throw std::runtime_error("Move must stay inside res://; root and links cannot be moved");
        if (!std::filesystem::exists(source) || !std::filesystem::is_directory(destination.parent_path())) throw std::runtime_error("Move source or destination is missing");
        if (source==destination) return;
        if (std::filesystem::exists(destination)) throw std::runtime_error("Destination already exists; nothing was overwritten");
        if (std::filesystem::is_directory(source) && Within(destination,source)) throw std::runtime_error("A folder cannot be moved into itself or its children");
        Engine::AssetManager::Get().WaitForLoads();
        const auto edits=CollectEdits(m_Root,source,destination);
        // File references are prepared before mutation. Roll back ordinary write
        // failures; this is a synchronous edit, not a crash-safe disk transaction.
        std::filesystem::rename(source,destination);
        try { for (const auto& edit:edits) Write(edit.After,edit.Updated); }
        catch (const std::exception& error) {
            std::string rollbackError;
            for (const auto& edit:edits) try { Write(edit.After,edit.Original); } catch (const std::exception& rollback) { rollbackError=rollback.what(); }
            try { std::filesystem::rename(destination,source); } catch (const std::exception& rollback) { rollbackError=rollback.what(); }
            throw std::runtime_error(std::string(error.what())+(rollbackError.empty() ? "; move rolled back" : "; rollback also failed: "+rollbackError));
        }
        Engine::AssetManager::Get().UpdateSourcePaths(source.generic_string(),destination.generic_string());
        const auto directory=Remap(m_Directory,source,destination), selected=Remap(m_SelectedPath,source,destination);
        for (auto& history:m_History) history=Remap(history,source,destination);
        for (auto& pending:m_PendingImports) { pending.Directory=Remap(pending.Directory,source,destination); pending.Source=Remap(pending.Source,source,destination); }
        m_TreeEntries.clear(); NavigateTo(directory,false); if (!selected.empty()) SelectPath(selected);
    }
    void EditorAssetPanel::DeleteFilePath(const std::filesystem::path& input)
    {
        const auto source=std::filesystem::weakly_canonical(input);
        if (!Within(source,m_Root) || source==m_Root || std::filesystem::is_symlink(input)) throw std::runtime_error("Root, links and paths outside res:// cannot be deleted");
        Engine::AssetManager::Get().WaitForLoads();
        CollectEdits(m_Root,source,{}); // Reject dangling incoming file dependencies.
        std::filesystem::remove_all(source);
        Engine::AssetManager::Get().UpdateSourcePaths(source.generic_string(),{});
        const auto directory=Within(m_Directory,source) ? source.parent_path() : m_Directory;
        const auto selected=Within(m_SelectedPath,source) ? std::filesystem::path{} : m_SelectedPath;
        std::erase_if(m_PendingImports,[&](const auto& pending){return Within(pending.Directory,source) || Within(pending.Source,source);});
        m_TreeEntries.clear(); m_History.clear(); m_HistoryIndex=0; NavigateTo(directory);
        if (!selected.empty()) SelectPath(selected);
        if (m_Material && m_Material->GetPath().empty()) m_Material.reset();
        if (m_Texture && m_Texture->GetPath().empty()) m_Texture.reset();
    }
}
