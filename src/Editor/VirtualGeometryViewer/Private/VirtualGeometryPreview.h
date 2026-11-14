#pragma once
#include "StaticMeshResources.h"
#include "Math.hpp"
#include <memory>

namespace Engine { class StaticMeshAsset; class Material; class DirectionalLightComponent; }
namespace Renderer { class Scene; }
namespace Editor
{
    class VirtualGeometryPreview
    {
    public:
        VirtualGeometryPreview();
        ~VirtualGeometryPreview();
        void SetMesh(const std::shared_ptr<Engine::StaticMeshAsset>& asset);
        void Render(Core::Int2 size, uint32_t mipLevel, bool material, float yaw, float pitch, float distance,
                    bool dynamicLOD = true, float targetPixelError = 1.0f, bool wireframe = false);
        void Reset();
        RenderCore::RenderTexture* GetTexture() const { return m_Color.get(); }
        const Engine::VirtualGeometryResource* GetGeometry() const;
        const std::vector<uint32_t>& GetSelectedClusters() const { return m_SelectedClusters; }
    private:
        void WaitForLastDraw();
        std::shared_ptr<Engine::StaticMeshAsset> m_Asset;
        std::shared_ptr<Engine::StaticMeshRenderData> m_Data;
        std::unique_ptr<RenderCore::RenderTexture> m_Color;
        std::unique_ptr<Renderer::Scene> m_LightScene;
        std::unique_ptr<Engine::DirectionalLightComponent> m_Light;
        std::unique_ptr<Engine::Material> m_DefaultMaterial;
        std::vector<uint32_t> m_SelectedClusters;
    };
}
