#include "GIRayTracing.h"

#include "GlobalShader.h"
#include "Log.h"
#include "RHIPipelineStateCache.h"
#include "ShaderCompiler.h"

#include <algorithm>
#include <unordered_map>

namespace Renderer
{
    using namespace RenderCore;
    using namespace RHI;

    BEGIN_SHADER_PARAMETER_STRUCT(GIHardwareHit)
        SHADER_PARAMETER(Core::Float4, DistanceBarycentric)
        SHADER_PARAMETER(Core::UInt4, IDs)
    END_SHADER_PARAMETER_STRUCT(GIHardwareHit)

    BEGIN_SHADER_PARAMETER_STRUCT(GIHardwareTriangle)
        SHADER_PARAMETER(Core::Float4, Edge1)
        SHADER_PARAMETER(Core::Float4, Edge2)
        SHADER_PARAMETER(Core::Float4, Normal0)
        SHADER_PARAMETER(Core::Float4, Normal1)
        SHADER_PARAMETER(Core::Float4, Normal2)
        SHADER_PARAMETER(Core::UInt4, Info)
    END_SHADER_PARAMETER_STRUCT(GIHardwareTriangle)

    BEGIN_SHADER_PARAMETER_STRUCT(GIHardwareInstance)
        SHADER_PARAMETER(Core::Mat4, LocalToWorld)
        SHADER_PARAMETER(Core::Mat4, NormalToWorld)
        SHADER_PARAMETER(Core::UInt4, Info)
    END_SHADER_PARAMETER_STRUCT(GIHardwareInstance)

    BEGIN_SHADER_PARAMETER_STRUCT(GIHardwareMaterial)
        SHADER_PARAMETER(Core::Float4, BaseMetal)
        SHADER_PARAMETER(Core::Float4, Surface)
        SHADER_PARAMETER(Core::UInt4, Flags)
    END_SHADER_PARAMETER_STRUCT(GIHardwareMaterial)

    BEGIN_SHADER_PARAMETER_STRUCT(GIHardwareDispatchParameters)
        SHADER_PARAMETER_RDG_STRUCTURED_BUFFER(SurfaceCacheQuery, HardwareQueries)
        SHADER_PARAMETER_RDG_RWSTRUCTURED_BUFFER(GIHardwareHit, HardwareHits)
    END_SHADER_PARAMETER_STRUCT(GIHardwareDispatchParameters)

    BEGIN_SHADER_PARAMETER_STRUCT(GIHardwareUploadParameters)
        RDG_BUFFER_ACCESS(Triangles, ERHIResourceAccess::TransferDest)
        RDG_BUFFER_ACCESS(Instances, ERHIResourceAccess::TransferDest)
        RDG_BUFFER_ACCESS(Materials, ERHIResourceAccess::TransferDest)
    END_SHADER_PARAMETER_STRUCT(GIHardwareUploadParameters)

    BEGIN_SHADER_PARAMETER_STRUCT(GIHardwareResolveParameters)
        SHADER_PARAMETER_STRUCT_REFERENCE(SurfaceCacheMaterialParameters, Cache)
        SHADER_PARAMETER(uint32_t, QueryCount)
        SHADER_PARAMETER_RDG_STRUCTURED_BUFFER(SurfaceCacheQuery, ResolveQueries)
        SHADER_PARAMETER_RDG_STRUCTURED_BUFFER(GIHardwareHit, TraceHits)
        SHADER_PARAMETER_RDG_STRUCTURED_BUFFER(GIHardwareInstance, TraceInstances)
        SHADER_PARAMETER_RDG_STRUCTURED_BUFFER(GIHardwareTriangle, TraceTriangles)
        SHADER_PARAMETER_RDG_STRUCTURED_BUFFER(GIHardwareMaterial, TraceMaterials)
        SHADER_PARAMETER_RDG_RWSTRUCTURED_BUFFER(SurfaceCacheQueryResult, TraceResults)
    END_SHADER_PARAMETER_STRUCT(GIHardwareResolveParameters)

    class GIHardwareResolveCS : public GlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER_TYPE(GIHardwareResolveCS)
        static bool ShouldCompilePermutation(const ShaderPermutationParameters&) { return GIRayTracingScene::IsSupported(); }
        static void ModifyShaderCompilerEnvironment(const ShaderPermutationParameters&, ShaderCompilerEnvironment&) {}
        static const ShaderParametersMetadata* GetShaderParameterMetadata() { return GIHardwareResolveParameters::GetMetaData(); }
    };

    IMPLEMENT_GLOBAL_SHADER_TYPE(GIHardwareResolveCS, "GIHardwareResolveCS", "/GI/GIHardwareResolve.sf", "MainCS", ERHIShaderFrequency::Compute);

    namespace
    {
        struct BLAS
        {
            std::shared_ptr<Engine::StaticMeshRenderData> Mesh;
            std::vector<uint32_t> Indices;
            std::vector<GIHardwareTriangle> Triangles;
            RHIBufferSP Positions, IndexBuffer;
            RHIRayTracingGeometrySP Geometry;
            bool bBuildScheduled = false;
        };

        struct BuildTransitions
        {
            // The deferred command list consumes these during graph execution.
            std::unique_ptr<std::byte[]> Storage;
            RHITransition* Transition = nullptr;

            ~BuildTransitions()
            {
                if (Transition)
                {
                    GRHIApi->RHIReleaseTransition(Transition);
                }
            }

            void Initialize(std::vector<RHITransitionInfo> infos)
            {
                Storage = std::make_unique<std::byte[]>(G_RHITransition_TotalSize);
                Transition = new (Storage.get()) RHITransition();
                GRHIApi->RHICreateTransition(Transition, {ERHITransitionCreateFlags::None, std::move(infos)});
            }

            void Record(RHIGraphicCommandList& cmd) const
            {
                cmd.BeginTransitions({Transition});
                cmd.EndTransitions({Transition});
            }
        };

        Core::Float4 ReadVertex(const Engine::VertexBuffer& vertices, uint32_t index)
        {
            if (vertices.NumComponents < 3 || index >= vertices.GetNumVertices())
            {
                return {};
            }
            const auto* values = vertices.Vertices.data() + size_t(vertices.NumComponents) * index;
            return {values[0], values[1], values[2], 0.0f};
        }

        bool InitializeBLAS(BLAS& mesh)
        {
            const auto& lod = mesh.Mesh->LODResources.front();
            const auto& positions = lod.VertexBuffers.PositionBuffer;
            const auto& normals = lod.VertexBuffers.NormalBuffer;
            const uint64_t vertexCount = positions.GetNumVertices();
            if (positions.NumComponents != 3 || vertexCount == 0 || positions.Vertices.size() > UINT32_MAX / sizeof(float))
            {
                return false;
            }

            for (uint32_t slot = 0; slot < lod.Sections.size(); ++slot)
            {
                const auto& section = lod.Sections[slot];
                const auto end = uint64_t(section.FirstIndex) + section.NumIndices;
                if (section.NumIndices % 3 != 0 || end > lod.IndexBuffer.Indices.size() ||
                    uint64_t(mesh.Indices.size()) + section.NumIndices > UINT32_MAX / sizeof(uint32_t))
                {
                    return false;
                }
                for (uint64_t first = section.FirstIndex; first < end; first += 3)
                {
                    uint32_t indices[3];
                    for (uint32_t i = 0; i < 3; ++i)
                    {
                        const auto index = int64_t(lod.IndexBuffer.Indices[first + i]) + section.BaseVertexIndex;
                        if (index < 0 || uint64_t(index) >= vertexCount)
                        {
                            return false;
                        }
                        indices[i] = static_cast<uint32_t>(index);
                    }
                    mesh.Indices.insert(mesh.Indices.end(), std::begin(indices), std::end(indices));

                    const auto p0 = ReadVertex(positions, indices[0]);
                    GIHardwareTriangle triangle{};
                    triangle.Edge1 = ReadVertex(positions, indices[1]) - p0;
                    triangle.Edge2 = ReadVertex(positions, indices[2]) - p0;
                    triangle.Normal0 = ReadVertex(normals, indices[0]);
                    triangle.Normal1 = ReadVertex(normals, indices[1]);
                    triangle.Normal2 = ReadVertex(normals, indices[2]);
                    triangle.Info.X = slot;
                    mesh.Triangles.push_back(triangle);
                }
            }
            if (mesh.Indices.empty())
            {
                return false;
            }

            RHIBufferDesc vertices{};
            vertices.Size = positions.Vertices.size() * sizeof(float);
            vertices.Stride = sizeof(float) * 3;
            vertices.Usage = ERHIBufferUsageFlag::Vertex | ERHIBufferUsageFlag::TransferDst |
                ERHIBufferUsageFlag::AccelerationStructureBuildInput;
            mesh.Positions = GRHIApi->CreateBuffer(vertices);
            RHIBufferDesc indices{};
            indices.Size = mesh.Indices.size() * sizeof(uint32_t);
            indices.Stride = sizeof(uint32_t);
            indices.Usage = ERHIBufferUsageFlag::Index | ERHIBufferUsageFlag::TransferDst |
                ERHIBufferUsageFlag::AccelerationStructureBuildInput;
            mesh.IndexBuffer = GRHIApi->CreateBuffer(indices);
            if (!mesh.Positions || !mesh.IndexBuffer)
            {
                return false;
            }

            RHIRayTracingGeometryDesc desc;
            desc.VertexBuffer = mesh.Positions.get();
            desc.IndexBuffer = mesh.IndexBuffer.get();
            desc.VertexStride = vertices.Stride;
            desc.VertexCount = static_cast<uint32_t>(vertexCount);
            desc.IndexCount = static_cast<uint32_t>(mesh.Indices.size());
            mesh.Geometry = GRHIApi->CreateRayTracingGeometry(desc);
            return mesh.Geometry && mesh.Geometry->GetSizeInfo().ResultSize != 0;
        }

        GIHardwareMaterial PackMaterial(const Engine::MaterialRenderProxy* proxy)
        {
            using namespace Engine;
            using Semantic = EMaterialParameterSemantic;
            GIHardwareMaterial result{};
            FVectorType::Type color{1, 1, 1, 1};
            float metallic = 0.8f, roughness = 0.2f;
            if (proxy)
            {
                proxy->GetValue<Semantic::BaseColor, FVectorType>("BaseColor", color);
                proxy->GetValue<Semantic::Metallic, FScalarType>("Metallic", metallic);
                proxy->GetValue<Semantic::Roughness, FScalarType>("Roughness", roughness);

                FTextureType::Type base, orm, normal;
                proxy->GetValue<Semantic::BaseColor, FTextureType>("BaseColor", base);
                proxy->GetValue<Semantic::Custom, FTextureType>("ORM", orm);
                proxy->GetValue<Semantic::Normal, FTextureType>("Normal", normal);
                result.Flags = {base || orm || normal ? 1u : 0u,
                    proxy->GetShadingModel() == EShadingModel::Unlit ? 1u : 0u, normal ? 1u : 0u, 0u};
            }
            result.BaseMetal = {color[0], color[1], color[2], metallic};
            result.Surface.X = roughness;
            return result;
        }

        bool SameInstances(const std::vector<SurfaceCacheMeshInstance>& a, const std::vector<SurfaceCacheMeshInstance>& b)
        {
            return std::ranges::equal(a, b, [](const auto& lhs, const auto& rhs)
            {
                return lhs.InstanceID == rhs.InstanceID && lhs.RenderData == rhs.RenderData &&
                    std::ranges::equal(lhs.LocalToWorld.Elements, rhs.LocalToWorld.Elements) && lhs.Materials == rhs.Materials;
            });
        }

        template<class T>
        std::unique_ptr<RenderBuffer> CreateMetadataBuffer(const std::vector<T>& data)
        {
            RHIBufferDesc desc{};
            desc.Size = sizeof(T) * data.size();
            desc.Stride = sizeof(T);
            desc.Usage = ERHIBufferUsageFlag::Structured | ERHIBufferUsageFlag::ShaderResource | ERHIBufferUsageFlag::TransferDst;
            auto buffer = std::make_unique<RenderBuffer>(desc);
            buffer->InitRHIResource();
            return buffer;
        }

        RenderGraphBufferRef CreateTraceBuffer(RenderGraphBuilder& builder, const char* name, uint32_t stride, uint32_t count)
        {
            RenderGraphBufferDesc desc;
            desc.Size = uint64_t(stride) * count;
            desc.Stride = stride;
            desc.Usage = ERHIBufferUsageFlag::Structured | ERHIBufferUsageFlag::ShaderResource |
                ERHIBufferUsageFlag::UnorderedAccess | ERHIBufferUsageFlag::TransferSrc;
            return builder.CreateBuffer(name, desc);
        }

        RenderGraphBufferSRVRef ReadBuffer(RenderGraphBuilder& builder, const std::string& name,
            RenderGraphBufferRef buffer, uint32_t count)
        {
            RenderGraphBufferSRVDesc desc;
            desc.Buffer = buffer;
            desc.Stride = buffer->GetDesc().Stride;
            desc.NumElements = count;
            return builder.CreateBufferSRV(name, desc);
        }

        RenderGraphBufferUAVRef WriteBuffer(RenderGraphBuilder& builder, const std::string& name, RenderGraphBufferRef buffer)
        {
            RenderGraphBufferUAVDesc desc;
            desc.Buffer = buffer;
            desc.Stride = buffer->GetDesc().Stride;
            desc.NumElements = uint32_t(buffer->GetDesc().Size / desc.Stride);
            return builder.CreateBufferUAV(name, desc);
        }
    }

    struct GIRayTracingScene::State
    {
        std::unordered_map<const Engine::StaticMeshRenderData*, BLAS> Meshes;
        std::vector<SurfaceCacheMeshInstance> Snapshot;
        std::vector<GIHardwareTriangle> Triangles;
        std::vector<GIHardwareInstance> Instances;
        std::vector<GIHardwareMaterial> Materials;
        std::unique_ptr<RenderBuffer> TriangleBuffer, InstanceBuffer, MaterialBuffer;
        RHIRayTracingInstanceSP TLAS;
        RHIRayTracingPipelineStateSP Pipeline;
        std::shared_ptr<RHIRayGenShader> RayGen;
        std::shared_ptr<RHIMissShader> Miss;
        std::shared_ptr<RHICloseHitShader> ClosestHit;
        uint32_t SceneBinding = 0, QueryBinding = 0, HitBinding = 0;
        uint64_t Revision = 0;
        bool bReady = false, bBuildScheduled = false, bMetadataUploaded = false;

        bool InitializePipeline()
        {
            if (Pipeline)
            {
                return true;
            }

            ShaderCompileInput input;
            input.Platform = GShaderPlatform;
            input.VirtualSourceFilePath = "/GI/GIRayTracing.sf";
            ShaderCompiler::GenerateOrGetShaderPrameterMetaDataSF(*GIHardwareDispatchParameters::GetMetaData());
            input.Frequency = ERHIShaderFrequency::RayGen;
            input.EntryPoint = "RayGenMain";
            input.Environment.SetDefine("GI_RAY_STAGE", 0);
            const auto rayGen = ShaderCompiler::Compile(input);
            input.Frequency = ERHIShaderFrequency::Miss;
            input.EntryPoint = "MissMain";
            input.Environment.SetDefine("GI_RAY_STAGE", 1);
            const auto miss = ShaderCompiler::Compile(input);
            input.Frequency = ERHIShaderFrequency::ClosestHit;
            input.EntryPoint = "ClosestHitMain";
            input.Environment.SetDefine("GI_RAY_STAGE", 2);
            const auto closestHit = ShaderCompiler::Compile(input);
            if (!rayGen.bSuccess || !miss.bSuccess || !closestHit.bSuccess)
            {
                LOG_WARN("GI hardware tracing shader compilation failed. Raygen: %s, miss: %s, closest hit: %s",
                    rayGen.ErrorMessage.c_str(), miss.ErrorMessage.c_str(), closestHit.ErrorMessage.c_str());
                return false;
            }

            const auto sceneBinding = rayGen.ParameterMap.FindParameterAllocation("HardwareScene");
            const auto queryBinding = rayGen.ParameterMap.FindParameterAllocation("HardwareQueries");
            const auto hitBinding = rayGen.ParameterMap.FindParameterAllocation("HardwareHits");
            if (!sceneBinding || !queryBinding || !hitBinding)
            {
                return false;
            }
            SceneBinding = sceneBinding->BaseIndex;
            QueryBinding = queryBinding->BaseIndex;
            HitBinding = hitBinding->BaseIndex;
            RayGen = GRHIApi->CreateRayGenShader(rayGen.PackedBinaryData);
            Miss = GRHIApi->CreateMissShader(miss.PackedBinaryData);
            ClosestHit = GRHIApi->CreateCloseHitShader(closestHit.PackedBinaryData);
            if (!RayGen || !Miss || !ClosestHit)
            {
                return false;
            }

            RHIRayTracingPipelineStateDesc desc;
            desc.RayGenTable = {RayGen.get()};
            desc.MissTable = {Miss.get()};
            desc.HitGroupTable = {{ClosestHit.get(), nullptr, nullptr}};
            Pipeline = GRHIApi->CreateRayTracingsPipelineState(desc);
            return Pipeline != nullptr;
        }
    };

    GIRayTracingScene::GIRayTracingScene() : m_State(std::make_unique<State>()) {}
    GIRayTracingScene::~GIRayTracingScene() = default;

    bool GIRayTracingScene::IsSupported()
    {
        return GRHIApi && GShaderPlatform == ERHIShaderPlatform::Vulkan && GRHIApi->GetPlatformInfo().bEnableRayTracing;
    }

    uint32_t GIRayTracingScene::GetBLASCount() const { return uint32_t(m_State->Meshes.size()); }
    uint32_t GIRayTracingScene::GetInstanceCount() const { return m_State->TLAS ? uint32_t(m_State->Instances.size()) : 0; }
    uint64_t GIRayTracingScene::GetTLASRevision() const { return m_State->Revision; }

    bool GIRayTracingScene::Update(const std::vector<SurfaceCacheMeshInstance>& instances)
    {
        auto& state = *m_State;
        if (!IsSupported())
        {
            return false;
        }
        if (SameInstances(state.Snapshot, instances))
        {
            return state.bReady;
        }
        state.Snapshot = instances;
        state.bReady = false;
        state.bBuildScheduled = false;
        state.bMetadataUploaded = false;
        state.TLAS.reset();
        state.Instances.clear();
        state.Materials.clear();
        state.Triangles.clear();
        state.InstanceBuffer.reset();
        state.MaterialBuffer.reset();
        state.TriangleBuffer.reset();

        std::erase_if(state.Meshes, [&](const auto& item)
        {
            return std::ranges::none_of(instances, [&](const auto& instance) { return instance.RenderData.get() == item.first; });
        });
        if (instances.empty() || instances.size() > 0x1000000 || !GShaderMap.GetShader<GIHardwareResolveCS>(0) || !state.InitializePipeline())
        {
            return false;
        }

        // The existing RHI exposes opaque triangle geometry. Preserve SDF for snapshots requiring masked/translucent hits.
        for (const auto& source : instances)
        {
            if (!source.RenderData || source.RenderData->LODResources.empty() ||
                std::ranges::any_of(source.Materials, [](const auto& material)
                {
                    return material && material->GetBlendMode() != Engine::EBlendMode::Opaque;
                }))
            {
                return false;
            }
        }

        RHIRayTracingInstancesDesc tlas;
        std::unordered_map<const Engine::StaticMeshRenderData*, uint32_t> triangleOffsets;
        for (const auto& source : instances)
        {
            auto [found, inserted] = state.Meshes.try_emplace(source.RenderData.get());
            auto& mesh = found->second;
            if (inserted)
            {
                mesh.Mesh = source.RenderData;
                if (!InitializeBLAS(mesh))
                {
                    state.Meshes.erase(found);
                    return false;
                }
            }
            auto [offset, newMesh] = triangleOffsets.try_emplace(source.RenderData.get(), uint32_t(state.Triangles.size()));
            if (newMesh)
            {
                if (state.Triangles.size() + mesh.Triangles.size() > UINT32_MAX / sizeof(GIHardwareTriangle))
                {
                    return false;
                }
                state.Triangles.insert(state.Triangles.end(), mesh.Triangles.begin(), mesh.Triangles.end());
            }

            GIHardwareInstance instance{};
            instance.LocalToWorld = source.LocalToWorld;
            instance.NormalToWorld = Transpose(Inverse(source.LocalToWorld));
            instance.Info = {source.InstanceID, offset->second, uint32_t(state.Materials.size()), 0u};
            const auto& sections = source.RenderData->LODResources.front().Sections;
            if (state.Materials.size() + sections.size() > UINT32_MAX / sizeof(GIHardwareMaterial))
            {
                return false;
            }
            for (const auto& section : sections)
            {
                const uint32_t slot = section.MaterialIndex < 0 ? 0 : uint32_t(section.MaterialIndex);
                state.Materials.push_back(PackMaterial(slot < source.Materials.size() ? source.Materials[slot].get() : nullptr));
            }
            RHIRayTracingInstanceDesc nativeInstance;
            nativeInstance.Geometry = mesh.Geometry.get();
            nativeInstance.Transform = source.LocalToWorld;
            nativeInstance.InstanceID = uint32_t(state.Instances.size());
            tlas.Instances.push_back(nativeInstance);
            state.Instances.push_back(instance);
        }

        state.TLAS = GRHIApi->CreateRayTracingInstance(tlas);
        if (!state.TLAS || state.TLAS->GetSizeInfo().ResultSize == 0)
        {
            state.TLAS.reset();
            return false;
        }
        state.TriangleBuffer = CreateMetadataBuffer(state.Triangles);
        state.InstanceBuffer = CreateMetadataBuffer(state.Instances);
        state.MaterialBuffer = CreateMetadataBuffer(state.Materials);
        ++state.Revision;
        state.bReady = true;
        return true;
    }

    RenderGraphBufferRef GIRayTracingScene::AddTracePasses(RenderGraphBuilder& builder,
        const SurfaceCacheMaterialParameters& material, RenderGraphBufferRef queries, uint32_t count)
    {
        auto& state = *m_State;
        const auto name = queries->GetName() + ".Hardware";
        auto hits = CreateTraceBuffer(builder, (name + ".Hits").c_str(), sizeof(GIHardwareHit), count);
        auto results = CreateTraceBuffer(builder, (name + ".Results").c_str(), sizeof(SurfaceCacheQueryResult), count);
        auto* dispatch = builder.AllocateParameter<GIHardwareDispatchParameters>();
        dispatch->HardwareQueries = ReadBuffer(builder, name + ".QueriesSRV", queries, count);
        dispatch->HardwareHits = WriteBuffer(builder, name + ".HitsUAV", hits);

        auto triangles = builder.RegisterExternalBuffer("GIHardware.Triangles", state.TriangleBuffer.get());
        auto instances = builder.RegisterExternalBuffer("GIHardware.Instances", state.InstanceBuffer.get());
        auto materials = builder.RegisterExternalBuffer("GIHardware.Materials", state.MaterialBuffer.get());
        if (!state.bMetadataUploaded)
        {
            auto* upload = builder.AllocateParameter<GIHardwareUploadParameters>();
            upload->Triangles = triangles;
            upload->Instances = instances;
            upload->Materials = materials;
            // The snapshot owns large payloads until Execute. RDG's inline upload arena has fixed-size pages.
            builder.AddPass("GIHardwareMetadataUpload", GIHardwareUploadParameters::GetMetaData(), upload, EPassFlag::Graphic,
                [=, &state](RHICommandListBase& cmd)
            {
                GRHIApi->UpdateBuffer(cmd, triangles->GetRHIBuffer(), state.Triangles.data(), {0, uint32_t(triangles->GetDesc().Size)});
                GRHIApi->UpdateBuffer(cmd, instances->GetRHIBuffer(), state.Instances.data(), {0, uint32_t(instances->GetDesc().Size)});
                GRHIApi->UpdateBuffer(cmd, materials->GetRHIBuffer(), state.Materials.data(), {0, uint32_t(materials->GetDesc().Size)});
            });
            state.bMetadataUploaded = true;
        }

        auto* builds = builder.AllocateParameter<std::vector<BLAS*>>();
        BuildTransitions* uploaded = nullptr;
        BuildTransitions* bottomBuilt = nullptr;
        BuildTransitions* topBuilt = nullptr;
        if (!state.bBuildScheduled)
        {
            std::vector<RHITransitionInfo> uploads, geometries;
            for (auto& [key, mesh] : state.Meshes)
            {
                if (mesh.bBuildScheduled)
                {
                    continue;
                }
                uploads.emplace_back(mesh.Positions.get(), ERHIResourceAccess::TransferDest, ERHIResourceAccess::BVHRead);
                uploads.emplace_back(mesh.IndexBuffer.get(), ERHIResourceAccess::TransferDest, ERHIResourceAccess::BVHRead);
                geometries.emplace_back(mesh.Geometry.get(), ERHIResourceAccess::BVHWrite, ERHIResourceAccess::BVHRead);
                builds->push_back(&mesh);
                mesh.bBuildScheduled = true;
            }
            if (!builds->empty())
            {
                uploaded = builder.AllocateParameter<BuildTransitions>();
                uploaded->Initialize(std::move(uploads));
                bottomBuilt = builder.AllocateParameter<BuildTransitions>();
                bottomBuilt->Initialize(std::move(geometries));
            }
            topBuilt = builder.AllocateParameter<BuildTransitions>();
            topBuilt->Initialize({RHITransitionInfo(state.TLAS.get(), ERHIResourceAccess::BVHWrite, ERHIResourceAccess::BVHRead)});
            state.bBuildScheduled = true;
        }

        builder.AddPass("GIHardwareBuildAndTrace", GIHardwareDispatchParameters::GetMetaData(), dispatch, EPassFlag::Graphic,
            [=, &state](RHICommandListBase& list)
        {
            auto& cmd = static_cast<RHIGraphicCommandList&>(list);
            if (uploaded)
            {
                for (auto* mesh : *builds)
                {
                    const auto& vertices = mesh->Mesh->LODResources.front().VertexBuffers.PositionBuffer.Vertices;
                    GRHIApi->UpdateBuffer(cmd, mesh->Positions.get(), vertices.data(), {0, uint32_t(mesh->Positions->GetDesc().Size)});
                    GRHIApi->UpdateBuffer(cmd, mesh->IndexBuffer.get(), mesh->Indices.data(), {0, uint32_t(mesh->IndexBuffer->GetDesc().Size)});
                }
                uploaded->Record(cmd);
                for (const auto* mesh : *builds)
                {
                    cmd.BuildAccelerationStructure(mesh->Geometry.get());
                }
                bottomBuilt->Record(cmd);
            }
            if (topBuilt)
            {
                cmd.BuildAccelerationStructure(state.TLAS.get());
                topBuilt->Record(cmd);
            }
            cmd.SetRayTracingPipelineState(state.Pipeline.get());
            RHIBatchedShaderParameters bindings;
            using Type = RHIShaderResourceParameter::EType;
            bindings.ResourceParameters = {{state.TLAS.get(), Type::AccelerationStructure, state.SceneBinding, 0},
                {dispatch->HardwareQueries->GetRHIShaderResourceView(), Type::SRV, state.QueryBinding, 0},
                {dispatch->HardwareHits->GetRHIShaderResourceView(), Type::UAV, state.HitBinding, 0}};
            cmd.SetBatchedShaderParameters(state.RayGen.get(), bindings);
            constexpr uint32_t width = 512;
            cmd.TraceRays(width, (count + width - 1) / width, 1);
        });

        auto* resolve = builder.AllocateParameter<GIHardwareResolveParameters>();
        resolve->Cache = material;
        // Resolve uses Cards and the three textures. No per-instance distance field is needed.
        resolve->Cache.Instances = nullptr;
        resolve->QueryCount = count;
        resolve->ResolveQueries = dispatch->HardwareQueries;
        resolve->TraceHits = ReadBuffer(builder, name + ".HitsSRV", hits, count);
        resolve->TraceInstances = ReadBuffer(builder, name + ".InstancesSRV", instances, uint32_t(state.Instances.size()));
        resolve->TraceTriangles = ReadBuffer(builder, name + ".TrianglesSRV", triangles, uint32_t(state.Triangles.size()));
        resolve->TraceMaterials = ReadBuffer(builder, name + ".MaterialsSRV", materials, uint32_t(state.Materials.size()));
        resolve->TraceResults = WriteBuffer(builder, name + ".ResultsUAV", results);
        const auto shader = GShaderMap.GetShader<GIHardwareResolveCS>(0);
        RHIComputePipelineStateDesc pipelineDesc;
        pipelineDesc.ComputeShader = static_cast<RHIComputeShader*>(shader->GetRHIShader());
        auto pipeline = RHIPipelineStateCache::GetOrCreateComputePipelineState(pipelineDesc);
        builder.AddPass("GIHardwareResolve", GIHardwareResolveParameters::GetMetaData(), resolve, EPassFlag::Compute,
            [=](RHICommandListBase& list)
        {
            auto& cmd = static_cast<RHIComputeCommandList&>(list);
            cmd.SetComputePipelineState(pipeline);
            SetShaderParameters(cmd, shader, resolve);
            cmd.Dispatch((count + 63) / 64, 1, 1);
        });
        return results;
    }

    RenderGraphBufferRef AddGITracePasses(RenderGraphBuilder& builder, const SurfaceCacheMaterialParameters& material,
        RenderGraphBufferRef queries, uint32_t count, GIRayTracingScene* hardwareScene)
    {
        return hardwareScene ? hardwareScene->AddTracePasses(builder, material, queries, count) :
            AddSurfaceCacheQueryPass(builder, material, queries, count);
    }
}
