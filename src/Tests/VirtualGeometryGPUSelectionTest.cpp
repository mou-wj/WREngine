#include "TestBase.h"
#include "../Framework/Renderer/VirtualGeometry/VirtualGeometryRasterPass.h"
#include "GlobalShader.h"
#include "MeshMaterialShader.h"
#include "RHIPipelineStateCache.h"
#include "Transform.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <set>
#include <stdexcept>

namespace Test
{
    namespace
    {
        using namespace Engine;
        using namespace RenderCore;
        using namespace RHI;

        void VerifyGPU(bool condition, const char* message)
        {
            if (!condition)
            {
                throw std::runtime_error(std::string("Virtual geometry GPU selection test: ") + message);
            }
        }

        VirtualGeometryResource CrossingDAG()
        {
            VirtualGeometryResource resource;
            resource.Positions = {{-0.5f, -0.5f, 0}, {0.5f, -0.5f, 0}, {0, 0.5f, 0}};
            resource.Indices = {0, 1, 2};
            resource.Normals.resize(3, {0, 0, 1});
            resource.Tangents.resize(3, {1, 0, 0, 1});
            resource.TexCoords.resize(3);
            resource.Colors.resize(3, {1, 1, 1, 1});
            resource.Clusters.resize(12);
            for (auto& cluster : resource.Clusters)
            {
                cluster.VertexCount = 3;
                cluster.TriangleCount = 1;
                cluster.Bounds = Core::BoxSphereBounds({{-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}});
            }
            const auto group = [&](std::initializer_list<uint32_t> children, std::initializer_list<uint32_t> parents,
                                   float childError, float parentError)
            {
                const auto index = uint32_t(resource.Groups.size());
                VirtualGeometryGroup g;
                g.ChildClusterOffset = resource.GroupChildClusterIndices.size();
                g.ChildClusterCount = children.size();
                g.ParentClusterOffset = resource.GroupParentClusterIndices.size();
                g.ParentClusterCount = parents.size();
                g.ChildMetric.Bounds = g.ParentMetric.Bounds = resource.Clusters.front().Bounds;
                g.ChildMetric.Error = childError;
                g.ParentMetric.Error = parentError;
                for (const auto child : children)
                {
                    resource.Clusters[child].GroupIndex = index;
                    resource.GroupChildClusterIndices.push_back(child);
                }
                for (const auto parent : parents)
                {
                    resource.Clusters[parent].GeneratingGroupIndex = index;
                    resource.GroupParentClusterIndices.push_back(parent);
                }
                resource.Groups.push_back(g);
                if (parents.size() == 0)
                {
                    resource.RootGroupIndices.push_back(index);
                }
            };
            group({0, 1}, {6, 7}, 0, 0.01f);
            group({2, 3}, {8, 9}, 0, 0.02f);
            group({7, 8}, {10}, 0.02f, 0.04f);
            group({6, 9, 10}, {11}, 0.04f, 0.08f);
            group({4}, {}, 0, 0);
            group({5}, {}, 0, 0);
            group({11}, {}, 0.08f, 0.08f);
            // Repeat disconnected graph islands so both Group evaluation and Cluster finalization span workgroups.
            // This fixture checks selection only; overlapping synthetic triangles do not model a Build boundary test.
            const auto clusters = resource.Clusters;
            const auto groups = resource.Groups;
            const auto children = resource.GroupChildClusterIndices;
            const auto parents = resource.GroupParentClusterIndices;
            const auto roots = resource.RootGroupIndices;
            for (uint32_t island = 1; island < 20; ++island)
            {
                const uint32_t clusterBase = resource.Clusters.size(), groupBase = resource.Groups.size();
                const uint32_t childBase = resource.GroupChildClusterIndices.size();
                const uint32_t parentBase = resource.GroupParentClusterIndices.size();
                for (auto cluster : clusters)
                {
                    cluster.GroupIndex += groupBase;
                    if (cluster.GeneratingGroupIndex != INVALID_VIRTUAL_GEOMETRY_INDEX)
                    {
                        cluster.GeneratingGroupIndex += groupBase;
                    }
                    resource.Clusters.push_back(cluster);
                }
                for (auto group : groups)
                {
                    group.ChildClusterOffset += childBase;
                    group.ParentClusterOffset += parentBase;
                    resource.Groups.push_back(group);
                }
                for (const auto child : children)
                {
                    resource.GroupChildClusterIndices.push_back(child + clusterBase);
                }
                for (const auto parent : parents)
                {
                    resource.GroupParentClusterIndices.push_back(parent + clusterBase);
                }
                for (const auto root : roots)
                {
                    resource.RootGroupIndices.push_back(root + groupBase);
                }
            }
            return resource;
        }

        BEGIN_SHADER_PARAMETER_STRUCT(GPUSelectionReadbackParameters)
        RDG_TEXTURE_ACCESS(Color, ERHIResourceAccess::TransferSrc)
        RDG_BUFFER_ACCESS(Flags, ERHIResourceAccess::TransferSrc)
        RDG_BUFFER_ACCESS(Arguments, ERHIResourceAccess::TransferSrc)
        RDG_BUFFER_ACCESS(Statistics, ERHIResourceAccess::TransferSrc)
        RDG_BUFFER_ACCESS(Visible, ERHIResourceAccess::TransferSrc)
        END_SHADER_PARAMETER_STRUCT(GPUSelectionReadbackParameters)

        bool ReferenceVisible(const Core::BoundingSphere& sphere, const Core::Mat4& matrix, bool zeroToOne)
        {
            for (uint32_t axis = 0; axis < 3; ++axis)
            {
                for (const double sign : {-1.0, 1.0})
                {
                    const bool nearZO = axis == 2 && sign == 1 && zeroToOne;
                    Core::Double3 normal;
                    double distance = 0;
                    for (uint32_t component = 0; component < 4; ++component)
                    {
                        const double coefficient = (nearZO ? 0 : double(matrix(3, component))) +
                                                   sign * double(matrix(axis, component));
                        if (component < 3)
                        {
                            normal[component] = coefficient;
                            distance += coefficient * sphere.Center[component];
                        }
                        else
                        {
                            distance += coefficient;
                        }
                    }
                    if (distance < -sphere.Radius * normal.Length() - 1e-4)
                    {
                        return false;
                    }
                }
            }
            return true;
        }
    }

    class VirtualGeometryGPUSelectionTest final : public TestBase
    {
    public:
        void Setup() override
        {
            GRHIApi = CreateTestRHIApi();
            GRHIApi->Init();
            GTransientResourceAllocator.InitRHI();
            InitGlobalRenderResource();
            auto& types = ShaderType::GetRegisterMap()[ShaderType::EShaderTypeFlag::Global];
            const auto original = types;
            std::erase_if(types, [](const auto& entry) { return !entry.first.starts_with("VirtualGeometry"); });
            GShaderMap.Initialize();
            types = original;
        }

        void Run() override
        {
            auto data = std::make_shared<StaticMeshRenderData>();
            auto& resource = data->VirtualGeometryData;
            resource = CrossingDAG();
            resource.InitializeResources();
            RHITextureDesc desc;
            desc.Width = desc.Height = 100;
            desc.Format = ERHIFormat::R8G8B8A8_UNorm;
            desc.Usage = ERHITextureCreateFlag::RenderTarget | ERHITextureCreateFlag::ShaderResource |
                         ERHITextureCreateFlag::TransferSrc;
            RenderTexture color(desc);
            color.InitRHIResource();
            uint32_t cases = 0;
            for (uint32_t configuration = 0; configuration < 9; ++configuration)
            {
                SceneView view;
                view.Viewport = {0, 0, 100, 100};
                view.ViewProjectionMatrix = Core::Mat4::Identity();
                view.ViewProjectionMatrix(2, 2) = -0.01f;
                view.ViewProjectionMatrix(2, 3) = 0.5f;
                Core::Mat4 transform = Core::Mat4::Identity();
                if (configuration == 1)
                {
                    transform(0, 0) = -1.7f;
                    transform(1, 0) = 0.3f;
                    transform(1, 1) = 0.6f;
                }
                if (configuration == 2)
                {
                    transform(0, 3) = 5;
                }
                if (configuration >= 3 && configuration <= 6)
                {
                    const float distance = configuration == 6 ? 0.2f : 4;
                    view.ViewMatrix = Core::LookAtRH({0, 0, distance}, {0, 0, 0}, {0, 1, 0});
                    view.ProjectionMatrix = Core::PerspectiveRhZo(Core::DegToRad(60.0f), 1, 0.1f, 100);
                    view.ViewProjectionMatrix = view.ProjectionMatrix * view.ViewMatrix;
                    if (configuration == 4)
                    {
                        transform(0, 3) = 20;
                    }
                    if (configuration == 5)
                    {
                        transform(2, 3) = 10;
                    }
                }
                if (configuration == 7)
                {
                    view.bDepthRangeZeroToOne = false;
                    view.ViewProjectionMatrix(2, 3) = 0;
                }
                if (configuration == 8)
                {
                    view.ViewProjectionMatrix(0, 2) = 0.35f;
                    transform(0, 3) = 1.2f; // Intersects a frustum boundary: conservative visibility.
                }
                for (const float error : {0.0f, 0.5f, 1.0f, 2.0f, 3.0f, 6.0f, 100.0f})
                {
                    for (const bool cull : {false, true})
                    {
                        std::cout << "GPU_SELECTION_CASE config=" << configuration << " error=" << error
                                  << " cull=" << cull << std::endl;
                        const auto expected = Renderer::SelectVirtualGeometryClusters(resource, transform, view, error);
                        RenderGraphBuilder builder;
                        Renderer::VirtualGeometryInstance instance;
                        instance.RenderData = data;
                        instance.LocalToWorld = transform;
                        instance.TargetPixelError = error;
                        instance.bGPUSelection = true;
                        instance.bFrustumCulling = cull;
                        auto output = Renderer::AddVirtualGeometryRasterPass(builder, view, {instance}, 100, 100);
                        auto target = builder.RegisterExternalTexture("GPUSelectionTest.Color", &color);
                        Renderer::AddVirtualGeometryVisibilityDebugPass(builder, output, target, view);
                        const auto& gpu = output->GPUSelections.front();
                        auto* params = builder.AllocateParameter<GPUSelectionReadbackParameters>();
                        params->Color = target;
                        params->Flags = gpu.ClusterFlags;
                        params->Arguments = gpu.IndirectArguments;
                        params->Statistics = gpu.Statistics;
                        params->Visible = gpu.VisibleClusters;
                        const std::array<RenderGraphBufferRef, 4> buffers{params->Flags, params->Arguments,
                                                                        params->Statistics, params->Visible};
                        std::array<void*, 4> mapped{};
                        const auto read = [&](RHICommandListBase& cmd)
                        {
                            for (uint32_t i = 0; i < buffers.size(); ++i)
                            {
                                mapped[i] = GRHIApi->MapReadBuffer(cmd, buffers[i]->GetRHIBuffer(), {});
                            }
                        };
                        const bool immediate = GShaderPlatform == ERHIShaderPlatform::OpenGL;
                        builder.AddPass("GPUSelectionTest.Readback", GPUSelectionReadbackParameters::GetMetaData(),
                                        params, EPassFlag::Graphic, [&](RHICommandListBase& cmd)
                        {
                            if (!immediate)
                            {
                                read(cmd);
                            }
                        });
                        builder.Execute();
                        const auto fence = color.GetTracker().GetLastAccessFence();
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
                        VerifyGPU(std::all_of(mapped.begin(), mapped.end(), [](void* p) { return p != nullptr; }), "map failed");
                        const auto* flags = static_cast<const uint32_t*>(mapped[0]);
                        const auto* arguments = static_cast<const RHIDrawIndexedIndirectArguments*>(mapped[1]);
                        const auto* stats = static_cast<const uint32_t*>(mapped[2]);
                        const auto* list = static_cast<const uint32_t*>(mapped[3]);
                        std::vector<uint32_t> actual, visible;
                        for (uint32_t c = 0; c < resource.Clusters.size(); ++c)
                        {
                            const bool inCut = std::binary_search(expected.begin(), expected.end(), c);
                            const bool inView = inCut && (!cull || ReferenceVisible(resource.Clusters[c].Bounds.Sphere,
                                view.ViewProjectionMatrix * transform, view.bDepthRangeZeroToOne));
                            if (flags[c] & 1)
                            {
                                actual.push_back(c);
                            }
                            if (inView)
                            {
                                visible.push_back(c);
                            }
                            if (flags[c] != uint32_t(inCut) + uint32_t(inView) * 2)
                            {
                                std::cout << "GPU_SELECTION_MISMATCH cluster=" << c << " flags=" << flags[c]
                                          << " expected=" << uint32_t(inCut) + uint32_t(inView) * 2 << std::endl;
                            }
                            VerifyGPU(flags[c] == uint32_t(inCut) + uint32_t(inView) * 2, "GPU cut/culling flags differ from CPU");
                            VerifyGPU(arguments[c].IndexCount == (inView ? 3u : 0u) && arguments[c].InstanceCount == 1 &&
                                      arguments[c].FirstIndex == 0 && arguments[c].VertexOffset == 0 &&
                                      arguments[c].FirstInstance == 0, "invalid GPU indexed-indirect arguments");
                        }
                        VerifyGPU(actual == expected, "many-to-many DAG cut differs from CPU");
                        VerifyGPU(stats[0] == actual.size() && stats[1] == visible.size() &&
                                  stats[2] == actual.size() - visible.size() && stats[3] == visible.size(), "incorrect GPU statistics");
                        std::vector<uint32_t> compact(list, list + stats[1]);
                        std::sort(compact.begin(), compact.end());
                        VerifyGPU(compact == visible, "compact visible list contains missing or duplicate clusters");
                        for (auto* p : mapped)
                        {
                            GRHIApi->Unmap(p);
                        }
                        ++cases;
                    }
                }
            }
            resource.ReleaseResources();
            std::cout << "VIRTUAL_GEOMETRY_GPU_SELECTION_TEST_OK cases=" << cases << std::endl;
        }

        void Teardown() override
        {
            GMeshMaterialShaderMap.Clear();
            GShaderMap.Clear();
            RHIPipelineStateCache::ClearAll();
            GTransientResourceAllocator.ReleaseRHI();
            ReleaseGlobalRenderResource();
            GRHIApi->Shutdown();
            delete GRHIApi;
            GRHIApi = nullptr;
        }
    };
    REGISTER_RENDER_TEST("VirtualGeometryGPUSelectionTest", VirtualGeometryGPUSelectionTest);
}
