#pragma once
#include "ImWidget.h"
#include "Math.hpp"
#include <array>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Engine { class StaticMeshAsset; }
namespace Editor
{
    class MeshViewerPreview;

    // A per-tick scene snapshot. The asset remains the owner of source mesh data.
    struct MeshViewerMesh
    {
        std::string Name;
        std::shared_ptr<Engine::StaticMeshAsset> Asset;
    };

    class MESHVIEWER_API EditorMeshViewer final : public ImGUISlate::PopupImWidget
    {
    public:
        explicit EditorMeshViewer(std::function<std::vector<MeshViewerMesh>()> getMeshes);
        ~EditorMeshViewer() override;
        void Open(const std::string& preferredNode = {});
        // Call before the window records this frame's GUI texture references.
        void Tick();
        void Reset();
        void Draw() override;
        bool HitTest(float x, float y) const override;
    private:
        void DrawMeshInfo();
        std::function<std::vector<MeshViewerMesh>()> m_GetMeshes;
        std::vector<MeshViewerMesh> m_Meshes;
        std::unique_ptr<MeshViewerPreview> m_Preview;
        std::shared_ptr<Engine::StaticMeshAsset> m_Asset;
        std::string m_SelectedNode;
        std::string m_Status;
        SlateCore::WidgetGeometry m_PopupGeometry;
        Core::Int2 m_PreviewSize{420,300};
        std::array<bool,5> m_Columns{true,true,true,false,false};
        int m_Lod = 0;
        int m_RequestedLod = 0;
        float m_Yaw = 0.785398f;
        float m_Pitch = 0.4f;
        float m_Distance = 3.2f;
        float m_NormalLength = 0.08f;
        bool m_bOpen = false;
        bool m_bVisible = false;
        bool m_bWireframe = false;
        bool m_bShowNormals = false;
    };
}
