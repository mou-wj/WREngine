#include "EditorGIViewer.h"
#include "GIViewerPreview.h"
#include "imgui.h"
#include <algorithm>
#include <cstdio>

namespace Editor
{
    namespace
    {
        struct ViewerModeOption
        {
            const char* Name;
            Renderer::ELumenViewerMode Mode;
        };

        constexpr ViewerModeOption Modes[] = {
            {"Scene / Mesh Cards", Renderer::ELumenViewerMode::Scene},
            {"Surface Cache / Base Color", Renderer::ELumenViewerMode::SurfaceBaseColor},
            {"Surface Cache / World Normal", Renderer::ELumenViewerMode::SurfaceWorldNormal},
            {"Surface Cache / Card Coverage", Renderer::ELumenViewerMode::SurfaceCoverage},
            {"Surface Cache / Roughness", Renderer::ELumenViewerMode::SurfaceRoughness},
            {"Surface Cache / Metallic", Renderer::ELumenViewerMode::SurfaceMetallic},
            {"Surface Cache / Selected Mesh Card", Renderer::ELumenViewerMode::SurfaceCardAtlas},
            {"Direct Lighting Atlas", Renderer::ELumenViewerMode::DirectLightingAtlas},
            {"Radiosity / Indirect Atlas", Renderer::ELumenViewerMode::RadiosityIndirectAtlas},
            {"Radiosity / Iteration Delta", Renderer::ELumenViewerMode::RadiosityIterationDelta},
            {"Screen Probes / Placement", Renderer::ELumenViewerMode::ProbePlacement},
            {"Screen Probes / Ray Directions", Renderer::ELumenViewerMode::ProbeRayDirections},
            {"Screen Probes / Hit Ratio", Renderer::ELumenViewerMode::ProbeHitRatio},
            {"Screen Probes / Radiance", Renderer::ELumenViewerMode::ProbeRadiance},
            {"Screen Probes / Indirect Diffuse", Renderer::ELumenViewerMode::IndirectDiffuse}
        };

        const char* GetModeName(Renderer::ELumenViewerMode mode)
        {
            for (const auto& option : Modes)
            {
                if (option.Mode == mode)
                {
                    return option.Name;
                }
            }
            return "Scene / Mesh Cards";
        }

        const char* GetModeDescription(Renderer::ELumenViewerMode mode)
        {
            switch (mode)
            {
            case Renderer::ELumenViewerMode::Scene:
                return "Scene preview. Mesh Card rectangles are projected from their world-space capture planes.";
            case Renderer::ELumenViewerMode::SurfaceBaseColor:
            case Renderer::ELumenViewerMode::SurfaceWorldNormal:
            case Renderer::ELumenViewerMode::SurfaceCoverage:
            case Renderer::ELumenViewerMode::SurfaceRoughness:
            case Renderer::ELumenViewerMode::SurfaceMetallic:
                return "Surface Cache atlas. Each colored tile corresponds to one directional Mesh Card.";
            case Renderer::ELumenViewerMode::SurfaceCardAtlas:
                return "Selected Mesh Card atlas tile, enlarged to show its captured surface attributes.";
            case Renderer::ELumenViewerMode::DirectLightingAtlas:
                return "Direct-lighting cache atlas, evaluated for the captured Mesh Cards.";
            case Renderer::ELumenViewerMode::RadiosityIndirectAtlas:
                return "Accumulated diffuse outgoing radiance across the Surface Cards, excluding direct lighting.";
            case Renderer::ELumenViewerMode::RadiosityIterationDelta:
                return "Absolute change since the previous bounce, amplified 32x. Darkening indicates convergence.";
            case Renderer::ELumenViewerMode::ProbePlacement:
                return "Screen-space probe placement, surface normals, and tile boundaries.";
            case Renderer::ELumenViewerMode::ProbeRayDirections:
                return "Cosine-weighted hemisphere ray directions for each screen-space probe.";
            case Renderer::ELumenViewerMode::ProbeHitRatio:
                return "Fraction of probe rays with a valid Surface Cache lighting result.";
            case Renderer::ELumenViewerMode::ProbeRadiance:
                return "Average incoming radiance gathered by each screen-space probe.";
            case Renderer::ELumenViewerMode::IndirectDiffuse:
                return "Diffuse indirect lighting reconstructed from the probe grid. Toggle radiosity to compare with one bounce.";
            }
            return "";
        }

        const char* GetCardAxisName(uint32_t axisIndex)
        {
            constexpr const char* axisNames[] = {"+X", "-X", "+Y", "-Y", "+Z", "-Z"};
            return axisIndex < std::size(axisNames) ? axisNames[axisIndex] : "Unknown";
        }

        bool DrawCardViewSelector(Renderer::ELumenViewerCardView& view)
        {
            constexpr const char* viewNames[] = {"Base Color", "World Normal", "Coverage", "Roughness", "Metallic"};
            int current = static_cast<int>(view);
            if (ImGui::Combo("Card channel", &current, viewNames, static_cast<int>(std::size(viewNames))))
            {
                view = static_cast<Renderer::ELumenViewerCardView>(current);
                return true;
            }
            return false;
        }

        void DrawCardProjectionDiagram(const Renderer::LumenViewerCardInfo& card)
        {
            ImGui::TextUnformatted("Capture projection plane (not world scale)");
            const auto canvasPosition = ImGui::GetCursorScreenPos();
            const ImVec2 canvasSize{200, 94};
            ImGui::Dummy(canvasSize);

            auto* drawList = ImGui::GetWindowDrawList();
            const ImVec2 canvasEnd{canvasPosition.x + canvasSize.x, canvasPosition.y + canvasSize.y};
            drawList->AddRectFilled(canvasPosition, canvasEnd, IM_COL32(28, 31, 38, 255), 4.0f);

            const float aspect = std::clamp(card.Extent.X / std::max(card.Extent.Y, 0.001f), 0.55f, 2.4f);
            const float halfWidth = std::min(64.0f, 31.0f * aspect);
            const float halfHeight = std::min(31.0f, 31.0f / aspect);
            const ImVec2 center{canvasPosition.x + 88.0f, canvasPosition.y + canvasSize.y * 0.5f};
            drawList->AddRect({center.x - halfWidth, center.y - halfHeight},
                {center.x + halfWidth, center.y + halfHeight}, IM_COL32(95, 105, 120, 255));
            drawList->AddCircleFilled(center, 3.0f, IM_COL32(240, 240, 240, 255));
            drawList->AddLine(center, {center.x + halfWidth * 0.75f, center.y}, IM_COL32(230, 75, 75, 255), 2.0f);
            drawList->AddLine(center, {center.x, center.y - halfHeight * 0.75f}, IM_COL32(75, 190, 100, 255), 2.0f);
            drawList->AddText({center.x + halfWidth * 0.76f, center.y - 8.0f}, IM_COL32(255, 255, 255, 255), "U");
            drawList->AddText({center.x - 5.0f, center.y - halfHeight - 17.0f}, IM_COL32(255, 255, 255, 255), "V");
            drawList->AddText({canvasPosition.x + 158.0f, center.y - 17.0f}, IM_COL32(230, 190, 80, 255), "N");
            drawList->AddText({canvasPosition.x + 151.0f, center.y - 1.0f}, IM_COL32(170, 175, 185, 255), "view");
        }

        Core::Float3 CardCorner(const Renderer::LumenViewerCardInfo& card, float u, float v)
        {
            return {
                card.Center.X + card.AxisU.X * u * card.Extent.X + card.AxisV.X * v * card.Extent.Y,
                card.Center.Y + card.AxisU.Y * u * card.Extent.X + card.AxisV.Y * v * card.Extent.Y,
                card.Center.Z + card.AxisU.Z * u * card.Extent.X + card.AxisV.Z * v * card.Extent.Y
            };
        }

        bool ProjectCardPoint(const Engine::SceneView& view, const Core::Float3& point,
            const ImVec2& imagePosition, const ImVec2& imageSize, ImVec2& screenPosition)
        {
            const auto clip = view.ViewProjectionMatrix * Core::Float4(point.X, point.Y, point.Z, 1.0f).Data;
            if (clip[3] <= 0.0001f)
            {
                return false;
            }

            const float ndcX = clip[0] / clip[3];
            const float ndcY = clip[1] / clip[3];
            screenPosition = {
                imagePosition.x + (ndcX * 0.5f + 0.5f) * imageSize.x,
                imagePosition.y + (0.5f - ndcY * 0.5f) * imageSize.y
            };
            return true;
        }

        void DrawMeshCardOverlay(const Engine::SceneView& view,
            const std::vector<Renderer::LumenViewerCardInfo>& cards, uint32_t selectedCardIndex,
            bool selectedOnly, const ImVec2& imagePosition, const ImVec2& imageSize)
        {
            auto* drawList = ImGui::GetWindowDrawList();
            const ImVec2 imageEnd{imagePosition.x + imageSize.x, imagePosition.y + imageSize.y};
            drawList->PushClipRect(imagePosition, imageEnd, true);

            constexpr ImU32 axisColors[] = {
                IM_COL32(245, 75, 75, 220), IM_COL32(255, 150, 150, 180),
                IM_COL32(75, 150, 255, 220), IM_COL32(150, 195, 255, 180),
                IM_COL32(80, 220, 115, 220), IM_COL32(160, 240, 175, 180)
            };
            for (const auto& card : cards)
            {
                if (selectedOnly && card.Index != selectedCardIndex)
                {
                    continue;
                }

                const Core::Float3 worldCorners[] = {
                    CardCorner(card, -1, -1), CardCorner(card, 1, -1),
                    CardCorner(card, 1, 1), CardCorner(card, -1, 1)
                };
                ImVec2 corners[4];
                bool projected = true;
                for (size_t index = 0; index < 4; ++index)
                {
                    projected = ProjectCardPoint(view, worldCorners[index], imagePosition, imageSize, corners[index]) && projected;
                }
                if (!projected)
                {
                    continue;
                }

                const bool selected = card.Index == selectedCardIndex;
                const ImU32 color = selected ? IM_COL32(255, 220, 65, 255) : axisColors[std::min(card.AxisIndex, 5u)];
                if (selected)
                {
                    drawList->AddConvexPolyFilled(corners, 4, IM_COL32(255, 220, 65, 28));
                }
                drawList->AddPolyline(corners, 4, color, ImDrawFlags_Closed, selected ? 2.5f : 1.5f);
                if (selected)
                {
                    char label[32];
                    std::snprintf(label, sizeof(label), "Card #%u", card.Index);
                    drawList->AddText(corners[0], color, label);
                }
            }

            drawList->PopClipRect();
        }
    }

    EditorGIViewer::EditorGIViewer(std::function<GIViewerSource()> getSource)
        : m_GetSource(std::move(getSource)), m_Preview(std::make_unique<GIViewerPreview>())
    {
    }

    EditorGIViewer::~EditorGIViewer() = default;

    void EditorGIViewer::Open()
    {
        m_bOpen = true;
        m_bVisible = true;
    }

    void EditorGIViewer::Reset()
    {
        m_Preview->Reset();
        m_bOpen = false;
        m_bVisible = false;
        m_bHasScene = false;
    }

    bool EditorGIViewer::HitTest(float x, float y) const
    {
        return m_bOpen && m_bVisible && x >= m_PopupGeometry.X && y >= m_PopupGeometry.Y &&
            x < m_PopupGeometry.X + m_PopupGeometry.Width && y < m_PopupGeometry.Y + m_PopupGeometry.Height;
    }

    void EditorGIViewer::Draw()
    {
        if (!m_bOpen)
        {
            return;
        }

        ImGui::SetNextWindowSize({1180, 780}, ImGuiCond_FirstUseEver);
        m_bVisible = ImGui::Begin("Lumen GI Viewer", &m_bOpen);
        const auto position = ImGui::GetWindowPos();
        const auto size = ImGui::GetWindowSize();
        m_PopupGeometry = {position.x, position.y, size.x, size.y};
        if (m_bOpen && m_bVisible)
        {
            ImGui::BeginChild("Lumen GI stages", {245, 0}, ImGuiChildFlags_Borders);
            ImGui::TextUnformatted("Lumen stages");
            ImGui::Separator();
            for (const auto& option : Modes)
            {
                if (ImGui::Selectable(option.Name, option.Mode == m_Mode))
                {
                    m_Mode = option.Mode;
                }
            }
            ImGui::Spacing();
            const auto& cards = m_Preview->GetSurfaceCards();
            const char* captureLabel = cards.empty() ? "Build Surface Cache" : "Rebuild Surface Cache";
            if (ImGui::Button(captureLabel, {-1, 0}))
            {
                if (!cards.empty())
                {
                    m_Preview->ResetCache();
                }
                m_SelectedCardIndex = UINT32_MAX;
                m_Mode = Renderer::ELumenViewerMode::SurfaceCoverage;
            }
            ImGui::TextWrapped("Build captures six axis-aligned Mesh Cards per static mesh.");

            ImGui::Separator();
            ImGui::Text("Mesh Cards (%zu)", cards.size());
            ImGui::Checkbox("Draw Mesh Cards in scene", &m_bShowMeshCards);
            ImGui::BeginDisabled(m_SelectedCardIndex >= cards.size());
            ImGui::Checkbox("Selected Card only", &m_bSelectedCardOnly);
            ImGui::EndDisabled();
            if (cards.empty())
            {
                ImGui::TextDisabled("Build Surface Cache to generate scene Card overlays.");
            }
            ImGui::BeginChild("Surface Cache cards", {0, 190}, ImGuiChildFlags_Borders);
            for (const auto& card : cards)
            {
                char label[128];
                std::snprintf(label, sizeof(label), "Instance %u | Card %s | #%u",
                    card.InstanceID, GetCardAxisName(card.AxisIndex), card.Index);
                if (ImGui::Selectable(label, card.Index == m_SelectedCardIndex))
                {
                    m_SelectedCardIndex = card.Index;
                    m_Mode = Renderer::ELumenViewerMode::SurfaceCardAtlas;
                }
            }
            ImGui::EndChild();

            if (m_SelectedCardIndex < cards.size())
            {
                const auto& card = cards[m_SelectedCardIndex];
                ImGui::Text("Instance %u / %s", card.InstanceID, GetCardAxisName(card.AxisIndex));
                ImGui::Text("Center: %.2f, %.2f, %.2f", card.Center.X, card.Center.Y, card.Center.Z);
                ImGui::Text("Normal: %.2f, %.2f, %.2f", card.Normal.X, card.Normal.Y, card.Normal.Z);
                ImGui::Text("Extent: %.2f x %.2f x %.2f", card.Extent.X, card.Extent.Y, card.Extent.Z);
                ImGui::Text("Atlas tile: %u,%u  %ux%u / %ux%u", card.Tile.X, card.Tile.Y,
                    card.Tile.Z, card.Tile.W, card.AtlasSize.X, card.AtlasSize.Y);
                DrawCardProjectionDiagram(card);
            }
            ImGui::EndChild();

            ImGui::SameLine();
            ImGui::BeginGroup();
            ImGui::Text("View: %s", GetModeName(m_Mode));
            ImGui::TextWrapped("%s", GetModeDescription(m_Mode));
            if (m_Mode >= Renderer::ELumenViewerMode::ProbePlacement)
            {
                ImGui::Checkbox("Use radiosity in Screen Probes", &m_Radiosity.bEnabled);
                ImGui::SameLine();
                ImGui::Checkbox("Pause", &m_Radiosity.bPaused);
                ImGui::SameLine();
                if (ImGui::Button("Step"))
                {
                    m_Radiosity.bPaused = true;
                    m_Radiosity.bStep = true;
                }
                ImGui::SameLine();
                m_Radiosity.bRestart = ImGui::Button("Restart");
                ImGui::SameLine();
                ImGui::Text("Iteration %u / %u", m_Preview->GetRadiosityIteration(), m_Radiosity.IterationLimit);
            }
            if (m_Mode == Renderer::ELumenViewerMode::SurfaceCardAtlas)
            {
                DrawCardViewSelector(m_CardView);
                if (m_SelectedCardIndex >= m_Preview->GetSurfaceCards().size())
                {
                    ImGui::TextDisabled("Select a Mesh Card from the list.");
                }
            }
            const auto source = m_GetSource ? m_GetSource() : GIViewerSource{};
            const auto available = ImGui::GetContentRegionAvail();
            float aspect = 16.0f / 9.0f;
            if (source.View && source.View->Viewport.Height > 0)
            {
                aspect = static_cast<float>(source.View->Viewport.Width) / static_cast<float>(source.View->Viewport.Height);
            }
            float imageHeight = std::max(96.0f, available.y - 42.0f);
            float imageWidth = imageHeight * aspect;
            if (imageWidth > available.x)
            {
                imageWidth = available.x;
                imageHeight = imageWidth / std::max(0.01f, aspect);
            }
            m_PreviewSize = {std::max(1, static_cast<int>(imageWidth)), std::max(1, static_cast<int>(imageHeight))};
            m_bHasScene = source.Scene && source.View && m_Preview->Render(source, m_PreviewSize, m_Mode,
                m_SelectedCardIndex, m_CardView, m_Radiosity);
            m_Radiosity.bStep = false;
            m_Radiosity.bRestart = false;
            if (auto* texture = m_Preview->GetTexture())
            {
                const auto imagePosition = ImGui::GetCursorScreenPos();
                const ImVec2 imageSize{static_cast<float>(m_PreviewSize.X), static_cast<float>(m_PreviewSize.Y)};
                ImGui::Image(static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(texture)),
                    imageSize);
                if (m_Mode == Renderer::ELumenViewerMode::Scene && m_bShowMeshCards && source.View && !cards.empty())
                {
                    DrawMeshCardOverlay(*source.View, cards, m_SelectedCardIndex, m_bSelectedCardOnly,
                        imagePosition, imageSize);
                }
            }
            if (!m_bHasScene)
            {
                ImGui::TextDisabled("Add a static mesh to the scene to build the Surface Cache and run Screen Probe Gather.");
            }
            ImGui::EndGroup();
        }
        ImGui::End();
        if (!m_bOpen)
        {
            m_Preview->Reset();
        }
    }
}
