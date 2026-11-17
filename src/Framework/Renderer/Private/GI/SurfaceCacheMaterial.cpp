#include "SurfaceCacheMaterial.h"
#include "../Scene.h"
#include "StaticMeshComponent.h"
#include "StaticMeshProxy.h"
#include "StaticMesh.h"
#include "Transform.hpp"
#include "DistanceFieldMgr.h"
#include "MeshMaterialShader.h"
#include "MaterialShaderParameter.h"
#include "GlobalShader.h"
#include "RHIPipelineStateCache.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

using namespace Core;
using namespace Engine;
using namespace RenderCore;
using namespace RHI;

namespace Renderer
{
    BEGIN_SHADER_PARAMETER_STRUCT(SurfaceCacheCaptureViewParameters)
        SHADER_PARAMETER_STRUCT_REFERENCE(LocalVertexFactoryParameters, Factory)
        SHADER_PARAMETER(Core::Mat4, NormalToWorld)
        SHADER_PARAMETER(Core::Float3, CaptureNormal)
        SHADER_PARAMETER(float, Handedness)
    END_SHADER_PARAMETER_STRUCT(SurfaceCacheCaptureViewParameters)

    BEGIN_SHADER_PARAMETER_STRUCT(SurfaceCacheCapturePSParameters)
        SHADER_PARAMETER_STRUCT_REFERENCE(MaterialShaderParameters, Material)
        SHADER_PARAMETER_STRUCT_REFERENCE(SurfaceCacheCaptureViewParameters, View)
    END_SHADER_PARAMETER_STRUCT(SurfaceCacheCapturePSParameters)

    BEGIN_SHADER_PARAMETER_STRUCT(SurfaceCacheCaptureParameters)
        SHADER_PARAMETER_STRUCT_REFERENCE(SurfaceCacheCaptureViewParameters, Vertex)
        SHADER_PARAMETER_STRUCT_REFERENCE(SurfaceCacheCapturePSParameters, Pixel)
        SHADER_PARAMETER_RENDER_TARGET_BINDING_SLOTS(Targets)
        RDG_BUFFER_ACCESS(Positions, ERHIResourceAccess::VertexOrIndexBuffer)
        RDG_BUFFER_ACCESS(Normals, ERHIResourceAccess::VertexOrIndexBuffer)
        RDG_BUFFER_ACCESS(UV, ERHIResourceAccess::VertexOrIndexBuffer)
        RDG_BUFFER_ACCESS(Tangents, ERHIResourceAccess::VertexOrIndexBuffer)
        RDG_BUFFER_ACCESS(Indices, ERHIResourceAccess::VertexOrIndexBuffer)
    END_SHADER_PARAMETER_STRUCT(SurfaceCacheCaptureParameters)

    BEGIN_SHADER_PARAMETER_STRUCT(SurfaceCacheUploadParameters)
        RDG_BUFFER_ACCESS(Buffer, ERHIResourceAccess::TransferDest)
    END_SHADER_PARAMETER_STRUCT(SurfaceCacheUploadParameters)

    BEGIN_SHADER_PARAMETER_STRUCT(SurfaceCacheQueryParameters)
        SHADER_PARAMETER_STRUCT_REFERENCE(SurfaceCacheMaterialParameters, Cache)
        SHADER_PARAMETER_RDG_TEXTURE(Texture3D, MeshSDFAtlas)
        SHADER_PARAMETER_SAMPLER(SDFSampler)
        SHADER_PARAMETER(Core::Float3, SDFAtlasSize)
        SHADER_PARAMETER(uint32_t, QueryCount)
        SHADER_PARAMETER(uint32_t, MaxSteps)
        SHADER_PARAMETER(float, HitTolerance)
        SHADER_PARAMETER_RDG_STRUCTURED_BUFFER(SurfaceCacheQuery, Queries)
        SHADER_PARAMETER_RDG_RWSTRUCTURED_BUFFER(SurfaceCacheQueryResult, Results)
    END_SHADER_PARAMETER_STRUCT(SurfaceCacheQueryParameters)

    BEGIN_SHADER_PARAMETER_STRUCT(SurfaceCacheDebugParameters)
        SHADER_PARAMETER_STRUCT_REFERENCE(SurfaceCacheMaterialParameters, Cache)
        SHADER_PARAMETER(Core::UInt2, OutputSize)
        SHADER_PARAMETER(uint32_t, Mode)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, Output)
    END_SHADER_PARAMETER_STRUCT(SurfaceCacheDebugParameters)

    BEGIN_SHADER_PARAMETER_STRUCT(SurfaceCacheQueryDebugParameters)
        SHADER_PARAMETER(Core::UInt2, OutputSize)
        SHADER_PARAMETER(uint32_t, QueryCount)
        SHADER_PARAMETER(uint32_t, Mode)
        SHADER_PARAMETER_RDG_STRUCTURED_BUFFER(SurfaceCacheQueryResult, Results)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, Output)
    END_SHADER_PARAMETER_STRUCT(SurfaceCacheQueryDebugParameters)

    class SurfaceCacheCaptureVS : public MeshMaterialShader
    {
    public:
        DECLARE_MESH_MATERIAL_SHADER_TYPE(SurfaceCacheCaptureVS)
        static bool ShouldCompilePermutation(const ShaderPermutationParameters& p)
        {
            const auto& mesh = static_cast<const MeshMaterialShaderPermutationParameters&>(p);
            return mesh.VFType == &LocalVertexFactory::StaticType;
        }
        static void ModifyShaderCompilerEnvironment(const ShaderPermutationParameters&, ShaderCompilerEnvironment& env)
        {
            env.SetDefine("VERTEX_SHADER", 1);
        }
        static const ShaderParametersMetadata* GetShaderParameterMetadata() { return SurfaceCacheCaptureViewParameters::GetMetaData(); }
    };

    class SurfaceCacheCapturePS : public MeshMaterialShader
    {
    public:
        DECLARE_MESH_MATERIAL_SHADER_TYPE(SurfaceCacheCapturePS)
        static bool ShouldCompilePermutation(const ShaderPermutationParameters& p) { return SurfaceCacheCaptureVS::ShouldCompilePermutation(p); }
        static void ModifyShaderCompilerEnvironment(const ShaderPermutationParameters& p, ShaderCompilerEnvironment& env)
        {
            env.SetDefine("PIXEL_SHADER", 1);
            Engine::ModifyShaderCompilerEnvironment(static_cast<const MeshMaterialShaderPermutationParameters&>(p).MaterialParams, env);
        }
        static const ShaderParametersMetadata* GetShaderParameterMetadata() { return SurfaceCacheCapturePSParameters::GetMetaData(); }
    };

    IMPLEMENT_MESH_MATERIAL_SHADER_TYPE(SurfaceCacheCaptureVS, "SurfaceCacheCaptureVS", "/GI/SurfaceCacheCaptureVS.sf", "MainVS", ERHIShaderFrequency::Vertex);
    IMPLEMENT_MESH_MATERIAL_SHADER_TYPE(SurfaceCacheCapturePS, "SurfaceCacheCapturePS", "/GI/SurfaceCacheCapturePS.sf", "MainPS", ERHIShaderFrequency::Fragment);

    class SurfaceCacheQueryCS : public GlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER_TYPE(SurfaceCacheQueryCS)
        static bool ShouldCompilePermutation(const ShaderPermutationParameters&) { return true; }
        static void ModifyShaderCompilerEnvironment(const ShaderPermutationParameters&, ShaderCompilerEnvironment&) {}
        static const ShaderParametersMetadata* GetShaderParameterMetadata() { return SurfaceCacheQueryParameters::GetMetaData(); }
    };

    class SurfaceCacheDebugCS : public GlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER_TYPE(SurfaceCacheDebugCS)
        static bool ShouldCompilePermutation(const ShaderPermutationParameters&) { return true; }
        static void ModifyShaderCompilerEnvironment(const ShaderPermutationParameters&, ShaderCompilerEnvironment&) {}
        static const ShaderParametersMetadata* GetShaderParameterMetadata() { return SurfaceCacheDebugParameters::GetMetaData(); }
    };
    IMPLEMENT_GLOBAL_SHADER_TYPE(SurfaceCacheQueryCS, "SurfaceCacheQueryCS", "/GI/SurfaceCacheQuery.sf", "MainCS", ERHIShaderFrequency::Compute);
    IMPLEMENT_GLOBAL_SHADER_TYPE(SurfaceCacheDebugCS, "SurfaceCacheDebugCS", "/GI/SurfaceCacheDebug.sf", "MainCS", ERHIShaderFrequency::Compute);

    class SurfaceCacheQueryDebugCS : public GlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER_TYPE(SurfaceCacheQueryDebugCS)
        static bool ShouldCompilePermutation(const ShaderPermutationParameters&) { return true; }
        static void ModifyShaderCompilerEnvironment(const ShaderPermutationParameters&, ShaderCompilerEnvironment&) {}
        static const ShaderParametersMetadata* GetShaderParameterMetadata() { return SurfaceCacheQueryDebugParameters::GetMetaData(); }
    };
    IMPLEMENT_GLOBAL_SHADER_TYPE(SurfaceCacheQueryDebugCS, "SurfaceCacheQueryDebugCS", "/GI/SurfaceCacheQueryDebug.sf", "MainCS", ERHIShaderFrequency::Compute);

    namespace
    {
        void Require(bool value, const char* message)
        {
            if (!value)
            {
                throw std::invalid_argument(std::string("Surface Cache: ") + message);
            }
        }

        Float3 XYZ(const Float4& v) { return {v.X, v.Y, v.Z}; }
        Float4 V4(const Float3& v, float w = 0) { return {v.X, v.Y, v.Z, w}; }

        template<class T>
        RenderGraphBufferRef Upload(RenderGraphBuilder& builder, const std::vector<T>& values, const std::string& name)
        {
            auto* snapshot = builder.AllocateParameter<std::vector<T>>();
            *snapshot = values;
            if (snapshot->empty())
            {
                snapshot->emplace_back();
            }
            RenderGraphBufferDesc desc;
            desc.Size = snapshot->size() * sizeof(T);
            desc.Stride = sizeof(T);
            desc.Usage = ERHIBufferUsageFlag::Structured | ERHIBufferUsageFlag::ShaderResource | ERHIBufferUsageFlag::TransferDst;
            auto buffer = builder.CreateBuffer(name, desc);
            auto* params = builder.AllocateParameter<SurfaceCacheUploadParameters>();
            params->Buffer = buffer;
            builder.AddPass("SurfaceCacheUpload", SurfaceCacheUploadParameters::GetMetaData(), params, EPassFlag::Graphic,
                [=](RHICommandListBase& cmd)
            {
                RHIUpdateBufferRegion region;
                region.Size = snapshot->size() * sizeof(T);
                GRHIApi->UpdateBuffer(cmd, buffer->GetRHIBuffer(), snapshot->data(), region);
            });
            return buffer;
        }

        RenderGraphBufferSRVRef SRV(RenderGraphBuilder& builder, RenderGraphBufferRef buffer)
        {
            RenderGraphBufferSRVDesc desc;
            desc.Buffer = buffer;
            desc.Stride = buffer->GetDesc().Stride;
            desc.NumElements = buffer->GetDesc().Size / desc.Stride;
            return builder.CreateBufferSRV(buffer->GetName() + ".SRV", desc);
        }

        RHIRenderPassInfo PassInfo(const RenderTargetBindingSlots& targets, uint32_t size)
        {
            RHIRenderPassInfo info;
            info.RenderTargets = targets.GetBoundRenderTarget();
            info.RenderTargets.DepthStencil.ClearBinding.Depth = 1;
            info.RenderArea.Width = info.RenderArea.Height = size;
            return info;
        }

        Core::Mat4 CardProjection(const SurfaceCard& card)
        {
            Mat4 matrix;
            const Float3 rows[] = {XYZ(card.AxisU) / card.Extent.X, XYZ(card.AxisV) / card.Extent.Y,
                                  XYZ(card.AxisN) * (-0.5f / card.Extent.Z)};
            for (uint32_t row = 0; row < 3; ++row)
            {
                for (uint32_t column = 0; column < 3; ++column)
                {
                    matrix(row, column) = rows[row][column];
                }
                matrix(row, 3) = -Dot(rows[row], XYZ(card.Center)) + (row == 2 ? 0.5f : 0);
            }
            matrix(3, 3) = 1;
            return matrix;
        }
    }

    std::vector<SurfaceCacheMeshInstance> SurfaceCacheMaterial::GatherSceneInstances(Scene& scene)
    {
        std::vector<SurfaceCacheMeshInstance> result;
        const auto& ids = scene.GetGPUResourceInfo().PrimitiveResourceInfo.LocalVertexFactoryInstanceInfo.StaticMeshProxyToInstanceId;
        scene.ForEachPrimitive([&](PrimitiveSceneProxy* primitive)
        {
            if (!primitive || !primitive->IsVisible() || !primitive->IsA<StaticMeshProxy>())
            {
                return;
            }
            auto* proxy = static_cast<StaticMeshProxy*>(primitive);
            const auto* component = proxy->GetStaticMeshComponent();
            const auto* mesh = component ? component->GetStaticMesh() : nullptr;
            if (!mesh || !mesh->GetRenderData())
            {
                return;
            }
            const auto id = ids.find(proxy);
            Require(id != ids.end(), "flush scene before gathering instances");
            SurfaceCacheMeshInstance instance;
            instance.InstanceID = id->second;
            instance.LocalToWorld = proxy->GetLocalToWorld();
            instance.RenderData = mesh->GetRenderData();
            for (uint32_t slot = 0; slot < component->GetNumMaterials(); ++slot)
            {
                const auto* material = component->GetMaterial(slot);
                instance.Materials.push_back(material ? material->GetRenderProxyShared() : nullptr);
            }
            result.push_back(std::move(instance));
        });
        return result;
    }

    void SurfaceCacheMaterial::Initialize(const std::vector<SurfaceCacheMeshInstance>& instances, uint32_t resolution)
    {
        Require(!m_BaseColor, "release the previous snapshot after retiring its GPU users");
        Require(resolution >= 8 && resolution <= 512 && instances.size() <= 1024, "invalid card resolution or instance count");
        std::vector<SurfaceCard> cards;
        std::vector<SurfaceCacheInstance> data;
        std::set<uint32_t> ids;
        const uint32_t columns = std::max(1u, uint32_t(std::ceil(std::sqrt(double(instances.size() * 6)))));
        const uint32_t size = columns * (resolution + 2);
        Require(size <= 8192, "atlas exceeds first-version 8192 limit");
        for (uint32_t index = 0; index < instances.size(); ++index)
        {
            const auto& instance = instances[index];
            Require(instance.InstanceID != UINT32_MAX && ids.insert(instance.InstanceID).second, "invalid or duplicate InstanceID");
            Require(instance.RenderData && !instance.RenderData->LODResources.empty(), "missing LOD0");
            const auto& lod = instance.RenderData->LODResources.front();
            Require(lod.VertexFactory && lod.IndexBuffer.Buffer && !lod.Sections.empty(), "LOD0 resources/sections are not initialized");
            Require(lod.VertexBuffers.PositionBuffer.Buffer && lod.VertexBuffers.NormalBuffer.Buffer && lod.VertexBuffers.UVBuffer.Buffer,
                    "capture requires position, normal and UV streams");
            const auto vertexCount = lod.VertexBuffers.PositionBuffer.GetNumVertices();
            Require(lod.VertexBuffers.NormalBuffer.NumComponents == 3 && lod.VertexBuffers.NormalBuffer.GetNumVertices() == vertexCount &&
                    lod.VertexBuffers.UVBuffer.NumComponents == 2 && lod.VertexBuffers.UVBuffer.GetNumVertices() == vertexCount,
                    "mismatched normal/UV streams");
            for (const auto& section : lod.Sections)
            {
                const uint32_t slot = section.MaterialIndex < 0 ? 0 : uint32_t(section.MaterialIndex);
                Require(slot < instance.Materials.size() && instance.Materials[slot], "missing section material");
                instance.Materials[slot]->ValidateSurfacePass();
                const auto* normalParameter = instance.Materials[slot]->GetParameter("Normal", EMaterialParameterType::Texture);
                const auto normalTexture = std::get<std::shared_ptr<TextureAsset>>(normalParameter->GetValueVariant());
                Require(!normalTexture || (lod.VertexBuffers.TangentBuffer.NumComponents == 4 &&
                    lod.VertexBuffers.TangentBuffer.GetNumVertices() == vertexCount && lod.VertexBuffers.TangentBuffer.Buffer),
                    "normal-mapped capture requires a tangent stream");
                Require(section.NumIndices % 3 == 0 && uint64_t(section.FirstIndex) + section.NumIndices <= lod.IndexBuffer.Indices.size(),
                        "section index range is invalid");
                for (uint32_t i = 0; i < section.NumIndices; ++i)
                {
                    const int64_t vertex = int64_t(lod.IndexBuffer.Indices[section.FirstIndex + i]) + section.BaseVertexIndex;
                    Require(vertex >= 0 && uint64_t(vertex) < vertexCount, "section base vertex/index is invalid");
                }
            }
            const auto& transform = instance.LocalToWorld;
            for (const auto value : transform.Elements)
            {
                Require(std::isfinite(value), "nonfinite transform");
            }
            Require(std::abs(Determinant(transform)) > 1e-10f && transform(3, 0) == 0 && transform(3, 1) == 0 &&
                    transform(3, 2) == 0 && transform(3, 3) == 1, "capture requires an invertible affine transform");
            SurfaceCacheInstance meta;
            meta.WorldToLocal = Inverse(transform);
            meta.NormalToWorld = Transpose(meta.WorldToLocal);
            AABB bounds;
            bounds.SetEmpty();
            const auto& positions = lod.VertexBuffers.PositionBuffer;
            Require(positions.NumComponents == 3 && !positions.Vertices.empty() && positions.Vertices.size() % 3 == 0, "invalid positions");
            for (size_t p = 0; p < positions.Vertices.size(); p += 3)
            {
                const auto world = TransformPoint(transform, Float3(positions.Vertices[p], positions.Vertices[p + 1], positions.Vertices[p + 2]));
                Require(std::isfinite(world.X) && std::isfinite(world.Y) && std::isfinite(world.Z), "nonfinite position");
                bounds.ExpandBy(world);
            }
            meta.BoundsMin = V4(bounds.Min);
            meta.BoundsMax = V4(bounds.Max);
            meta.Info = {instance.InstanceID, uint32_t(cards.size()), 6, 0};
            const auto& sdf = lod.DistanceFieldData;
            const auto& volume = sdf.CPUData.Volume;
            if (sdf.Allocation.IsValid() && sdf.CPUData.IsValid() && volume.ResolutionX > 1 && volume.ResolutionY > 1 &&
                volume.ResolutionZ > 1 && volume.VoxelSize.X > 0 && volume.VoxelSize.Y > 0 && volume.VoxelSize.Z > 0)
            {
                meta.Info.W = 1;
                meta.SDFMin = V4(volume.MinBounds);
                meta.SDFMax = V4(volume.MaxBounds);
                float inverseNorm = 0, forwardNorm = 0;
                for (uint32_t row = 0; row < 3; ++row)
                {
                    for (uint32_t col = 0; col < 3; ++col)
                    {
                        inverseNorm += meta.WorldToLocal(row, col) * meta.WorldToLocal(row, col);
                        forwardNorm += transform(row, col) * transform(row, col);
                    }
                }
                meta.SDFAllocation = {float(sdf.Allocation.X), float(sdf.Allocation.Y), float(sdf.Allocation.Z), 1 / std::sqrt(inverseNorm)};
                meta.SDFResolution = {float(volume.ResolutionX), float(volume.ResolutionY), float(volume.ResolutionZ),
                    std::max({volume.VoxelSize.X, volume.VoxelSize.Y, volume.VoxelSize.Z}) * std::sqrt(forwardNorm) * 1.5f};
            }
            const auto center = bounds.GetCenter();
            const auto extent = (bounds.Max - bounds.Min) * 0.5f;
            const Float3 axes[] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
            for (const auto& normal : axes)
            {
                const auto up = std::abs(normal.Y) > 0.5f ? Float3(0, 0, 1) : Float3(0, 1, 0);
                const auto u = Normalize(Cross(up, normal));
                const auto v = Cross(normal, u);
                const auto projected = [&](const Float3& axis)
                {
                    return std::max(0.001f, std::abs(axis.X) * extent.X + std::abs(axis.Y) * extent.Y + std::abs(axis.Z) * extent.Z) * 1.02f;
                };
                SurfaceCard card;
                card.Center = V4(center, 1);
                card.AxisU = V4(u);
                card.AxisV = V4(v);
                card.AxisN = V4(normal);
                card.Extent = {projected(u), projected(v), projected(normal), 0};
                card.Extent.W = 2 * std::sqrt(card.Extent.X * card.Extent.X + card.Extent.Y * card.Extent.Y) / resolution;
                const uint32_t tile = uint32_t(cards.size());
                card.Tile = {(tile % columns) * (resolution + 2) + 1, (tile / columns) * (resolution + 2) + 1, resolution, resolution};
                card.Owner = {instance.InstanceID, index, tile % 6, 0};
                cards.push_back(card);
            }
            data.push_back(meta);
        }
        const auto texture = [&](ERHIFormat format, const char* name)
        {
            RHITextureDesc desc;
            desc.Width = desc.Height = size;
            desc.Format = format;
            desc.DebugName = name;
            desc.Usage = ERHITextureCreateFlag::ShaderResource | ERHITextureCreateFlag::TransferSrc |
                (format == ERHIFormat::D32_Float ? ERHITextureCreateFlag::DepthStencil : ERHITextureCreateFlag::RenderTarget);
            auto result = std::make_shared<RenderTexture>(desc);
            result->InitRHIResource();
            return result;
        };
        // Publish only after all CPU validation and texture creation succeed.
        auto base = texture(ERHIFormat::R32G32B32A32_Float, "SurfaceCache.BaseColorMetallic");
        auto normal = texture(ERHIFormat::R32G32B32A32_Float, "SurfaceCache.NormalRoughness");
        auto position = texture(ERHIFormat::R32G32B32A32_Float, "SurfaceCache.PositionCoverage");
        auto depth = texture(ERHIFormat::D32_Float, "SurfaceCache.Depth");
        m_Instances = instances;
        m_Cards = std::move(cards);
        m_InstanceData = std::move(data);
        m_BaseColor = std::move(base);
        m_Normal = std::move(normal);
        m_Position = std::move(position);
        m_Depth = std::move(depth);
        m_Size = size;
        m_bCaptureScheduled = false;
    }

    void SurfaceCacheMaterial::Release()
    {
        m_BaseColor.reset();
        m_Normal.reset();
        m_Position.reset();
        m_Depth.reset();
        m_Instances.clear();
        m_Cards.clear();
        m_InstanceData.clear();
        m_Size = 0;
        m_bCaptureScheduled = false;
    }

    SurfaceCacheMaterialParameters SurfaceCacheMaterial::Register(RenderGraphBuilder& builder) const
    {
        Require(m_BaseColor && m_bCaptureScheduled, "capture must be scheduled/executed before lookup");
        SurfaceCacheMaterialParameters result;
        const auto prefix = "SurfaceCache." + std::to_string(reinterpret_cast<uintptr_t>(this));
        result.CardCount = uint32_t(m_Cards.size());
        result.InstanceCount = uint32_t(m_Instances.size());
        result.AtlasSize = {m_Size, m_Size};
        result.ScreenYSign = GShaderPlatform == ERHIShaderPlatform::OpenGL ? 1.0f : -1.0f;
        result.Cards = SRV(builder, Upload(builder, m_Cards, prefix + ".Cards"));
        result.Instances = SRV(builder, Upload(builder, m_InstanceData, prefix + ".Instances"));
        result.BaseColorMetallic = builder.RegisterExternalTexture(prefix + ".BaseColor", m_BaseColor.get());
        result.NormalRoughness = builder.RegisterExternalTexture(prefix + ".Normal", m_Normal.get());
        result.PositionCoverage = builder.RegisterExternalTexture(prefix + ".Position", m_Position.get());
        return result;
    }

    SurfaceCacheMaterialParameters SurfaceCacheMaterial::AddCapturePasses(RenderGraphBuilder& builder)
    {
        Require(m_BaseColor != nullptr, "initialize before capture");
        m_bCaptureScheduled = true;
        auto cache = Register(builder);
        const auto depth = builder.RegisterExternalTexture("SurfaceCache.Depth." + std::to_string(reinterpret_cast<uintptr_t>(this)), m_Depth.get());
        auto* clear = builder.AllocateParameter<SurfaceCacheCaptureParameters>();
        clear->Targets.NumColorRenderTargets = 3;
        const RenderGraphTextureRef colors[] = {cache.BaseColorMetallic, cache.NormalRoughness, cache.PositionCoverage};
        for (uint32_t i = 0; i < 3; ++i)
        {
            clear->Targets[i].Texture = colors[i];
            clear->Targets[i].Action = ERenderTargetActions::Clear_Store;
        }
        clear->Targets.DepthStencil.Texture = depth;
        clear->Targets.DepthStencil.DepthAction = ERenderTargetActions::Clear_Store;
        const auto size = m_Size;
        builder.AddPass("SurfaceCacheClear", SurfaceCacheCaptureParameters::GetMetaData(), clear, EPassFlag::Graphic,
            [=](RHICommandListBase& list)
        {
            auto& cmd = static_cast<RHIGraphicCommandList&>(list);
            cmd.BeginRenderPass(PassInfo(clear->Targets, size));
            cmd.EndRenderPass();
        });
        for (const auto& card : m_Cards)
        {
            const auto instance = m_Instances[card.Owner.Y];
            const auto& lod = instance.RenderData->LODResources.front();
            for (const auto section : lod.Sections)
            {
                auto material = instance.Materials[section.MaterialIndex < 0 ? 0 : section.MaterialIndex];
                MeshMaterialShaderKey key;
                key.VF = &LocalVertexFactory::StaticType;
                key.VertexFactoryFlags = lod.VertexFactory->GetVertexFactoryFlags();
                key.MaterialParameter.ShadingModel = material->GetShadingModel();
                key.ShaderType = static_cast<MeshMaterialShaderType*>(SurfaceCacheCaptureVS::GetStaticType());
                auto vs = material->GetResource()->GetMeshShader(key);
                key.ShaderType = static_cast<MeshMaterialShaderType*>(SurfaceCacheCapturePS::GetStaticType());
                auto ps = material->GetResource()->GetMeshShader(key);
                Require(vs && ps, "capture shader compilation failed");
                RHIGraphicsPipelineStateDesc desc;
                desc.ShaderStages.VertexShader = static_cast<RHIVertexShader*>(vs->GetRHIShader());
                desc.ShaderStages.FragmentShader = static_cast<RHIFragmentShader*>(ps->GetRHIShader());
                desc.VertexDescState = lod.VertexFactory->GetRHIVertexDescState();
                RHIRasterizerStateDesc raster;
                raster.CullMode = ERHICullMode::None; // PS marks back-facing nearest layers uncovered, independent of API winding.
                desc.RasterizerState = RHIPipelineStateCache::GetOrCreateRasterizerState(raster);
                RHIDepthStencilStateDesc depthState;
                depthState.bDepthTestEnable = depthState.bDepthWriteEnable = true;
                depthState.DepthCompareOp = ERHICompareOp::Less;
                desc.DepthStencilState = RHIPipelineStateCache::GetOrCreateDepthStencilState(depthState);
                RHIColorBlendStateDesc blend;
                blend.Attachments.resize(3);
                desc.ColorBlendState = RHIPipelineStateCache::GetOrCreateColorBlendState(blend);
                desc.AttachmentDesc.ColorAttachmentCount = 3;
                for (uint32_t i = 0; i < 3; ++i)
                {
                    desc.AttachmentDesc.ColorAttachments[i].Format = ERHIFormat::R32G32B32A32_Float;
                    desc.AttachmentDesc.ColorAttachments[i].Actions = ERenderTargetActions::Load_Store;
                }
                desc.AttachmentDesc.bEnableDepth = true;
                desc.AttachmentDesc.DepthStencilFormat = ERHIFormat::D32_Float;
                desc.AttachmentDesc.DepthActions = ERenderTargetActions::Load_Store;
                auto pipeline = RHIPipelineStateCache::GetOrCreateGraphicsPipelineState(desc);
                auto* params = builder.AllocateParameter<SurfaceCacheCaptureParameters>();
                params->Targets = clear->Targets;
                for (uint32_t i = 0; i < 3; ++i)
                {
                    params->Targets[i].Action = ERenderTargetActions::Load_Store;
                }
                params->Targets.DepthStencil.DepthAction = ERenderTargetActions::Load_Store;
                params->Vertex.Factory.LocalToWorld = instance.LocalToWorld;
                params->Vertex.Factory.ViewProjection = CardProjection(card);
                params->Vertex.Factory.CameraWorldPosition = XYZ(card.Center) + XYZ(card.AxisN) * card.Extent.Z;
                params->Vertex.NormalToWorld = m_InstanceData[card.Owner.Y].NormalToWorld;
                params->Vertex.Handedness = Determinant(instance.LocalToWorld) < 0 ? -1.0f : 1.0f;
                params->Vertex.CaptureNormal = XYZ(card.AxisN);
                params->Pixel.View = params->Vertex;
                BuildShaderParameters(material.get(), builder, params->Pixel.Material);
                const auto vertex = [&](RenderBuffer* buffer)
                {
                    return buffer ? builder.RegisterExternalBuffer("SurfaceCache.Vertex." + std::to_string(reinterpret_cast<uintptr_t>(buffer)), buffer) : nullptr;
                };
                params->Positions = vertex(lod.VertexBuffers.PositionBuffer.Buffer.get());
                params->Normals = vertex(lod.VertexBuffers.NormalBuffer.Buffer.get());
                params->UV = vertex(lod.VertexBuffers.UVBuffer.Buffer.get());
                params->Tangents = vertex(lod.VertexBuffers.TangentBuffer.Buffer.get());
                params->Indices = vertex(lod.IndexBuffer.Buffer.get());
                builder.AddPass("SurfaceCacheMaterialCapture", SurfaceCacheCaptureParameters::GetMetaData(), params, EPassFlag::Graphic,
                    [=](RHICommandListBase& list)
                {
                    auto& cmd = static_cast<RHIGraphicCommandList&>(list);
                    const auto& mesh = instance.RenderData->LODResources.front();
                    cmd.SetGraphicPipelineState(pipeline);
                    mesh.VertexFactory->Bind(cmd);
                    cmd.BeginRenderPass(PassInfo(params->Targets, size));
                    // BeginRenderPass may initialize dynamic viewport/scissor to the full atlas.
                    cmd.SetViewport(card.Tile.X, card.Tile.Y, card.Tile.Z, card.Tile.W, 0, 1);
                    cmd.SetScissor(card.Tile.X, card.Tile.Y, card.Tile.Z, card.Tile.W);
                    SetShaderParameters(cmd, vs, &params->Vertex);
                    SetShaderParameters(cmd, ps, &params->Pixel);
                    cmd.DrawIndexed(mesh.IndexBuffer.Buffer->GetRHI(), section.NumIndices, 1, section.FirstIndex, section.BaseVertexIndex, 0);
                    cmd.EndRenderPass();
                });
            }
        }
        return cache;
    }

    RenderGraphBufferRef AddSurfaceCacheQueryPass(RenderGraphBuilder& builder, const SurfaceCacheMaterialParameters& cache,
        const std::vector<SurfaceCacheQuery>& queries, uint32_t maxSteps, float hitTolerance)
    {
        Require(!queries.empty() && queries.size() <= 65535 * 64 && maxSteps > 0 && maxSteps <= 4096 &&
                std::isfinite(hitTolerance) && hitTolerance > 0, "invalid query settings");
        auto* atlas = GDistanceFieldMgr.GetAtlas().GetAtlasTexture();
        Require(atlas && atlas->GetRHI(), "query requires initialized Engine distance field atlas");
        for (const auto& query : queries)
        {
            Require(query.Control.Y <= 1 && query.OriginDistance.W >= 0 && std::isfinite(query.OriginDistance.W), "invalid query mode/distance");
            for (uint32_t axis = 0; axis < 3; ++axis)
            {
                Require(std::isfinite(query.OriginDistance[axis]) && std::isfinite(query.Direction[axis]), "nonfinite query");
            }
            Require(XYZ(query.Direction).LengthSquared() > 1e-12f, "zero query direction/normal");
        }
        auto* params = builder.AllocateParameter<SurfaceCacheQueryParameters>();
        params->Cache = cache;
        params->QueryCount = uint32_t(queries.size());
        params->MaxSteps = maxSteps;
        params->HitTolerance = hitTolerance;
        params->MeshSDFAtlas = builder.RegisterExternalTexture("SurfaceCache.MeshSDFAtlas", atlas);
        const auto& atlasDesc = atlas->GetRHI()->GetDesc();
        params->SDFAtlasSize = {float(atlasDesc.Width), float(atlasDesc.Height), float(atlasDesc.Depth)};
        params->SDFSampler = GlobalSampler.get();
        const auto prefix = "SurfaceCache.Query." + std::to_string(reinterpret_cast<uintptr_t>(params));
        params->Queries = SRV(builder, Upload(builder, queries, prefix + ".Input"));
        RenderGraphBufferDesc desc;
        desc.Size = sizeof(SurfaceCacheQueryResult) * queries.size();
        desc.Stride = sizeof(SurfaceCacheQueryResult);
        desc.Usage = ERHIBufferUsageFlag::Structured | ERHIBufferUsageFlag::UnorderedAccess |
                     ERHIBufferUsageFlag::ShaderResource | ERHIBufferUsageFlag::TransferSrc;
        auto results = builder.CreateBuffer(prefix + ".Results", desc);
        RenderGraphBufferUAVDesc uav;
        uav.Buffer = results;
        uav.Stride = desc.Stride;
        uav.NumElements = queries.size();
        params->Results = builder.CreateBufferUAV(prefix + ".UAV", uav);
        auto shader = GShaderMap.GetShader<SurfaceCacheQueryCS>(0);
        Require(shader != nullptr, "query shader is unavailable");
        RHIComputePipelineStateDesc pipelineDesc;
        pipelineDesc.ComputeShader = static_cast<RHIComputeShader*>(shader->GetRHIShader());
        auto pipeline = RHIPipelineStateCache::GetOrCreateComputePipelineState(pipelineDesc);
        builder.AddPass("SurfaceCacheSDFQuery", SurfaceCacheQueryParameters::GetMetaData(), params, EPassFlag::Compute,
            [=](RHICommandListBase& list)
        {
            auto& cmd = static_cast<RHIComputeCommandList&>(list);
            cmd.SetComputePipelineState(pipeline);
            SetShaderParameters(cmd, shader, params);
            cmd.Dispatch((params->QueryCount + 63) / 64, 1, 1);
        });
        return results;
    }

    void AddSurfaceCacheDebugPass(RenderGraphBuilder& builder, const SurfaceCacheMaterialParameters& cache,
        RenderGraphTextureRef output, uint32_t mode)
    {
        Require(output && mode <= 4 && output->GetDesc().Format == ERHIFormat::R32G32B32A32_Float, "debug requires float4 UAV target and mode 0..4");
        auto* p = builder.AllocateParameter<SurfaceCacheDebugParameters>();
        p->Cache = cache;
        p->Mode = mode;
        p->OutputSize = {output->GetDesc().Width, output->GetDesc().Height};
        RenderGraphTextureUAVDesc desc;
        desc.Texture = output;
        desc.Format = output->GetDesc().Format;
        p->Output = builder.CreateTextureUAV(output->GetName() + ".SurfaceDebug", desc);
        auto shader = GShaderMap.GetShader<SurfaceCacheDebugCS>(0);
        Require(shader != nullptr, "debug shader is unavailable");
        RHIComputePipelineStateDesc pipelineDesc;
        pipelineDesc.ComputeShader = static_cast<RHIComputeShader*>(shader->GetRHIShader());
        auto pipeline = RHIPipelineStateCache::GetOrCreateComputePipelineState(pipelineDesc);
        builder.AddPass("SurfaceCacheDebug", SurfaceCacheDebugParameters::GetMetaData(), p, EPassFlag::Compute,
            [=](RHICommandListBase& list)
        {
            auto& cmd = static_cast<RHIComputeCommandList&>(list);
            cmd.SetComputePipelineState(pipeline);
            SetShaderParameters(cmd, shader, p);
            cmd.Dispatch((p->OutputSize.X + 7) / 8, (p->OutputSize.Y + 7) / 8, 1);
        });
    }

    void AddSurfaceCacheQueryDebugPass(RenderGraphBuilder& builder, RenderGraphBufferRef results,
        uint32_t count, RenderGraphTextureRef output, uint32_t mode)
    {
        Require(results && count > 0 && uint64_t(count) * sizeof(SurfaceCacheQueryResult) <= results->GetDesc().Size &&
                results->GetDesc().Stride == sizeof(SurfaceCacheQueryResult) && output &&
                output->GetDesc().Format == ERHIFormat::R32G32B32A32_Float && mode <= 2, "invalid query debug resources");
        auto* p = builder.AllocateParameter<SurfaceCacheQueryDebugParameters>();
        p->OutputSize = {output->GetDesc().Width, output->GetDesc().Height};
        p->QueryCount = count;
        p->Mode = mode;
        p->Results = SRV(builder, results);
        RenderGraphTextureUAVDesc desc;
        desc.Texture = output;
        desc.Format = output->GetDesc().Format;
        p->Output = builder.CreateTextureUAV(output->GetName() + ".QueryDebug", desc);
        auto shader = GShaderMap.GetShader<SurfaceCacheQueryDebugCS>(0);
        Require(shader != nullptr, "query debug shader is unavailable");
        RHIComputePipelineStateDesc pipelineDesc;
        pipelineDesc.ComputeShader = static_cast<RHIComputeShader*>(shader->GetRHIShader());
        auto pipeline = RHIPipelineStateCache::GetOrCreateComputePipelineState(pipelineDesc);
        builder.AddPass("SurfaceCacheQueryDebug", SurfaceCacheQueryDebugParameters::GetMetaData(), p, EPassFlag::Compute,
            [=](RHICommandListBase& list)
        {
            auto& cmd = static_cast<RHIComputeCommandList&>(list);
            cmd.SetComputePipelineState(pipeline);
            SetShaderParameters(cmd, shader, p);
            cmd.Dispatch((p->OutputSize.X + 7) / 8, (p->OutputSize.Y + 7) / 8, 1);
        });
    }
}
