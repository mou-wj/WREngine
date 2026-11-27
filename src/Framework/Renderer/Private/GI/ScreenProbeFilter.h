#pragma once

#include "ScreenProbeGather.h"
#include <array>

namespace Renderer
{
    enum class EScreenProbeFilterView : uint32_t
    {
        Spatial, Raw, Temporal, HistoryValidity, HistoryWeight, ReprojectionError, HistoryLength, Variance
    };

    struct ScreenProbeFilterInput
    {
        RenderCore::RenderGraphTextureRef RawGI = nullptr;
        RenderCore::RenderGraphTextureRef Output = nullptr;
        RenderCore::RenderGraphTextureRef SceneColor = nullptr;
        GBufferInputParameters GBuffer{};
        Core::Float4x4 ViewProjection = Core::Float4x4::Identity();
        Core::Float4x4 InverseViewProjection = Core::Float4x4::Identity();
        Core::Float3 CameraPosition{};
        EScreenProbeFilterView View = EScreenProbeFilterView::Spatial;
        float HistoryWeightLimit = 0.90f;
        uint32_t SpatialIterations = 1;
        bool bUseVariance = false;
    };

    // One update per submitted preview graph. Retire its last output access before destruction or resizing.
    class RENDERER_API ScreenProbeFilter
    {
    public:
        ScreenProbeFilter() = default;
        ScreenProbeFilter(const ScreenProbeFilter&) = delete;
        ScreenProbeFilter& operator=(const ScreenProbeFilter&) = delete;

        void Invalidate();
        uint32_t GetSamplePhase() const { return m_SamplePhase; }
        void AddPasses(RenderCore::RenderGraphBuilder& builder, const ScreenProbeFilterInput& input);

    private:
        struct History
        {
            RenderCore::RenderTextureSP Lighting;
            RenderCore::RenderTextureSP Geometry;
            RenderCore::RenderTextureSP Moments;
        };

        std::array<History, 2> m_History{};
        Core::UInt2 m_Size{};
        Core::UInt2 m_GeometrySize{};
        Core::Float3 m_PreviousCamera{};
        Core::Float4x4 m_PreviousViewProjection = Core::Float4x4::Identity();
        Core::Float4x4 m_PreviousInverseViewProjection = Core::Float4x4::Identity();
        uint32_t m_NextHistorySlot = 0;
        uint32_t m_SamplePhase = 1;
        bool m_bHistoryValid = false;
        bool m_bUseVariance = false;
    };
}
