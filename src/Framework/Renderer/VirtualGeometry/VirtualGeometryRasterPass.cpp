#include "VirtualGeometryRasterPass.h"
#include "VirtualGeometryLODSelection.h"
#include "VirtualGeometryVertexFactory.h"
#include "VirtualGeometryStreaming.h"
#include "MeshMaterialShader.h"
#include "GlobalShader.h"
#include "RHIPipelineStateCache.h"
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <cmath>

using namespace RenderCore;
using namespace RHI;

namespace Renderer
{
    BEGIN_SHADER_PARAMETER_STRUCT(VirtualGeometryRasterParameters)
    SHADER_PARAMETER_RENDER_TARGET_BINDING_SLOTS(Targets)
    RDG_BUFFER_ACCESS(Positions, ERHIResourceAccess::VertexOrIndexBuffer)
    RDG_BUFFER_ACCESS(Indices, ERHIResourceAccess::VertexOrIndexBuffer)
    RDG_BUFFER_ACCESS(Arguments, ERHIResourceAccess::IndirectArgs)
    END_SHADER_PARAMETER_STRUCT(VirtualGeometryRasterParameters)

    BEGIN_SHADER_PARAMETER_STRUCT(VirtualGeometryDrawUploadParameters)
    RDG_BUFFER_ACCESS(Metadata, ERHIResourceAccess::TransferDest)
    END_SHADER_PARAMETER_STRUCT(VirtualGeometryDrawUploadParameters)

    BEGIN_SHADER_PARAMETER_STRUCT(VirtualGeometryDebugParameters)
    SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint2>, Visibility)
    SHADER_PARAMETER_RDG_STRUCTURED_BUFFER(VirtualGeometryDrawMetadata, DrawMetadata)
    SHADER_PARAMETER(uint32_t, DrawCount)
    SHADER_PARAMETER_RENDER_TARGET_BINDING_SLOTS(Targets)
    END_SHADER_PARAMETER_STRUCT(VirtualGeometryDebugParameters)

    class VirtualGeometryRasterVS : public MeshMaterialShader
    {
    public:
        DECLARE_MESH_MATERIAL_SHADER_TYPE(VirtualGeometryRasterVS)

        static bool ShouldCompilePermutation(const ShaderPermutationParameters& parameters)
        {
            const auto& mesh = static_cast<const Engine::MeshMaterialShaderPermutationParameters&>(parameters);
            return mesh.VFType == &VirtualGeometryVertexFactory::StaticType &&
                   mesh.MaterialParams.ShadingModel == Engine::EShadingModel::Unlit;
        }

        static void ModifyShaderCompilerEnvironment(const ShaderPermutationParameters& parameters,
                                                    ShaderCompilerEnvironment& environment)
        {
            environment.SetDefine("VERTEX_SHADER", 1);
        }

        static const ShaderParametersMetadata* GetShaderParameterMetadata()
        {
            return VirtualGeometryVertexFactoryParameters::GetMetaData();
        }
    };

    IMPLEMENT_MESH_MATERIAL_SHADER_TYPE(VirtualGeometryRasterVS, "VirtualGeometryRasterVS",
                                        "/virtualgeometry/RasterVS.sf", "MainVS", ERHIShaderFrequency::Vertex);

    class VirtualGeometryRasterPS : public MeshMaterialShader
    {
    public:
        DECLARE_MESH_MATERIAL_SHADER_TYPE(VirtualGeometryRasterPS)

        static bool ShouldCompilePermutation(const ShaderPermutationParameters& parameters)
        {
            const auto& mesh = static_cast<const Engine::MeshMaterialShaderPermutationParameters&>(parameters);
            return mesh.VFType == &VirtualGeometryVertexFactory::StaticType &&
                   mesh.MaterialParams.ShadingModel == Engine::EShadingModel::Unlit;
        }

        static void ModifyShaderCompilerEnvironment(const ShaderPermutationParameters& parameters,
                                                    ShaderCompilerEnvironment& environment)
        {
            environment.SetDefine("VERTEX_SHADER", 0);
        }

        static const ShaderParametersMetadata* GetShaderParameterMetadata()
        {
            return VirtualGeometryRasterParameters::GetMetaData();
        }
    };

    IMPLEMENT_MESH_MATERIAL_SHADER_TYPE(VirtualGeometryRasterPS, "VirtualGeometryRasterPS",
                                        "/virtualgeometry/RasterPS.sf", "MainPS", ERHIShaderFrequency::Fragment);

    class VirtualGeometryClearVS : public GlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER_TYPE(VirtualGeometryClearVS)

        static bool ShouldCompilePermutation(const ShaderPermutationParameters& parameters)
        {
            return true;
        }

        static void ModifyShaderCompilerEnvironment(const ShaderPermutationParameters& parameters,
                                                    ShaderCompilerEnvironment& environment)
        {
            environment.SetDefine("VERTEX_SHADER", 1);
        }

        static const ShaderParametersMetadata* GetShaderParameterMetadata()
        {
            return VirtualGeometryRasterParameters::GetMetaData();
        }
    };

    IMPLEMENT_GLOBAL_SHADER_TYPE(VirtualGeometryClearVS, "VirtualGeometryClearVS", "/virtualgeometry/Clear.sf",
                                 "MainVS", ERHIShaderFrequency::Vertex);

    class VirtualGeometryClearPS : public GlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER_TYPE(VirtualGeometryClearPS)

        static bool ShouldCompilePermutation(const ShaderPermutationParameters& parameters)
        {
            return true;
        }

        static void ModifyShaderCompilerEnvironment(const ShaderPermutationParameters& parameters,
                                                    ShaderCompilerEnvironment& environment)
        {
            environment.SetDefine("VERTEX_SHADER", 0);
        }

        static const ShaderParametersMetadata* GetShaderParameterMetadata()
        {
            return VirtualGeometryRasterParameters::GetMetaData();
        }
    };

    IMPLEMENT_GLOBAL_SHADER_TYPE(VirtualGeometryClearPS, "VirtualGeometryClearPS", "/virtualgeometry/Clear.sf",
                                 "MainPS", ERHIShaderFrequency::Fragment);

    class VirtualGeometryDebugPS : public GlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER_TYPE(VirtualGeometryDebugPS)

        static bool ShouldCompilePermutation(const ShaderPermutationParameters& parameters)
        {
            return true;
        }

        static void ModifyShaderCompilerEnvironment(const ShaderPermutationParameters& parameters,
                                                    ShaderCompilerEnvironment& environment)
        {
            environment.SetDefine("VERTEX_SHADER", 0);
        }

        static const ShaderParametersMetadata* GetShaderParameterMetadata()
        {
            return VirtualGeometryDebugParameters::GetMetaData();
        }
    };

    IMPLEMENT_GLOBAL_SHADER_TYPE(VirtualGeometryDebugPS, "VirtualGeometryDebugPS", "/virtualgeometry/Debug.sf",
                                 "MainPS", ERHIShaderFrequency::Fragment);

    namespace
    {
        RHIGraphicsPipelineState* Pipeline(Shader* vs, Shader* ps, RHIVertexDescState* vertexState, ERHIFormat format,
                                           bool depth, bool clear)
        {
            if (!vs || !ps)
            {
                throw std::runtime_error("Virtual geometry shader compilation failed: " +
                                         Engine::GMeshMaterialShaderMap.GetLastError());
            }
            RHIGraphicsPipelineStateDesc desc;
            desc.ShaderStages.VertexShader = dynamic_cast<RHIVertexShader*>(vs->GetRHIShader());
            desc.ShaderStages.FragmentShader = dynamic_cast<RHIFragmentShader*>(ps->GetRHIShader());
            desc.VertexDescState = vertexState;
            RHIRasterizerStateDesc rasterizer;
            rasterizer.CullMode = ERHICullMode::None;
            desc.RasterizerState = RHIPipelineStateCache::GetOrCreateRasterizerState(rasterizer);
            RHIDepthStencilStateDesc depthState;
            depthState.bDepthTestEnable = depth;
            depthState.bDepthWriteEnable = depth;
            depthState.DepthCompareOp = clear ? ERHICompareOp::Always : ERHICompareOp::Less;
            desc.DepthStencilState = RHIPipelineStateCache::GetOrCreateDepthStencilState(depthState);
            RHIColorBlendStateDesc blend;
            blend.Attachments.resize(1);
            blend.Attachments[0].bBlendEnable = false;
            desc.ColorBlendState = RHIPipelineStateCache::GetOrCreateColorBlendState(blend);
            desc.AttachmentDesc.ColorAttachmentCount = 1;
            desc.AttachmentDesc.ColorAttachments[0].Format = format;
            desc.AttachmentDesc.ColorAttachments[0].Actions =
                clear ? ERenderTargetActions::DontCare_Store : ERenderTargetActions::Load_Store;
            desc.AttachmentDesc.bEnableDepth = depth;
            desc.AttachmentDesc.DepthStencilFormat = depth ? ERHIFormat::D32_Float : ERHIFormat::Unknown;
            desc.AttachmentDesc.DepthActions =
                clear ? ERenderTargetActions::Clear_Store : ERenderTargetActions::Load_Store;
            return RHIPipelineStateCache::GetOrCreateGraphicsPipelineState(desc);
        }

        RHIRenderPassInfo RenderPass(const RenderTargetBindingSlots& targets, uint32_t width, uint32_t height)
        {
            RHIRenderPassInfo info;
            info.RenderTargets = targets.GetBoundRenderTarget();
            info.RenderTargets.DepthStencil.ClearBinding.Depth = 1.0f;
            info.RenderArea.Width = width;
            info.RenderArea.Height = height;
            return info;
        }

        struct RasterInstance
        {
            VirtualGeometryInstance Instance;
            std::shared_ptr<VirtualGeometryVertexFactory> Factory;
            uint32_t FirstDraw = 0;
            std::vector<uint32_t> Clusters;
            VirtualGeometryGPUSelection GPUSelection;
            VirtualGeometryRasterRouting Routing;
        };
    } // namespace

    static std::shared_ptr<VirtualGeometryRasterOutput> AddVirtualGeometryRasterPassInternal(
        RenderGraphBuilder& builder, const Engine::SceneView& view,
        const std::vector<VirtualGeometryInstance>& instances, uint32_t width, uint32_t height,
        RenderGraphTextureRef sceneDepth, const VirtualGeometryRasterSettings& settings)
    {
        if (!width || !height || view.Viewport.X < 0 || view.Viewport.Y < 0 || view.Viewport.Width <= 0 ||
            view.Viewport.Height <= 0 || uint64_t(view.Viewport.X) + uint64_t(view.Viewport.Width) > width ||
            uint64_t(view.Viewport.Y) + uint64_t(view.Viewport.Height) > height)
        {
            throw std::invalid_argument("Invalid virtual geometry viewport");
        }
        auto output = std::make_shared<VirtualGeometryRasterOutput>();
        const bool software = settings.Mode != EVirtualGeometryRasterMode::Hardware;
        if (static_cast<uint32_t>(settings.Mode) > 2 || !std::isfinite(settings.SoftwareClusterPixels) ||
            settings.SoftwareClusterPixels <= 0 || settings.SoftwareClusterPixels > 128 || uint64_t(width) * height > UINT32_MAX)
        {
            throw std::invalid_argument("Invalid virtual geometry software raster settings or target size");
        }
        if (software && (GRHIApi->GetPlatformInfo().RasterSubPixelBits < 4 || GRHIApi->GetPlatformInfo().RasterSubPixelBits > 8))
        {
            throw std::invalid_argument("Virtual geometry software raster requires reported 4 to 8 bit subpixel precision");
        }
        output->Instances = instances;
        output->GPUSelections.resize(instances.size());
        output->RasterRouting.resize(instances.size());
        // Graph-owned parameters remain unique until execution completes, including across views.
        auto* clearParams = builder.AllocateParameter<VirtualGeometryRasterParameters>();
        const auto prefix = "VirtualGeometry." + std::to_string(reinterpret_cast<uintptr_t>(clearParams)) + ".";
        RenderGraphTextureDesc visibility;
        visibility.Width = width;
        visibility.Height = height;
        visibility.Format = ERHIFormat::R32G32_UInt;
        visibility.Usage = ERHITextureCreateFlag::RenderTarget | ERHITextureCreateFlag::ShaderResource |
                           ERHITextureCreateFlag::TransferSrc;
        output->VirtualGeometryVisibilityTexture =
            builder.CreateTexture(prefix + "VirtualGeometryVisibilityTexture", visibility);
        RenderGraphTextureDesc depth = visibility;
        depth.Format = ERHIFormat::D32_Float;
        depth.Usage = ERHITextureCreateFlag::DepthStencil | ERHITextureCreateFlag::ShaderResource |
                      ERHITextureCreateFlag::TransferSrc;
        output->VirtualGeometryDepthTexture =
            sceneDepth ? sceneDepth : builder.CreateTexture(prefix + "VirtualGeometryDepthTexture", depth);
        auto hardwareVisibility = software ? builder.CreateTexture(prefix + "HardwareVisibility", visibility) :
                                             output->VirtualGeometryVisibilityTexture;
        auto hardwareDepth = software ? builder.CreateTexture(prefix + "HardwareDepth", depth) :
                                       output->VirtualGeometryDepthTexture;

        std::vector<RasterInstance> rasterInstances;
        for (const auto& instance : instances)
        {
            if (software && (!instance.bGPUSelection || instance.bInspectGeneratedLevel))
            {
                throw std::invalid_argument("Software raster requires GPU DAG selection, not generated-level inspection");
            }
            if (!instance.RenderData)
            {
                throw std::invalid_argument("Virtual geometry instance has no render data");
            }
            const auto& geometry = instance.RenderData->VirtualGeometryData;
            if (instance.Streaming)
            {
                if (&instance.Streaming->GetResource() != &geometry || !instance.bGPUSelection || instance.bInspectGeneratedLevel)
                {
                    throw std::invalid_argument("Streaming requires its matching resource and GPU DAG selection");
                }
                instance.Streaming->Prepare(builder);
            }
            else if (geometry.StreamingPageSlots)
            {
                throw std::invalid_argument("Paged geometry requires its streaming cache on every instance");
            }
            if (!geometry.IsValid() || !geometry.IndexBuffer || !geometry.IndexBuffer->GetRHI())
            {
                throw std::invalid_argument("Virtual geometry must be built and uploaded before rasterization");
            }
            auto it = std::find(output->Resources.begin(), output->Resources.end(), instance.RenderData);
            uint32_t resourceIndex = static_cast<uint32_t>(it - output->Resources.begin());
            if (it == output->Resources.end())
            {
                output->Resources.push_back(instance.RenderData);
            }
            rasterInstances.push_back({instance, std::make_shared<VirtualGeometryVertexFactory>(geometry),
                                       static_cast<uint32_t>(output->Draws.size())});
            auto& selected = rasterInstances.back().Clusters;
            if (instance.bGPUSelection && !instance.bInspectGeneratedLevel)
            {
                auto& selection = rasterInstances.back().GPUSelection;
                selection = AddVirtualGeometryGPUSelectionPass(builder, geometry, instance.LocalToWorld, view,
                                                               instance.TargetPixelError, instance.bFrustumCulling,
                                                               instance.Streaming.get());
                output->GPUSelections[rasterInstances.size() - 1] = selection;
                if (software)
                {
                    auto& routing = rasterInstances.back().Routing;
                    routing = AddVirtualGeometryRasterRouting(builder, geometry, selection, instance.LocalToWorld, view, settings);
                    output->RasterRouting[rasterInstances.size() - 1] = routing;
                }
                // Stable DrawIDs for all candidates; GPU rejects draws by writing an index count of zero.
                for (uint32_t c = 0; c < geometry.Clusters.size(); ++c)
                {
                    selected.push_back(c);
                }
            }
            else if (instance.bInspectGeneratedLevel)
            {
                for (uint32_t c = 0; c < geometry.Clusters.size(); ++c)
                {
                    if (geometry.Clusters[c].MipLevel == instance.MipLevel)
                    {
                        selected.push_back(c);
                    }
                }
            }
            else
            {
                selected = SelectVirtualGeometryClusters(geometry, instance.LocalToWorld, view, instance.TargetPixelError);
            }
            for (const auto c : selected)
            {
                const auto& cluster = geometry.Clusters[c];
                if (output->Draws.size() >= UINT32_MAX || cluster.VertexOffset > INT32_MAX ||
                    uint64_t(cluster.IndexOffset) + cluster.TriangleCount * 3ull > geometry.Indices.size() ||
                    uint64_t(cluster.VertexOffset) + cluster.VertexCount > geometry.Positions.size())
                {
                    throw std::runtime_error("Invalid virtual geometry draw range");
                }
                VirtualGeometryDrawMetadata metadata{};
                metadata.InstanceID = instance.InstanceID;
                metadata.ClusterID = c;
                metadata.ResourceIndex = resourceIndex;
                metadata.InstanceIndex = static_cast<uint32_t>(rasterInstances.size() - 1);
                output->Draws.push_back(metadata);
            }
        }

        if (software && output->Draws.size() > 0x01ffffffu)
        {
            throw std::invalid_argument("Software visibility exceeds its 25-bit DrawID and 7-bit triangle range");
        }
        RenderGraphBufferDesc metadataDesc;
        metadataDesc.Size = std::max<size_t>(1, output->Draws.size()) * sizeof(VirtualGeometryDrawMetadata);
        if (metadataDesc.Size > UINT32_MAX)
        {
            throw std::invalid_argument("Virtual geometry draw metadata exceeds the upload byte range");
        }
        metadataDesc.Stride = sizeof(VirtualGeometryDrawMetadata);
        metadataDesc.Usage =
            ERHIBufferUsageFlag::Structured | ERHIBufferUsageFlag::ShaderResource | ERHIBufferUsageFlag::TransferDst;
        auto metadata = builder.CreateBuffer(prefix + "VirtualGeometryDrawMetadata", metadataDesc);
        auto* snapshot = builder.AllocateParameter<std::vector<VirtualGeometryDrawMetadata>>();
        *snapshot = output->Draws;
        if (snapshot->empty())
        {
            snapshot->emplace_back();
        }
        auto* metadataUpload = builder.AllocateParameter<VirtualGeometryDrawUploadParameters>();
        metadataUpload->Metadata = metadata;
        builder.AddPass("VirtualGeometryDrawMetadataUpload", VirtualGeometryDrawUploadParameters::GetMetaData(),
                        metadataUpload, EPassFlag::Graphic, [=](RHICommandListBase& cmd)
        {
            RHIUpdateBufferRegion region;
            region.Size = metadataDesc.Size;
            GRHIApi->UpdateBuffer(cmd, metadataUpload->Metadata->GetRHIBuffer(), snapshot->data(), region);
        });
        RenderGraphBufferSRVDesc srv;
        srv.Buffer = metadata;
        srv.Stride = sizeof(VirtualGeometryDrawMetadata);
        srv.NumElements = snapshot->size();
        output->DrawMetadata = builder.CreateBufferSRV(prefix + "VirtualGeometryDrawMetadataSRV", srv);

        auto clearVS = GShaderMap.GetShader<VirtualGeometryClearVS>(0);
        auto clearPS = GShaderMap.GetShader<VirtualGeometryClearPS>(0);
        auto emptyState = RHIPipelineStateCache::GetOrCreateVertexDescState({});
        auto clearPipeline = Pipeline(clearVS, clearPS, emptyState, visibility.Format, true, true);
        clearParams->Targets.NumColorRenderTargets = 1;
        clearParams->Targets[0].Texture = hardwareVisibility;
        clearParams->Targets[0].Action = ERenderTargetActions::DontCare_Store;
        clearParams->Targets.DepthStencil.Texture = hardwareDepth;
        builder.AddPass("VirtualGeometryVisibilityClear", VirtualGeometryRasterParameters::GetMetaData(), clearParams,
                        EPassFlag::Graphic, [=](RHICommandListBase& list)
        {
            auto& cmd = static_cast<RHIGraphicCommandList&>(list);
            cmd.SetGraphicPipelineState(clearPipeline);
            cmd.SetViewport(0, 0, width, height, 0, 1);
            cmd.SetScissor(0, 0, width, height);
            cmd.BeginRenderPass(RenderPass(clearParams->Targets, width, height));
            cmd.Draw(3);
            cmd.EndRenderPass();
        });

        for (const auto& raster : rasterInstances)
        {
            Engine::MeshMaterialShaderKey key;
            key.VF = &VirtualGeometryVertexFactory::StaticType;
            key.ShaderType = static_cast<Engine::MeshMaterialShaderType*>(VirtualGeometryRasterVS::GetStaticType());
            auto vs = Engine::GMeshMaterialShaderMap.GetShader(key);
            key.ShaderType = static_cast<Engine::MeshMaterialShaderType*>(VirtualGeometryRasterPS::GetStaticType());
            auto ps = Engine::GMeshMaterialShaderMap.GetShader(key);
            auto pipeline = Pipeline(vs, ps, raster.Factory->GetRHIVertexDescState(), visibility.Format, true, false);
            auto* params = builder.AllocateParameter<VirtualGeometryRasterParameters>();
            params->Targets = clearParams->Targets;
            params->Targets[0].Action = ERenderTargetActions::Load_Store;
            params->Targets.DepthStencil.DepthAction = ERenderTargetActions::Load_Store;
            const auto& geometry = raster.Instance.RenderData->VirtualGeometryData;
            const auto resourceName = prefix + std::to_string(reinterpret_cast<uintptr_t>(&geometry));
            auto positions = builder.RegisterExternalBuffer(resourceName + ".Positions", geometry.PositionBuffer.get());
            auto indices = builder.RegisterExternalBuffer(resourceName + ".Indices", geometry.IndexBuffer.get());
            params->Positions = positions;
            params->Indices = indices;
            params->Arguments = software ? raster.Routing.HardwareArguments : raster.GPUSelection.IndirectArguments;
            builder.AddPass("VirtualGeometryRasterPass", VirtualGeometryRasterParameters::GetMetaData(),
                                         params, EPassFlag::Graphic, [=](RHICommandListBase& list)
            {
                auto& cmd = static_cast<RHIGraphicCommandList&>(list);
                const auto& data = raster.Instance.RenderData->VirtualGeometryData;
                cmd.SetGraphicPipelineState(pipeline);
                raster.Factory->Bind(cmd);
                cmd.BeginRenderPass(RenderPass(params->Targets, width, height));
                // BeginRenderPass establishes backend defaults; apply the requested sub-view afterwards.
                cmd.SetViewport(view.Viewport.X, view.Viewport.Y, view.Viewport.Width, view.Viewport.Height, 0, 1);
                cmd.SetScissor(view.Viewport.X, view.Viewport.Y, view.Viewport.Width, view.Viewport.Height);
                uint32_t draw = raster.FirstDraw;
                for (const auto clusterIndex : raster.Clusters)
                {
                    const auto& cluster = data.Clusters[clusterIndex];
                    VirtualGeometryVertexFactoryParameters vertexParams{};
                    vertexParams.LocalToWorld = raster.Instance.LocalToWorld;
                    vertexParams.ViewProjection = view.ViewProjectionMatrix;
                    vertexParams.DrawID = draw++;
                    SetShaderParameters(cmd, vs, &vertexParams);
                    if (raster.GPUSelection.IndirectArguments)
                    {
                        cmd.DrawIndexedIndirect(data.IndexBuffer->GetRHI(),
                            params->Arguments->GetRHIBuffer(),
                            uint64_t(clusterIndex) * sizeof(RHIDrawIndexedIndirectArguments));
                    }
                    else
                    {
                        cmd.DrawIndexed(data.IndexBuffer->GetRHI(), cluster.TriangleCount * 3, 1, cluster.IndexOffset,
                                        static_cast<int32_t>(cluster.VertexOffset), 0);
                    }
                }
                cmd.EndRenderPass();
            });
        }
        if (software)
        {
            const auto targets = AddVirtualGeometrySoftwareClear(builder, {int(width), int(height)});
            output->SoftwareTargets = targets;
            // Finish every instance's depth reduction before ANY instance resolves the winning visibility ID.
            for (const bool writeVisibility : {false, true})
            {
                for (const auto& raster : rasterInstances)
                {
                    AddVirtualGeometrySoftwareRaster(builder, raster.Instance.RenderData->VirtualGeometryData,
                        raster.Routing, targets, raster.Instance.LocalToWorld, view, raster.FirstDraw, writeVisibility);
                }
            }
            AddVirtualGeometryRasterMerge(builder, view, targets, hardwareVisibility, hardwareDepth,
                output->VirtualGeometryVisibilityTexture, output->VirtualGeometryDepthTexture);
        }
        std::vector<VirtualGeometryStreaming*> feedbackCaches;
        for (const auto& instance : instances)
        {
            if (instance.Streaming && std::find(feedbackCaches.begin(), feedbackCaches.end(), instance.Streaming.get()) == feedbackCaches.end())
            {
                instance.Streaming->AddFeedbackReadback(builder);
                feedbackCaches.push_back(instance.Streaming.get());
            }
        }
        return output;
    }

    std::shared_ptr<VirtualGeometryRasterOutput> AddVirtualGeometryRasterPass(
        RenderGraphBuilder& builder, const Engine::SceneView& view,
        const std::vector<VirtualGeometryInstance>& instances, uint32_t width, uint32_t height,
        const VirtualGeometryRasterSettings& settings)
    {
        return AddVirtualGeometryRasterPassInternal(builder, view, instances, width, height, nullptr, settings);
    }

    std::shared_ptr<VirtualGeometryRasterOutput> AddVirtualGeometryRasterPass(
        RenderGraphBuilder& builder, const Engine::SceneView& view,
        const std::vector<VirtualGeometryInstance>& instances, RenderGraphTextureRef sceneDepth,
        const VirtualGeometryRasterSettings& settings)
    {
        if (!sceneDepth)
        {
            throw std::invalid_argument("Virtual geometry raster requires a scene depth target");
        }
        const auto& desc = sceneDepth->GetDesc();
        if (desc.Format != ERHIFormat::D32_Float || desc.Type != ERHITextureType::Texture2D ||
            desc.SampleCount != 1 || desc.ArraySize != 1 ||
            !desc.Usage.HasAll(ERHITextureCreateFlag::DepthStencil | ERHITextureCreateFlag::ShaderResource))
        {
            throw std::invalid_argument("Virtual geometry scene depth requires shader-readable D32 depth usage");
        }
        return AddVirtualGeometryRasterPassInternal(builder, view, instances, desc.Width, desc.Height, sceneDepth, settings);
    }

    void AddVirtualGeometryVisibilityDebugPass(RenderGraphBuilder& builder,
                                               const std::shared_ptr<VirtualGeometryRasterOutput>& output,
                                               RenderGraphTextureRef target, const Engine::SceneView& view)
    {
        if (!output || !target)
        {
            throw std::invalid_argument("Missing virtual geometry debug target");
        }
        auto vs = GShaderMap.GetShader<VirtualGeometryClearVS>(0);
        auto ps = GShaderMap.GetShader<VirtualGeometryDebugPS>(0);
        auto vertexState = RHIPipelineStateCache::GetOrCreateVertexDescState({});
        auto pipeline = Pipeline(vs, ps, vertexState, target->GetDesc().Format, false, false);
        auto* params = builder.AllocateParameter<VirtualGeometryDebugParameters>();
        params->Visibility = output->VirtualGeometryVisibilityTexture;
        params->DrawMetadata = output->DrawMetadata;
        params->DrawCount = static_cast<uint32_t>(output->Draws.size());
        params->Targets.NumColorRenderTargets = 1;
        params->Targets[0].Texture = target;
        params->Targets[0].Action = ERenderTargetActions::Load_Store;
        builder.AddPass("VirtualGeometryVisibilityDebugPass", VirtualGeometryDebugParameters::GetMetaData(), params,
                        EPassFlag::Graphic, [=](RHICommandListBase& list)
        {
            auto& cmd = static_cast<RHIGraphicCommandList&>(list);
            cmd.SetGraphicPipelineState(pipeline);
            cmd.SetViewport(view.Viewport.X, view.Viewport.Y, view.Viewport.Width, view.Viewport.Height, 0, 1);
            cmd.SetScissor(view.Viewport.X, view.Viewport.Y, view.Viewport.Width, view.Viewport.Height);
            SetShaderParameters(cmd, ps, params);
            cmd.BeginRenderPass(RenderPass(params->Targets, target->GetDesc().Width, target->GetDesc().Height));
            cmd.Draw(3);
            cmd.EndRenderPass();
        });
    }
} // namespace Renderer
