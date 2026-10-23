#include "imgui_impl_rhi.h"
#include "RHIApi.h"
#include "ShaderCompiler.h"
#include "RHIPipelineStateCache.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace ImGUISlate
{
    using namespace RHI;
    using namespace RenderCore;

    namespace
    {
        const char* guiShader = R"(
struct VertexInput { float2 Position : POSITION; float2 UV : TEXCOORD0; float4 Color : COLOR0; };
struct VertexOutput { float4 Position : SV_POSITION; float2 UV : TEXCOORD0; float4 Color : COLOR0; };
#if GUI_VERTEX_SHADER
cbuffer GUIProjection : register(b0, space0) { float4 ScaleTranslate; };
VertexOutput VSMain(VertexInput input) {
    VertexOutput output;
    output.Position = float4(input.Position * ScaleTranslate.xy + ScaleTranslate.zw, 0.0, 1.0);
    output.UV = input.UV;
    output.Color = input.Color;
    return output;
}
#else
Texture2D GUITexture : register(t0, GUI_PIXEL_SPACE);
SamplerState GUISampler : register(s0, GUI_PIXEL_SPACE);
float4 PSMain(VertexOutput input) : SV_Target0 { return input.Color * GUITexture.Sample(GUISampler, input.UV); }
#endif
)";

        ShaderCompilationOutput CompileGUI(ERHIShaderFrequency frequency, const char* entry)
        {
            ShaderCompileInput input;
            input.Platform = GShaderPlatform;
            input.Frequency = frequency;
            input.EntryPoint = entry;
            input.VirtualSourceFilePath = "imgui_rhi.hlsl";
            input.Environment.VirtualIncludes[input.VirtualSourceFilePath] = guiShader;
            input.Environment.Definitions["GUI_VERTEX_SHADER"] = frequency == ERHIShaderFrequency::Vertex ? "1" : "0";
            // Vulkan maps shader stages to separate descriptor sets; D3D12 RHI resources use space0.
            input.Environment.Definitions["GUI_PIXEL_SPACE"] = GShaderPlatform == ERHIShaderPlatform::Vulkan ? "space1" : "space0";
            auto output = ShaderCompiler::Compile(input);
            if (!output.bSuccess)
                throw std::runtime_error("ImGui RHI shader compilation failed: " + output.ErrorMessage);
            return output;
        }
    }

    ImTextureID ImGuiRHIBackend::GetTextureID(RenderTexture* texture)
    {
        return static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(texture));
    }

    ImGuiRHIBackend::ImGuiRHIBackend()
    {
        auto vs = CompileGUI(ERHIShaderFrequency::Vertex, "VSMain");
        auto ps = CompileGUI(ERHIShaderFrequency::Fragment, "PSMain");
        m_VertexShader = GRHIApi->CreateVertexShader(vs.PackedBinaryData);
        m_PixelShader = GRHIApi->CreateFragmentShader(ps.PackedBinaryData);
        m_VertexParameters = vs.ParameterMap;
        m_PixelParameters = ps.ParameterMap;
        RHISamplerDesc sampler;
        sampler.AddressU = sampler.AddressV = sampler.AddressW = ERHIAddressMode::ClampToEdge;
        m_Sampler = GRHIApi->CreateSampler(sampler);
        if (!m_VertexShader || !m_PixelShader || !m_Sampler)
            throw std::runtime_error("Failed to create ImGui RHI resources");
        auto& io = ImGui::GetIO();
        io.BackendRendererName = "imgui_impl_rhi";
        io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures;
        auto& platform = ImGui::GetPlatformIO();
        platform.Renderer_TextureMaxWidth = platform.Renderer_TextureMaxHeight = 4096;
    }

    ImGuiRHIBackend::~ImGuiRHIBackend()
    {
        // The window renderer has waited for the last submission before backend destruction.
        for (auto& entry : m_Textures)
        {
            entry.first->SetTexID(ImTextureID_Invalid);
            entry.first->SetStatus(ImTextureStatus_Destroyed);
        }
        auto& io = ImGui::GetIO();
        io.BackendRendererName = nullptr;
        io.BackendFlags &= ~(ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures);
    }

    void ImGuiRHIBackend::UpdateTextures(ImDrawData* drawData)
    {
        if (!drawData->Textures)
            return;
        for (auto* texture : *drawData->Textures)
        {
            if (texture->Status == ImTextureStatus_WantCreate)
            {
                if (texture->Format != ImTextureFormat_RGBA32)
                    throw std::runtime_error("ImGui RHI requires RGBA32 atlas textures");
                RHITextureDesc desc;
                desc.Width = texture->Width;
                desc.Height = texture->Height;
                desc.Type = ERHITextureType::Texture2D;
                desc.Format = ERHIFormat::R8G8B8A8_UNorm;
                desc.Usage = ERHITextureCreateFlag::ShaderResource | ERHITextureCreateFlag::TransferDest;
                desc.DebugName = "ImGuiAtlas";
                auto resource = std::make_unique<RenderTexture>(desc);
                resource->InitRHIResource();
                resource->UploadData(texture->GetPixels(), 0, 0);
                texture->SetTexID(GetTextureID(resource.get()));
                m_Textures.emplace(texture, std::move(resource));
                texture->SetStatus(ImTextureStatus_OK);
            }
            else if (texture->Status == ImTextureStatus_WantUpdates)
            {
                // Upload the full CPU atlas; existing RenderTexture upload handles row pitch and synchronization.
                auto* resource = m_Textures.at(texture).get();
                const auto fence = resource->GetTracker().GetLastAccessFence();
                if (fence.Value) GRHIApi->GetQueue(fence.QueueType)->WaitValue(fence.Value);
                resource->UploadData(texture->GetPixels(), 0, 0);
                texture->SetStatus(ImTextureStatus_OK);
            }
            else if (texture->Status == ImTextureStatus_WantDestroy)
            {
                auto* resource = m_Textures.at(texture).get();
                const auto fence = resource->GetTracker().GetLastAccessFence();
                if (fence.Value) GRHIApi->GetQueue(fence.QueueType)->WaitValue(fence.Value);
                m_Textures.erase(texture);
                texture->SetTexID(ImTextureID_Invalid);
                texture->SetStatus(ImTextureStatus_Destroyed);
            }
        }
    }

    RHIGraphicsPipelineState* ImGuiRHIBackend::GetPipeline(ERHIFormat format)
    {
        auto& pipeline = m_Pipelines[format];
        if (pipeline)
            return pipeline.get();
        RHIGraphicsPipelineStateDesc desc;
        desc.ShaderStages.VertexShader = m_VertexShader.get();
        desc.ShaderStages.FragmentShader = m_PixelShader.get();
        RHIVertexDescStateDesc vertices;
        vertices.Bindings = { { 0, sizeof(ImDrawVert), ERHIInputRate::PerVertex } };
        vertices.Attributes = {
            { 0, 0, offsetof(ImDrawVert, pos), ERHIFormat::R32G32_Float },
            { 1, 0, offsetof(ImDrawVert, uv), ERHIFormat::R32G32_Float },
            { 2, 0, offsetof(ImDrawVert, col), ERHIFormat::R8G8B8A8_UNorm }
        };
        desc.VertexDescState = RHIPipelineStateCache::GetOrCreateVertexDescState(vertices);
        RHIRasterizerStateDesc rasterizer;
        rasterizer.CullMode = ERHICullMode::None;
        desc.RasterizerState = RHIPipelineStateCache::GetOrCreateRasterizerState(rasterizer);
        desc.DepthStencilState = RHIPipelineStateCache::GetOrCreateDepthStencilState({});
        RHIColorBlendStateDesc blend;
        blend.Attachments.resize(1);
        blend.Attachments[0].bBlendEnable = true;
        desc.ColorBlendState = RHIPipelineStateCache::GetOrCreateColorBlendState(blend);
        desc.AttachmentDesc.ColorAttachmentCount = 1;
        desc.AttachmentDesc.ColorAttachments[0].Format = format;
        desc.AttachmentDesc.ColorAttachments[0].Actions = ERenderTargetActions::Clear_Store;
        pipeline = GRHIApi->CreateGraphicsPipelineState(desc);
        if (!pipeline)
            throw std::runtime_error("Failed to create ImGui RHI pipeline");
        return pipeline.get();
    }

    void ImGuiRHIBackend::EndFrame(uint64_t fence)
    {
        auto& frame = m_Frames[m_FrameIndex];
        frame.Fence = fence;
        m_FrameIndex = (m_FrameIndex + 1) % m_Frames.size();
    }

    void ImGuiRHIBackend::RenderDrawData(ImDrawData* drawData, RHIGraphicCommandList& cmd, RHITexture* target,
                                       std::vector<RHIWaitInfo>& waits, SlateCore::SlateRenderer::DrawResources& resources)
    {
        auto& frame = m_Frames[m_FrameIndex];
        auto* queue = GRHIApi->GetQueue(EQueueType::Graphics);
        if (frame.Fence > queue->GetCurrentTimelineValue()) queue->WaitValue(frame.Fence);
        UpdateTextures(drawData);
        // A surface may clamp its swapchain extent while an OS resize is in progress.
        const int width = static_cast<int>(target->GetDesc().Width);
        const int height = static_cast<int>(target->GetDesc().Height);
        if (width <= 0 || height <= 0 || drawData->DisplaySize.x <= 0 || drawData->DisplaySize.y <= 0)
            return;

        const float framebufferScaleX = width / drawData->DisplaySize.x;
        const float framebufferScaleY = height / drawData->DisplaySize.y;
        auto& vertices = frame.Vertices;
        auto& indices = frame.Indices;
        vertices.clear();
        indices.clear();
        vertices.reserve(drawData->TotalVtxCount);
        indices.reserve(drawData->TotalIdxCount);
        for (const auto* list : drawData->CmdLists)
        {
            const auto vertexBase = static_cast<uint32_t>(vertices.size());
            const auto indexBase = indices.size();
            vertices.insert(vertices.end(), list->VtxBuffer.begin(), list->VtxBuffer.end());
            indices.insert(indices.end(), list->IdxBuffer.begin(), list->IdxBuffer.end());
            // Bake base vertices into 32-bit indices, including large ImGui draw lists.
            for (const auto& draw : list->CmdBuffer)
                for (uint32_t i = 0; i < draw.ElemCount; ++i)
                    indices[indexBase + draw.IdxOffset + i] += vertexBase + draw.VtxOffset;
        }
        if (!vertices.empty())
        {
            auto upload = [&cmd, &resources](std::unique_ptr<RenderBuffer>& buffer, const void* data, uint32_t size,
                             uint32_t stride, ERHIBufferUsageFlag usage)
            {
                if (!buffer || buffer->GetRHI()->GetDesc().Size < size)
                {
                    RHIBufferDesc desc;
                    desc.Size = size;
                    desc.Stride = stride;
                    desc.Usage = usage | ERHIBufferUsageFlag::TransferDst;
                    buffer = std::make_unique<RenderBuffer>(desc);
                    buffer->InitRHIResource();
                }
                TransitionResource(GRHIApi, cmd, buffer->GetRHI(), buffer->GetTracker().GetLastAccess(),
                    ERHIResourceAccess::TransferDest, EQueueType::Graphics, EQueueType::Graphics);
                RHIUpdateBufferRegion region;
                region.Offset = 0;
                region.Size = size;
                GRHIApi->UpdateBuffer(cmd, buffer->GetRHI(), data, region);
                TransitionResource(GRHIApi, cmd, buffer->GetRHI(), ERHIResourceAccess::TransferDest,
                    ERHIResourceAccess::VertexOrIndexBuffer, EQueueType::Graphics, EQueueType::Graphics);
                resources.VertexOrIndexBuffers.push_back(buffer.get());
            };
            upload(frame.VertexBuffer, vertices.data(), static_cast<uint32_t>(vertices.size() * sizeof(ImDrawVert)), sizeof(ImDrawVert), ERHIBufferUsageFlag::Vertex);
            upload(frame.IndexBuffer, indices.data(), static_cast<uint32_t>(indices.size() * sizeof(uint32_t)), sizeof(uint32_t), ERHIBufferUsageFlag::Index);
        }

        // Transition all sampled textures before entering the render pass.
        for (const auto* list : drawData->CmdLists)
            for (const auto& draw : list->CmdBuffer)
                if (!draw.UserCallback && draw.ElemCount)
                {
                    auto* texture = reinterpret_cast<RenderTexture*>(static_cast<uintptr_t>(draw.GetTexID()));
                    if (!texture)
                        throw std::runtime_error("ImGui draw command has no RHI texture");
                    if (std::find(resources.SampledTextures.begin(), resources.SampledTextures.end(), texture) != resources.SampledTextures.end())
                        continue;
                    resources.SampledTextures.push_back(texture);
                    const auto fence = texture->GetTracker().GetLastAccessFence();
                    const auto before = texture->GetTracker().GetSubresourceAccess({});
                    if (fence.QueueType == EQueueType::Graphics)
                    {
                        TransitionResource(GRHIApi, cmd, texture->GetRHI(), before, ERHIResourceAccess::SRV,
                            EQueueType::Graphics, EQueueType::Graphics);
                        if (fence.Value) waits.push_back({nullptr, fence.QueueType, fence.Value, ERHIPipelineStage::FragmentShader});
                    }
                    else
                    {
                        auto* sourceQueue = GRHIApi->GetQueue(fence.QueueType);
                        auto* sourceContext = sourceQueue->AcquireCommandContext();
                        RHICommandListBase sourceCmd(sourceContext);
                        sourceCmd.SetImmediate(true);
                        sourceCmd.Begin();
                        TransitionResource(GRHIApi, sourceCmd, texture->GetRHI(), before, ERHIResourceAccess::SRV,
                            fence.QueueType, EQueueType::Graphics);
                        sourceCmd.End();
                        std::vector<RHIWaitInfo> sourceWaits;
                        if (fence.Value) sourceWaits.push_back({nullptr, fence.QueueType, fence.Value});
                        const auto value = sourceQueue->ExecuteContext({sourceContext}, sourceWaits);
                        texture->GetTracker().UpdateSubresourceAccess({}, ERHIResourceAccess::SRV);
                        texture->GetTracker().UpdateLastAccessFence({fence.QueueType, value});
                        waits.push_back({nullptr, fence.QueueType, value, ERHIPipelineStage::FragmentShader});
                    }
                }

        RHIRenderPassInfo pass;
        pass.RenderTargets.Bound(target, ERenderTargetActions::Clear_Store, nullptr, ERenderTargetActions::DontCare_DontCare, width, height);
        const float clearColor[4] = { 0.08f, 0.08f, 0.10f, 1.0f };
        std::copy(std::begin(clearColor), std::end(clearColor), pass.RenderTargets.ColorAttachments[0].ClearBinding.Color);
        pass.RenderArea.Width = width;
        pass.RenderArea.Height = height;
        cmd.BeginRenderPass(pass);
        if (!vertices.empty())
        {
            auto setup = [&]()
            {
                cmd.SetGraphicPipelineState(GetPipeline(target->GetDesc().Format));
                cmd.SetStreamSource(0, frame.VertexBuffer->GetRHI(), 0);
                cmd.SetViewport(0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1);
                // VulkanRHI already normalizes its viewport with a negative native height.
                float projection[4] = { 2.0f / drawData->DisplaySize.x,
                    -2.0f / drawData->DisplaySize.y, 0, 0 };
                projection[2] = -1.0f - drawData->DisplayPos.x * projection[0];
                projection[3] = 1.0f - drawData->DisplayPos.y * projection[1];
                auto allocation = m_VertexParameters.FindParameterAllocation("ScaleTranslate");
                if (!allocation)
                    throw std::runtime_error("Missing ImGui projection reflection");
                RHIBatchedShaderParameters parameters;
                parameters.Data.resize(sizeof(projection));
                std::memcpy(parameters.Data.data(), projection, sizeof(projection));
                parameters.UniformParameters.push_back({ allocation->BufferIndex, allocation->BaseIndex, 0, sizeof(projection) });
                cmd.SetBatchedShaderParameters(m_VertexShader.get(), parameters);
            };
            setup();
            uint32_t indexBase = 0;
            for (const auto* list : drawData->CmdLists)
            {
                for (const auto& draw : list->CmdBuffer)
                {
                    if (draw.UserCallback)
                    {
                        if (draw.UserCallback == ImDrawCallback_ResetRenderState)
                            setup();
                        else
                        {
                            draw.UserCallback(list, &draw);
                            setup();
                        }
                        continue;
                    }
                    const auto clip = draw.ClipRect;
                    const float x1 = (clip.x - drawData->DisplayPos.x) * framebufferScaleX;
                    const float y1 = (clip.y - drawData->DisplayPos.y) * framebufferScaleY;
                    const float x2 = (clip.z - drawData->DisplayPos.x) * framebufferScaleX;
                    const float y2 = (clip.w - drawData->DisplayPos.y) * framebufferScaleY;
                    const int left = std::clamp(static_cast<int>(std::floor(x1)), 0, width);
                    const int top = std::clamp(static_cast<int>(std::floor(y1)), 0, height);
                    const int right = std::clamp(static_cast<int>(std::ceil(x2)), 0, width);
                    const int bottom = std::clamp(static_cast<int>(std::ceil(y2)), 0, height);
                    if (right <= left || bottom <= top || !draw.ElemCount)
                        continue;
                    cmd.SetScissor(left, GShaderPlatform == ERHIShaderPlatform::OpenGL ? height - bottom : top,
                                   right - left, bottom - top);
                    auto* texture = reinterpret_cast<RenderTexture*>(static_cast<uintptr_t>(draw.GetTexID()));
                    RHIBatchedShaderParameters parameters;
                    auto textureBinding = m_PixelParameters.FindParameterAllocation("GUITexture");
                    auto samplerBinding = m_PixelParameters.FindParameterAllocation("GUISampler");
                    if (!textureBinding || !samplerBinding)
                        throw std::runtime_error("Missing ImGui texture reflection");
                    auto* srv = texture->GetViewCache().GetOrCreateSRV(texture->GetRHI(), {});
                    parameters.ResourceParameters.push_back({ srv, RHIShaderResourceParameter::EType::SRV, textureBinding->BaseIndex });
                    parameters.ResourceParameters.push_back({ m_Sampler.get(), RHIShaderResourceParameter::EType::Sampler, samplerBinding->BaseIndex });
                    cmd.SetBatchedShaderParameters(m_PixelShader.get(), parameters);
                    cmd.DrawIndexed(frame.IndexBuffer->GetRHI(), draw.ElemCount, 1, indexBase + draw.IdxOffset, 0);
                }
                indexBase += list->IdxBuffer.Size;
            }
        }
        cmd.EndRenderPass();
    }
}
