#pragma once

#include "RenderGraphResource.h"
#include "RenderResource.h"
#include "SceneInterface.h"
#include "SceneView.h"
#include <array>

namespace Renderer
{
    class RENDERER_API FrameInterpolation
    {
    public:
        ~FrameInterpolation();
        void Invalidate() { m_bHasPrevious = false; }
        bool AddPasses(RenderCore::RenderGraphBuilder& builder, RenderCore::RenderGraphTextureRef realColor,
            RenderCore::RenderGraphTextureRef depth, RenderCore::RenderGraphTextureRef output,
            const Engine::SceneView& view, Core::Float2 depthJitter, double time,
            Engine::EFrameInterpolationView debugView = Engine::EFrameInterpolationView::Generated);

    private:
        void RetireFrames();

        Core::UInt2 m_Size{};
        Core::Float4x4 m_PreviousVP = Core::Float4x4::Identity();
        Core::Float4x4 m_PreviousInverseVP = Core::Float4x4::Identity();
        Core::Float3 m_PreviousCamera{};
        Core::Float3 m_PreviousViewAxis{};
        double m_PreviousTime = 0.0;
        std::array<RenderCore::RenderTextureSP, 2> m_Frames;
        uint32_t m_NextFrame = 0;
        bool m_bHasPrevious = false;
    };
}
