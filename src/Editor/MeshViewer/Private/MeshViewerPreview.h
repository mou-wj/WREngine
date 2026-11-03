#pragma once
#include "Math.hpp"
#include <memory>
#include <cstdint>

namespace Engine { class StaticMeshAsset; }
namespace RenderCore { class RenderTexture; class RenderBuffer; }
namespace Editor
{
    // Owns only derived preview buffers and its offscreen color target.
    class MeshViewerPreview
    {
    public:
        MeshViewerPreview() = default;
        ~MeshViewerPreview();
        void SetMesh(const std::shared_ptr<Engine::StaticMeshAsset>& asset, int lod);
        void Render(Core::Int2 size, bool wireframe, bool normals, float normalLength,
            float yaw, float pitch, float distance);
        void Reset();
        RenderCore::RenderTexture* GetTexture() const { return m_Color.get(); }
        bool HasGeneratedNormals() const { return m_bGeneratedNormals; }
    private:
        void WaitForLastDraw();
        std::shared_ptr<Engine::StaticMeshAsset> m_Asset;
        std::unique_ptr<RenderCore::RenderTexture> m_Color;
        std::unique_ptr<RenderCore::RenderBuffer> m_Vertices;
        std::unique_ptr<RenderCore::RenderBuffer> m_Indices;
        Core::Float4 m_CenterAndScale;
        uint32_t m_VertexCount = 0;
        uint32_t m_IndexCount = 0;
        int m_Lod = -1;
        bool m_bGeneratedNormals = false;
    };
}
