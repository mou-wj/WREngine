#pragma once

#include "imgui.h"
#include "RHICommandList.h"
#include "RenderResource.h"
#include "Shader.h"
#include "SlateRenderer.h"
#include <memory>
#include <unordered_map>
#include <array>
#include <vector>

namespace ImGUISlate
{
    // Private renderer backend. Texture IDs refer to RenderTexture objects, never native handles.
    class ImGuiRHIBackend
    {
    public:
        ImGuiRHIBackend();
        ~ImGuiRHIBackend();
        void RenderDrawData(ImDrawData* drawData, RHI::RHIGraphicCommandList& cmd, RHI::RHITexture* target,
                            std::vector<RHI::RHIWaitInfo>& waits, SlateCore::SlateRenderer::DrawResources& resources);
        void EndFrame(uint64_t fence);
        static ImTextureID GetTextureID(RenderCore::RenderTexture* texture);

    private:
        void UpdateTextures(ImDrawData* drawData);
        RHI::RHIGraphicsPipelineState* GetPipeline(RHI::ERHIFormat format);
        RHI::RHIVertexShaderSP m_VertexShader;
        RHI::RHIFragmentShaderSP m_PixelShader;
        RHI::RHISamplerSP m_Sampler;
        RenderCore::ShaderParameterAllocationMap m_VertexParameters;
        RenderCore::ShaderParameterAllocationMap m_PixelParameters;
        std::unordered_map<ImTextureData*, std::unique_ptr<RenderCore::RenderTexture>> m_Textures;
        std::unordered_map<RHI::ERHIFormat, RHI::RHIGraphicsPipelineStateSP> m_Pipelines;
        struct FrameResources
        {
            std::unique_ptr<RenderCore::RenderBuffer> VertexBuffer;
            std::unique_ptr<RenderCore::RenderBuffer> IndexBuffer;
            std::vector<ImDrawVert> Vertices;
            std::vector<uint32_t> Indices;
            uint64_t Fence = 0;
        };
        std::array<FrameResources, 3> m_Frames;
        size_t m_FrameIndex = 0;
    };
}
