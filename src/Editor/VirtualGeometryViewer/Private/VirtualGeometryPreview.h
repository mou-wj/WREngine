#pragma once
#include "StaticMeshResources.h"
#include "Math.hpp"
#include <memory>
#include <array>
#include <filesystem>

namespace Engine { class StaticMeshAsset; class Material; class DirectionalLightComponent; }
namespace Renderer { class Scene; class VirtualGeometryStreaming; struct VirtualGeometryStreamingStatistics; }
namespace Editor
{
    class VirtualGeometryPreview
    {
    public:
        VirtualGeometryPreview();
        ~VirtualGeometryPreview();
        void SetMesh(const std::shared_ptr<Engine::StaticMeshAsset>& asset);
        void Render(Core::Int2 size, uint32_t mipLevel, bool material, float yaw, float pitch, float distance,
                    bool dynamicLOD = true, float targetPixelError = 1.0f, bool wireframe = false,
                    bool gpuSelection = false, bool frustumCulling = true, bool captureGPU = false,
                    uint32_t rasterMode = 0, float softwareClusterPixels = 32, bool measureFrame = false,
                    bool streaming = false, uint64_t streamingBudget = 16ull * 1024 * 1024);
        void Reset();
        RenderCore::RenderTexture* GetTexture() const { return m_Color.get(); }
        const Engine::VirtualGeometryResource* GetGeometry() const;
        const std::vector<uint32_t>& GetSelectedClusters() const { return m_SelectedClusters; }
        const std::array<uint32_t, 4>& GetGPUStatistics() const { return m_GPUStatistics; }
        uint32_t GetCutDifferenceCount() const { return m_CutDifferenceCount; }
        bool HasGPUCapture() const { return m_bGPUCapture; }
        const std::array<uint32_t, 4>& GetRasterStatistics() const { return m_RasterStatistics; }
        double GetCompletedFrameMilliseconds() const { return m_CompletedFrameMilliseconds; }
        const Renderer::VirtualGeometryStreamingStatistics* GetStreamingStatistics() const;
    private:
        void WaitForLastDraw();
        std::shared_ptr<Engine::StaticMeshAsset> m_Asset;
        std::shared_ptr<Engine::StaticMeshRenderData> m_Data;
        std::unique_ptr<RenderCore::RenderTexture> m_Color;
        std::unique_ptr<Renderer::Scene> m_LightScene;
        std::unique_ptr<Engine::DirectionalLightComponent> m_Light;
        std::unique_ptr<Engine::Material> m_DefaultMaterial;
        std::vector<uint32_t> m_SelectedClusters;
        std::array<uint32_t, 4> m_GPUStatistics{};
        uint32_t m_CutDifferenceCount = 0;
        bool m_bGPUCapture = false;
        std::array<uint32_t, 4> m_RasterStatistics{};
        double m_CompletedFrameMilliseconds = 0;
        std::shared_ptr<Renderer::VirtualGeometryStreaming> m_Streaming;
        std::filesystem::path m_PageArchive;
    };
}
