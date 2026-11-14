#pragma once
#include "ImWidget.h"
#include "Math.hpp"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Engine { class StaticMeshAsset; }
namespace Editor
{
    class VirtualGeometryPreview;
    struct VirtualGeometryViewerMesh
    {
        std::string Name;
        std::shared_ptr<Engine::StaticMeshAsset> Asset;
    };

    class VIRTUALGEOMETRYVIEWER_API EditorVirtualGeometryViewer final : public ImGUISlate::PopupImWidget
    {
    public:
        explicit EditorVirtualGeometryViewer(std::function<std::vector<VirtualGeometryViewerMesh>()> getMeshes);
        ~EditorVirtualGeometryViewer() override;
        void Open(const std::string& preferredNode = {});
        void Tick();
        void Reset();
        void Draw() override;
        bool HitTest(float x, float y) const override;
    private:
        void DrawGeometryInfo();
        std::function<std::vector<VirtualGeometryViewerMesh>()> m_GetMeshes;
        std::vector<VirtualGeometryViewerMesh> m_Meshes;
        std::shared_ptr<Engine::StaticMeshAsset> m_Asset;
        std::unique_ptr<VirtualGeometryPreview> m_Preview;
        std::string m_SelectedNode;
        SlateCore::WidgetGeometry m_PopupGeometry;
        Core::Int2 m_PreviewSize{640, 360};
        float m_Yaw = 0.785398f;
        float m_Pitch = 0.4f;
        float m_Distance = 3.2f;
        float m_TargetPixelError = 1.0f;
        int m_MipLevel = 0;
        int m_SelectedCluster = -1;
        bool m_bOpen = false;
        bool m_bVisible = false;
        bool m_bMaterial = false;
        bool m_bRebuild = false;
        bool m_bDynamicLOD = true;
        bool m_bWireframe = false;
        bool m_bGPUSelection = true;
        bool m_bFrustumCulling = true;
        bool m_bCaptureGPU = false;
        int m_RasterMode = 1;
        float m_SoftwareClusterPixels = 32;
        bool m_bMeasureRasterFrame = false;
    };
}
