#pragma once
#include "RenderGraphBuilder.h"
#include "StaticMeshResources.h"
#include "VirtualGeometryPages.h"
#include "SceneView.h"

namespace Renderer
{
    struct VirtualGeometryStreamingStatistics
    {
        uint32_t TotalPages = 0, ResidentPages = 0, PinnedPages = 0, LoadingPages = 0;
        uint64_t CacheBytes = 0, BudgetBytes = 0, StoredBytes = 0, RawBytes = 0, UploadedBytes = 0;
        uint64_t MetadataBytes = 0;
        uint64_t Loads = 0, Evictions = 0, FailedLoads = 0, BudgetBlocked = 0, FeedbackFrames = 0;
        uint64_t PageReferences = 0, PageHits = 0;
        double LoadMilliseconds = 0;
    };

    // Per-resource physical cache. The caller serializes Prepare/graph execution/EndFrame with resource edits.
    // No global owner or alternate queue; all uploads and feedback use the existing RDG and resource fences.
    class RENDERER_API VirtualGeometryStreaming
    {
    public:
        VirtualGeometryStreaming(std::shared_ptr<Engine::StaticMeshRenderData> data,
            std::shared_ptr<const Engine::VirtualGeometryPageStorage> storage, uint64_t budgetBytes);
        ~VirtualGeometryStreaming();
        void Prepare(RenderCore::RenderGraphBuilder& builder);
        void AddFeedbackReadback(RenderCore::RenderGraphBuilder& builder);
        void EndFrame(); // Call after Execute, before the graph is destroyed. No CPU wait.
        RenderCore::RenderGraphBufferRef GetResidencyBuffer() const;
        RenderCore::RenderGraphBufferRef GetClusterPageBuffer() const;
        RenderCore::RenderGraphBufferRef GetFeedbackBuffer() const;
        const Engine::VirtualGeometryResource& GetResource() const;
        const std::vector<uint32_t>& GetResidency() const;
        const std::vector<Engine::VirtualGeometryCluster>& GetPhysicalClusters() const;
        const VirtualGeometryStreamingStatistics& GetStatistics() const;
        const std::string& GetLastError() const;
        uint32_t GetPageCount() const;
        std::vector<uint32_t> SelectResidentReference(const Core::Mat4& transform, const Engine::SceneView& view,
                                                     float targetPixelError) const;
    private:
        struct State;
        std::unique_ptr<State> m_State;
    };
}
