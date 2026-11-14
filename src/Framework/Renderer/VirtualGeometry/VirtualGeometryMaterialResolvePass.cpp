#include "VirtualGeometryMaterialResolvePass.h"
#include "MateiralShader.h"
#include "GlobalShader.h"
#include "MaterialShaderParameter.h"
#include "StaticMeshMaterialShader.h"
#include "StaticMeshProcess.h"
#include "Scene.h"
#include "SceneRenderer.h"
#include "RHIPipelineStateCache.h"
#include <cmath>
#include <cstddef>
#include <stdexcept>

using namespace RenderCore;
using namespace RHI;

namespace Renderer
{
    BEGIN_SHADER_PARAMETER_STRUCT(VirtualGeometryResolveTargets)
    SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, ResolveGBufferA)
    SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, ResolveGBufferB)
    SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, ResolveGBufferC)
    SHADER_PARAMETER(Core::Int2, ViewOrigin)
    SHADER_PARAMETER(Core::Int2, ViewSize)
    END_SHADER_PARAMETER_STRUCT(VirtualGeometryResolveTargets)

    BEGIN_SHADER_PARAMETER_STRUCT(VirtualGeometryMaterialResolveParameters)
    SHADER_PARAMETER_STRUCT_INCLUDE(VirtualGeometryResolveTargets, Targets)
    SHADER_PARAMETER_STRUCT_REFERENCE(Engine::MaterialShaderParameters, Material)
    SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint2>, Visibility)
    SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, VisibilityDepth)
    SHADER_PARAMETER_RDG_STRUCTURED_BUFFER(VirtualGeometryDrawMetadata, DrawMetadata)
    SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, Positions)
    SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, Normals)
    SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, Tangents)
    SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, TexCoords)
    SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, Indices)
    SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, Clusters)
    SHADER_PARAMETER(Core::Mat4, LocalToWorld)
    SHADER_PARAMETER(Core::Mat4, NormalToWorld)
    SHADER_PARAMETER(Core::Mat4, ViewProjection)
    SHADER_PARAMETER(Core::Float3, CameraPosition)
    SHADER_PARAMETER(float, ScreenYSign)
    SHADER_PARAMETER(uint32_t, ResolveDrawID)
    SHADER_PARAMETER(uint32_t, ClusterStrideWords)
    SHADER_PARAMETER(float, TransformHandedness)
    END_SHADER_PARAMETER_STRUCT(VirtualGeometryMaterialResolveParameters)

    class VirtualGeometryMaterialResolveCS : public MaterialShader
    {
    public:
        DECLARE_MATERIAL_SHADER_TYPE(VirtualGeometryMaterialResolveCS)

        static bool ShouldCompilePermutation(const ShaderPermutationParameters&)
        {
            return true;
        }

        static void ModifyShaderCompilerEnvironment(const ShaderPermutationParameters& parameters,
                                                    ShaderCompilerEnvironment& environment)
        {
            const auto& material = static_cast<const Engine::MaterialShaderPermutationParameters&>(parameters);
            Engine::ModifyShaderCompilerEnvironment(material.MaterialParams, environment);
        }

        static const ShaderParametersMetadata* GetShaderParameterMetadata()
        {
            return VirtualGeometryMaterialResolveParameters::GetMetaData();
        }
    };

    IMPLEMENT_MATERIAL_SHADER_TYPE(VirtualGeometryMaterialResolveCS, "VirtualGeometryMaterialResolveCS",
                                   "/virtualgeometry/MaterialResolve.sf", "MainCS", ERHIShaderFrequency::Compute);

    class VirtualGeometryResolveClearCS : public GlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER_TYPE(VirtualGeometryResolveClearCS)

        static bool ShouldCompilePermutation(const ShaderPermutationParameters&)
        {
            return true;
        }

        static void ModifyShaderCompilerEnvironment(const ShaderPermutationParameters&, ShaderCompilerEnvironment&) {}

        static const ShaderParametersMetadata* GetShaderParameterMetadata()
        {
            return VirtualGeometryResolveTargets::GetMetaData();
        }
    };

    IMPLEMENT_GLOBAL_SHADER_TYPE(VirtualGeometryResolveClearCS, "VirtualGeometryResolveClearCS",
                                 "/virtualgeometry/MaterialResolveClear.sf", "MainCS", ERHIShaderFrequency::Compute);

    namespace
    {
        RHIComputePipelineState* ComputePipeline(Shader* shader)
        {
            if (!shader)
            {
                throw std::runtime_error("Virtual geometry compute shader compilation failed: " +
                                         Engine::GMaterialShaderMap.GetLastError());
            }
            RHIComputePipelineStateDesc desc;
            desc.ComputeShader = dynamic_cast<RHIComputeShader*>(shader->GetRHIShader());
            return RHIPipelineStateCache::GetOrCreateComputePipelineState(desc);
        }

        RenderGraphBufferSRVRef ScalarBuffer(RenderGraphBuilder& builder, RenderBuffer* buffer, const std::string& name)
        {
            if (!buffer || !buffer->GetRHI() || buffer->GetRHI()->GetDesc().Size % sizeof(uint32_t))
            {
                throw std::invalid_argument("Virtual geometry attribute buffer is missing or misaligned");
            }
            const auto key = name + std::to_string(reinterpret_cast<uintptr_t>(buffer->GetRHI()));
            auto resource = builder.RegisterExternalBuffer(key, buffer);
            RenderGraphBufferSRVDesc desc;
            desc.Buffer = resource;
            desc.Stride = sizeof(uint32_t);
            desc.NumElements = buffer->GetRHI()->GetDesc().Size / sizeof(uint32_t);
            return builder.CreateBufferSRV(key + ".SRV", desc);
        }
    } // namespace

    VirtualGeometryMaterialResolveOutput AddVirtualGeometryMaterialResolvePass(
        RenderGraphBuilder& builder, const Engine::SceneView& view,
        const std::shared_ptr<VirtualGeometryRasterOutput>& visibility, RenderGraphTextureRef gbufferA,
        RenderGraphTextureRef gbufferB, RenderGraphTextureRef gbufferC)
    {
        if (!visibility || !gbufferA || !gbufferB || !gbufferC)
        {
            throw std::invalid_argument("Missing virtual geometry material resolve input");
        }
        const auto width = visibility->VirtualGeometryVisibilityTexture->GetDesc().Width;
        const auto height = visibility->VirtualGeometryVisibilityTexture->GetDesc().Height;
        const auto layout = CreateGBufferInfo({});
        RenderGraphTextureRef buffers[] = {gbufferA, gbufferB, gbufferC};
        for (uint32_t i = 0; i < 3; ++i)
        {
            const auto& desc = buffers[i]->GetDesc();
            if (desc.Width != width || desc.Height != height || desc.Format != layout.Targets[i].TargetFormat ||
                !EnumHasAnyFlags(desc.Usage, ERHITextureCreateFlag::UAV))
            {
                throw std::invalid_argument("Virtual geometry resolve requires matching UAV GBuffer targets");
            }
        }
        auto* clear = builder.AllocateParameter<VirtualGeometryResolveTargets>();
        const auto prefix = "VirtualGeometryResolve." + std::to_string(reinterpret_cast<uintptr_t>(clear));
        RenderGraphTextureRef targets[] = {gbufferA, gbufferB, gbufferC};
        RenderGraphTextureUAVRef* slots[] = {&clear->ResolveGBufferA, &clear->ResolveGBufferB, &clear->ResolveGBufferC};
        for (uint32_t i = 0; i < 3; ++i)
        {
            RenderGraphTextureUAVDesc desc;
            desc.Texture = targets[i];
            *slots[i] = builder.CreateTextureUAV(prefix + ".Target" + std::to_string(i), desc);
        }
        clear->ViewOrigin = {view.Viewport.X, view.Viewport.Y};
        clear->ViewSize = {view.Viewport.Width, view.Viewport.Height};
        auto clearShader = GShaderMap.GetShader<VirtualGeometryResolveClearCS>(0);
        auto clearPipeline = ComputePipeline(clearShader);
        builder.AddPass("VirtualGeometryResolveClear", VirtualGeometryResolveTargets::GetMetaData(), clear,
                        EPassFlag::Compute, [=](RHICommandListBase& list)
        {
            auto& cmd = static_cast<RHIComputeCommandList&>(list);
            cmd.SetComputePipelineState(static_cast<RHIComputePipelineState*>(clearPipeline));
            SetShaderParameters(cmd, clearShader, clear);
            cmd.Dispatch((clear->ViewSize.X + 7) / 8, (clear->ViewSize.Y + 7) / 8, 1);
        });

        static_assert(offsetof(Engine::VirtualGeometryCluster, VertexOffset) == 0);
        static_assert(offsetof(Engine::VirtualGeometryCluster, IndexOffset) == 8);
        static_assert(offsetof(Engine::VirtualGeometryCluster, TriangleCount) == 12);
        static_assert(offsetof(Engine::VirtualGeometryCluster, MaterialIndex) == 24);
        static_assert(sizeof(Core::Float3) == 12 && sizeof(Core::Float4) == 16 && sizeof(Core::Float2) == 8);
        for (uint32_t drawID = 0; drawID < visibility->Draws.size(); ++drawID)
        {
            const auto& draw = visibility->Draws[drawID];
            const auto& instance = visibility->Instances.at(draw.InstanceIndex);
            const auto& geometry = visibility->Resources.at(draw.ResourceIndex)->VirtualGeometryData;
            const auto& cluster = geometry.Clusters.at(draw.ClusterID);
            const auto materialIndex =
                cluster.MaterialIndex == Engine::INVALID_VIRTUAL_GEOMETRY_INDEX ? 0 : cluster.MaterialIndex;
            auto material = instance.Materials.at(materialIndex);
            if (!material)
            {
                throw std::invalid_argument("Virtual geometry cluster has no material snapshot");
            }
            material->ValidateSurfacePass();
            const float determinant = Core::Determinant(instance.LocalToWorld);
            if (!std::isfinite(determinant) || std::abs(determinant) < 1e-12f)
            {
                throw std::invalid_argument("Virtual geometry instance transform is singular");
            }
            auto* params = builder.AllocateParameter<VirtualGeometryMaterialResolveParameters>();
            params->Targets = *clear;
            params->Visibility = visibility->VirtualGeometryVisibilityTexture;
            params->VisibilityDepth = visibility->VirtualGeometryDepthTexture;
            params->DrawMetadata = visibility->DrawMetadata;
            params->Positions =
                ScalarBuffer(builder, geometry.PositionBuffer.get(), "VirtualGeometryResolve.Positions.");
            params->Normals = ScalarBuffer(builder, geometry.NormalBuffer.get(), "VirtualGeometryResolve.Normals.");
            params->Tangents = ScalarBuffer(builder, geometry.TangentBuffer.get(), "VirtualGeometryResolve.Tangents.");
            params->TexCoords = ScalarBuffer(builder, geometry.TexCoordBuffer.get(), "VirtualGeometryResolve.UV.");
            params->Indices = ScalarBuffer(builder, geometry.IndexBuffer.get(), "VirtualGeometryResolve.Indices.");
            params->Clusters = ScalarBuffer(builder, geometry.ClusterBuffer.get(), "VirtualGeometryResolve.Clusters.");
            params->LocalToWorld = instance.LocalToWorld;
            params->NormalToWorld = Core::Transpose(Core::Inverse(instance.LocalToWorld));
            params->ViewProjection = view.ViewProjectionMatrix;
            params->CameraPosition = view.CameraWorldPos;
            params->ScreenYSign = GShaderPlatform == ERHIShaderPlatform::D3D12 ? -1.0f : 1.0f;
            params->ResolveDrawID = drawID;
            params->ClusterStrideWords = sizeof(Engine::VirtualGeometryCluster) / sizeof(uint32_t);
            params->TransformHandedness = determinant < 0 ? -1.0f : 1.0f;
            Engine::BuildShaderParameters(material.get(), builder, params->Material);
            Engine::MaterialShaderKey key;
            key.ShaderType =
                static_cast<Engine::MaterialShaderType*>(VirtualGeometryMaterialResolveCS::GetStaticType());
            key.MaterialParameter.ShadingModel = material->GetShadingModel();
            auto shader = Engine::GMaterialShaderMap.GetShader(key);
            auto pipeline = ComputePipeline(shader);
            builder.AddPass("VirtualGeometryMaterialResolvePass",
                            VirtualGeometryMaterialResolveParameters::GetMetaData(), params, EPassFlag::Compute,
                            [=, material = material](RHICommandListBase& list)
            {
                auto& cmd = static_cast<RHIComputeCommandList&>(list);
                cmd.SetComputePipelineState(static_cast<RHIComputePipelineState*>(pipeline));
                SetShaderParameters(cmd, shader, params);
                cmd.Dispatch((params->Targets.ViewSize.X + 7) / 8, (params->Targets.ViewSize.Y + 7) / 8, 1);
            });
        }
        return {gbufferA, gbufferB, gbufferC};
    }

    void AddVirtualGeometryDeferredShadingPass(RenderGraphBuilder& builder, const Engine::SceneView& view, Scene* scene,
                                               const VirtualGeometryMaterialResolveOutput& material,
                                               RenderGraphTextureRef depth, RenderGraphTextureRef target)
    {
        if (!scene || view.Viewport.X || view.Viewport.Y || view.Viewport.Width != target->GetDesc().Width ||
            view.Viewport.Height != target->GetDesc().Height)
        {
            throw std::invalid_argument("Existing deferred shading requires a full-target virtual geometry view");
        }
        auto* params = builder.AllocateParameter<StaticMeshMaterialDefferedShadingCSParameters>();
        const auto prefix = "VirtualGeometryLighting." + std::to_string(reinterpret_cast<uintptr_t>(params));
        if (!EnumHasAnyFlags(target->GetDesc().Usage, ERHITextureCreateFlag::UAV))
        {
            throw std::invalid_argument("Deferred shading requires a UAV scene color target");
        }
        RenderGraphTextureUAVDesc uav;
        uav.Texture = target;
        params->GBuffer.OutputColor = builder.CreateTextureUAV(prefix + ".UAV", uav);
        params->InvViewProj = view.InvViewProjectionMatrix;
        params->CameraPos = view.CameraWorldPos;
        params->ScreenSize = {float(view.Viewport.Width), float(view.Viewport.Height)};
        params->GBuffer.GBufferInput.GBufferA = material.GBufferA;
        params->GBuffer.GBufferInput.GBufferB = material.GBufferB;
        params->GBuffer.GBufferInput.GBufferC = material.GBufferC;
        params->GBuffer.GBufferInput.Depth = depth;
        params->GBuffer.GBufferInput.PointSampler = GlobalSampler.get();
        // Independent consumers do not pass through the main scene renderer's preparation stages.
        SceneRenderer::UploadShadowMapInfo(scene, builder);
        SceneRenderer::UpdateCascadeShadowInfo(scene, builder, view);
        BuildShaderParameters(scene, builder, params->Scene);
        AddStaticMeshDefferedShadingPass(builder, params, false);
        if (scene->GetGPUResourceInfo().LightResourceInfo.IBLSpecularTexture)
        {
            BuildEvnIBLLightParameters(scene, builder, params->EnvIBLParameters);
            AddStaticMeshDefferedShadingPass(builder, params, true);
        }
    }
} // namespace Renderer
