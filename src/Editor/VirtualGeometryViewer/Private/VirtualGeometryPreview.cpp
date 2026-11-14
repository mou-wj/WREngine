#include "VirtualGeometryPreview.h"
#include "StaticMeshAsset.h"
#include "StaticMesh.h"
#include "Material.h"
#include "LightComponent.h"
#include "Transform.hpp"
#include "GlobalShader.h"
#include "RHIPipelineStateCache.h"
#include "../../../Framework/Renderer/VirtualGeometry/VirtualGeometryRasterPass.h"
#include "../../../Framework/Renderer/VirtualGeometry/VirtualGeometryMaterialResolvePass.h"
#include "../../../Framework/Renderer/VirtualGeometry/VirtualGeometryVertexFactory.h"
#include "../../../Framework/Renderer/Private/Scene.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace Editor
{
    using namespace RenderCore;
    using namespace RHI;

    BEGIN_SHADER_PARAMETER_STRUCT(VirtualGeometryWireframePassParameters)
    SHADER_PARAMETER_STRUCT_INCLUDE(Renderer::VirtualGeometryVertexFactoryParameters, Vertex)
    SHADER_PARAMETER_RENDER_TARGET_BINDING_SLOTS(Targets)
    RDG_BUFFER_ACCESS(Positions, ERHIResourceAccess::VertexOrIndexBuffer)
    RDG_BUFFER_ACCESS(Indices, ERHIResourceAccess::VertexOrIndexBuffer)
    RDG_BUFFER_ACCESS(Arguments, ERHIResourceAccess::IndirectArgs)
    END_SHADER_PARAMETER_STRUCT(VirtualGeometryWireframePassParameters)

    BEGIN_SHADER_PARAMETER_STRUCT(VirtualGeometryWireframeWord)
    SHADER_PARAMETER(uint32_t, Value)
    END_SHADER_PARAMETER_STRUCT(VirtualGeometryWireframeWord)

    BEGIN_SHADER_PARAMETER_STRUCT(VirtualGeometryWireframeArgumentsParameters)
    SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, TriangleArguments)
    SHADER_PARAMETER_RDG_RWSTRUCTURED_BUFFER(VirtualGeometryWireframeWord, LineArguments)
    SHADER_PARAMETER(uint32_t, ArgumentCount)
    END_SHADER_PARAMETER_STRUCT(VirtualGeometryWireframeArgumentsParameters)

    class VirtualGeometryViewerWireframeArgumentsCS : public GlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER_TYPE(VirtualGeometryViewerWireframeArgumentsCS)
        static void ModifyShaderCompilerEnvironment(const ShaderPermutationParameters&, ShaderCompilerEnvironment&) {}
        static const ShaderParametersMetadata* GetShaderParameterMetadata()
        {
            return VirtualGeometryWireframeArgumentsParameters::GetMetaData();
        }
    };
    IMPLEMENT_GLOBAL_SHADER_TYPE(VirtualGeometryViewerWireframeArgumentsCS, "VirtualGeometryViewerWireframeArgumentsCS",
                                 "/editor/VirtualGeometryWireframeArguments.sf", "MainCS", ERHIShaderFrequency::Compute);

    BEGIN_SHADER_PARAMETER_STRUCT(VirtualGeometryViewerReadbackParameters)
    RDG_TEXTURE_ACCESS(Color, ERHIResourceAccess::TransferSrc)
    RDG_BUFFER_ACCESS(Flags, ERHIResourceAccess::TransferSrc)
    RDG_BUFFER_ACCESS(Statistics, ERHIResourceAccess::TransferSrc)
    END_SHADER_PARAMETER_STRUCT(VirtualGeometryViewerReadbackParameters)

    class VirtualGeometryViewerWireframeVS : public GlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER_TYPE(VirtualGeometryViewerWireframeVS)
        static void ModifyShaderCompilerEnvironment(const ShaderPermutationParameters&, ShaderCompilerEnvironment& environment)
        {
            environment.SetDefine("VERTEX_SHADER", 1);
        }
        static const ShaderParametersMetadata* GetShaderParameterMetadata()
        {
            return Renderer::VirtualGeometryVertexFactoryParameters::GetMetaData();
        }
    };

    class VirtualGeometryViewerWireframePS : public GlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER_TYPE(VirtualGeometryViewerWireframePS)
        static const ShaderParametersMetadata* GetShaderParameterMetadata() { return nullptr; }
    };

    IMPLEMENT_GLOBAL_SHADER_TYPE(VirtualGeometryViewerWireframeVS, "VirtualGeometryViewerWireframeVS",
                                 "/editor/VirtualGeometryWireframe.sf", "MainVS", ERHIShaderFrequency::Vertex);
    IMPLEMENT_GLOBAL_SHADER_TYPE(VirtualGeometryViewerWireframePS, "VirtualGeometryViewerWireframePS",
                                 "/editor/VirtualGeometryWireframe.sf", "MainPS", ERHIShaderFrequency::Fragment);

    namespace
    {
        void AddWireframePass(RenderGraphBuilder& builder, const Engine::SceneView& view,
                              const Renderer::VirtualGeometryInstance& instance, const std::vector<uint32_t>& selected,
                              RenderGraphTextureRef color, RenderGraphTextureRef depth,
                              RenderGraphBufferRef triangleArguments = nullptr)
        {
            if (selected.empty())
            {
                return;
            }
            const auto& geometry = instance.RenderData->VirtualGeometryData;
            auto* edges = builder.AllocateParameter<std::vector<uint32_t>>();
            if (triangleArguments)
            {
                if (geometry.Indices.size() > UINT32_MAX / 2)
                {
                    throw std::invalid_argument("GPU wireframe index range exceeds 32 bits");
                }
                edges->resize(geometry.Indices.size() * 2);
            }
            for (const auto clusterIndex : selected)
            {
                const auto& cluster = geometry.Clusters[clusterIndex];
                if (!triangleArguments && uint64_t(edges->size()) + uint64_t(cluster.TriangleCount) * 6 > UINT32_MAX)
                {
                    throw std::invalid_argument("Virtual geometry wireframe exceeds the draw index range");
                }
                for (uint32_t triangle = 0; triangle < cluster.TriangleCount; ++triangle)
                {
                    const auto offset = cluster.IndexOffset + triangle * 3;
                    const uint32_t a = cluster.VertexOffset + geometry.Indices[offset];
                    const uint32_t b = cluster.VertexOffset + geometry.Indices[offset + 1];
                    const uint32_t c = cluster.VertexOffset + geometry.Indices[offset + 2];
                    if (triangleArguments)
                    {
                        const std::array<uint32_t, 6> line{a, b, b, c, c, a};
                        std::copy(line.begin(), line.end(), edges->begin() + offset * 2ull);
                    }
                    else
                    {
                        edges->insert(edges->end(), {a, b, b, c, c, a});
                    }
                }
            }
            RenderGraphBufferDesc indicesDesc;
            indicesDesc.Size = edges->size() * sizeof(uint32_t);
            indicesDesc.Stride = sizeof(uint32_t);
            indicesDesc.Usage = ERHIBufferUsageFlag::Index | ERHIBufferUsageFlag::TransferDst;
            auto indices = builder.CreateBuffer("VirtualGeometryViewer.WireframeIndices", indicesDesc);
            builder.AddUploadBuffer(indices, edges->data(), indicesDesc.Size);
            auto* params = builder.AllocateParameter<VirtualGeometryWireframePassParameters>();
            params->Vertex.LocalToWorld = instance.LocalToWorld;
            params->Vertex.ViewProjection = view.ViewProjectionMatrix;
            params->Positions = builder.RegisterExternalBuffer("VirtualGeometryViewer.WireframePositions", geometry.PositionBuffer.get());
            params->Indices = indices;
            if (triangleArguments)
            {
                RenderGraphBufferDesc argumentDesc;
                argumentDesc.Size = triangleArguments->GetDesc().Size;
                argumentDesc.Stride = 4;
                argumentDesc.Usage = ERHIBufferUsageFlag::Structured | ERHIBufferUsageFlag::UnorderedAccess |
                                     ERHIBufferUsageFlag::Indirect;
                params->Arguments = builder.CreateBuffer("VirtualGeometryViewer.WireframeArguments", argumentDesc);
                auto* args = builder.AllocateParameter<VirtualGeometryWireframeArgumentsParameters>();
                RenderGraphBufferSRVDesc srv;
                srv.Buffer = triangleArguments;
                srv.Stride = 4;
                srv.NumElements = argumentDesc.Size / 4;
                args->TriangleArguments = builder.CreateBufferSRV("VirtualGeometryViewer.TriangleArgs", srv);
                RenderGraphBufferUAVDesc uav;
                uav.Buffer = params->Arguments;
                uav.Stride = 4;
                uav.NumElements = argumentDesc.Size / 4;
                args->LineArguments = builder.CreateBufferUAV("VirtualGeometryViewer.LineArgs", uav);
                args->ArgumentCount = geometry.Clusters.size();
                auto* shader = GShaderMap.GetShader<VirtualGeometryViewerWireframeArgumentsCS>(0);
                if (!shader)
                {
                    throw std::runtime_error("GPU wireframe argument shader is unavailable");
                }
                RHIComputePipelineStateDesc pipeline;
                pipeline.ComputeShader = static_cast<RHIComputeShader*>(shader->GetRHIShader());
                auto* state = RHIPipelineStateCache::GetOrCreateComputePipelineState(pipeline);
                builder.AddPass("VirtualGeometryViewer.WireframeArguments",
                    VirtualGeometryWireframeArgumentsParameters::GetMetaData(), args, EPassFlag::Compute,
                    [=](RHICommandListBase& commands)
                {
                    auto& cmd = static_cast<RHIComputeCommandList&>(commands);
                    cmd.SetComputePipelineState(state);
                    SetShaderParameters(cmd, shader, args);
                    const uint32_t groups = (args->ArgumentCount + 63) / 64;
                    cmd.Dispatch(std::min(65535u, groups), (groups + 65534) / 65535, 1);
                });
            }
            params->Targets.NumColorRenderTargets = 1;
            params->Targets[0].Texture = color;
            params->Targets[0].Action = ERenderTargetActions::Load_Store;
            params->Targets.DepthStencil.Texture = depth;
            params->Targets.DepthStencil.DepthAction = ERenderTargetActions::Load_Store;
            auto factory = std::make_shared<Renderer::VirtualGeometryVertexFactory>(geometry);
            auto* vs = GShaderMap.GetShader<VirtualGeometryViewerWireframeVS>(0);
            auto* ps = GShaderMap.GetShader<VirtualGeometryViewerWireframePS>(0);
            if (!vs || !ps)
            {
                throw std::runtime_error("Virtual geometry viewer wireframe shaders are unavailable");
            }
            RHIGraphicsPipelineStateDesc pipelineDesc;
            pipelineDesc.ShaderStages.VertexShader = static_cast<RHIVertexShader*>(vs->GetRHIShader());
            pipelineDesc.ShaderStages.FragmentShader = static_cast<RHIFragmentShader*>(ps->GetRHIShader());
            pipelineDesc.VertexDescState = factory->GetRHIVertexDescState();
            RHIRasterizerStateDesc rasterizer;
            rasterizer.CullMode = ERHICullMode::None;
            pipelineDesc.RasterizerState = RHIPipelineStateCache::GetOrCreateRasterizerState(rasterizer);
            RHIDepthStencilStateDesc depthState;
            depthState.bDepthTestEnable = true;
            depthState.bDepthWriteEnable = false;
            depthState.DepthCompareOp = ERHICompareOp::LessOrEqual;
            pipelineDesc.DepthStencilState = RHIPipelineStateCache::GetOrCreateDepthStencilState(depthState);
            RHIColorBlendStateDesc blend;
            blend.Attachments.resize(1);
            pipelineDesc.ColorBlendState = RHIPipelineStateCache::GetOrCreateColorBlendState(blend);
            pipelineDesc.PrimitiveTopology = EPrimitiveTopology::LineList;
            pipelineDesc.AttachmentDesc.ColorAttachmentCount = 1;
            pipelineDesc.AttachmentDesc.ColorAttachments[0].Format = color->GetDesc().Format;
            pipelineDesc.AttachmentDesc.ColorAttachments[0].Actions = ERenderTargetActions::Load_Store;
            pipelineDesc.AttachmentDesc.bEnableDepth = true;
            pipelineDesc.AttachmentDesc.DepthStencilFormat = depth->GetDesc().Format;
            pipelineDesc.AttachmentDesc.DepthActions = ERenderTargetActions::Load_Store;
            auto* pipeline = RHIPipelineStateCache::GetOrCreateGraphicsPipelineState(pipelineDesc);
            builder.AddPass("VirtualGeometryViewer.Wireframe", VirtualGeometryWireframePassParameters::GetMetaData(),
                            params, EPassFlag::Graphic, [=](RHICommandListBase& commands)
            {
                auto& cmd = static_cast<RHIGraphicCommandList&>(commands);
                RHIRenderPassInfo pass;
                pass.RenderTargets = params->Targets.GetBoundRenderTarget();
                pass.RenderArea = {0, 0, uint32_t(view.Viewport.Width), uint32_t(view.Viewport.Height)};
                cmd.SetGraphicPipelineState(pipeline);
                factory->Bind(cmd);
                cmd.SetViewport(0, 0, view.Viewport.Width, view.Viewport.Height, 0, 1);
                cmd.SetScissor(0, 0, view.Viewport.Width, view.Viewport.Height);
                cmd.BeginRenderPass(pass);
                SetShaderParameters(cmd, vs, &params->Vertex);
                if (params->Arguments)
                {
                    for (const auto cluster : selected)
                    {
                        cmd.DrawIndexedIndirect(indices->GetRHIBuffer(), params->Arguments->GetRHIBuffer(),
                                                uint64_t(cluster) * sizeof(RHIDrawIndexedIndirectArguments));
                    }
                }
                else
                {
                    cmd.DrawIndexed(indices->GetRHIBuffer(), uint32_t(edges->size()), 1, 0, 0, 0);
                }
                cmd.EndRenderPass();
            });
        }
    }

    VirtualGeometryPreview::VirtualGeometryPreview() = default;
    VirtualGeometryPreview::~VirtualGeometryPreview() { Reset(); }

    void VirtualGeometryPreview::WaitForLastDraw()
    {
        const auto wait = [](const auto& resource)
        {
            if (!resource)
            {
                return;
            }
            const auto fence = resource->GetTracker().GetLastAccessFence();
            if (fence.Value)
            {
                RHI::GRHIApi->GetQueue(fence.QueueType)->WaitValue(fence.Value);
            }
        };
        wait(m_Color);
        if (m_Data)
        {
            const auto& geometry = m_Data->VirtualGeometryData;
            wait(geometry.PositionBuffer);
            wait(geometry.NormalBuffer);
            wait(geometry.TangentBuffer);
            wait(geometry.TexCoordBuffer);
            wait(geometry.ColorBuffer);
            wait(geometry.IndexBuffer);
            wait(geometry.ClusterBuffer);
            wait(geometry.GroupBuffer);
            wait(geometry.GroupChildClusterIndexBuffer);
            wait(geometry.GroupParentClusterIndexBuffer);
            wait(geometry.RootGroupIndexBuffer);
        }
    }

    void VirtualGeometryPreview::Reset()
    {
        WaitForLastDraw();
        if (m_LightScene && m_Light)
        {
            m_LightScene->RemoveLight(m_Light.get());
            m_LightScene->FlushPendingUpdates();
        }
        m_Light.reset();
        m_LightScene.reset();
        m_DefaultMaterial.reset();
        m_Color.reset();
        m_Data.reset();
        m_Asset.reset();
        m_SelectedClusters.clear();
        m_bGPUCapture = false;
    }

    const Engine::VirtualGeometryResource* VirtualGeometryPreview::GetGeometry() const
    {
        return m_Data ? &m_Data->VirtualGeometryData : nullptr;
    }

    void VirtualGeometryPreview::SetMesh(const std::shared_ptr<Engine::StaticMeshAsset>& asset)
    {
        if (asset == m_Asset)
        {
            return;
        }
        Reset();
        if (!asset || !asset->GetMesh())
        {
            return;
        }
        // Preview owns its derived geometry; the shared asset's traditional data is untouched.
        m_Data = std::make_shared<Engine::StaticMeshRenderData>();
        m_Data->VirtualGeometryData.Build(asset->GetMesh());
        m_Data->VirtualGeometryData.InitializeResources();
        m_Asset = asset;
        m_DefaultMaterial = std::make_unique<Engine::Material>();
        m_DefaultMaterial->SetShadingModel(Engine::EShadingModel::Lit);
        m_DefaultMaterial->SetParameter("BaseColor", std::array<float, 4>{0.7f, 0.72f, 0.75f, 1});
    }

    void VirtualGeometryPreview::Render(Core::Int2 size, uint32_t mipLevel, bool material,
                                       float yaw, float pitch, float distance, bool dynamicLOD, float targetPixelError,
                                       bool wireframe, bool gpuSelection, bool frustumCulling, bool captureGPU)
    {
        using namespace RenderCore;
        using namespace RHI;
        if (!m_Data || !m_Data->VirtualGeometryData.IsValid())
        {
            return;
        }
        if (!m_Color || m_Color->GetRHI()->GetDesc().Width != uint32_t(size.X) ||
            m_Color->GetRHI()->GetDesc().Height != uint32_t(size.Y))
        {
            WaitForLastDraw();
            RHITextureDesc desc;
            desc.Width = size.X;
            desc.Height = size.Y;
            desc.Format = ERHIFormat::R8G8B8A8_UNorm;
            desc.Usage = ERHITextureCreateFlag::RenderTarget | ERHITextureCreateFlag::ShaderResource |
                         ERHITextureCreateFlag::UAV | ERHITextureCreateFlag::TransferSrc;
            m_Color = std::make_unique<RenderTexture>(desc);
            m_Color->InitRHIResource();
        }
        const auto& bounds = m_Data->VirtualGeometryData.Bounds.Box;
        const auto center = (bounds.Min + bounds.Max) * 0.5f;
        const float scale = 1.0f / std::max(0.00001f, (bounds.Max - bounds.Min).Length() * 0.5f);
        Renderer::VirtualGeometryInstance instance;
        instance.RenderData = m_Data;
        instance.MipLevel = mipLevel;
        instance.bInspectGeneratedLevel = !dynamicLOD;
        instance.TargetPixelError = targetPixelError;
        instance.bGPUSelection = gpuSelection && dynamicLOD;
        instance.bFrustumCulling = frustumCulling;
        instance.LocalToWorld = Core::Mat4::Identity();
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            instance.LocalToWorld(axis, axis) = scale;
            instance.LocalToWorld(axis, 3) = -center[axis] * scale;
        }
        const auto* mesh = m_Asset->GetMesh();
        uint32_t slots = std::max<size_t>(1, mesh->GetNumMaterials());
        for (const auto& cluster : m_Data->VirtualGeometryData.Clusters)
        {
            if (cluster.MaterialIndex != Engine::INVALID_VIRTUAL_GEOMETRY_INDEX)
            {
                slots = std::max(slots, cluster.MaterialIndex + 1);
            }
        }
        for (uint32_t slot = 0; slot < slots; ++slot)
        {
            auto* source = mesh->GetMaterial(slot);
            instance.Materials.push_back((source ? source : m_DefaultMaterial.get())->GetRenderProxyShared());
        }
        Engine::SceneView view;
        view.Viewport = {0, 0, size.X, size.Y};
        view.NearClip = 0.05f;
        view.FarClip = 50.0f;
        view.CameraWorldPos = {distance * std::cos(pitch) * std::sin(yaw), distance * std::sin(pitch),
                               distance * std::cos(pitch) * std::cos(yaw)};
        view.ViewMatrix = Core::LookAtRH(view.CameraWorldPos, {0, 0, 0}, {0, 1, 0});
        view.ProjectionMatrix = Core::PerspectiveRhZo(Core::DegToRad(45.0f), float(size.X) / size.Y, 0.05f, 50);
        view.ViewProjectionMatrix = view.ProjectionMatrix * view.ViewMatrix;
        view.InvViewProjectionMatrix = Core::Inverse(view.ViewProjectionMatrix);
        view.BuildSplitDepths();
        RenderGraphBuilder builder;
        auto color = builder.RegisterExternalTexture("VirtualGeometryViewer.Color", m_Color.get());
        auto visibility = Renderer::AddVirtualGeometryRasterPass(builder, view, {instance}, size.X, size.Y);
        std::vector<uint32_t> candidates;
        for (const auto& draw : visibility->Draws)
        {
            candidates.push_back(draw.ClusterID);
        }
        if (!instance.bGPUSelection)
        {
            m_SelectedClusters = candidates;
            m_bGPUCapture = false;
        }
        else if (!m_bGPUCapture)
        {
            m_SelectedClusters.clear();
        }
        if (!material)
        {
            Renderer::AddVirtualGeometryVisibilityDebugPass(builder, visibility, color, view);
        }
        else
        {
            if (!m_LightScene)
            {
                m_LightScene = std::make_unique<Renderer::Scene>();
                m_Light = std::make_unique<Engine::DirectionalLightComponent>();
                m_Light->SetDirection({-0.4f, -0.8f, -0.5f});
                m_Light->SetIntensity(3.0f);
                m_Light->SetCastShadow(false);
                m_LightScene->AddLight(m_Light.get());
                m_LightScene->FlushPendingUpdates();
            }
            RenderGraphTextureDesc desc;
            desc.Width = size.X;
            desc.Height = size.Y;
            desc.Usage = ERHITextureCreateFlag::UAV | ERHITextureCreateFlag::ShaderResource;
            desc.Format = ERHIFormat::R8G8B8A8_UNorm;
            auto a = builder.CreateTexture("VirtualGeometryViewer.A", desc);
            auto c = builder.CreateTexture("VirtualGeometryViewer.C", desc);
            desc.Format = ERHIFormat::R16G16B16A16_Float;
            auto b = builder.CreateTexture("VirtualGeometryViewer.B", desc);
            auto resolved = Renderer::AddVirtualGeometryMaterialResolvePass(builder, view, visibility, a, b, c);
            Renderer::AddVirtualGeometryDeferredShadingPass(builder, view, m_LightScene.get(), resolved,
                                                           visibility->VirtualGeometryDepthTexture, color);
        }
        if (wireframe)
        {
            AddWireframePass(builder, view, instance, candidates, color, visibility->VirtualGeometryDepthTexture,
                             visibility->GPUSelections.front().IndirectArguments);
        }
        std::array<void*, 2> mapped{};
        const auto& selection = visibility->GPUSelections.front();
        const bool readGPU = instance.bGPUSelection && captureGPU;
        const bool immediate = GShaderPlatform == ERHIShaderPlatform::OpenGL;
        const auto read = [&](RHICommandListBase& cmd)
        {
            mapped[0] = GRHIApi->MapReadBuffer(cmd, selection.ClusterFlags->GetRHIBuffer(), {});
            mapped[1] = GRHIApi->MapReadBuffer(cmd, selection.Statistics->GetRHIBuffer(), {});
        };
        if (readGPU)
        {
            auto* params = builder.AllocateParameter<VirtualGeometryViewerReadbackParameters>();
            params->Color = color;
            params->Flags = selection.ClusterFlags;
            params->Statistics = selection.Statistics;
            builder.AddPass("VirtualGeometryViewer.CaptureGPU", VirtualGeometryViewerReadbackParameters::GetMetaData(),
                            params, EPassFlag::Graphic, [&](RHICommandListBase& cmd)
            {
                if (!immediate)
                {
                    read(cmd);
                }
            });
        }
        builder.Execute();
        if (readGPU)
        {
            const auto fence = m_Color->GetTracker().GetLastAccessFence();
            GRHIApi->GetQueue(fence.QueueType)->WaitValue(fence.Value);
            if (immediate)
            {
                auto* queue = GRHIApi->GetQueue(EQueueType::Graphics);
                auto* context = queue->AcquireCommandContext();
                RHICommandListBase cmd(context);
                cmd.SetImmediate(true);
                cmd.Begin();
                read(cmd);
                cmd.End();
                queue->WaitValue(queue->ExecuteContext(context));
            }
            if (!mapped[0] || !mapped[1])
            {
                for (auto* mapping : mapped)
                {
                    if (mapping)
                    {
                        GRHIApi->Unmap(mapping);
                    }
                }
                throw std::runtime_error("Virtual geometry GPU statistics readback failed");
            }
            const auto* flags = static_cast<const uint32_t*>(mapped[0]);
            const auto* statistics = static_cast<const uint32_t*>(mapped[1]);
            std::copy(statistics, statistics + 4, m_GPUStatistics.begin());
            const auto reference = Renderer::SelectVirtualGeometryClusters(m_Data->VirtualGeometryData,
                instance.LocalToWorld, view, targetPixelError);
            m_SelectedClusters.clear();
            m_CutDifferenceCount = 0;
            for (uint32_t c = 0; c < m_Data->VirtualGeometryData.Clusters.size(); ++c)
            {
                const bool expected = std::binary_search(reference.begin(), reference.end(), c);
                m_CutDifferenceCount += expected != bool(flags[c] & 1);
                if (flags[c] & 2)
                {
                    m_SelectedClusters.push_back(c);
                }
            }
            for (auto* mapping : mapped)
            {
                GRHIApi->Unmap(mapping);
            }
            m_bGPUCapture = true;
        }
    }
}
