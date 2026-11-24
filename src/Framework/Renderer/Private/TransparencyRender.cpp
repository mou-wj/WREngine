#include "TransparencyRender.h"
#include "GlobalShader.h"
#include "RenderGraphBuilder.h"
#include "RHIPipelineStateCache.h"
#include "Scene.h"
#include "SceneRenderer.h"
#include "SceneView.h"
#include "StaticMeshComponent.h"
#include "StaticMeshMaterialShader.h"
#include "StaticMeshProcess.h"
#include "StaticMeshProxy.h"
#include <algorithm>
#include <tuple>

namespace Renderer
{
    using namespace Engine;
    using namespace RenderCore;
    using namespace RHI;

    BEGIN_SHADER_PARAMETER_STRUCT(TransparencyBackgroundParameters)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, TransparencySourceColor)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, TransparencySourceDepth)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, TransparencyBackgroundOutput)
        SHADER_PARAMETER(Core::UInt2, TransparencyTargetSize)
    END_SHADER_PARAMETER_STRUCT(TransparencyBackgroundParameters)

    class TransparencyBackgroundCS : public GlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER_TYPE(TransparencyBackgroundCS)

        static const ShaderParametersMetadata* GetShaderParameterMetadata()
        {
            return TransparencyBackgroundParameters::GetMetaData();
        }
    };

    IMPLEMENT_GLOBAL_SHADER_TYPE(TransparencyBackgroundCS, "TransparencyBackgroundCS",
        "/material/TransparencyBackground.sf", "MainCS", ERHIShaderFrequency::Compute);

    BEGIN_SHADER_PARAMETER_STRUCT(TransparencyPixelParameters)
        SHADER_PARAMETER_STRUCT_REFERENCE(SceneShaderParameters, Scene)
        SHADER_PARAMETER_STRUCT_REFERENCE(SceneEvnIBLLightParameters, Environment)
        SHADER_PARAMETER_STRUCT_REFERENCE(LocalVertexFactoryParameters, VertexFactory)
        SHADER_PARAMETER_STRUCT_REFERENCE(MaterialShaderParameters, Material)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, TransparencyBackground)
        SHADER_PARAMETER(Core::Float4, TransparencyOptics)
        SHADER_PARAMETER(Core::Float3, TransparencyAbsorption)
        SHADER_PARAMETER(uint32_t, TransparencyDebugView)
        SHADER_PARAMETER(Core::Float4, TransparencyViewport)
        SHADER_PARAMETER(Core::Float2, TransparencyPixelSize)
        SHADER_PARAMETER(uint32_t, TransparencyUseEffectiveDepth)
        SHADER_PARAMETER(float, TransparencySortRank)
    END_SHADER_PARAMETER_STRUCT(TransparencyPixelParameters)

    class TransparencyMaterialPS : public MeshMaterialShader
    {
    public:
        static constexpr char WeightedDefine[] = "WEIGHTED_TRANSPARENCY";
        using WeightedPermutation = FPermutationDimensionBool<WeightedDefine>;
        using PermutationDomain = ShaderPermutationDomain<WeightedPermutation>;
        DECLARE_MESH_MATERIAL_SHADER_TYPE(TransparencyMaterialPS)

        static const ShaderParametersMetadata* GetShaderParameterMetadata()
        {
            return TransparencyPixelParameters::GetMetaData();
        }

        static bool ShouldCompilePermutation(const ShaderPermutationParameters& parameters)
        {
            return static_cast<const MeshMaterialShaderPermutationParameters&>(parameters).VFType != nullptr;
        }

        static void ModifyShaderCompilerEnvironment(const ShaderPermutationParameters& parameters, ShaderCompilerEnvironment& environment)
        {
            environment.SetDefine("PIXEL_SHADER", 1);
            PermutationDomain domain;
            domain.SetFromId(parameters.PermutationId);
            domain.ModifyCompilationEnvironment(environment);
            const auto platform = static_cast<ERHIShaderPlatform>(parameters.Platform);
            environment.SetDefine("TRANSPARENCY_RASTER_Y_SIGN", platform == ERHIShaderPlatform::D3D12 ? -1 : 1);
            Engine::ModifyShaderCompilerEnvironment(
                static_cast<const MeshMaterialShaderPermutationParameters&>(parameters).MaterialParams, environment);
        }
    };

    IMPLEMENT_MESH_MATERIAL_SHADER_TYPE(TransparencyMaterialPS, "TransparencyMaterialPS",
        "/material/TransparencyMaterialPS.sf", "MainPS", ERHIShaderFrequency::Fragment);

    BEGIN_SHADER_PARAMETER_STRUCT(TransparencyPassParameters)
        SHADER_PARAMETER_STRUCT_REFERENCE(StaticMeshMaterialShaderVSParameters, Vertex)
        SHADER_PARAMETER_STRUCT_REFERENCE(TransparencyPixelParameters, Pixel)
        SHADER_PARAMETER_RENDER_TARGET_BINDING_SLOTS(Targets)
    END_SHADER_PARAMETER_STRUCT(TransparencyPassParameters)

    BEGIN_SHADER_PARAMETER_STRUCT(TransparencyCompositeParameters)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, WeightedRadiance)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, WeightedCoverage)
        SHADER_PARAMETER_RENDER_TARGET_BINDING_SLOTS(Targets)
    END_SHADER_PARAMETER_STRUCT(TransparencyCompositeParameters)

    class TransparencyCompositeVS : public GlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER_TYPE(TransparencyCompositeVS)

        static const ShaderParametersMetadata* GetShaderParameterMetadata()
        {
            return nullptr;
        }
    };

    class TransparencyCompositePS : public GlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER_TYPE(TransparencyCompositePS)

        static const ShaderParametersMetadata* GetShaderParameterMetadata()
        {
            return TransparencyCompositeParameters::GetMetaData();
        }
    };

    IMPLEMENT_GLOBAL_SHADER_TYPE(TransparencyCompositeVS, "TransparencyCompositeVS",
        "/tools/FullScreenVS.sf", "MainVS", ERHIShaderFrequency::Vertex);
    IMPLEMENT_GLOBAL_SHADER_TYPE(TransparencyCompositePS, "TransparencyCompositePS",
        "/material/TransparencyCompositePS.sf", "MainPS", ERHIShaderFrequency::Fragment);

    namespace
    {
        struct TransparentDraw
        {
            std::shared_ptr<const MaterialRenderProxy> Material;
            RenderCore::VertexFactory* VertexFactory = nullptr;
            RenderBuffer* IndexBuffer = nullptr;
            Core::Mat4 LocalToWorld;
            MeshBatchElement Indices;
            float SortDepth = 0.0f;
            int32_t PrimitiveId = 0;
        };

        float Scalar(const MaterialRenderProxy& material, const char* name)
        {
            return std::get<float>(material.GetParameter(name, EMaterialParameterType::Scalar)->GetValueVariant());
        }

        std::vector<TransparentDraw> GatherTransparentDraws(Scene& scene, const SceneView& view)
        {
            std::vector<TransparentDraw> draws;
            for (auto* primitive : scene.GatherVisiblePrimitives(view))
            {
                if (!primitive->IsA<StaticMeshProxy>())
                {
                    continue;
                }

                const auto* component = static_cast<const StaticMeshProxy*>(primitive)->GetStaticMeshComponent();
                const auto* mesh = component->GetStaticMesh();
                const auto& renderData = mesh->GetRenderData();
                if (!renderData || renderData->GetLODCount() == 0)
                {
                    continue;
                }
                const auto& lod = renderData->GetLODResource(0);
                if (!lod.VertexFactory || !lod.IndexBuffer.Buffer)
                {
                    continue;
                }

                const auto center = primitive->GetBounds().Sphere.Center;
                const float depth = Core::Dot(center - view.CameraWorldPos, view.CameraWorldDir);
                for (const auto& section : lod.Sections)
                {
                    const auto* material = component->GetMaterial(section.MaterialIndex);
                    if (!material || !IsTransparentBlend(material->GetBlendMode()) || section.NumIndices == 0)
                    {
                        continue;
                    }

                    // Keep instances separate so meshes that share a material still sort individually.
                    draws.push_back({material->GetRenderProxyShared(), lod.VertexFactory.get(), lod.IndexBuffer.Buffer.get(),
                        primitive->GetLocalToWorld(), {section.FirstIndex, section.NumIndices, section.BaseVertexIndex},
                        depth, primitive->GetPrimitiveId()});
                }
            }

            return draws;
        }

        void SortTransparentDraws(std::vector<TransparentDraw>& draws)
        {
            std::sort(draws.begin(), draws.end(), [](const TransparentDraw& a, const TransparentDraw& b)
            {
                if (a.SortDepth != b.SortDepth)
                {
                    return a.SortDepth > b.SortDepth;
                }
                return std::tie(a.PrimitiveId, a.Indices.FirstIndex) < std::tie(b.PrimitiveId, b.Indices.FirstIndex);
            });
        }

        bool UsesTransmission(const TransparentDraw& draw)
        {
            return draw.Material->GetBlendMode() == EBlendMode::Translucent && Scalar(*draw.Material, "Transmission") > 0.0f;
        }

        RenderGraphTextureRef SnapshotBackground(RenderGraphBuilder& builder, RenderGraphTextureRef color,
            RenderGraphTextureRef depth, const std::string& name)
        {
            RenderGraphTextureDesc desc{};
            desc.Width = color->GetDesc().Width;
            desc.Height = color->GetDesc().Height;
            desc.Format = ERHIFormat::R32G32B32A32_Float;
            desc.Usage = ERHITextureCreateFlag::ShaderResource | ERHITextureCreateFlag::UAV;
            const auto background = builder.CreateTexture(name, desc);
            RenderGraphTextureUAVDesc uav{};
            uav.Texture = background;
            auto* parameters = builder.AllocateParameter<TransparencyBackgroundParameters>();
            parameters->TransparencySourceColor = color;
            parameters->TransparencySourceDepth = depth;
            parameters->TransparencyTargetSize = {desc.Width, desc.Height};
            parameters->TransparencyBackgroundOutput = builder.CreateTextureUAV(name + ".UAV", uav);
            const auto* shader = GShaderMap.GetShader<TransparencyBackgroundCS>(0);
            RHIComputePipelineStateDesc pipelineDesc{};
            pipelineDesc.ComputeShader = static_cast<RHIComputeShader*>(shader->GetRHIShader());
            auto* pipeline = RHIPipelineStateCache::GetOrCreateComputePipelineState(pipelineDesc);
            builder.AddPass("Transparency.SnapshotBackground", TransparencyBackgroundParameters::GetMetaData(), parameters,
                EPassFlag::Compute, [=](RHICommandListBase& list)
                {
                    auto& cmd = static_cast<RHIComputeCommandList&>(list);
                    cmd.SetComputePipelineState(pipeline);
                    SetShaderParameters(cmd, shader, parameters);
                    cmd.Dispatch((desc.Width + 7) / 8, (desc.Height + 7) / 8, 1);
                });
            return background;
        }

        void AddWeightedCompositePass(RenderGraphBuilder& builder, const SceneView& view, RenderGraphTextureRef sceneColor,
            RenderGraphTextureRef weightedRadiance, RenderGraphTextureRef weightedCoverage)
        {
            const auto* vertex = GShaderMap.GetShader<TransparencyCompositeVS>(0);
            const auto* pixel = GShaderMap.GetShader<TransparencyCompositePS>(0);
            RHIRasterizerStateDesc raster{};
            raster.CullMode = ERHICullMode::None;
            RHIColorBlendAttachmentDesc color{};
            color.bBlendEnable = true;
            color.ColorWriteMask = 0x7;
            RHIColorBlendStateDesc blend{};
            blend.Attachments = {color};

            RHIGraphicsPipelineStateDesc desc{};
            desc.ShaderStages.VertexShader = static_cast<RHIVertexShader*>(vertex->GetRHIShader());
            desc.ShaderStages.FragmentShader = static_cast<RHIFragmentShader*>(pixel->GetRHIShader());
            desc.VertexDescState = RHIPipelineStateCache::GetOrCreateVertexDescState({});
            desc.RasterizerState = RHIPipelineStateCache::GetOrCreateRasterizerState(raster);
            RHIDepthStencilStateDesc depth{};
            depth.bDepthTestEnable = false;
            depth.bDepthWriteEnable = false;
            desc.DepthStencilState = RHIPipelineStateCache::GetOrCreateDepthStencilState(depth);
            desc.ColorBlendState = RHIPipelineStateCache::GetOrCreateColorBlendState(blend);
            desc.AttachmentDesc.ColorAttachmentCount = 1;
            desc.AttachmentDesc.ColorAttachments[0] = {sceneColor->GetDesc().Format, ERenderTargetActions::Load_Store};
            auto* pipeline = RHIPipelineStateCache::GetOrCreateGraphicsPipelineState(desc);

            auto* parameters = builder.AllocateParameter<TransparencyCompositeParameters>();
            parameters->WeightedRadiance = weightedRadiance;
            parameters->WeightedCoverage = weightedCoverage;
            parameters->Targets.NumColorRenderTargets = 1;
            parameters->Targets[0].Texture = sceneColor;
            parameters->Targets[0].Action = ERenderTargetActions::Load_Store;
            builder.AddPass("Transparency.WeightedComposite", TransparencyCompositeParameters::GetMetaData(), parameters,
                EPassFlag::Graphic, [=, viewport = view.Viewport](RHICommandListBase& list)
                {
                    auto& cmd = static_cast<RHIGraphicCommandList&>(list);
                    cmd.SetGraphicPipelineState(pipeline);
                    cmd.SetViewport(viewport.X, viewport.Y, viewport.Width, viewport.Height, 0.0f, 1.0f);
                    cmd.SetScissor(viewport.X, viewport.Y, viewport.Width, viewport.Height);
                    SetShaderParameters(cmd, pixel, parameters);
                    RHIRenderPassInfo pass{};
                    pass.RenderTargets = parameters->Targets.GetBoundRenderTarget();
                    pass.RenderArea.X = viewport.X;
                    pass.RenderArea.Y = viewport.Y;
                    pass.RenderArea.Width = viewport.Width;
                    pass.RenderArea.Height = viewport.Height;
                    cmd.BeginRenderPass(pass);
                    cmd.Draw(3);
                    cmd.EndRenderPass();
                });
        }
    }

    void AddTransparencyPasses(RenderGraphBuilder& builder, SceneInterface& sceneInterface, const SceneView& view,
        RenderGraphTextureRef sceneColor, RenderGraphTextureRef sceneDepth, RenderGraphTextureRef effectiveDepth)
    {
        auto& scene = static_cast<Scene&>(sceneInterface);
        auto draws = GatherTransparentDraws(scene, view);
        if (draws.empty())
        {
            return;
        }

        const bool bUseEffectiveDepth = effectiveDepth && effectiveDepth != sceneDepth;
        const auto& settings = scene.GetSceneSetting();
        const auto debug = settings.TransparencyView;
        const bool bLayered = debug == ETransparencyView::Composite &&
            settings.TransparencyMethod == ETransparencyMethod::LayeredRefraction;
        const bool bHasTransmission = std::any_of(draws.begin(), draws.end(), UsesTransmission);
        const bool bWeighted = debug == ETransparencyView::Composite && settings.TransparencyMethod == ETransparencyMethod::Weighted &&
            !bHasTransmission && std::none_of(draws.begin(), draws.end(), [](const TransparentDraw& draw)
            {
                return draw.Material->GetBlendMode() == EBlendMode::Additive;
            });
        if (!bWeighted)
        {
            SortTransparentDraws(draws);
        }

        const auto refractionDepth = bUseEffectiveDepth ? effectiveDepth : sceneDepth;
        const std::string resourceName = "Transparency." + std::to_string(view.Viewport.X) + "." + std::to_string(view.Viewport.Y) +
            "." + std::to_string(view.Viewport.Width) + "." + std::to_string(view.Viewport.Height);
        const auto background = bUseEffectiveDepth || (!bLayered && bHasTransmission) ?
            SnapshotBackground(builder, sceneColor, refractionDepth, resourceName + ".Background") :
            builder.RegisterExternalTexture("Transparency.EmptyBackground", GlobalWhiteTexture.get());

        const uint32_t targetCount = bWeighted ? 2u : 1u;
        std::array<RenderGraphTextureRef, 2> colorTargets{sceneColor, nullptr};
        if (bWeighted)
        {
            RenderGraphTextureDesc desc{};
            desc.Width = sceneColor->GetDesc().Width;
            desc.Height = sceneColor->GetDesc().Height;
            desc.Format = ERHIFormat::R16G16B16A16_Float;
            desc.Usage = ERHITextureCreateFlag::RenderTarget | ERHITextureCreateFlag::ShaderResource;
            colorTargets = {builder.CreateTexture(resourceName + ".WeightedRadiance", desc),
                builder.CreateTexture(resourceName + ".WeightedCoverage", desc)};

            RenderTargetBindingSlots clear{};
            clear.NumColorRenderTargets = targetCount;
            for (uint32_t slot = 0; slot < targetCount; ++slot)
            {
                clear[slot].Texture = colorTargets[slot];
                clear[slot].Action = ERenderTargetActions::Clear_Store;
                clear[slot].ClearValue = {0, 0, 0, 0};
            }
            SceneRenderer::AddClearRenderTargetsPass(builder, clear);
        }

        SceneShaderParameters sceneParameters{};
        SceneEvnIBLLightParameters environment{};
        BuildShaderParameters(&scene, builder, sceneParameters);
        BuildEvnIBLLightParameters(&scene, builder, environment);
        auto* vertexType = static_cast<MeshMaterialShaderType*>(StaticMeshMaterialShaderVS::GetStaticType());
        auto* pixelType = static_cast<MeshMaterialShaderType*>(TransparencyMaterialPS::GetStaticType());
        auto* factory = VertexFactoryType::GetRegisterMap().at("LocalVertexFactory");

        for (size_t index = 0; index < draws.size(); ++index)
        {
            const auto& draw = draws[index];
            const bool bAdditive = draw.Material->GetBlendMode() == EBlendMode::Additive;
            const auto resource = draw.Material->GetResource();
            const auto model = MaterialPermutationParameters{draw.Material->GetBlendMode(), draw.Material->GetShadingModel()};
            const auto vfFlags = draw.VertexFactory->GetVertexFactoryFlags();
            const auto* vertex = resource->GetMeshShader({vertexType,
                factory, 0, vfFlags, model});
            const auto* pixel = resource->GetMeshShader({pixelType,
                factory, bWeighted ? 1u : 0u, vfFlags, model});

            RHIRasterizerStateDesc raster{};
            raster.CullMode = draw.Material->ResolveCullMode();
            raster.FrontFace = ERHIFrontFace::CounterClockwise;
            RHIDepthStencilStateDesc depth{};
            depth.bDepthTestEnable = true;
            depth.bDepthWriteEnable = false;
            depth.DepthCompareOp = ERHICompareOp::Less;
            RHIColorBlendAttachmentDesc blendAttachment{};
            blendAttachment.bBlendEnable = true;
            blendAttachment.ColorWriteMask = 0x7; // Keep the opaque scene's alpha.
            if (bWeighted || (bAdditive && debug == ETransparencyView::Composite))
            {
                blendAttachment.BlendMode = ERHIColorBlendMode::Additive;
            }
            RHIColorBlendStateDesc blend{};
            blend.Attachments.assign(targetCount, blendAttachment);

            RHIGraphicsPipelineStateDesc pipelineDesc{};
            pipelineDesc.ShaderStages.VertexShader = static_cast<RHIVertexShader*>(vertex->GetRHIShader());
            pipelineDesc.ShaderStages.FragmentShader = static_cast<RHIFragmentShader*>(pixel->GetRHIShader());
            pipelineDesc.VertexDescState = draw.VertexFactory->GetRHIVertexDescState();
            pipelineDesc.RasterizerState = RHIPipelineStateCache::GetOrCreateRasterizerState(raster);
            pipelineDesc.DepthStencilState = RHIPipelineStateCache::GetOrCreateDepthStencilState(depth);
            pipelineDesc.ColorBlendState = RHIPipelineStateCache::GetOrCreateColorBlendState(blend);
            pipelineDesc.AttachmentDesc.ColorAttachmentCount = targetCount;
            for (uint32_t slot = 0; slot < targetCount; ++slot)
            {
                pipelineDesc.AttachmentDesc.ColorAttachments[slot].Format = colorTargets[slot]->GetDesc().Format;
                pipelineDesc.AttachmentDesc.ColorAttachments[slot].Actions = ERenderTargetActions::Load_Store;
            }
            pipelineDesc.AttachmentDesc.bEnableDepth = true;
            pipelineDesc.AttachmentDesc.DepthStencilFormat = sceneDepth->GetDesc().Format;
            pipelineDesc.AttachmentDesc.DepthActions = ERenderTargetActions::Load_Store;
            auto* pipeline = RHIPipelineStateCache::GetOrCreateGraphicsPipelineState(pipelineDesc);

            auto* parameters = builder.AllocateParameter<TransparencyPassParameters>();
            auto& vf = parameters->Vertex.vertexFactoryParameters;
            vf.LocalToWorld = draw.LocalToWorld;
            vf.ViewProjection = view.ViewProjectionMatrix;
            vf.CameraWorldPosition = view.CameraWorldPos;
            auto& ps = parameters->Pixel;
            ps.VertexFactory = vf;
            ps.Scene = sceneParameters;
            ps.Environment = environment;
            BuildShaderParameters(draw.Material.get(), builder, ps.Material);

            const auto& material = *draw.Material;
            ps.TransparencyOptics = {std::clamp(Scalar(material, "IndexOfRefraction"), 1.0f, 3.0f),
                bAdditive ? 0.0f : std::clamp(Scalar(material, "Transmission"), 0.0f, 1.0f),
                std::max(0.0f, Scalar(material, "Thickness")), std::clamp(Scalar(material, "RefractionStrength"), 0.0f, 1.0f)};
            const auto absorption = std::get<std::array<float, 4>>(material.GetParameter("Absorption", EMaterialParameterType::Vector)->GetValueVariant());
            ps.TransparencyAbsorption = {std::max(0.0f, absorption[0]), std::max(0.0f, absorption[1]), std::max(0.0f, absorption[2])};
            ps.TransparencyBackground = bLayered && UsesTransmission(draw) ?
                SnapshotBackground(builder, sceneColor, refractionDepth, resourceName + ".Layer." + std::to_string(index)) : background;
            ps.TransparencyViewport = {float(view.Viewport.X), float(view.Viewport.Y), float(view.Viewport.Width), float(view.Viewport.Height)};
            ps.TransparencyPixelSize = {1.0f / sceneColor->GetDesc().Width, 1.0f / sceneColor->GetDesc().Height};
            ps.TransparencyDebugView = uint32_t(debug);
            ps.TransparencyUseEffectiveDepth = bUseEffectiveDepth ? 1u : 0u;
            ps.TransparencySortRank = float(index + 1) / float(draws.size());
            if (bAdditive)
            {
                ps.TransparencySortRank = -ps.TransparencySortRank;
            }

            auto& targets = parameters->Targets;
            targets.NumColorRenderTargets = targetCount;
            for (uint32_t slot = 0; slot < targetCount; ++slot)
            {
                targets[slot].Texture = colorTargets[slot];
                targets[slot].Action = ERenderTargetActions::Load_Store;
            }
            // Vulkan's existing render-pass attachment uses the writable layout.
            // The PSO disables fragment depth writes while the load/store preserves opaque depth.
            targets.DepthStencil.Texture = sceneDepth;
            targets.DepthStencil.DepthAction = ERenderTargetActions::Load_Store;
            builder.AddPass(bWeighted ? "Transparency.Weighted" : (bAdditive ? "Transparency.Additive" : "Transparency.Alpha"),
                TransparencyPassParameters::GetMetaData(), parameters, EPassFlag::Graphic,
                [=, viewport = view.Viewport](RHICommandListBase& list)
                {
                    auto& cmd = static_cast<RHIGraphicCommandList&>(list);
                    cmd.SetGraphicPipelineState(pipeline);
                    draw.VertexFactory->Bind(cmd);
                    cmd.SetViewport(viewport.X, viewport.Y, viewport.Width, viewport.Height, 0.0f, 1.0f);
                    cmd.SetScissor(viewport.X, viewport.Y, viewport.Width, viewport.Height);
                    SetShaderParameters(cmd, vertex, &parameters->Vertex);
                    SetShaderParameters(cmd, pixel, &parameters->Pixel);
                    RHIRenderPassInfo pass{};
                    pass.RenderTargets = parameters->Targets.GetBoundRenderTarget();
                    pass.RenderArea.X = viewport.X;
                    pass.RenderArea.Y = viewport.Y;
                    pass.RenderArea.Width = viewport.Width;
                    pass.RenderArea.Height = viewport.Height;
                    cmd.BeginRenderPass(pass);
                    cmd.DrawIndexed(draw.IndexBuffer->GetRHI(), draw.Indices.NumIndices, 1, draw.Indices.FirstIndex, draw.Indices.BaseVertexIndex, 0);
                    cmd.EndRenderPass();
                });
        }
        if (bWeighted)
        {
            AddWeightedCompositePass(builder, view, sceneColor, colorTargets[0], colorTargets[1]);
        }
    }
}
