#include "SurfaceCacheDirectLighting.h"
#include "../Scene.h"
#include "../StaticMeshMaterialShader.h"
#include "LightSceneProxy.h"
#include "Transform.hpp"
#include "RHIPipelineStateCache.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

using namespace Core;
using namespace Engine;
using namespace RenderCore;
using namespace RHI;

namespace Renderer
{
    BEGIN_SHADER_PARAMETER_STRUCT(SurfaceCacheLightData)
        SHADER_PARAMETER(Core::Float4, ColorIntensity)
        SHADER_PARAMETER(Core::Float4, PositionRadius)
        SHADER_PARAMETER(Core::Float4, DirectionInnerCos)
        SHADER_PARAMETER(Core::Float4, ConeBias)
        SHADER_PARAMETER(Core::UInt4, Info) // Type, first slice, slice count, reserved.
    END_SHADER_PARAMETER_STRUCT(SurfaceCacheLightData)

    BEGIN_SHADER_PARAMETER_STRUCT(SurfaceCacheShadowSlice)
        SHADER_PARAMETER(Core::Mat4, ViewProjection)
        SHADER_PARAMETER(Core::UInt4, Rect)
    END_SHADER_PARAMETER_STRUCT(SurfaceCacheShadowSlice)

    BEGIN_SHADER_PARAMETER_STRUCT(SurfaceCacheDirectParameters)
        SHADER_PARAMETER_STRUCT_REFERENCE(SurfaceCacheMaterialParameters, Cache)
        SHADER_PARAMETER(uint32_t, LightCount)
        SHADER_PARAMETER_RDG_STRUCTURED_BUFFER(SurfaceCacheLightData, Lights)
        SHADER_PARAMETER_RDG_STRUCTURED_BUFFER(SurfaceCacheShadowSlice, Shadows)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ShadowAtlas)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, Output)
    END_SHADER_PARAMETER_STRUCT(SurfaceCacheDirectParameters)

    BEGIN_SHADER_PARAMETER_STRUCT(SurfaceCacheShadowParameters)
        SHADER_PARAMETER_STRUCT_REFERENCE(PositionOnlyVSParameters, Vertex)
        SHADER_PARAMETER_RENDER_TARGET_BINDING_SLOTS(Targets)
        RDG_BUFFER_ACCESS(Positions, ERHIResourceAccess::VertexOrIndexBuffer)
        RDG_BUFFER_ACCESS(Indices, ERHIResourceAccess::VertexOrIndexBuffer)
    END_SHADER_PARAMETER_STRUCT(SurfaceCacheShadowParameters)

    BEGIN_SHADER_PARAMETER_STRUCT(SurfaceCacheLightingUploadParameters)
        RDG_BUFFER_ACCESS(Buffer, ERHIResourceAccess::TransferDest)
    END_SHADER_PARAMETER_STRUCT(SurfaceCacheLightingUploadParameters)

    BEGIN_SHADER_PARAMETER_STRUCT(SurfaceCacheLightingLookupParameters)
        SHADER_PARAMETER_STRUCT_REFERENCE(SurfaceCacheMaterialParameters, Cache)
        SHADER_PARAMETER_STRUCT_REFERENCE(SurfaceCacheLightingParameters, Lighting)
        SHADER_PARAMETER(uint32_t, QueryCount)
        SHADER_PARAMETER_RDG_STRUCTURED_BUFFER(SurfaceCacheQueryResult, MaterialResults)
        SHADER_PARAMETER_RDG_RWSTRUCTURED_BUFFER(SurfaceCacheLightingQueryResult, Results)
    END_SHADER_PARAMETER_STRUCT(SurfaceCacheLightingLookupParameters)

    BEGIN_SHADER_PARAMETER_STRUCT(SurfaceCacheLightingDebugParameters)
        SHADER_PARAMETER_STRUCT_REFERENCE(SurfaceCacheLightingParameters, Lighting)
        SHADER_PARAMETER(Core::UInt2, OutputSize)
        SHADER_PARAMETER(uint32_t, QueryCount)
        SHADER_PARAMETER(float, Exposure)
        SHADER_PARAMETER_RDG_STRUCTURED_BUFFER(SurfaceCacheLightingQueryResult, QueryResults)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, Output)
    END_SHADER_PARAMETER_STRUCT(SurfaceCacheLightingDebugParameters)

    class SurfaceCacheDirectCS : public GlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER_TYPE(SurfaceCacheDirectCS)
        static bool ShouldCompilePermutation(const ShaderPermutationParameters&) { return true; }
        static void ModifyShaderCompilerEnvironment(const ShaderPermutationParameters&, ShaderCompilerEnvironment&) {}
        static const ShaderParametersMetadata* GetShaderParameterMetadata() { return SurfaceCacheDirectParameters::GetMetaData(); }
    };
    class SurfaceCacheLightingLookupCS : public GlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER_TYPE(SurfaceCacheLightingLookupCS)
        static bool ShouldCompilePermutation(const ShaderPermutationParameters&) { return true; }
        static void ModifyShaderCompilerEnvironment(const ShaderPermutationParameters&, ShaderCompilerEnvironment&) {}
        static const ShaderParametersMetadata* GetShaderParameterMetadata() { return SurfaceCacheLightingLookupParameters::GetMetaData(); }
    };
    class SurfaceCacheLightingDebugCS : public GlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER_TYPE(SurfaceCacheLightingDebugCS)
        static bool ShouldCompilePermutation(const ShaderPermutationParameters&) { return true; }
        static void ModifyShaderCompilerEnvironment(const ShaderPermutationParameters&, ShaderCompilerEnvironment&) {}
        static const ShaderParametersMetadata* GetShaderParameterMetadata() { return SurfaceCacheLightingDebugParameters::GetMetaData(); }
    };
    // Reuse the engine's clip-depth shadow PS without its legacy mesh-only global permutation predicate.
    class SurfaceCacheShadowPS : public GlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER_TYPE(SurfaceCacheShadowPS)
        static bool ShouldCompilePermutation(const ShaderPermutationParameters&) { return true; }
        static void ModifyShaderCompilerEnvironment(const ShaderPermutationParameters&, ShaderCompilerEnvironment& env) { env.SetDefine("DEPTH_STORE_MODE", 0); }
        static const ShaderParametersMetadata* GetShaderParameterMetadata() { return DepthShadowPassPSParameters::GetMetaData(); }
    };
    IMPLEMENT_GLOBAL_SHADER_TYPE(SurfaceCacheDirectCS, "SurfaceCacheDirectCS", "/GI/SurfaceCacheDirect.sf", "MainCS", ERHIShaderFrequency::Compute);
    IMPLEMENT_GLOBAL_SHADER_TYPE(SurfaceCacheLightingLookupCS, "SurfaceCacheLightingLookupCS", "/GI/SurfaceCacheLightingLookup.sf", "MainCS", ERHIShaderFrequency::Compute);
    IMPLEMENT_GLOBAL_SHADER_TYPE(SurfaceCacheLightingDebugCS, "SurfaceCacheLightingDebugCS", "/GI/SurfaceCacheLightingDebug.sf", "MainCS", ERHIShaderFrequency::Compute);
    IMPLEMENT_GLOBAL_SHADER_TYPE(SurfaceCacheShadowPS, "SurfaceCacheShadowPS", "/tools/DepthShadowPassPS.sf", "MainPS", ERHIShaderFrequency::Fragment);

    namespace
    {
        void Check(bool value, const char* message)
        {
            if (!value)
            {
                throw std::invalid_argument(std::string("Surface Cache lighting: ") + message);
            }
        }
        Float3 XYZ(const Float4& v) { return {v.X, v.Y, v.Z}; }
        Float4 V4(const Float3& v, float w) { return {v.X, v.Y, v.Z, w}; }
        Float3 Up(const Float3& direction) { return std::abs(direction.Y) > 0.95f ? Float3(0, 0, 1) : Float3(0, 1, 0); }

        RenderGraphBufferSRVRef ReadBuffer(RenderGraphBuilder& builder, RenderGraphBufferRef buffer)
        {
            RenderGraphBufferSRVDesc desc;
            desc.Buffer = buffer;
            desc.Stride = buffer->GetDesc().Stride;
            desc.NumElements = buffer->GetDesc().Size / desc.Stride;
            return builder.CreateBufferSRV(buffer->GetName() + ".LightingSRV", desc);
        }
        template<class T>
        RenderGraphBufferRef Upload(RenderGraphBuilder& builder, const std::vector<T>& data, const std::string& name)
        {
            auto* copy = builder.AllocateParameter<std::vector<T>>();
            *copy = data;
            if (copy->empty())
            {
                copy->emplace_back();
            }
            RenderGraphBufferDesc desc;
            desc.Size = copy->size() * sizeof(T);
            desc.Stride = sizeof(T);
            desc.Usage = ERHIBufferUsageFlag::Structured | ERHIBufferUsageFlag::ShaderResource | ERHIBufferUsageFlag::TransferDst;
            auto buffer = builder.CreateBuffer(name, desc);
            auto* p = builder.AllocateParameter<SurfaceCacheLightingUploadParameters>();
            p->Buffer = buffer;
            builder.AddPass("SurfaceCacheLightingUpload", SurfaceCacheLightingUploadParameters::GetMetaData(), p, EPassFlag::Graphic,
                [=](RHICommandListBase& cmd)
            {
                RHIUpdateBufferRegion region;
                region.Size = desc.Size;
                GRHIApi->UpdateBuffer(cmd, buffer->GetRHIBuffer(), copy->data(), region);
            });
            return buffer;
        }
        RenderGraphTextureUAVRef WriteTexture(RenderGraphBuilder& builder, RenderGraphTextureRef texture)
        {
            RenderGraphTextureUAVDesc desc;
            desc.Texture = texture;
            desc.Format = texture->GetDesc().Format;
            return builder.CreateTextureUAV(texture->GetName() + ".LightingUAV", desc);
        }
        template<class T, class P>
        void Compute(RenderGraphBuilder& builder, const char* name, P* p, uint32_t x, uint32_t y = 1)
        {
            auto shader = GShaderMap.GetShader<T>(0);
            Check(shader != nullptr, "required compute shader is unavailable");
            RHIComputePipelineStateDesc desc;
            desc.ComputeShader = static_cast<RHIComputeShader*>(shader->GetRHIShader());
            auto pipeline = RHIPipelineStateCache::GetOrCreateComputePipelineState(desc);
            builder.AddPass(name, P::GetMetaData(), p, EPassFlag::Compute, [=](RHICommandListBase& list)
            {
                auto& cmd = static_cast<RHIComputeCommandList&>(list);
                cmd.SetComputePipelineState(pipeline);
                SetShaderParameters(cmd, shader, p);
                cmd.Dispatch(x, y, 1);
            });
        }
        RHIRenderPassInfo ShadowPassInfo(const RenderTargetBindingSlots& targets, uint32_t size)
        {
            RHIRenderPassInfo info;
            info.RenderTargets = targets.GetBoundRenderTarget();
            std::fill_n(info.RenderTargets.ColorAttachments[0].ClearBinding.Color, 4, 1.0f);
            info.RenderTargets.DepthStencil.ClearBinding.Depth = 1;
            info.RenderArea.Width = info.RenderArea.Height = size;
            return info;
        }
    }

    std::vector<SurfaceCacheLight> SurfaceCacheDirectLighting::GatherSceneLights(Scene& scene)
    {
        std::vector<SurfaceCacheLight> result;
        scene.ForEachLight([&](LightSceneProxy* proxy)
        {
            SurfaceCacheLight light;
            light.Color = proxy->GetColor();
            light.Intensity = proxy->GetIntensity();
            light.Position = proxy->GetPosition();
            light.bCastShadow = proxy->IsCastShadow();
            switch (proxy->GetLightType())
            {
            case ELightType::Directional:
                light.Direction = static_cast<DirectionalLightSceneProxy*>(proxy)->GetDirection();
                break;
            case ELightType::Point:
                light.Type = ESurfaceCacheLightType::Point;
                light.Radius = static_cast<PointLightSceneProxy*>(proxy)->GetAttenuationRadius();
                break;
            case ELightType::Spot:
            {
                const auto* spot = static_cast<SpotLightSceneProxy*>(proxy);
                light.Type = ESurfaceCacheLightType::Spot;
                light.Radius = spot->GetAttenuationRadius();
                light.Direction = spot->GetDirection();
                light.InnerConeAngle = spot->GetInnerConeAngle();
                light.OuterConeAngle = spot->GetOuterConeAngle();
                break;
            }
            default:
                return; // Sky lighting belongs to a later stage.
            }
            result.push_back(light);
        }); // Scene only visits enabled lights; requires its normal update/flush beforehand.
        return result;
    }

    void SurfaceCacheDirectLighting::Initialize(const SurfaceCacheMaterial& material, uint32_t resolution, uint32_t maxSlices)
    {
        Check(!m_Radiance && material.GetBaseColorAtlas(), "initialize once with a material snapshot, or release after retiring users");
        Check(resolution >= 32 && resolution <= 1024 && maxSlices >= 6 && maxSlices <= 256, "invalid shadow capacity/resolution");
        auto desc = material.GetBaseColorAtlas()->GetRHI()->GetDesc();
        desc.Usage = ERHITextureCreateFlag::UAV | ERHITextureCreateFlag::ShaderResource | ERHITextureCreateFlag::TransferSrc;
        desc.DebugName = "SurfaceCache.DirectDiffuseRadiance";
        auto radiance = std::make_shared<RenderTexture>(desc);
        radiance->InitRHIResource();
        auto allocator = std::make_unique<ShadowMapAllocator>();
        const uint32_t columns = uint32_t(std::ceil(std::sqrt(double(maxSlices))));
        const uint32_t size = columns * (resolution + 2);
        Check(size <= 8192, "shadow atlas exceeds 8192");
        ShadowAllocatorDesc shadowDesc;
        shadowDesc.SpotShadowAtlas.Width = shadowDesc.SpotShadowAtlas.Height = size;
        Check(allocator->Initialize(shadowDesc), "shadow allocation failed");
        std::vector<ShadowAllocationSlice> slots;
        for (uint32_t i = 0; i < maxSlices; ++i)
        {
            const auto allocation = allocator->AllocateSpotShadow(resolution + 2);
            Check(allocation.IsValid(), "shadow atlas capacity mismatch");
            auto slice = allocation.Slices.front();
            ++slice.X;
            ++slice.Y;
            slice.Width = slice.Height = resolution;
            slots.push_back(slice);
        }
        m_Radiance = std::move(radiance);
        m_ShadowAllocator = std::move(allocator);
        m_ShadowSlots = std::move(slots);
        m_ShadowSize = size;
        m_Material = &material;
        m_bValid = false;
    }

    void SurfaceCacheDirectLighting::Release()
    {
        m_bValid = false;
        m_Radiance.reset();
        m_ShadowAllocator.reset();
        m_ShadowSlots.clear();
        m_Material = nullptr;
    }

    SurfaceCacheLightingParameters SurfaceCacheDirectLighting::Register(RenderGraphBuilder& builder, const SurfaceCacheMaterial& material) const
    {
        Check(IsValidFor(material), "lighting is invalid/stale; update after material capture");
        SurfaceCacheLightingParameters result{};
        result.DirectRadiance = builder.RegisterExternalTexture("SurfaceCache.Lighting." + std::to_string(reinterpret_cast<uintptr_t>(this)), m_Radiance.get());
        // A bound SRV is required even when the zero tile size disables indirect sampling.
        result.IndirectRadiance = result.DirectRadiance;
        return result;
    }

    SurfaceCacheLightingParameters SurfaceCacheDirectLighting::AddUpdatePasses(RenderGraphBuilder& builder,
        const SurfaceCacheMaterial& material, const std::vector<SurfaceCacheLight>& lights)
    {
        m_bValid = false;
        Check(m_Radiance && m_Material == &material && material.GetBaseColorAtlas(), "initialize against this material cache first");
        Check(m_Radiance->GetRHI()->GetDesc().Width == material.GetBaseColorAtlas()->GetRHI()->GetDesc().Width, "atlas resized; retire and reinitialize lighting");
        auto cache = material.Register(builder);
        std::vector<SurfaceCacheLightData> data;
        std::vector<SurfaceCacheShadowSlice> shadows;
        AABB worldBounds;
        worldBounds.SetEmpty();
        for (const auto& card : material.GetCards())
        {
            // Only world bounds are needed here; six axis Cards enclose the same instance.
            if (card.Owner.Z == 0)
            {
                const auto extent = XYZ(card.AxisU) * card.Extent.X;
                const auto vertical = XYZ(card.AxisV) * card.Extent.Y;
                const auto normal = XYZ(card.AxisN) * card.Extent.Z;
                for (int x : {-1, 1})
                {
                    for (int y : {-1, 1})
                    {
                        for (int z : {-1, 1})
                        {
                            worldBounds.ExpandBy(XYZ(card.Center) + extent * float(x) + vertical * float(y) + normal * float(z));
                        }
                    }
                }
            }
        }
        if (material.GetCards().empty())
        {
            worldBounds = AABB(Float3(-1, -1, -1), Float3(1, 1, 1));
        }
        for (const auto& light : lights)
        {
            if (!light.bEnabled)
            {
                continue;
            }
            Check(uint32_t(light.Type) <= 2 && std::isfinite(light.Intensity) && light.Intensity >= 0, "invalid light type/intensity");
            for (uint32_t axis = 0; axis < 3; ++axis)
            {
                Check(std::isfinite(light.Color[axis]) && light.Color[axis] >= 0 && std::isfinite(light.Position[axis]) &&
                    std::isfinite(light.Direction[axis]), "nonfinite/negative light data");
            }
            Check(light.Direction.LengthSquared() > 1e-10f && std::isfinite(light.Radius) && light.Radius > 0, "invalid direction/radius");
            Check(std::isfinite(light.InnerConeAngle) && std::isfinite(light.OuterConeAngle) && light.InnerConeAngle >= 0 &&
                light.InnerConeAngle <= light.OuterConeAngle && light.OuterConeAngle > 0 && light.OuterConeAngle < 1.56f, "invalid spot half angles");
            SurfaceCacheLightData value;
            const auto direction = Normalize(light.Direction);
            value.ColorIntensity = V4(light.Color, light.Intensity);
            value.PositionRadius = V4(light.Position, light.Radius);
            value.DirectionInnerCos = V4(direction, std::cos(light.InnerConeAngle));
            value.ConeBias = {std::cos(light.OuterConeAngle), 0.001f, 0, 0};
            value.Info = {uint32_t(light.Type), uint32_t(shadows.size()), 0, 0};
            if (light.bCastShadow)
            {
                value.Info.Z = light.Type == ESurfaceCacheLightType::Point ? 6 : 1;
                Check(shadows.size() + value.Info.Z <= m_ShadowSlots.size(), "shadow slice capacity exceeded (point light requires six)");
                const Float3 faces[] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
                for (uint32_t face = 0; face < value.Info.Z; ++face)
                {
                    SurfaceCacheShadowSlice slice;
                    if (light.Type == ESurfaceCacheLightType::Directional)
                    {
                        const auto center = (worldBounds.Min + worldBounds.Max) * 0.5f;
                        const float radius = std::max((worldBounds.Max - worldBounds.Min).Length() * 0.5f, 0.01f);
                        const auto view = LookAtRH(center - direction * (radius * 2), center, Up(direction));
                        slice.ViewProjection = OrthoRhZo(-radius, radius, -radius, radius, radius * 0.1f, radius * 4) * view;
                    }
                    else
                    {
                        const auto forward = light.Type == ESurfaceCacheLightType::Point ? faces[face] : direction;
                        const float fov = light.Type == ESurfaceCacheLightType::Point ? DegToRad(90.0f) : light.OuterConeAngle * 2;
                        slice.ViewProjection = PerspectiveRhZo(fov, 1, light.Radius * 0.005f, light.Radius) *
                            LookAtRH(light.Position, light.Position + forward, Up(forward));
                    }
                    const auto& slot = m_ShadowSlots[shadows.size()];
                    slice.Rect = {slot.X, slot.Y, slot.Width, slot.Height};
                    shadows.push_back(slice);
                }
            }
            data.push_back(value);
        }
        Check(data.size() <= 1024, "first-version light count exceeds 1024");

        auto shadow = builder.RegisterExternalTexture("SurfaceCache.Shadows." + std::to_string(reinterpret_cast<uintptr_t>(this)), m_ShadowAllocator->GetShadowAtlas());
        RenderGraphTextureDesc depthDesc;
        depthDesc.Width = depthDesc.Height = m_ShadowSize;
        depthDesc.Format = ERHIFormat::D32_Float;
        depthDesc.Usage = ERHITextureCreateFlag::DepthStencil;
        auto depth = builder.CreateTexture("SurfaceCache.ShadowDepth", depthDesc);
        auto* clear = builder.AllocateParameter<SurfaceCacheShadowParameters>();
        clear->Targets.NumColorRenderTargets = 1;
        clear->Targets[0].Texture = shadow;
        clear->Targets[0].Action = ERenderTargetActions::Clear_Store;
        clear->Targets.DepthStencil.Texture = depth;
        clear->Targets.DepthStencil.DepthAction = ERenderTargetActions::Clear_Store;
        const uint32_t shadowSize = m_ShadowSize;
        builder.AddPass("SurfaceCacheClearShadows", SurfaceCacheShadowParameters::GetMetaData(), clear, EPassFlag::Graphic,
            [=](RHICommandListBase& list)
        {
            auto& cmd = static_cast<RHIGraphicCommandList&>(list);
            cmd.BeginRenderPass(ShadowPassInfo(clear->Targets, shadowSize));
            cmd.EndRenderPass();
        });
        if (!shadows.empty())
        {
            auto vs = GShaderMap.GetShader<PositionOnlyVS>(0);
            auto ps = GShaderMap.GetShader<SurfaceCacheShadowPS>(0);
            Check(vs && ps, "shadow shaders unavailable (initialize PositionOnlyVS and SurfaceCache shaders)");
            RHIGraphicsPipelineStateDesc desc;
            desc.ShaderStages.VertexShader = static_cast<RHIVertexShader*>(vs->GetRHIShader());
            desc.ShaderStages.FragmentShader = static_cast<RHIFragmentShader*>(ps->GetRHIShader());
            desc.VertexDescState = GetVertexOnlyState(false);
            RHIRasterizerStateDesc raster;
            raster.CullMode = ERHICullMode::None;
            desc.RasterizerState = RHIPipelineStateCache::GetOrCreateRasterizerState(raster);
            RHIDepthStencilStateDesc ds;
            ds.bDepthTestEnable = ds.bDepthWriteEnable = true;
            ds.DepthCompareOp = ERHICompareOp::Less;
            desc.DepthStencilState = RHIPipelineStateCache::GetOrCreateDepthStencilState(ds);
            RHIColorBlendStateDesc blend;
            blend.Attachments.resize(1);
            desc.ColorBlendState = RHIPipelineStateCache::GetOrCreateColorBlendState(blend);
            desc.AttachmentDesc.ColorAttachmentCount = 1;
            desc.AttachmentDesc.ColorAttachments[0].Format = shadow->GetDesc().Format;
            desc.AttachmentDesc.ColorAttachments[0].Actions = ERenderTargetActions::Load_Store;
            desc.AttachmentDesc.bEnableDepth = true;
            desc.AttachmentDesc.DepthStencilFormat = ERHIFormat::D32_Float;
            desc.AttachmentDesc.DepthActions = ERenderTargetActions::Load_Store;
            auto pipeline = RHIPipelineStateCache::GetOrCreateGraphicsPipelineState(desc);
            for (const auto& slice : shadows)
            {
                for (const auto& instance : material.GetInstances())
                {
                    auto* p = builder.AllocateParameter<SurfaceCacheShadowParameters>();
                    p->Targets = clear->Targets;
                    p->Targets[0].Action = ERenderTargetActions::Load_Store;
                    p->Targets.DepthStencil.DepthAction = ERenderTargetActions::Load_Store;
                    p->Vertex.Model = instance.LocalToWorld;
                    p->Vertex.ViewProjection = slice.ViewProjection;
                    const auto& lod = instance.RenderData->LODResources.front();
                    const auto prefix = "SurfaceCache.ShadowMesh." + std::to_string(reinterpret_cast<uintptr_t>(instance.RenderData.get()));
                    p->Positions = builder.RegisterExternalBuffer(prefix + ".Positions", lod.VertexBuffers.PositionBuffer.Buffer.get());
                    p->Indices = builder.RegisterExternalBuffer(prefix + ".Indices", lod.IndexBuffer.Buffer.get());
                    builder.AddPass("SurfaceCacheShadow", SurfaceCacheShadowParameters::GetMetaData(), p, EPassFlag::Graphic,
                        [=](RHICommandListBase& list)
                    {
                        auto& cmd = static_cast<RHIGraphicCommandList&>(list);
                        const auto& mesh = instance.RenderData->LODResources.front();
                        cmd.SetGraphicPipelineState(pipeline);
                        cmd.SetStreamSource(0, mesh.VertexBuffers.PositionBuffer.Buffer->GetRHI(), 0);
                        cmd.BeginRenderPass(ShadowPassInfo(p->Targets, shadowSize));
                        cmd.SetViewport(slice.Rect.X, slice.Rect.Y, slice.Rect.Z, slice.Rect.W, 0, 1);
                        cmd.SetScissor(slice.Rect.X, slice.Rect.Y, slice.Rect.Z, slice.Rect.W);
                        SetShaderParameters(cmd, vs, &p->Vertex);
                        for (const auto& section : mesh.Sections)
                        {
                            cmd.DrawIndexed(mesh.IndexBuffer.Buffer->GetRHI(), section.NumIndices, 1, section.FirstIndex, section.BaseVertexIndex, 0);
                        }
                        cmd.EndRenderPass();
                    });
                }
            }
        }
        auto radiance = builder.RegisterExternalTexture("SurfaceCache.Lighting." + std::to_string(reinterpret_cast<uintptr_t>(this)), m_Radiance.get());
        auto* p = builder.AllocateParameter<SurfaceCacheDirectParameters>();
        p->Cache = cache;
        p->LightCount = uint32_t(data.size());
        p->Lights = ReadBuffer(builder, Upload(builder, data, "SurfaceCache.LightData"));
        p->Shadows = ReadBuffer(builder, Upload(builder, shadows, "SurfaceCache.ShadowSlices"));
        p->ShadowAtlas = shadow;
        p->Output = WriteTexture(builder, radiance);
        Compute<SurfaceCacheDirectCS>(builder, "SurfaceCacheDirectLighting", p, (cache.AtlasSize.X + 7) / 8, (cache.AtlasSize.Y + 7) / 8);
        m_MaterialRevision = material.GetRevision();
        m_bValid = true; // Scheduled, not GPU-complete. A discarded/failed graph requires Invalidate().
        return Register(builder, material);
    }

    RenderGraphBufferRef AddSurfaceCacheLightingLookupPass(RenderGraphBuilder& builder, const SurfaceCacheMaterialParameters& material,
        const SurfaceCacheLightingParameters& lighting, RenderGraphBufferRef materialResults, uint32_t count)
    {
        Check(materialResults && count > 0 && materialResults->GetDesc().Stride == sizeof(SurfaceCacheQueryResult) &&
            uint64_t(count) * sizeof(SurfaceCacheQueryResult) <= materialResults->GetDesc().Size, "invalid material query buffer/count");
        auto* p = builder.AllocateParameter<SurfaceCacheLightingLookupParameters>();
        p->Cache = material;
        p->Lighting = lighting;
        p->QueryCount = count;
        p->MaterialResults = ReadBuffer(builder, materialResults);
        RenderGraphBufferDesc desc;
        desc.Stride = sizeof(SurfaceCacheLightingQueryResult);
        desc.Size = uint64_t(count) * desc.Stride;
        desc.Usage = ERHIBufferUsageFlag::Structured | ERHIBufferUsageFlag::ShaderResource | ERHIBufferUsageFlag::UnorderedAccess | ERHIBufferUsageFlag::TransferSrc;
        auto result = builder.CreateBuffer(materialResults->GetName() + ".LightingResults", desc);
        RenderGraphBufferUAVDesc uav;
        uav.Buffer = result;
        uav.Stride = desc.Stride;
        uav.NumElements = count;
        p->Results = builder.CreateBufferUAV(result->GetName() + ".UAV", uav);
        Compute<SurfaceCacheLightingLookupCS>(builder, "SurfaceCacheLightingLookup", p, (count + 63) / 64);
        return result;
    }

    void AddSurfaceCacheLightingDebugPass(RenderGraphBuilder& builder, const SurfaceCacheLightingParameters& lighting,
        RenderGraphTextureRef output, RenderGraphBufferRef queries, uint32_t count, float exposure)
    {
        Check(output && output->GetDesc().Format == ERHIFormat::R32G32B32A32_Float && std::isfinite(exposure) && exposure >= 0,
            "debug requires float4 UAV and nonnegative exposure");
        Check(!queries || (count > 0 && queries->GetDesc().Stride == sizeof(SurfaceCacheLightingQueryResult) &&
            uint64_t(count) * sizeof(SurfaceCacheLightingQueryResult) <= queries->GetDesc().Size), "invalid lighting query debug buffer");
        auto* p = builder.AllocateParameter<SurfaceCacheLightingDebugParameters>();
        p->Lighting = lighting;
        p->OutputSize = {output->GetDesc().Width, output->GetDesc().Height};
        p->QueryCount = queries ? count : 0;
        p->Exposure = exposure;
        if (!queries)
        {
            queries = Upload(builder, std::vector<SurfaceCacheLightingQueryResult>(1), "SurfaceCache.EmptyDebugQuery");
        }
        p->QueryResults = ReadBuffer(builder, queries);
        p->Output = WriteTexture(builder, output);
        Compute<SurfaceCacheLightingDebugCS>(builder, "SurfaceCacheLightingDebug", p, (p->OutputSize.X + 7) / 8, (p->OutputSize.Y + 7) / 8);
    }
}
