#include "EditorTextureViewer.h"
#include "TextureViewerPreview.h"
#include "TextureAsset.h"
#include "AssetManager.h"
#include "EnvironmentLightResourceManager.h"
#include "RenderResource.h"
#include "Widgets/ImCommonWidgets.h"
#include "imgui.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <stdexcept>

namespace Editor
{
    RenderCore::RenderTexture* TextureViewerSource::GetTexture() const
    {
        if (Kind==ETextureViewerSource::Asset) return Asset ? Asset->GetTexture() : nullptr;
        if (!Environment) return nullptr;
        return Kind==ETextureViewerSource::EnvironmentDiffuse ? Environment->GetDiffuseIrradiance() : Environment->GetSpecularPrefilter();
    }
    struct EditorTextureViewer::Image
    {
        uint64_t Id=0;
        TextureViewerSource Source;
        TextureViewerPreview Preview;
        double CenterX=0, CenterY=0;
        int Mip=0, Slice=0;
        bool bDirty=true;
    };
    namespace
    {
        std::string Lower(std::string text)
        { for (auto& c:text) c=static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return text; }
        std::string TextureInfo(RenderCore::RenderTexture* texture)
        {
            if (!texture || !texture->GetRHI()) return "Image file";
            const auto& d=texture->GetRHI()->GetDesc(); const auto format=RHI::GFormatInfoMap.find(d.Format);
            const char* type=d.Type==RHI::ERHITextureType::TextureCube ? "Cube" : d.Type==RHI::ERHITextureType::Texture2DArray ? "2D array" : "2D";
            return std::to_string(d.Width)+" x "+std::to_string(d.Height)+" | "+type+" | "+std::to_string(d.MipLevels)+" mips | "+
                (format==RHI::GFormatInfoMap.end() ? "Unknown" : format->second.Name);
        }
        bool Contains(const SlateCore::WidgetGeometry& g,float x,float y)
        { return x>=g.X && y>=g.Y && x<g.X+g.Width && y<g.Y+g.Height; }
    }
    EditorTextureViewer::EditorTextureViewer(std::function<std::vector<TextureViewerSource>()> getSources)
        : m_GetSources(std::move(getSources)) {}
    EditorTextureViewer::~EditorTextureViewer() { Reset(); }
    void EditorTextureViewer::Open() { m_bOpen=true; m_bVisible=true; }
    void EditorTextureViewer::Reset()
    {
        // Preview destruction waits its exact GUI/source fences before owners go.
        m_Images.clear(); m_Sources.clear(); m_PendingAdd.clear(); m_PendingRemove.clear(); m_PickerSelection.clear();
        m_Selected=m_DragImage=0; m_NextId=1; m_PanX=m_PanY=0; m_Zoom=1;
        m_bOpen=m_bVisible=m_bPickerOpen=m_bRequestPicker=m_bPanning=false; m_Status.clear(); m_Search.clear();
        Engine::EnvironmentLightResourceManager::Get().ReleaseUnused();
    }
    EditorTextureViewer::Image* EditorTextureViewer::GetSelected()
    { for (auto& image:m_Images) if (image->Id==m_Selected) return image.get(); return nullptr; }
    void EditorTextureViewer::Tick()
    {
        if (!m_bOpen) { if (!m_Images.empty() || !m_Sources.empty() || !m_PendingAdd.empty()) Reset(); return; }
        for (const auto id:m_PendingRemove) {
            m_Images.erase(std::remove_if(m_Images.begin(),m_Images.end(),[id](const auto& image){return image->Id==id;}),m_Images.end());
            if (m_Selected==id) m_Selected=m_Images.empty() ? 0 : m_Images.back()->Id;
        }
        if (!m_PendingRemove.empty()) Engine::EnvironmentLightResourceManager::Get().ReleaseUnused();
        m_PendingRemove.clear();
        for (auto& source:m_PendingAdd) {
            try {
                if (source.Kind==ETextureViewerSource::Asset && !source.Asset) {
                    source.Asset=Engine::AssetManager::Get().LoadSync<Engine::TextureAsset>(source.Path.generic_string());
                    if (!source.Asset) throw std::runtime_error("Cannot load "+source.Name);
                }
                const auto reason=TextureViewerPreview::UnsupportedReason(source.GetTexture());
                if (!reason.empty()) throw std::runtime_error(source.Name+": "+reason);
                auto image=std::make_unique<Image>(); image->Id=m_NextId++; image->Source=std::move(source);
                // The canvas center is world=-pan/zoom, independent of window size.
                image->CenterX=-m_PanX/m_Zoom; image->CenterY=-m_PanY/m_Zoom;
                m_Selected=image->Id; m_Images.push_back(std::move(image)); m_Status.clear();
            } catch (const std::exception& error) { m_Status=error.what(); }
        }
        m_PendingAdd.clear();
        if (!m_bPickerOpen) m_Sources.clear();
        if (m_bVisible) for (auto& image:m_Images) if (image->bDirty) {
            try { image->Preview.Render(image->Source,image->Mip,image->Slice); image->bDirty=false; }
            catch (const std::exception& error) { m_Status=error.what(); image->bDirty=false; }
        }
    }
    bool EditorTextureViewer::HitTest(float x,float y) const
    { return m_bOpen && ((m_bVisible && Contains(m_PopupGeometry,x,y)) || (m_bPickerOpen && Contains(m_PickerGeometry,x,y))); }
    void EditorTextureViewer::RefreshSources()
    {
        try { m_Sources=m_GetSources ? m_GetSources() : std::vector<TextureViewerSource>{}; m_PickerSelection.clear(); }
        catch (const std::exception& error) { m_Status=error.what(); m_Sources.clear(); }
    }
    void EditorTextureViewer::DrawList()
    {
        ImGui::TextUnformatted("Viewed textures");
        if (ImGui::BeginChild("TextureList",{0,std::max(100.0f,ImGui::GetContentRegionAvail().y*0.58f)},ImGuiChildFlags_Borders)) {
            for (const auto& image:m_Images) {
                ImGui::PushID(static_cast<int>(image->Id));
                if (ImGui::Selectable(image->Source.Name.c_str(),image->Id==m_Selected)) m_Selected=image->Id;
                if (ImGui::BeginPopupContextItem("Texture actions")) {
                    m_Selected=image->Id;
                    if (ImGui::MenuItem("Remove")) m_PendingRemove.push_back(image->Id);
                    ImGui::EndPopup();
                }
                ImGui::TextDisabled("%s",TextureInfo(image->Source.GetTexture()).c_str());
                ImGui::PopID();
            }
            if (m_Images.empty()) ImGui::TextDisabled("Right-click empty space to add textures.");
            if (ImGui::BeginPopupContextWindow("Texture list menu",ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
                if (ImGui::MenuItem("Add textures")) m_bRequestPicker=true;
                ImGui::EndPopup();
            }
        }
        ImGui::EndChild();
        if (auto* image=GetSelected()) {
            ImGui::SeparatorText("Selected texture"); ImGui::TextWrapped("%s",image->Source.Name.c_str());
            ImGui::TextWrapped("%s",TextureInfo(image->Source.GetTexture()).c_str());
            const auto& desc=image->Source.GetTexture()->GetRHI()->GetDesc();
            if (ImGui::SliderInt("Mip",&image->Mip,0,int(desc.MipLevels)-1)) image->bDirty=true;
            if (desc.Type==RHI::ERHITextureType::TextureCube) {
                const char* faces[]={"+X","-X","+Y","-Y","+Z","-Z"};
                if (ImGui::Combo("Face",&image->Slice,faces,6)) image->bDirty=true;
            } else if (desc.ArraySize>1 && ImGui::SliderInt("Layer",&image->Slice,0,int(desc.ArraySize)-1)) image->bDirty=true;
            if (ImGui::Button("Refresh image")) image->bDirty=true;
            if (ImGui::Button("Frame selected")) { m_PanX=-image->CenterX*m_Zoom; m_PanY=-image->CenterY*m_Zoom; }
        }
    }
    void EditorTextureViewer::DrawPicker()
    {
        if (m_bRequestPicker) {
            RefreshSources(); m_bPickerOpen=true; m_bRequestPicker=false; ImGui::OpenPopup("Add textures");
        }
        ImGui::SetNextWindowSize({650,470},ImGuiCond_FirstUseEver);
        if (ImGui::BeginPopupModal("Add textures",nullptr,ImGuiWindowFlags_NoSavedSettings)) {
            const auto p=ImGui::GetWindowPos(),s=ImGui::GetWindowSize(); m_PickerGeometry={p.x,p.y,s.x,s.y};
            const char* filters[]={"All sources","Assets","Environment lighting"};
            ImGui::SetNextItemWidth(190); ImGui::Combo("Source",&m_SourceFilter,filters,3);
            ImGUISlate::ImPropertyControls::Text("Search",m_Search,"Filter textures");
            if (ImGui::Button("Refresh sources")) RefreshSources();
            if (ImGui::BeginChild("TextureSources",{0,-44},ImGuiChildFlags_Borders)) {
                const auto search=Lower(m_Search); size_t shown=0;
                for (const auto& source:m_Sources) {
                    const bool asset=source.Kind==ETextureViewerSource::Asset;
                    if ((m_SourceFilter==1 && !asset) || (m_SourceFilter==2 && asset) || Lower(source.Name).find(search)==std::string::npos) continue;
                    ++shown; ImGui::PushID(source.Id.c_str());
                    auto selected=std::find(m_PickerSelection.begin(),m_PickerSelection.end(),source.Id);
                    auto* texture=source.GetTexture();
                    const auto reason=texture ? TextureViewerPreview::UnsupportedReason(texture) : source.Path.empty() ? "Unavailable texture" : "";
                    ImGui::BeginDisabled(!reason.empty());
                    if (ImGui::Selectable(source.Name.c_str(),selected!=m_PickerSelection.end())) {
                        if (selected==m_PickerSelection.end()) m_PickerSelection.push_back(source.Id);
                        else m_PickerSelection.erase(selected);
                    }
                    ImGui::EndDisabled();
                    if (!reason.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s",reason.c_str());
                    ImGui::TextDisabled("%s | %s",asset ? "Asset" : "Environment",TextureInfo(texture).c_str());
                    ImGui::PopID();
                }
                if (!shown) ImGui::TextDisabled("No textures match this source and search.");
            }
            ImGui::EndChild();
            ImGui::BeginDisabled(m_PickerSelection.empty());
            if (ImGui::Button("Add selected")) {
                for (const auto& source:m_Sources)
                    if (std::find(m_PickerSelection.begin(),m_PickerSelection.end(),source.Id)!=m_PickerSelection.end()) m_PendingAdd.push_back(source);
                m_bPickerOpen=false; ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled(); ImGui::SameLine();
            if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) { m_bPickerOpen=false; ImGui::CloseCurrentPopup(); }
            ImGui::SameLine(); ImGui::Text("%zu selected",m_PickerSelection.size());
            ImGui::EndPopup();
        }
    }
    void EditorTextureViewer::DrawCanvas()
    {
        const auto origin=ImGui::GetCursorScreenPos(),available=ImGui::GetContentRegionAvail();
        const ImVec2 size{std::max(1.0f,available.x),std::max(1.0f,available.y)};
        const ImVec2 end{origin.x+size.x,origin.y+size.y}, center{origin.x+size.x*0.5f,origin.y+size.y*0.5f};
        ImGui::InvisibleButton("TextureCanvas",size,ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
        const bool hovered=ImGui::IsItemHovered(), active=ImGui::IsItemActive(); const auto& io=ImGui::GetIO();
        if (hovered && io.MouseWheel!=0) {
            const double worldX=(io.MousePos.x-center.x-m_PanX)/m_Zoom, worldY=(io.MousePos.y-center.y-m_PanY)/m_Zoom;
            m_Zoom=std::clamp(m_Zoom*std::pow(1.15,io.MouseWheel),1.0/128,64.0);
            m_PanX=io.MousePos.x-center.x-worldX*m_Zoom; m_PanY=io.MousePos.y-center.y-worldY*m_Zoom;
        }
        const auto bounds=[&](const Image& image) {
            const auto& d=image.Source.GetTexture()->GetRHI()->GetDesc();
            const double w=std::max(1u,d.Width>>image.Mip)*m_Zoom, h=std::max(1u,d.Height>>image.Mip)*m_Zoom;
            const double x=center.x+m_PanX+image.CenterX*m_Zoom, y=center.y+m_PanY+image.CenterY*m_Zoom;
            return SlateCore::WidgetGeometry{float(x-w*0.5),float(y-h*0.5),float(w),float(h)};
        };
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            Image* hit=nullptr;
            if (auto* selected=GetSelected(); selected && Contains(bounds(*selected),io.MousePos.x,io.MousePos.y)) hit=selected;
            if (!hit) for (auto it=m_Images.rbegin();it!=m_Images.rend();++it)
                if (Contains(bounds(**it),io.MousePos.x,io.MousePos.y)) { hit=it->get(); break; }
            m_Selected=hit ? hit->Id : 0; m_DragImage=m_Selected; m_bPanning=!hit;
        }
        if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Middle,2)) {
            m_PanX+=io.MouseDelta.x; m_PanY+=io.MouseDelta.y;
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        } else if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Left,2)) {
            if (m_bPanning) { m_PanX+=io.MouseDelta.x; m_PanY+=io.MouseDelta.y; }
            else for (auto& image:m_Images) if (image->Id==m_DragImage) { image->CenterX+=io.MouseDelta.x/m_Zoom; image->CenterY+=io.MouseDelta.y/m_Zoom; }
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        }
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) { m_DragImage=0; m_bPanning=false; }
        if (ImGui::BeginPopupContextItem("Canvas actions")) {
            if (ImGui::MenuItem("Add textures")) m_bRequestPicker=true;
            if (m_Selected && ImGui::MenuItem("Remove selected")) m_PendingRemove.push_back(m_Selected);
            if (ImGui::MenuItem("Reset canvas")) { m_PanX=m_PanY=0; m_Zoom=1; }
            ImGui::EndPopup();
        }
        auto* draw=ImGui::GetWindowDrawList(); draw->PushClipRect(origin,end,true);
        draw->AddRectFilled(origin,end,IM_COL32(24,27,32,255));
        const double step=std::pow(2.0,std::floor(std::log2(64.0/m_Zoom)));
        const auto grid=[&](double spacing,int alpha) {
            if (!alpha) return;
            const double left=(origin.x-center.x-m_PanX)/m_Zoom, right=(end.x-center.x-m_PanX)/m_Zoom;
            const double top=(origin.y-center.y-m_PanY)/m_Zoom, bottom=(end.y-center.y-m_PanY)/m_Zoom;
            for (double x=std::floor(left/spacing)*spacing;x<=right;x+=spacing) {
                if (x==0) continue; // The origin axes are drawn once below.
                const float screen=float(center.x+m_PanX+x*m_Zoom); draw->AddLine({screen,origin.y},{screen,end.y},IM_COL32(130,143,160,alpha));
            }
            for (double y=std::floor(top/spacing)*spacing;y<=bottom;y+=spacing) {
                if (y==0) continue;
                const float screen=float(center.y+m_PanY+y*m_Zoom); draw->AddLine({origin.x,screen},{end.x,screen},IM_COL32(130,143,160,alpha));
            }
        };
        grid(step/8,int(22*std::clamp((step*m_Zoom/8-4)/4,0.0,1.0))); grid(step,55);
        // World zero follows the same pan/zoom transform as images and grid.
        const float zeroX=float(center.x+m_PanX), zeroY=float(center.y+m_PanY);
        const auto xColor=IM_COL32(255,31,31,235), yColor=IM_COL32(31,89,255,235);
        const auto labelSize=ImGui::CalcTextSize("+X");
        if (zeroY>=origin.y && zeroY<=end.y) {
            draw->AddLine({origin.x,zeroY},{end.x,zeroY},xColor,2.0f);
            draw->AddText({end.x-labelSize.x-8,std::max(origin.y,std::min(zeroY+4,end.y-labelSize.y))},xColor,"+X");
        }
        if (zeroX>=origin.x && zeroX<=end.x) {
            draw->AddLine({zeroX,origin.y},{zeroX,end.y},yColor,2.0f);
            draw->AddText({std::max(origin.x,std::min(zeroX+5,end.x-labelSize.x)),end.y-labelSize.y-8},yColor,"+Y");
        }
        if (zeroX>=origin.x && zeroX<=end.x && zeroY>=origin.y && zeroY<=end.y)
            draw->AddText({zeroX+5,zeroY+4},IM_COL32(220,225,235,255),"0");
        const auto paint=[&](Image& image) {
            const auto g=bounds(image); if (g.X>end.x || g.Y>end.y || g.X+g.Width<origin.x || g.Y+g.Height<origin.y) return;
            const ImVec2 a{g.X,g.Y},b{g.X+g.Width,g.Y+g.Height};
            if (auto* texture=image.Preview.GetTexture()) draw->AddImage(static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(texture)),a,b);
            draw->AddRect(a,b,image.Id==m_Selected ? IM_COL32(255,200,60,255) : IM_COL32(105,115,130,255),0,0,image.Id==m_Selected ? 2.5f : 1.0f);
        };
        for (auto& image:m_Images) if (image->Id!=m_Selected) paint(*image);
        if (auto* selected=GetSelected()) paint(*selected);
        draw->PopClipRect();
    }
    void EditorTextureViewer::Draw()
    {
        if (!m_bOpen) return;
        ImGui::SetNextWindowSize({1120,720},ImGuiCond_FirstUseEver);
        m_bVisible=ImGui::Begin("Texture Viewer",&m_bOpen,ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        const auto p=ImGui::GetWindowPos(),s=ImGui::GetWindowSize(); m_PopupGeometry={p.x,p.y,s.x,s.y};
        if (m_bVisible) {
            ImGui::TextDisabled("Left drag image: move | Empty left / Middle drag: pan | Wheel: zoom");
            ImGui::SameLine(); ImGui::Text("%.1f%%",m_Zoom*100);
            if (!m_Status.empty()) ImGui::TextWrapped("Texture preview: %s",m_Status.c_str());
            if (ImGui::BeginChild("TextureSidebar",{280,0},ImGuiChildFlags_Borders)) DrawList();
            ImGui::EndChild(); ImGui::SameLine();
            if (ImGui::BeginChild("TextureCanvasRegion",{0,0},0,ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) DrawCanvas();
            ImGui::EndChild(); DrawPicker();
        }
        ImGui::End();
    }
}
