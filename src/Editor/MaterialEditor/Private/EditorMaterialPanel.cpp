#include "EditorMaterialPanel.h"
#include "AssetManager.h"
#include "StaticMeshComponent.h"
#include "MaterialAsset.h"
#include "TextureAsset.h"
#include "Material.h"
#include "Camera.h"
#include "EngineGlobal.h"
#include "LightComponent.h"
#include "PathInfo.h"
#include "RHIApi.h"
#include "SceneInterface.h"
#include "SceneView.h"
#include "Shape.h"
#include "StaticMeshAsset.h"
#include "Viewport.h"
#include "imgui.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <utility>

namespace Editor
{
    using Controls = ImGUISlate::ImPropertyControls;

    namespace
    {
        constexpr const char* ShadingModels[] = {"All models", "Unlit", "Default Lit", "Subsurface", "Clear Coat", "Anisotropic", "Cloth"};
        static_assert(IM_ARRAYSIZE(ShadingModels) == 1 + int(Engine::EShadingModel::COUNT_MAX));
    }

    class MaterialPreview final : public Engine::RenderTarget
    {
    public:
        ~MaterialPreview()
        {
            WaitForDraw();
            m_Scene->RemovePrimitive(&m_Sphere);
            m_Scene->RemoveLight(&m_Light);
            m_Scene->FlushPendingUpdates();
        }

        explicit MaterialPreview(RenderCore::RenderInterface& renderer)
            : m_Renderer(renderer), m_Scene(renderer.AllocateScene())
        {
            const auto sphere = Engine::StaticMeshAsset::CreateShape(Engine::EBasicMeshShape::Sphere);
            m_Sphere.SetStaticMesh(sphere->GetMesh());
            m_Scene->AddPrimitive(&m_Sphere);
            m_Light.SetDirection(Core::Normalize(Core::Float3{-0.5f, -0.65f, -1.0f}));
            m_Light.SetIntensity(3.0f);
            m_Light.SetCastShadow(false);
            m_Scene->AddLight(&m_Light);

            RHI::RHITextureDesc desc{};
            desc.Width = Resolution;
            desc.Height = Resolution;
            desc.Format = RHI::ERHIFormat::R32G32B32A32_Float;
            desc.Usage = RHI::ERHITextureCreateFlag::RenderTarget | RHI::ERHITextureCreateFlag::ShaderResource |
                RHI::ERHITextureCreateFlag::UAV | RHI::ERHITextureCreateFlag::TransferSrc;
            m_Color = std::make_unique<RenderCore::RenderTexture>(desc);
            m_Color->InitRHIResource();
        }

        RenderCore::RenderTexture* GetRenderTarget() override { return m_Color.get(); }

        void Draw(const Engine::MaterialInterface& material, float extent)
        {
            const auto min = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("##Material preview", {extent, extent},
                ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
            const auto& input = ImGui::GetIO();
            bool bRedraw = UpdateMaterial(material);
            // The last motion and button release can arrive in the same UI frame.
            const bool bOrbiting = !ImGui::IsItemActivated() && (ImGui::IsItemActive() || ImGui::IsItemDeactivated());
            if (bOrbiting && (input.MouseDelta.x != 0.0f || input.MouseDelta.y != 0.0f))
            {
                m_Yaw = std::remainder(m_Yaw - input.MouseDelta.x * 0.01f, Core::DegToRad(360.0f));
                m_Pitch = std::clamp(m_Pitch + input.MouseDelta.y * 0.01f, -1.5f, 1.5f);
                bRedraw = true;
            }
            if (ImGui::IsItemHovered() && input.MouseWheel != 0.0f)
            {
                const float distance = std::clamp(m_Distance * std::exp(-input.MouseWheel * 0.12f), 1.25f, 10.0f);
                bRedraw |= distance != m_Distance;
                m_Distance = distance;
            }
            if (bRedraw)
            {
                Render();
            }
            const auto textureId = static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(GetRenderTarget()));
            ImGui::GetWindowDrawList()->AddImage(textureId, min, {min.x + extent, min.y + extent});
        }

    private:
        void WaitForDraw() const
        {
            const auto fence = m_Color->GetTracker().GetLastAccessFence();
            if (fence.Value)
            {
                RHI::GRHIApi->GetQueue(fence.QueueType)->WaitValue(fence.Value);
            }
        }

        bool UpdateMaterial(const Engine::MaterialInterface& material)
        {
            auto proxy = material.GetRenderProxyShared();
            if (proxy == m_MaterialProxy)
            {
                return false;
            }

            // Retire draws using the previous material before releasing its textures or rebuilding the scene proxy.
            WaitForDraw();
            const auto* sourceInstance = dynamic_cast<const Engine::MaterialInstance*>(&material);
            if (m_Sphere.GetMaterial(0)->GetMaterial() != material.GetMaterial())
            {
                m_Sphere.SetMaterial(0, material.GetAsset()->GetMaterial());
            }
            if (sourceInstance)
            {
                auto* previewInstance = m_Sphere.GetOrCreateMaterialInstance(0);
                for (const auto& parameter : material.GetParameterDefinitions())
                {
                    if (sourceInstance->HasOverride(parameter.Name, parameter.Type))
                    {
                        previewInstance->SetParameter(parameter.Name, proxy->GetParameter(parameter.Name, parameter.Type)->GetValueVariant());
                    }
                    else
                    {
                        previewInstance->ResetParameter(parameter.Name, parameter.Type);
                    }
                }
            }
            else if (m_Sphere.GetMaterialInstance(0))
            {
                m_Sphere.ResetMaterialInstance(0);
            }
            m_Scene->FlushPendingUpdates();
            m_MaterialProxy = std::move(proxy);
            return true;
        }

        void Render()
        {
            const Core::Float3 orbit{std::cos(m_Pitch) * std::sin(m_Yaw), std::sin(m_Pitch), std::cos(m_Pitch) * std::cos(m_Yaw)};
            Engine::Camera camera;
            camera.SetPosition(orbit * m_Distance);
            camera.SetTarget({0, 0, 0});
            camera.SetUp({0, 1, 0});
            camera.SetPerspective(Core::DegToRad(45.0f), 1.0f, 0.05f, 20.0f);

            Engine::SceneView view;
            view.Viewport = {0, 0, Resolution, Resolution};
            view.RenderTargetWidth = Resolution;
            view.RenderTargetHeight = Resolution;
            view.CameraWorldPos = camera.GetPosition();
            view.CameraWorldDir = orbit * -1.0f;
            view.NearClip = camera.GetNearPlane();
            view.FarClip = camera.GetFarPlane();
            view.ViewMatrix = camera.GetViewMatrix();
            view.ProjectionMatrix = camera.GetProjectionMatrix();
            view.ViewProjectionMatrix = camera.GetViewProjectionMatrix();
            view.InvViewProjectionMatrix = camera.GetInverseViewProjectionMatrix();

            Engine::SceneViewFamily family;
            family.Scene = m_Scene.get();
            family.RenderTarget = this;
            family.AddView(std::move(view));
            family.BuildAllViews();
            m_Renderer.BeginRender(&family);
        }

        static constexpr int Resolution = 512;
        RenderCore::RenderInterface& m_Renderer;
        Engine::StaticMeshComponent m_Sphere;
        Engine::DirectionalLightComponent m_Light;
        std::unique_ptr<Engine::SceneInterface> m_Scene;
        std::unique_ptr<RenderCore::RenderTexture> m_Color;
        std::shared_ptr<const Engine::MaterialRenderProxy> m_MaterialProxy;
        float m_Yaw = 0.35f;
        float m_Pitch = 0.2f;
        float m_Distance = 3.2f;
    };

    EditorMaterialPanel::EditorMaterialPanel(MaterialEditorCallbacks callbacks)
        : EditorMaterialPanel(std::move(callbacks), std::filesystem::path(Core::GetProjectDir()) / "resources/material") {}

    EditorMaterialPanel::EditorMaterialPanel(MaterialEditorCallbacks callbacks, std::filesystem::path libraryRoot)
        : ImPanelWidget("Material"), m_Callbacks(std::move(callbacks)),
          m_Library(std::move(libraryRoot)),
          m_SavePath(m_Library.GetRoot().empty() ? "" : (m_Library.GetRoot() / "EditorMaterial.json").string())
    {
        m_Grid.SetOnSelectionChanged([this](const std::string& id, bool activated) {
            try
            {
                OpenMaterial(std::filesystem::u8path(id));
                if (activated)
                {
                    m_bOpenProperties = true;
                    if (m_bExpanded) { m_bExpanded = false; ImGui::CloseCurrentPopup(); }
                }
            }
            catch (const std::exception& error) { m_Status = error.what(); }
        });
        m_Grid.SetOnItemActions([this](const std::string& id) {
            if (ImGui::BeginPopupContextItem("Material actions"))
            {
                if (ImGui::MenuItem("Edit material"))
                    try { OpenMaterial(std::filesystem::u8path(id)); m_bOpenProperties = true; m_bExpanded = false; }
                    catch (const std::exception& error) { m_Status = error.what(); }
                if (ImGui::MenuItem("Duplicate")) DuplicateMaterial(std::filesystem::u8path(id));
                if (ImGui::MenuItem("Apply to selected mesh"))
                    try { OpenMaterial(std::filesystem::u8path(id)); ApplySelectedMaterial(); }
                    catch (const std::exception& error) { m_Status = error.what(); }
                ImGui::EndPopup();
            }
        });
    }

    EditorMaterialPanel::~EditorMaterialPanel() = default;

    void EditorMaterialPanel::Draw()
    {
        if (!m_bPopupOpen)
        {
            m_Preview.reset();
            return;
        }
        ImGui::SetNextWindowSize({900,650},ImGuiCond_FirstUseEver);
        if (m_bOpenProperties && !m_bExpanded)
        {
            ImGui::SetNextWindowFocus();
        }
        if (ImGui::Begin("Material Editor",&m_bPopupOpen))
        {
            const auto position=ImGui::GetWindowPos(), size=ImGui::GetWindowSize();
            m_PopupGeometry={position.x,position.y,size.x,size.y}; DrawContent();
        }
        ImGui::End();
    }
    void EditorMaterialPanel::ShowMaterial(std::shared_ptr<Engine::MaterialAsset> material)
    {
        if (!material) return;
        m_SelectedMaterial=std::move(material); m_bPopupOpen=true; m_bOpenProperties=true; m_bEditAsset=true;
        m_Grid.SetSelectedId(MaterialLibrary::PathText(std::filesystem::path(m_SelectedMaterial->GetPath())));
    }
    void EditorMaterialPanel::SetContentRoot(const std::filesystem::path& root)
    {
        m_Library.SetRoot(root); m_bPopupOpen=false; m_bExpanded=false; m_bExpandedGeometryValid=false;
        m_SelectedMaterial.reset(); m_SelectedTexture.reset(); m_LastExternalMaterial.reset(); m_LastExternalTexture.reset();
        m_PendingImports.clear(); m_ImportErrors.clear(); m_Category.clear(); m_Search.clear(); m_Status.clear();
        m_Grid.SetItems({}); m_Grid.SetSelectedId({}); m_Slot=0;
        m_SavePath=root.empty() ? "" : (root/"EditorMaterial.json").string();
    }
    void EditorMaterialPanel::DrawContent()
    {
        ProcessImport();
        if (m_Callbacks.GetSelectedMaterial)
            if (auto external = m_Callbacks.GetSelectedMaterial(); external && external != m_LastExternalMaterial.lock())
            {
                m_LastExternalMaterial = external;
                m_SelectedMaterial = std::move(external);
                m_bEditAsset = true;
            }
        if (m_Callbacks.GetSelectedTexture)
            if (auto texture = m_Callbacks.GetSelectedTexture(); texture && texture != m_LastExternalTexture.lock())
            { m_LastExternalTexture = texture; m_SelectedTexture = std::move(texture); }
        if (ImGui::Button("Open large library")) { m_bExpanded = true; ImGui::OpenPopup("Material Library"); }
        ImGui::SameLine();
        if (ImGui::Button("Inspect mesh")) { m_bEditAsset = false; m_bOpenProperties = true; }
        if (!m_Status.empty()) ImGui::TextWrapped("%s", m_Status.c_str());
        if (!m_ImportErrors.empty() && ImGui::CollapsingHeader("Import errors"))
            for (const auto& error : m_ImportErrors) ImGui::TextWrapped("%s", error.c_str());
        if (!m_PendingImports.empty())
        {
            ImGui::Text("Pending imports: %zu", m_PendingImports.size());
            ImGui::SameLine();
            if (ImGui::SmallButton("Cancel remaining")) m_PendingImports.clear();
        }
        if (ImGui::BeginTabBar("Material views"))
        {
            if (ImGui::BeginTabItem("Library", nullptr, m_bOpenLibrary ? ImGuiTabItemFlags_SetSelected : 0))
            { m_bOpenLibrary = false; DrawLibrary(); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Properties", nullptr, m_bOpenProperties ? ImGuiTabItemFlags_SetSelected : 0))
            {
                m_bOpenProperties = false;
                const float propertiesWidth = ImGui::GetContentRegionAvail().x * 0.53f;
                Engine::MaterialInterface* material = nullptr;
                if (ImGui::BeginChild("Material properties", {propertiesWidth, 0}, ImGuiChildFlags_Borders))
                {
                    material = DrawProperties();
                }
                ImGui::EndChild();
                ImGui::SameLine();
                if (ImGui::BeginChild("Material preview pane", {0, 0}, ImGuiChildFlags_Borders,
                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
                {
                    ImGui::SeparatorText("Preview");
                    if (material)
                    {
                        ImGui::TextWrapped("Drag to orbit. Scroll to zoom.");
                        const auto available = ImGui::GetContentRegionAvail();
                        const float extent = std::min(available.x, available.y);
                        if (extent > 0)
                        {
                            if (!m_Preview)
                            {
                                m_Preview = std::make_unique<MaterialPreview>(*Engine::GetRenderModuleInstance());
                            }
                            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (available.x - extent) * 0.5f);
                            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (available.y - extent) * 0.5f);
                            m_Preview->Draw(*material, extent);
                        }
                    }
                    else
                    {
                        ImGui::TextDisabled("Select a material to preview.");
                    }
                }
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        m_bExpandedGeometryValid = false;
        ImGui::SetNextWindowSize({1000, 650}, ImGuiCond_FirstUseEver);
        if (ImGui::BeginPopupModal("Material Library", &m_bExpanded))
        {
            const auto position = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
            m_ExpandedGeometry = {position.x, position.y, size.x, size.y};
            m_bExpandedGeometryValid = true;
            if (m_bOpenProperties) { m_bExpanded = false; ImGui::CloseCurrentPopup(); }
            else DrawLibrary();
            ImGui::EndPopup();
        }
    }

    bool EditorMaterialPanel::HitTest(float x, float y) const
    {
        if (GetVisibility() != SlateCore::EVisibility::Visible) return false;
        const auto& expanded = m_ExpandedGeometry;
        const auto& popup=m_PopupGeometry;
        return (m_bPopupOpen && x>=popup.X && y>=popup.Y && x<popup.X+popup.Width && y<popup.Y+popup.Height) || (m_bExpanded && m_bExpandedGeometryValid &&
            x >= expanded.X && y >= expanded.Y && x < expanded.X + expanded.Width && y < expanded.Y + expanded.Height);
    }
    bool EditorMaterialPanel::OnFilesDropped(const SlateCore::FileDropEvent& event)
    {
        if (event.Paths.empty()) return false;
        if (m_PendingImports.empty()) { m_ImportedCount = 0; m_ImportTotal = 0; m_ImportErrors.clear(); }
        for (const auto& path : event.Paths) m_PendingImports.push_back({path, m_Category});
        m_ImportTotal += static_cast<uint32_t>(event.Paths.size());
        m_bOpenLibrary = true;
        m_Status = "Files queued for import into the current material category.";
        return true;
    }
    void EditorMaterialPanel::ProcessImport()
    {
        if (m_PendingImports.empty()) return;
        const auto pending = std::move(m_PendingImports.front());
        m_PendingImports.erase(m_PendingImports.begin());
        try
        {
            const auto path = m_Library.Import(pending.Path, pending.Category);
            OpenMaterial(path);
            if (MaterialLibrary::IsImage(pending.Path))
                m_SelectedTexture = std::get<std::shared_ptr<Engine::TextureAsset>>(
                    m_SelectedMaterial->GetMaterial()->GetParameter("BaseColor", Engine::EMaterialParameterType::Texture)->GetValueVariant());
            ++m_ImportedCount;
            std::cout << "[MaterialEditor] Imported " << MaterialLibrary::PathText(path) << std::endl;
        }
        catch (const std::exception& error)
        {
            m_ImportErrors.push_back(MaterialLibrary::PathText(pending.Path) + ": " + error.what());
            std::cerr << "[MaterialEditor] " << m_ImportErrors.back() << std::endl;
        }
        m_Status = "Imported " + std::to_string(m_ImportedCount) + "/" + std::to_string(m_ImportTotal) +
            ", errors: " + std::to_string(m_ImportErrors.size()) + ". Sources were not moved or overwritten.";
    }
    void EditorMaterialPanel::OpenMaterial(const std::filesystem::path& path)
    {
        // Runtime loading still uses the engine's resident/weak asset tables.
        auto asset = Engine::AssetManager::Get().LoadSync<Engine::MaterialAsset>(path.string());
        ShowMaterial(std::move(asset));
        m_bEditAsset = true;
        m_Grid.SetSelectedId(MaterialLibrary::PathText(path));
        m_Status = "Selected " + m_SelectedMaterial->GetName();
    }
    bool EditorMaterialPanel::ApplySelectedMaterial()
    {
        auto* component = m_Callbacks.GetSelectedComponent ? m_Callbacks.GetSelectedComponent() : nullptr;
        if (!component || !m_SelectedMaterial) { m_Status = "Select a mesh and a library material first."; return false; }
        try
        {
            m_Slot = std::clamp(m_Slot, 0, std::max(0, static_cast<int>(component->GetNumMaterials()) - 1));
            component->SetMaterial(m_Slot, m_SelectedMaterial->GetMaterial());
            NotifyChanged(false);
            m_bEditAsset = false;
            m_Status = "Assigned material to mesh slot " + std::to_string(m_Slot);
            return true;
        }
        catch (const std::exception& error) { m_Status = error.what(); return false; }
    }
    void EditorMaterialPanel::DuplicateMaterial(const std::filesystem::path& path)
    {
        try
        {
            auto name = MaterialLibrary::PathText(path.stem());
            for (const auto& entry : m_Library.GetEntries()) if (entry.Path == path) { name = entry.Name; break; }
            OpenMaterial(m_Library.Import(path, m_Category, name + " Copy"));
        }
        catch (const std::exception& error) { m_Status = error.what(); }
    }
    void EditorMaterialPanel::DrawLibrary()
    {
        ImGui::SetNextItemWidth(std::max(50.0f, ImGui::GetContentRegionAvail().x - 140));
        Controls::Text("Search", m_Search);
        ImGui::SameLine();
        if (ImGui::Button("Refresh"))
            try { m_Library.Refresh(); } catch (const std::exception& error) { m_Status = error.what(); }
        ImGui::SetNextItemWidth(110);
        Controls::Choice("##ShadingFilter", m_ShadingFilter, ShadingModels, IM_ARRAYSIZE(ShadingModels));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(105);
        ImGui::SliderFloat("Tile size", &m_TileWidth, 72, 160, "%.0f");
        if (ImGui::Button("New material")) ImGui::OpenPopup("Create material");
        ImGui::SameLine();
        ImGui::BeginDisabled(!m_SelectedMaterial);
        if (ImGui::Button("Duplicate")) DuplicateMaterial(std::filesystem::path(m_SelectedMaterial->GetPath()));
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Copy the saved material and its texture files; unsaved edits are not included.");
        ImGui::SameLine();
        if (ImGui::Button("Apply to mesh")) ApplySelectedMaterial();
        ImGui::EndDisabled();
        ImGui::TextDisabled("Drop material JSON / PNG / JPG / BMP / TGA files here");
        if (!m_Library.GetWarnings().empty())
        {
            ImGui::TextDisabled("%zu invalid material file(s)", m_Library.GetWarnings().size());
            if (ImGui::IsItemHovered())
            { ImGui::BeginTooltip(); for (const auto& warning : m_Library.GetWarnings()) ImGui::TextWrapped("%s", warning.c_str()); ImGui::EndTooltip(); }
        }
        if (ImGui::BeginPopupModal("Create material", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            Controls::Text("Name", m_NewName);
            if (ImGui::Button("Create"))
                try { OpenMaterial(m_Library.Create(m_NewName, m_Category)); ImGui::CloseCurrentPopup(); }
                catch (const std::exception& error) { m_Status = error.what(); }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        const float categoryWidth = std::clamp(ImGui::GetContentRegionAvail().x * 0.28f, 80.0f, 180.0f);
        if (ImGui::BeginChild("MaterialCategories", {categoryWidth, 0}, ImGuiChildFlags_Borders))
        {
            ImGui::TextUnformatted("Categories");
            if (ImGui::Selectable("All materials", m_Category.empty())) m_Category.clear();
            for (const auto& category : m_Library.GetCategories())
                if (ImGui::Selectable(category.c_str(), m_Category == category)) m_Category = category;
        }
        ImGui::EndChild();
        ImGui::SameLine();
        if (ImGui::BeginChild("MaterialCards", {0, 0}, ImGuiChildFlags_Borders))
        {
            auto lower = [](std::string text) {
                std::transform(text.begin(), text.end(), text.begin(), [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
                return text;
            };
            const auto search = lower(m_Search);
            std::vector<ImGUISlate::ImGridItem> cards;
            for (const auto& entry : m_Library.GetEntries())
            {
                if (!m_Category.empty() && entry.Category != m_Category && !entry.Category.starts_with(m_Category + "/")) continue;
                auto color = entry.BaseColor;
                auto model = entry.ShadingModel;
                if (m_SelectedMaterial && std::filesystem::path(m_SelectedMaterial->GetPath()) == entry.Path)
                {
                    color = std::get<std::array<float, 4>>(m_SelectedMaterial->GetMaterial()->GetParameter("BaseColor", Engine::EMaterialParameterType::Vector)->GetValueVariant());
                    model = Engine::GetShadingModelName(m_SelectedMaterial->GetMaterial()->GetShadingModel());
                }
                if (m_ShadingFilter && model != Engine::GetShadingModelName(static_cast<Engine::EShadingModel>(m_ShadingFilter - 1))) continue;
                if (model == "Lit") model = ShadingModels[1 + int(Engine::EShadingModel::Lit)];
                if (!search.empty() && lower(entry.Name + " " + entry.Category).find(search) == std::string::npos) continue;
                cards.push_back({MaterialLibrary::PathText(entry.Path), entry.Name, model, color});
            }
            ImGui::Text("%zu material(s)", cards.size());
            if (cards.empty()) ImGui::TextWrapped("No matching materials. Create one or drop a project material/image file into this panel.");
            m_Grid.SetItems(std::move(cards));
            m_Grid.SetTileWidth(m_TileWidth);
            if (m_SelectedMaterial) m_Grid.SetSelectedId(MaterialLibrary::PathText(std::filesystem::path(m_SelectedMaterial->GetPath())));
            m_Grid.Draw();
        }
        ImGui::EndChild();
    }

    Engine::MaterialInterface* EditorMaterialPanel::DrawProperties()
    {
        auto* component = m_Callbacks.GetSelectedComponent ? m_Callbacks.GetSelectedComponent() : nullptr;
        Controls::Boolean("Edit shared material asset", m_bEditAsset);
        ImGui::TextWrapped(m_bEditAsset ? "Changes affect all users of this base material." : "Only a real edit creates a mesh-owned material instance.");
        if (component && component->GetNumMaterials())
        {
            m_Slot = std::clamp(m_Slot, 0, static_cast<int>(component->GetNumMaterials()) - 1);
            ImGui::SliderInt("Material slot", &m_Slot, 0, static_cast<int>(component->GetNumMaterials()) - 1);
        }
        else m_Slot = 0;
        auto* material = component ? component->GetMaterial(static_cast<uint32_t>(m_Slot)) : nullptr;
        auto asset = m_bEditAsset ? m_SelectedMaterial : nullptr;
        if (!asset && material) asset = material->GetAsset();
        if (m_bEditAsset) material = asset ? asset->GetMaterial() : nullptr;
        if (!material) { ImGui::TextDisabled("Select a mesh or load a material asset."); return nullptr; }

        ImGui::TextWrapped("Asset: %s", asset ? asset->GetName().c_str() : "(none)");
        if (!m_bEditAsset && component->GetMaterialInstance(m_Slot))
        {
            if (ImGui::Button("Remove all instance overrides"))
            {
                component->ResetMaterialInstance(m_Slot);
                NotifyChanged(false);
                material = component->GetMaterial(m_Slot);
            }
        }
        if (m_bEditAsset)
        {
            auto* base = asset->GetMaterial();
            bool twoSided = base->IsTwoSided();
            if (Controls::Boolean("Two sided", twoSided)) { base->SetTwoSided(twoSided); NotifyChanged(true); }
            const char* cullModes[] = {"None", "Front", "Back"};
            int cull = base->GetCullMode() == RHI::ERHICullMode::None ? 0 : base->GetCullMode() == RHI::ERHICullMode::Front ? 1 : 2;
            if (Controls::Choice("Cull mode", cull, cullModes, 3))
            {
                base->SetCullMode(cull == 0 ? RHI::ERHICullMode::None : cull == 1 ? RHI::ERHICullMode::Front : RHI::ERHICullMode::Back);
                NotifyChanged(true);
            }
            if (ImGui::BeginCombo("Blend mode", Engine::GetBlendModeName(base->GetBlendMode())))
            {
                for (const auto blend : {Engine::EBlendMode::Opaque, Engine::EBlendMode::Masked, Engine::EBlendMode::Translucent, Engine::EBlendMode::Additive})
                {
                    const bool selected = base->GetBlendMode() == blend;
                    if (ImGui::Selectable(Engine::GetBlendModeName(blend), selected))
                    {
                        base->SetBlendMode(blend);
                        NotifyChanged(true);
                    }
                    if (selected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            int model = static_cast<int>(base->GetShadingModel());
            if (Controls::Choice("Shading model", model, ShadingModels + 1, IM_ARRAYSIZE(ShadingModels) - 1))
            { base->SetShadingModel(static_cast<Engine::EShadingModel>(model)); NotifyChanged(true); }
        }

        const auto proxy = material->GetRenderProxyShared();
        const auto& definitions = material->GetParameterDefinitions();
        const auto model = proxy->GetShadingModel();
        if (!m_bEditAsset)
        {
            ImGui::Text("Shading model: %s", ShadingModels[1 + int(model)]);
            ImGui::Text("Blend mode: %s", Engine::GetBlendModeName(proxy->GetBlendMode()));
        }
        std::vector<std::string> groups;
        for (const auto& definition : definitions)
            if ((!definition.ShadingModel || *definition.ShadingModel == model) &&
                std::find(groups.begin(), groups.end(), definition.Group) == groups.end())
            {
                groups.push_back(definition.Group);
            }
        for (const auto& group : groups)
        {
            if (group == "Transparency" && proxy->GetBlendMode() != Engine::EBlendMode::Translucent)
            {
                continue;
            }
            if (!ImGui::CollapsingHeader(group.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) continue;
            for (const auto& definition : definitions)
            {
                if (definition.Group != group || (definition.ShadingModel && *definition.ShadingModel != model)) continue;
                if (model == Engine::EShadingModel::Cloth && definition.Name == "Metallic")
                {
                    continue;
                }
                ImGui::PushID(definition.Name.c_str());
                ImGui::PushID(static_cast<int>(definition.Type));
                auto value = proxy->GetParameter(definition.Name, definition.Type)->GetValueVariant();
                bool changed = false;
                if (auto* scalar = std::get_if<float>(&value))
                {
                    const float minimum = definition.Name == "Anisotropy" ? -1.0f : definition.Name == "IndexOfRefraction" ? 1.0f : 0.0f;
                    const float maximum = definition.Name == "NormalIntensity" ? 4.0f :
                        definition.Name == "IndexOfRefraction" ? 3.0f : definition.Name == "Thickness" ? 10.0f : 1.0f;
                    changed = Controls::Scalar(definition.Name.c_str(), *scalar, 0.01f, minimum, maximum);
                }
                else if (auto* vector = std::get_if<std::array<float, 4>>(&value))
                    changed = group == "UV" || definition.Name == "Absorption" ?
                        Controls::Vector4(definition.Name.c_str(), *vector) : Controls::Color(definition.Name.c_str(), *vector);
                else if (auto* texture = std::get_if<std::shared_ptr<Engine::TextureAsset>>(&value))
                {
                    ImGui::TextWrapped("%s: %s", definition.Name.c_str(), *texture ? (*texture)->GetName().c_str() : "Default");
                        const auto selected = m_SelectedTexture;
                    ImGui::BeginDisabled(!selected);
                    if (ImGui::Button("Use selected")) { value = selected; changed = true; }
                    ImGui::EndDisabled();
                    ImGui::SameLine();
                    if (ImGui::Button("Clear")) { value = std::shared_ptr<Engine::TextureAsset>{}; changed = true; }
                }
                if (changed)
                {
                    try
                    {
                        if (m_bEditAsset) { material->SetParameter(definition.Name, value); NotifyChanged(true); }
                        else { component->SetMaterialParameter(m_Slot, definition.Name, value); NotifyChanged(false); }
                        std::cout << "[Editor] Parameter " << definition.Name << " updated on " << (m_bEditAsset ? "asset" : "mesh instance") << std::endl;
                        m_Status.clear();
                    }
                    catch (const std::exception& error) { m_Status = error.what(); }
                }
                auto* instance = component ? component->GetMaterialInstance(m_Slot) : nullptr;
                const bool overridden = !m_bEditAsset && instance && instance->HasOverride(definition.Name, definition.Type);
                if (overridden) ImGui::TextDisabled("Instance override");
                ImGui::BeginDisabled(!m_bEditAsset && !overridden);
                if (ImGui::SmallButton(m_bEditAsset ? "Restore default" : "Inherit parent"))
                {
                    if (m_bEditAsset) { asset->GetMaterial()->ResetParameter(definition.Name, definition.Type); NotifyChanged(true); }
                    else { component->ResetMaterialParameter(m_Slot, definition.Name, definition.Type); NotifyChanged(false); }
                }
                ImGui::EndDisabled();
                ImGui::PopID();
                ImGui::PopID();
            }
        }
        ImGui::SeparatorText("Save material asset copy");
        Controls::Text("Save path", m_SavePath);
        ImGui::BeginDisabled(!m_bEditAsset || !asset);
        if (ImGui::Button("Save copy"))
        {
            try
            {
                if (std::filesystem::exists(m_SavePath)) m_Status = "File already exists; choose a new path.";
                else if (asset->SaveToFile(m_SavePath)) { m_Status = "Material copy saved."; m_Library.Refresh(); }
                else m_Status = "Save failed; check the path and log.";
            }
            catch (const std::exception& error) { m_Status = error.what(); }
        }
        ImGui::EndDisabled();
        if (!m_Status.empty()) ImGui::TextWrapped("%s", m_Status.c_str());
        const auto resource = material->GetMaterial()->GetResource();
        if (!resource->GetLastError().empty()) ImGui::TextWrapped("Shader error: %s", resource->GetLastError().c_str());
        ImGui::TextWrapped("Surface rendering currently supports Opaque Lit/Unlit. Mesh instances are not assets and are not saved as MaterialAsset files.");
        return m_bEditAsset ? material : component->GetMaterial(m_Slot);
    }
    void EditorMaterialPanel::NotifyChanged(bool bSharedAsset)
    {
        if (m_Callbacks.OnMaterialChanged) m_Callbacks.OnMaterialChanged(bSharedAsset);
    }
}
