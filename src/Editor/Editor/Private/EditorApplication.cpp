#include "EditorApplication.h"
#include "EditorScene.h"
#include "EditorScenePanel.h"
#include "EditorAssetPanel.h"
#include "EditorMaterialPanel.h"
#include "EditorNodePanel.h"
#include "EditorMeshViewer.h"
#include "EditorTextureViewer.h"
#include "Widgets/ImFileDialogWidget.h"
#include "AssetManager.h"
#include "StaticMeshAsset.h"
#include "PathInfo.h"
#include "imgui.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include "ImMainWindow.h"
#include "SceneViewport.h"
#include "SlateWidget.h"
#include "SlateRHIModule.h"
#include "StaticMeshComponent.h"
#include "MaterialAsset.h"
#include "LightComponent.h"
#include "EnvironmentLightResourceManager.h"
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <utility>
#include <unordered_set>
#include <unordered_map>
#include <cctype>

namespace Editor
{
    namespace
    {
        // The editor owns the region arrangement; generic splitters only report
        // drag distance. All pointers here are borrowed from EditorApplication.
        class EditorLayout final : public ImGUISlate::LayoutManager
        {
        public:
            EditorLayout(EditorViewToolbar& toolbar, ImGUISlate::ImSplitterWidget& left,
                ImGUISlate::ImSplitterWidget& right, ImGUISlate::ImSplitterWidget& bottom,
                ImGUISlate::ImWidget& menu, EditorMaterialPanel& material, EditorNodePanel& node)
                : m_Toolbar(toolbar), m_LeftSplitter(left), m_RightSplitter(right), m_BottomSplitter(bottom), m_Menu(menu), m_MaterialPopup(material), m_NodePanel(node) {}
            void Layout(float width, float height) override
            {
                const float menuHeight = std::min(24.0f,std::max(0.0f,height));
                const float ribbonHeight = std::min(112.0f, std::max(0.0f, height-menuHeight));
                const float headerHeight = menuHeight+ribbonHeight;
                const float gap = std::min(6.0f, std::max(0.0f, width) / 2);
                const bool hasNodePanel = m_NodePanel.GetVisibility() == SlateCore::EVisibility::Visible;
                const float rightGap = hasNodePanel ? gap : 0.0f;
                m_AvailableWidth = std::max(0.0f, width - gap - rightGap);
                const float widthScale = std::min(1.0f, m_AvailableWidth / (hasNodePanel ? 660.0f : 400.0f));
                m_MinLeft = 160 * widthScale; m_MinCenter = 240 * widthScale; m_MinRight = hasNodePanel ? 260 * widthScale : 0.0f;
                m_LeftWidth = std::clamp(m_LeftRatio * m_AvailableWidth, m_MinLeft,
                    std::max(m_MinLeft, m_AvailableWidth - m_MinCenter - m_MinRight));
                m_RightWidth = hasNodePanel ? std::clamp(m_RightRatio * m_AvailableWidth, m_MinRight,
                    std::max(m_MinRight, m_AvailableWidth - m_LeftWidth - m_MinCenter)) : 0.0f;
                const float centerWidth = std::max(0.0f, m_AvailableWidth - m_LeftWidth - m_RightWidth);
                const float contentHeight = std::max(0.0f, height - headerHeight);
                const float horizontalGap = std::min(6.0f, contentHeight);
                m_AvailableHeight = contentHeight - horizontalGap;
                const float heightScale = std::min(1.0f, m_AvailableHeight / 360);
                m_MinTop = 220 * heightScale; m_MinBottom = 140 * heightScale;
                m_BottomHeight = std::clamp(m_BottomRatio * m_AvailableHeight, m_MinBottom,
                    std::max(m_MinBottom, m_AvailableHeight - m_MinTop));
                const float topHeight = std::max(0.0f, m_AvailableHeight - m_BottomHeight);
                const float centerX = m_LeftWidth + gap;
                const float rightX = centerX + centerWidth + rightGap;
                for (auto& element : _elements)
                {
                    auto* widget = element.WidgetPtr;
                    if (widget == &m_Menu || widget == &m_MaterialPopup || widget == &m_Toolbar || widget == &m_LeftSplitter || widget == &m_RightSplitter || widget == &m_BottomSplitter || dynamic_cast<ImGUISlate::PopupImWidget*>(widget)) continue;
                    if (element.Params.Row == 3)
                        widget->SetGeometry(0, headerHeight + topHeight + horizontalGap, width, m_BottomHeight);
                    else if (element.Params.Column == 0)
                        widget->SetGeometry(0, headerHeight, m_LeftWidth, topHeight);
                    else if (element.Params.Column == 1)
                        widget->SetGeometry(centerX, headerHeight, centerWidth, topHeight);
                    else
                        widget->SetGeometry(rightX, headerHeight, m_RightWidth, topHeight);
                }
                m_Menu.SetGeometry(0,0,width,menuHeight);
                m_Toolbar.SetGeometry(0, menuHeight, width, ribbonHeight);
                m_LeftSplitter.SetGeometry(m_LeftWidth, headerHeight, gap, topHeight);
                m_RightSplitter.SetVisibility(hasNodePanel ? SlateCore::EVisibility::Visible : SlateCore::EVisibility::Collapsed);
                m_RightSplitter.SetGeometry(rightX - rightGap, headerHeight, rightGap, topHeight);
                m_BottomSplitter.SetGeometry(0, headerHeight + topHeight, width, horizontalGap);
            }
            bool ResizeLeft(float delta)
            {
                if (m_AvailableWidth <= 0) return false;
                const float value = std::clamp(m_LeftWidth + delta, m_MinLeft,
                    std::max(m_MinLeft, m_AvailableWidth - m_RightWidth - m_MinCenter));
                if (value == m_LeftWidth) return false;
                m_LeftRatio = value / m_AvailableWidth; return true;
            }
            bool ResizeRight(float delta)
            {
                if (m_AvailableWidth <= 0) return false;
                const float value = std::clamp(m_RightWidth - delta, m_MinRight,
                    std::max(m_MinRight, m_AvailableWidth - m_LeftWidth - m_MinCenter));
                if (value == m_RightWidth) return false;
                m_RightRatio = value / m_AvailableWidth; return true;
            }
            bool ResizeBottom(float delta)
            {
                if (m_AvailableHeight <= 0) return false;
                const float value = std::clamp(m_BottomHeight - delta, m_MinBottom,
                    std::max(m_MinBottom, m_AvailableHeight - m_MinTop));
                if (value == m_BottomHeight) return false;
                m_BottomRatio = value / m_AvailableHeight; return true;
            }
        private:
            EditorViewToolbar& m_Toolbar;
            ImGUISlate::ImSplitterWidget& m_LeftSplitter;
            ImGUISlate::ImSplitterWidget& m_RightSplitter;
            ImGUISlate::ImSplitterWidget& m_BottomSplitter;
            ImGUISlate::ImWidget& m_Menu;
            EditorMaterialPanel& m_MaterialPopup;
            EditorNodePanel& m_NodePanel;
            float m_LeftRatio = 1.0f / 7, m_RightRatio = 2.0f / 7, m_BottomRatio = 0.35f;
            float m_AvailableWidth = 0, m_AvailableHeight = 0;
            float m_LeftWidth = 0, m_RightWidth = 0, m_BottomHeight = 0;
            float m_MinLeft = 0, m_MinCenter = 0, m_MinRight = 0, m_MinTop = 0, m_MinBottom = 0;
        };
    }

    EditorApplication::EditorApplication() = default;
    EditorApplication::~EditorApplication() { Shutdown(); }

    bool EditorApplication::Initialize()
    {
        _renderer = SlateRHIRenderer::GSlateRHIRendererModule->CreateSlateRenderer();
        m_Scene = std::make_unique<EditorScene>();
        m_Scene->Initialize();
        m_Window = std::make_shared<ImGUISlate::ImMainWindow>(1600, 900, "RenderingLearn Editor", _renderer);
        if (!m_Window->Initialize()) throw std::runtime_error("Failed to initialize editor window");
        m_Window->SetCloseCallback([this] { m_PendingProjectAction = ProjectAction::Exit; });
        m_Viewport = std::make_unique<Engine::SceneViewport>(m_Scene.get(), Core::Int2{800, 800});
        auto viewportWidget = std::make_unique<EditorSceneViewportWidget>(*m_Scene);
        viewportWidget->SetOnResourceDropped([this](const auto& path) { if (!m_ProjectDirectory.empty()) m_DroppedMesh=path; });
        viewportWidget->SetOnMaterialDropped([this](const auto& path, const auto& position) {
            if (!m_ProjectDirectory.empty()) { m_DroppedMaterial=path; m_DroppedMaterialPosition=position; }
        });
        m_ViewportWidget = std::move(viewportWidget);
        m_ViewportWidget->SetViewportChild(m_Viewport.get());
        m_ScenePanel = std::make_unique<EditorScenePanel>(*m_Scene);
        m_ScenePanel->SetOnAddObject([this](EEditorSceneObject type) {
            if (!m_ProjectDirectory.empty()) m_PendingObject = type;
        });
        m_ScenePanel->SetOnDeleteObject([this](int index) {
            if (!m_ProjectDirectory.empty()) m_PendingDeleteObject=index;
        });
        m_AssetPanel = std::make_unique<EditorAssetPanel>(std::filesystem::path{});
        m_AssetPanel->SetOnMeshOpened([this](const auto& asset) { if (!m_ProjectDirectory.empty()) m_PendingMeshAsset=asset; });
        m_AssetPanel->SetOnMaterialAssigned([this](const auto& asset) {
            auto* component = m_Scene->GetSelectedMesh();
            if (!component) return false;
            component->SetMaterial(0, asset->GetMaterial());
            m_Scene->NotifySelectedChanged();
            return true;
        });
        MaterialEditorCallbacks callbacks;
        callbacks.GetSelectedComponent = [this] { return m_Scene->GetSelectedMesh(); };
        callbacks.GetSelectedMaterial = [this] { return m_AssetPanel->GetSelectedMaterial(); };
        callbacks.GetSelectedTexture = [this] { return m_AssetPanel->GetSelectedTexture(); };
        callbacks.OnMaterialChanged = [this](bool bSharedAsset) {
            if (bSharedAsset) m_Scene->NotifyMaterialsChanged();
            else m_Scene->NotifySelectedChanged();
        };
        m_MaterialPanel = std::make_unique<EditorMaterialPanel>(std::move(callbacks),std::filesystem::path{});
        m_AssetPanel->SetOnMaterialOpened([this](const auto& asset) { m_MaterialPanel->ShowMaterial(asset); });
        m_NodePanel=std::make_unique<EditorNodePanel>([this] { return m_Scene->GetSelected(); },[this] {
            m_Scene->NotifySelectedChanged();
            Engine::EnvironmentLightResourceManager::Get().ReleaseUnused();
            if (dynamic_cast<Engine::SkyLightComponent*>(m_Scene->GetSelected())) {
                auto assets=CollectProjectAssets();
                m_Scene->SetProjectAssets(assets); m_AssetPanel->SetProjectAssets(std::move(assets));
            }
        });
        m_NodePanel->SetSelectedTextureProvider([this] { return m_AssetPanel->GetSelectedTexture(); });
        m_NodePanel->SetTranslationGizmoCallbacks(
            [this] { auto* selected = m_Scene->GetSelected(); return selected && m_Scene->GetTranslationTarget() == selected; },
            [this](bool enabled) { m_Scene->SetTranslationTarget(enabled ? m_Scene->GetSelected() : nullptr); });
        m_NodePanel->SetRotationGizmoCallbacks(
            [this] { auto* selected = m_Scene->GetSelected(); return selected && m_Scene->GetRotationTarget() == selected; },
            [this](bool enabled) { m_Scene->SetRotationTarget(enabled ? m_Scene->GetSelected() : nullptr); });
        m_NodePanel->SetScaleGizmoCallbacks(
            [this] { auto* selected = m_Scene->GetSelected(); return selected && m_Scene->GetScaleTarget() == selected; },
            [this](bool enabled) { m_Scene->SetScaleTarget(enabled ? m_Scene->GetSelected() : nullptr); });
        m_NodePanel->SetVisibility(SlateCore::EVisibility::Collapsed);
        m_MenuWidget=std::make_unique<ImGUISlate::ImWidget>([this](int,int,int,int) { DrawProjectMenu(); });
        m_ProjectDialog=std::make_unique<ImGUISlate::ImFileDialogWidget>(); m_ProjectDialog->Close();
        m_ProjectDialog->SetSelectionMode(ImGUISlate::ImFileDialogWidget::SelectionMode::Directory);
        m_ProjectDialog->SetOnConfirm([this](const auto& paths) {
            if (!paths.empty()) { m_PendingProjectAction=m_DialogAction; m_PendingProjectDirectory=paths.front(); }
        });
        m_ViewToolbar = std::make_unique<EditorViewToolbar>(*m_Scene);
        m_MeshViewer=std::make_unique<EditorMeshViewer>([this] {
            std::vector<MeshViewerMesh> result;
            for (const auto& object:m_Scene->GetObjects())
                if (const auto* component=dynamic_cast<const Engine::StaticMeshComponent*>(object.Component.get()))
                {
                    const auto* mesh=component->GetStaticMesh();
                    result.push_back({object.Name,mesh ? mesh->GetAsset() : nullptr});
                }
            return result;
        });
        m_TextureViewer=std::make_unique<EditorTextureViewer>([this] { return CollectTextureViewerSources(); });
        m_ViewToolbar->SetOnOpenTextureViewer([this] { m_TextureViewer->Open(); });
        m_ViewToolbar->SetOnOpenMeshViewer([this] {
            std::string selected;
            for (const auto& object:m_Scene->GetObjects())
                if (object.Component.get()==m_Scene->GetSelectedMesh()) { selected=object.Name; break; }
            m_MeshViewer->Open(selected);
        });
        m_LeftSplitter = std::make_unique<ImGUISlate::ImSplitterWidget>("EditorSplitLeft", true);
        m_RightSplitter = std::make_unique<ImGUISlate::ImSplitterWidget>("EditorSplitRight", true);
        m_BottomSplitter = std::make_unique<ImGUISlate::ImSplitterWidget>("EditorSplitBottom", false);
        // The node column only takes space while a component is selected.
        auto layoutOwner = std::make_unique<EditorLayout>(*m_ViewToolbar, *m_LeftSplitter, *m_RightSplitter, *m_BottomSplitter,*m_MenuWidget,*m_MaterialPanel,*m_NodePanel);
        auto* editorLayout = layoutOwner.get();
        m_LeftSplitter->SetOnDragged([this, editorLayout](float delta) { if (editorLayout->ResizeLeft(delta)) m_Window->UpdateLayout(); });
        m_RightSplitter->SetOnDragged([this, editorLayout](float delta) { if (editorLayout->ResizeRight(delta)) m_Window->UpdateLayout(); });
        m_BottomSplitter->SetOnDragged([this, editorLayout](float delta) { if (editorLayout->ResizeBottom(delta)) m_Window->UpdateLayout(); });
        m_Window->SetLayout(std::move(layoutOwner));
        // Process splitter changes before this frame records viewport texture uses.
        m_Window->AddWidget(m_LeftSplitter.get(), {});
        m_Window->AddWidget(m_RightSplitter.get(), {});
        m_Window->AddWidget(m_BottomSplitter.get(), {});
        m_Window->AddWidget(m_MenuWidget.get(),{});
        m_Window->AddWidget(m_ViewToolbar.get(), {});
        ImGUISlate::LayoutParams layout;
        layout.RowSpan = 3;
        m_Window->AddWidget(m_ScenePanel.get(), layout);
        layout.Column = 1;
        layout.ColumnSpan = 4;
        m_Window->AddWidget(m_ViewportWidget.get(), layout);
        layout.Row = 3;
        layout.RowSpan = 1;
        layout.Column = 0;
        layout.ColumnSpan = 7;
        m_Window->AddWidget(m_AssetPanel.get(), layout);
        layout.Row = 0;
        layout.RowSpan = 3;
        layout.Column = 5;
        layout.ColumnSpan = 2;
        m_Window->AddWidget(m_NodePanel.get(),layout);
        m_Window->AddWidget(m_MaterialPanel.get(),{});
        m_Window->AddWidget(m_MeshViewer.get(),{});
        m_Window->AddWidget(m_TextureViewer.get(),{});
        m_Window->Show();
        std::cout << "[Editor] Initialized Scene / Assets / Material panels" << std::endl;
        return true;
    }

    void EditorApplication::Shutdown()
    {
        // Existing window release covers its submitted sampling/present work.
        // Keep widgets, viewport and assets alive until that release has completed.
        m_Window.reset();
        m_MeshViewer.reset();
        m_TextureViewer.reset();
        m_ProjectDialog.reset(); m_MenuWidget.reset(); m_NodePanel.reset();
        m_LeftSplitter.reset();
        m_RightSplitter.reset();
        m_BottomSplitter.reset();
        m_ViewToolbar.reset();
        m_MaterialPanel.reset();
        m_AssetPanel.reset();
        m_ScenePanel.reset();
        m_ViewportWidget.reset();
        m_Viewport.reset();
        m_Scene.reset();
        if (GetApplication() == this) SetApplication(nullptr);
    }

    void EditorApplication::TickFrame()
    {
        m_Window->PollEvents();
        if (m_bQuit) return;
        try
        {
            auto action=m_PendingProjectAction; m_PendingProjectAction=ProjectAction::None;
            if ((action==ProjectAction::Create || action==ProjectAction::Open || action==ProjectAction::Close || action==ProjectAction::Exit) &&
                m_Scene->HasUnsavedChanges() && !m_bActionConfirmed)
            {
                m_ConfirmAction=action; m_ConfirmDirectory=m_PendingProjectDirectory; m_bShowUnsaved=true; action=ProjectAction::None;
            }
            m_bActionConfirmed=false;
            if (std::exchange(m_bSaveBeforeAction,false)) SaveProject();
            if (action==ProjectAction::Create) CreateProject(m_PendingProjectDirectory);
            else if (action==ProjectAction::Open) OpenProject(m_PendingProjectDirectory);
            else if (action==ProjectAction::Close) CloseProject();
            else if (action==ProjectAction::Save) SaveProject();
            else if (action==ProjectAction::SaveAs) SaveProjectAs(m_PendingProjectDirectory);
            else if (action==ProjectAction::Exit) { m_bQuit=true; return; }
            if (m_PendingDeleteObject>=0)
            {
                const int index=m_PendingDeleteObject; m_PendingDeleteObject=-1;
                if (!m_ProjectDirectory.empty() && index<static_cast<int>(m_Scene->GetObjects().size()))
                { m_Scene->Select(index); m_Scene->RemoveSelected(); m_ProjectError.clear(); }
            }
            if (m_PendingObject)
            {
                const auto type = *m_PendingObject; m_PendingObject.reset();
                if (!m_ProjectDirectory.empty()) { m_Scene->AddObject(type); m_ProjectError.clear(); }
            }
            if (m_PendingMeshAsset) {
                auto asset=std::move(m_PendingMeshAsset);
                if (!m_ProjectDirectory.empty()) m_Scene->AddMesh(asset);
            }
            if (!m_DroppedMesh.empty())
            {
                const auto path=std::move(m_DroppedMesh); m_DroppedMesh.clear();
                if (!m_ProjectDirectory.empty())
                {
                    const auto root=std::filesystem::weakly_canonical(m_ProjectDirectory/"Content");
                    const auto candidate=std::filesystem::weakly_canonical(path);
                    const auto relative=candidate.lexically_relative(root);
                    if (relative.empty() || relative.is_absolute() || *relative.begin()=="..") throw std::runtime_error("Mesh must be inside the current project");
                    m_Scene->AddMesh(Engine::AssetManager::Get().LoadSync<Engine::StaticMeshAsset>(candidate.string()));
                }
            }
            if (!m_DroppedMaterial.empty())
            {
                const auto path=std::move(m_DroppedMaterial); m_DroppedMaterial.clear();
                if (!m_ProjectDirectory.empty())
                {
                    const auto root=std::filesystem::weakly_canonical(m_ProjectDirectory/"Content");
                    const auto candidate=std::filesystem::weakly_canonical(path);
                    const auto relative=candidate.lexically_relative(root);
                    if (relative.empty() || relative.is_absolute() || *relative.begin()=="..") throw std::runtime_error("Material must be inside the current project");
                    if (!m_Scene->ApplyMaterialAt(Engine::AssetManager::Get().LoadSync<Engine::MaterialAsset>(candidate.string()),m_DroppedMaterialPosition))
                        throw std::runtime_error("Drop the material on a mesh surface");
                    m_ProjectError.clear();
                }
            }
        }
        catch (const std::exception& error) { m_ProjectError=error.what(); std::cerr<<"[Editor] "<<m_ProjectError<<std::endl; }
        const auto nodeVisibility = m_Scene->GetSelected() ? SlateCore::EVisibility::Visible : SlateCore::EVisibility::Collapsed;
        if (m_NodePanel->GetVisibility() != nodeVisibility)
        {
            m_NodePanel->SetVisibility(nodeVisibility);
            // Resize before this frame records viewport texture references.
            m_Window->UpdateLayout();
        }
        m_MeshViewer->Tick();
        m_TextureViewer->Tick();
        m_Window->Draw();
        const auto& io = ImGui::GetIO();
        if (!m_Window->HasMouseCapture() &&
            (io.WantCaptureMouse || !m_ViewportWidget->HitTest(io.MousePos.x,io.MousePos.y)))
            m_Scene->ClearTransformHover();
        if (!m_bLoggedFrame)
        {
            std::cout << "[Editor] First scene and GUI frame submitted" << std::endl;
            m_bLoggedFrame = true;
        }
    }

    size_t EditorApplication::GetSceneObjectCount() const { return m_Scene ? m_Scene->GetObjects().size() : 0; }
    void EditorApplication::ImportResources(const std::vector<std::filesystem::path>& files)
    {
        if (m_ProjectDirectory.empty()) throw std::runtime_error("Open a project before importing resources");
        m_AssetPanel->QueueImport(files);
    }
    void EditorApplication::CreateProject(const std::filesystem::path& directory)
    {
        namespace fs=std::filesystem;
        const auto root=fs::weakly_canonical(fs::absolute(directory));
        fs::create_directories(root);
        const auto marker=root/"RenderingLearn.project.json", content=root/"Content";
        if (fs::exists(marker)) throw std::runtime_error("A project already exists in this directory");
        if (fs::exists(content) && (!fs::is_directory(content) || !fs::is_empty(content))) throw std::runtime_error("Choose a directory with an empty Content folder");
        fs::create_directories(content);
        const auto relative=fs::weakly_canonical(content).lexically_relative(root);
        if (relative.empty() || relative.is_absolute() || *relative.begin()=="..") throw std::runtime_error("Project content must stay inside its directory");
        nlohmann::json data{{"version",1},{"name",MaterialLibrary::PathText(root.filename())},{"contentDirectory","Content"}};
        try { std::ofstream output(marker); if (!(output<<data.dump(2))) throw std::runtime_error("Cannot write project description"); output.close(); if (!output) throw std::runtime_error("Cannot flush project description"); }
        catch (...) { std::error_code ignored; fs::remove(marker,ignored); throw; }
        OpenProject(root);
    }
    void EditorApplication::OpenProject(const std::filesystem::path& directory)
    {
        namespace fs=std::filesystem;
        const auto root=fs::weakly_canonical(fs::absolute(directory));
        nlohmann::json data; std::ifstream input(root/"RenderingLearn.project.json");
        if (!input || !(input>>data) || !data.is_object() || (data.value("version",0)!=1 && data.value("version",0)!=2) || data.value("contentDirectory",std::string{})!="Content" ||
            (data.value("version",0)==1 && !fs::is_directory(root/"Content")))
            throw std::runtime_error("Select a valid RenderingLearn project directory");
        const auto content=fs::weakly_canonical(root/"Content");
        const auto relative=content.lexically_relative(root);
        if (relative.empty() || relative.is_absolute() || *relative.begin()=="..") throw std::runtime_error("Project content must stay inside its directory");
        auto objects=Engine::ProjectArchive::Load(root);
        fs::create_directories(content); // Saved snapshots do not depend on the import folder.
        m_Window->WaitForLastDraw();
        m_Scene->ReplaceObjects(std::move(objects));
        ResetProjectPanels(); Engine::AssetManager::Get().Clear(); m_ProjectDirectory=root;
        m_ScenePanel->SetCreationEnabled(true);
        m_AssetPanel->SetContentRoot(content); m_MaterialPanel->SetContentRoot(content); m_ProjectError.clear();
        m_AssetPanel->SetProjectAssets(m_Scene->GetProjectAssets());
    }
    void EditorApplication::CloseProject()
    {
        m_Window->WaitForLastDraw();
        ResetProjectPanels();
        m_Scene->ClearObjects(); Engine::AssetManager::Get().Clear();
        m_ProjectDirectory.clear(); m_ProjectError.clear();
    }
    void EditorApplication::ResetProjectPanels()
    {
        m_MeshViewer->Reset();
        m_TextureViewer->Reset();
        m_PendingMeshAsset.reset(); m_AssetPanel->SetProjectAssets({});
        m_PendingObject.reset(); m_PendingDeleteObject=-1; m_ScenePanel->SetCreationEnabled(false);
        m_ProjectDialog->Close(); m_DroppedMesh.clear(); m_DroppedMaterial.clear(); m_DroppedMaterialPosition={}; m_PendingProjectAction=ProjectAction::None;
        m_PendingProjectDirectory.clear();
        m_MaterialPanel->SetContentRoot({}); m_AssetPanel->SetContentRoot({});
    }
    void EditorApplication::SaveProject()
    {
        if (m_ProjectDirectory.empty()) throw std::runtime_error("Open a project before saving");
        auto assets=CollectProjectAssets();
        Engine::ProjectArchive::Save(m_ProjectDirectory,m_Scene->GetObjects(),assets);
        m_Scene->SetProjectAssets(assets); m_AssetPanel->SetProjectAssets(std::move(assets));
        m_Scene->MarkSaved(); m_ProjectError.clear();
        std::cout << "[Editor] Saved project snapshot: " << m_Scene->GetObjects().size() << " nodes" << std::endl;
    }
    void EditorApplication::SaveProjectAs(const std::filesystem::path& directory)
    {
        namespace fs=std::filesystem;
        if (m_ProjectDirectory.empty()) throw std::runtime_error("Open a project before saving a copy");
        const auto root=fs::weakly_canonical(fs::absolute(directory));
        if (root==m_ProjectDirectory) { SaveProject(); return; }
        if (fs::exists(root) && !fs::is_empty(root)) throw std::runtime_error("Save As requires an empty directory");
        fs::create_directories(root/"Content");
        const auto marker=root/"RenderingLearn.project.json";
        { std::ofstream output(marker); output<<nlohmann::json{{"version",1},{"name",MaterialLibrary::PathText(root.filename())},{"contentDirectory","Content"}}.dump(2);
          output.close(); if (!output) throw std::runtime_error("Cannot write project copy description"); }
        auto assets=CollectProjectAssets();
        Engine::ProjectArchive::Save(root,m_Scene->GetObjects(),assets);
        m_ProjectDirectory=root; m_AssetPanel->SetContentRoot(root/"Content"); m_MaterialPanel->SetContentRoot(root/"Content");
        m_Scene->SetProjectAssets(assets); m_AssetPanel->SetProjectAssets(std::move(assets));
        m_Scene->MarkSaved(); m_ProjectError.clear();
    }
    std::vector<std::shared_ptr<Engine::IAsset>> EditorApplication::CollectProjectAssets() const
    {
        auto result=m_Scene->GetProjectAssets();
        for (const auto& node:m_Scene->GetObjects())
            if (const auto* sky=dynamic_cast<const Engine::SkyLightComponent*>(node.Component.get()))
                if (const auto& environment=sky->GetEnvironmentResource(); environment && environment->GetInputTexture())
                    result.push_back(environment->GetInputTexture());
        for (const auto& node:m_Scene->GetObjects())
            if (const auto* component=dynamic_cast<const Engine::StaticMeshComponent*>(node.Component.get()))
                if (const auto* mesh=component->GetStaticMesh()) {
                    result.push_back(mesh->GetAsset());
                    for (uint32_t slot=0;slot<component->GetNumMaterials();++slot)
                        if (const auto* material=component->GetMaterial(slot)) result.push_back(material->GetAsset());
                }
        const auto content=std::filesystem::weakly_canonical(m_ProjectDirectory/"Content");
        for (const auto& asset:Engine::AssetManager::Get().GetResidentAssets()) {
            if (asset->GetPath().empty()) continue;
            const auto relative=std::filesystem::weakly_canonical(std::filesystem::path(asset->GetPath())).lexically_relative(content);
            if (!relative.empty() && !relative.is_absolute() && *relative.begin()!="..") result.push_back(asset);
        }
        if (auto asset=m_MaterialPanel->GetSelectedMaterial()) result.push_back(asset);
        std::unordered_set<const Engine::IAsset*> seen;
        result.erase(std::remove_if(result.begin(),result.end(),[&](const auto& asset) { return !asset || !seen.insert(asset.get()).second; }),result.end());
        return result;
    }
    std::vector<TextureViewerSource> EditorApplication::CollectTextureViewerSources() const
    {
        namespace fs=std::filesystem;
        std::vector<TextureViewerSource> result;
        const auto content=m_ProjectDirectory.empty() ? fs::path{} : fs::weakly_canonical(m_ProjectDirectory/"Content");
        const auto environments=Engine::EnvironmentLightResourceManager::Get().GetResources();
        std::unordered_set<std::string> files;
        const auto fileKey=[](const fs::path& path) {
            auto key=fs::weakly_canonical(fs::absolute(path)).generic_string();
#ifdef _WIN32
            std::transform(key.begin(),key.end(),key.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
#endif
            return key;
        };
        // Browse the asset catalog, not every runtime copy referenced by the
        // scene or by environment inputs. Those owners are not new sources.
        if (!content.empty() && fs::is_directory(content)) {
            for (const auto& entry:fs::recursive_directory_iterator(content)) if (entry.is_regular_file()) {
                auto extension=entry.path().extension().string();
                std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
                if (extension!=".png" && extension!=".jpg" && extension!=".jpeg" && extension!=".bmp" && extension!=".tga") continue;
                const auto key=fileKey(entry.path());
                if (!files.insert(key).second) continue;
                TextureViewerSource source; source.Id="file:"+key; source.Path=entry.path();
                source.Asset=Engine::AssetManager::Get().GetAssetShared<Engine::TextureAsset>(entry.path().generic_string());
                source.Name=MaterialLibrary::PathText(entry.path().lexically_relative(content));
                result.push_back(std::move(source));
            }
        }
        const auto distinguishNames=[&](size_t begin) {
            std::unordered_map<std::string,size_t> counts,indices;
            std::unordered_set<std::string> labels;
            for (size_t i=begin;i<result.size();++i) { ++counts[result[i].Name]; labels.insert(result[i].Name); }
            for (size_t i=begin;i<result.size();++i) if (counts[result[i].Name]>1) {
                const auto name=result[i].Name;
                do { result[i].Name=name+" ["+std::to_string(++indices[name])+"]"; }
                while (!labels.insert(result[i].Name).second);
            }
        };
        const auto environmentBegin=result.size();
        for (const auto& environment:environments) {
            const auto& input=environment->GetInputTexture();
            const auto name=input ? input->GetName() : "Default environment";
            for (int kind=0;kind<2;++kind) {
                TextureViewerSource source; source.Environment=environment;
                source.Kind=kind==0 ? ETextureViewerSource::EnvironmentDiffuse : ETextureViewerSource::EnvironmentSpecular;
                source.Id="environment:"+std::to_string(reinterpret_cast<uintptr_t>(environment.get()))+":"+std::to_string(kind);
                source.Name=name+(kind==0 ? " / Diffuse IBL" : " / Specular IBL"); result.push_back(std::move(source));
            }
        }
        distinguishNames(environmentBegin);
        // TODO: Add a material-texture source provider when that inspection API is available.
        std::stable_sort(result.begin(),result.end(),[](const auto& a,const auto& b) { return a.Kind==b.Kind ? a.Name<b.Name : a.Kind<b.Kind; });
        return result;
    }
    void EditorApplication::DrawProjectMenu()
    {
        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("Project"))
            {
                if (ImGui::MenuItem("Save","Ctrl+S",false,!m_ProjectDirectory.empty())) m_PendingProjectAction=ProjectAction::Save;
                if (ImGui::MenuItem("Save As...",nullptr,false,!m_ProjectDirectory.empty())) {
                    m_DialogAction=ProjectAction::SaveAs; m_ProjectDialog->SetTitle("Save project to an empty directory");
                    m_ProjectDialog->SetInitialDirectory(m_ProjectDirectory.parent_path()); m_ProjectDialog->Open();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("New"))
                {
                    m_DialogAction=ProjectAction::Create; m_ProjectDialog->SetTitle("New project directory");
                    m_ProjectDialog->SetInitialDirectory(m_ProjectDirectory.empty()?std::filesystem::path(Core::GetProjectDir()):m_ProjectDirectory); m_ProjectDialog->Open();
                }
                if (ImGui::MenuItem("Open"))
                {
                    m_DialogAction=ProjectAction::Open; m_ProjectDialog->SetTitle("Open project directory");
                    m_ProjectDialog->SetInitialDirectory(m_ProjectDirectory.empty()?std::filesystem::path(Core::GetProjectDir()):m_ProjectDirectory); m_ProjectDialog->Open();
                }
                if (ImGui::MenuItem("Close",nullptr,false,!m_ProjectDirectory.empty())) m_PendingProjectAction=ProjectAction::Close;
                ImGui::EndMenu();
            }
            ImGui::Separator(); ImGui::Text("%s%s | %zu nodes",m_ProjectDirectory.empty()?"No project":MaterialLibrary::PathText(m_ProjectDirectory.filename()).c_str(),m_Scene->HasUnsavedChanges()?" *":"",GetSceneObjectCount());
            if (!m_ProjectError.empty()) { ImGui::TextColored({1,0.4f,0.3f,1},"%s",m_ProjectError.c_str()); }
            ImGui::EndMainMenuBar();
        }
        m_ProjectDialog->Draw();
        if (!m_ProjectDirectory.empty() && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S,false)) m_PendingProjectAction=ProjectAction::Save;
        if (m_bShowUnsaved && !ImGui::IsPopupOpen("Unsaved project")) ImGui::OpenPopup("Unsaved project");
        if (ImGui::BeginPopupModal("Unsaved project",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Save changes before continuing?");
            const auto continueAction=[this](bool save) {
                m_PendingProjectAction=m_ConfirmAction; m_PendingProjectDirectory=m_ConfirmDirectory;
                m_bSaveBeforeAction=save; m_bActionConfirmed=true; m_bShowUnsaved=false; ImGui::CloseCurrentPopup();
            };
            if (ImGui::Button("Save")) continueAction(true);
            ImGui::SameLine(); if (ImGui::Button("Discard")) continueAction(false);
            ImGui::SameLine(); if (ImGui::Button("Cancel")) { m_bShowUnsaved=false; m_ConfirmAction=ProjectAction::None; ImGui::CloseCurrentPopup(); }
            ImGui::EndPopup();
        }
    }
}
