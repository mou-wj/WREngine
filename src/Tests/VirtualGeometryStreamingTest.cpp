#include "TestBase.h"
#include "../Framework/Renderer/Private/VirtualGeometry/VirtualGeometryStreaming.h"
#include "../Framework/Renderer/Private/VirtualGeometry/VirtualGeometryMaterialResolvePass.h"
#include "../Framework/Renderer/Private/VirtualGeometry/VirtualGeometryLODSelection.h"
#include "StaticMesh.h"
#include "MaterialAsset.h"
#include "GlobalShader.h"
#include "RHIPipelineStateCache.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace Test
{
    namespace
    {
        using namespace Engine;
        using namespace RenderCore;
        using namespace RHI;

        void CheckStream(bool condition, const char* message)
        {
            if (!condition)
            {
                throw std::runtime_error(std::string("Virtual geometry streaming: ") + message);
            }
        }

        BEGIN_SHADER_PARAMETER_STRUCT(StreamingTestReadback)
        RDG_TEXTURE_ACCESS(Visibility, ERHIResourceAccess::TransferSrc)
        RDG_TEXTURE_ACCESS(Depth, ERHIResourceAccess::TransferSrc)
        RDG_TEXTURE_ACCESS(Color, ERHIResourceAccess::TransferSrc)
        RDG_BUFFER_ACCESS(Flags, ERHIResourceAccess::TransferSrc)
        RDG_BUFFER_ACCESS(Arguments, ERHIResourceAccess::TransferSrc)
        END_SHADER_PARAMETER_STRUCT(StreamingTestReadback)

        void ValidateCut(const VirtualGeometryResource& g, const std::vector<uint32_t>& actual)
        {
            std::vector<uint8_t> selected(g.Clusters.size());
            for (const auto c : actual)
            {
                CheckStream(c < selected.size() && !selected[c], "duplicate/out-of-range cut cluster");
                selected[c] = 1;
            }
            for (uint32_t cursor = uint32_t(g.Groups.size()); cursor > 0; --cursor)
            {
                const auto& group = g.Groups[cursor - 1];
                bool parents = group.ParentClusterCount != 0;
                for (uint32_t i = 0; i < group.ParentClusterCount; ++i)
                {
                    parents &= selected[g.GroupParentClusterIndices[group.ParentClusterOffset + i]] != 0;
                }
                if (parents)
                {
                    for (uint32_t i = 0; i < group.ParentClusterCount; ++i)
                    {
                        selected[g.GroupParentClusterIndices[group.ParentClusterOffset + i]] = 0;
                    }
                    for (uint32_t i = 0; i < group.ChildClusterCount; ++i)
                    {
                        const auto child = g.GroupChildClusterIndices[group.ChildClusterOffset + i];
                        CheckStream(!selected[child], "cut contains parent and descendant or duplicate region");
                        selected[child] = 1;
                    }
                }
            }
            for (uint32_t c = 0; c < selected.size(); ++c)
            {
                CheckStream(bool(selected[c]) == (g.Clusters[c].GeneratingGroupIndex == UINT32_MAX), "cut cannot reconstruct all leaves");
            }
        }
    }

    class VirtualGeometryStreamingTest : public TestBase
    {
    public:
        void Setup() override
        {
            GRHIApi = CreateTestRHIApi();
            GRHIApi->Init();
            GTransientResourceAllocator.InitRHI();
            InitGlobalRenderResource();
            auto& types = ShaderType::GetRegisterMap()[ShaderType::EShaderTypeFlag::Global];
            const auto all = types;
            std::erase_if(types, [](const auto& item) { return item.first.find("VirtualGeometry") != 0; });
            GShaderMap.Initialize();
            types = all;
        }

        void Run() override
        {
            auto data = std::make_shared<StaticMeshRenderData>();
            auto& lod = data->LODResources.emplace_back();
            auto& v = lod.VertexBuffers;
            v.PositionBuffer.bValid = v.NormalBuffer.bValid = v.UVBuffer.bValid = true;
            v.PositionBuffer.NumComponents = v.NormalBuffer.NumComponents = 3;
            v.UVBuffer.NumComponents = 2;
            constexpr uint32_t side = 24;
            for (uint32_t y = 0; y <= side; ++y)
            {
                for (uint32_t x = 0; x <= side; ++x)
                {
                    v.PositionBuffer.Vertices.insert(v.PositionBuffer.Vertices.end(), {float(x) / side * 1.8f - 0.9f, float(y) / side * 1.8f - 0.9f, 0});
                    v.NormalBuffer.Vertices.insert(v.NormalBuffer.Vertices.end(), {0, 0, 1});
                    v.UVBuffer.Vertices.insert(v.UVBuffer.Vertices.end(), {float(x) / side, float(y) / side});
                }
            }
            for (uint32_t y = 0; y < side; ++y)
            {
                for (uint32_t x = 0; x < side; ++x)
                {
                    const auto a = y * (side + 1) + x;
                    lod.IndexBuffer.Indices.insert(lod.IndexBuffer.Indices.end(), {a, a + 1, a + side + 1, a + 1, a + side + 2, a + side + 1});
                }
            }
            SectionInfo section;
            section.NumIndices = uint32_t(lod.IndexBuffer.Indices.size());
            lod.Sections.push_back(section);
            auto material = std::make_unique<Material>();
            material->SetShadingModel(EShadingModel::Lit);
            material->SetParameter("BaseColor", std::array<float, 4>{0.25f, 0.5f, 0.75f, 1});
            material->SetParameter("Metallic", 0.3f);
            auto asset = MaterialAsset::Create("", "Streaming material", std::move(material));
            StaticMesh mesh(data);
            mesh.AddMaterial(asset);
            auto& g = data->VirtualGeometryData;
            g.Build(&mesh);
            const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            const auto path = std::filesystem::temp_directory_path() / ("RenderingLearn-stream-test-" + std::to_string(stamp) + ".vgpages");
            auto storage = std::make_shared<VirtualGeometryPageStorage>();
            std::string error;
            CheckStream(storage->Build(g, path, error), error.c_str());
            auto reopened = std::make_shared<VirtualGeometryPageStorage>();
            CheckStream(reopened->Open(path, error) && reopened->Matches(g), "archive reopening/source identity failed");
            const auto corruptDirectory = std::filesystem::path(path.string() + ".directory-test");
            std::filesystem::copy_file(path, corruptDirectory, std::filesystem::copy_options::overwrite_existing);
            {
                std::fstream file(corruptDirectory, std::ios::in | std::ios::out | std::ios::binary);
                file.seekg(32);
                char value = 0;
                file.get(value);
                file.seekp(32);
                file.put(char(uint8_t(value) ^ 1));
            }
            VirtualGeometryPageStorage invalidDirectory;
            CheckStream(!invalidDirectory.Open(corruptDirectory, error), "damaged directory checksum was accepted");
            std::filesystem::remove(corruptDirectory);
            const auto sourcePosition = g.Positions.front();
            g.Positions.front().X += 0.125f;
            CheckStream(!reopened->Matches(g), "archive accepted different geometry with the same cluster count");
            g.Positions.front() = sourcePosition;
            for (uint32_t page = 0; page < reopened->GetPages().size(); ++page)
            {
                VirtualGeometryPageData decoded;
                CheckStream(reopened->ReadPage(page, decoded, error), error.c_str());
                for (const auto c : reopened->GetPages()[page].Clusters)
                {
                    const auto& cluster = g.Clusters[c];
                    const auto& location = reopened->GetLocations()[c];
                    const auto same = [&](const auto& cpu, const auto& loaded)
                    {
                        using Element = typename std::decay_t<decltype(cpu)>::value_type;
                        return std::memcmp(cpu.data() + cluster.VertexOffset, loaded.data() + location.VertexOffset,
                                           cluster.VertexCount * sizeof(Element)) == 0;
                    };
                    CheckStream(same(g.Positions, decoded.Positions) && same(g.Normals, decoded.Normals) &&
                        same(g.Tangents, decoded.Tangents) && same(g.TexCoords, decoded.TexCoords) && same(g.Colors, decoded.Colors),
                        "lossless vertex attributes differ");
                    CheckStream(std::memcmp(g.Indices.data() + cluster.IndexOffset, decoded.Indices.data() + location.IndexOffset,
                                            cluster.TriangleCount * 12) == 0, "cluster-local index roundtrip differs");
                }
            }
            CheckStream(storage->GetStoredBytes() <= storage->GetRawBytes(), "raw fallback did not bound compression expansion");
            std::cout << "STREAM_ARCHIVE pages=" << storage->GetPages().size() << " raw=" << storage->GetRawBytes()
                      << " stored=" << storage->GetStoredBytes() << std::endl;
            storage = reopened;
            const uint32_t pinned = uint32_t(std::count_if(storage->GetPages().begin(), storage->GetPages().end(),
                [](const auto& page) { return page.bPinned; }));
            SceneView view;
            view.Viewport = {0, 0, 96, 96};
            view.ProjectionMatrix = Core::Mat4::Identity();
            view.ProjectionMatrix(2, 2) = 0.01f;
            view.ProjectionMatrix(2, 3) = 0.5f;
            view.ViewProjectionMatrix = view.ProjectionMatrix;
            bool sawEviction = false, sawFallback = false, sawConverged = false;
            bool sawFailedLoad = false;
            for (uint32_t phase = 0; phase < 4; ++phase)
            {
                const bool limited = phase != 1;
                const bool failure = phase == 2;
                const bool rootBudget = phase == 3;
                char originalByte = 0;
                if (failure)
                {
                    // Keep valid directory/source identity; only the independently checksummed payload is corrupted.
                    const auto position = storage->GetPages()[pinned].FileOffset;
                    std::fstream file(path, std::ios::in | std::ios::out | std::ios::binary);
                    file.seekg(position);
                    file.get(originalByte);
                    file.seekp(position);
                    file.put(char(uint8_t(originalByte) ^ 1));
                }
                const uint64_t metadata = g.Clusters.size() * (sizeof(VirtualGeometryCluster) + 8ull) +
                    g.Groups.size() * (sizeof(VirtualGeometryGroup) + 4ull) + storage->GetPages().size() * 4ull +
                    (g.GroupChildClusterIndices.size() + g.GroupParentClusterIndices.size() + g.RootGroupIndices.size()) * 4ull;
                const uint32_t slots = rootBudget ? pinned : limited ?
                    std::min<uint32_t>(uint32_t(storage->GetPages().size()), pinned + 2) : uint32_t(storage->GetPages().size());
                auto stream = std::make_shared<Renderer::VirtualGeometryStreaming>(data, storage, metadata + uint64_t(slots) * VIRTUAL_GEOMETRY_PAGE_BYTES);
                std::vector<uint8_t> baselineMask;
                for (uint32_t frame = 0; frame < ((failure || rootBudget) ? 12u : limited ? 36u : 48u); ++frame)
                {
                    RenderGraphBuilder graph;
                    Renderer::VirtualGeometryInstance instance;
                    instance.RenderData = data;
                    instance.Streaming = stream;
                    instance.bGPUSelection = true;
                    instance.bFrustumCulling = false;
                    instance.TargetPixelError = limited && frame % 12 >= 8 ? 1000 : 0;
                    instance.Materials = {asset->GetMaterial()->GetRenderProxyShared()};
                    Renderer::VirtualGeometryRasterSettings settings;
                    settings.Mode = static_cast<Renderer::EVirtualGeometryRasterMode>(frame % 3);
                    auto output = Renderer::AddVirtualGeometryRasterPass(graph, view, {instance}, 96, 96, settings);
                    std::array<std::unique_ptr<RenderTexture>, 3> targets;
                    std::array<RenderGraphTextureRef, 3> rdg;
                    for (uint32_t i = 0; i < 3; ++i)
                    {
                        RHITextureDesc desc;
                        desc.Width = desc.Height = 96;
                        desc.Format = i == 1 ? ERHIFormat::R16G16B16A16_Float : ERHIFormat::R8G8B8A8_UNorm;
                        desc.Usage = ERHITextureCreateFlag::ShaderResource | ERHITextureCreateFlag::UAV | ERHITextureCreateFlag::TransferSrc;
                        targets[i] = std::make_unique<RenderTexture>(desc);
                        targets[i]->InitRHIResource();
                        rdg[i] = graph.RegisterExternalTexture("StreamingTest.Target" + std::to_string(i), targets[i].get());
                    }
                    Renderer::AddVirtualGeometryMaterialResolvePass(graph, view, output, rdg[0], rdg[1], rdg[2]);
                    const auto reference = stream->SelectResidentReference(instance.LocalToWorld, view, instance.TargetPixelError);
                    const auto desired = Renderer::SelectVirtualGeometryClusters(g, instance.LocalToWorld, view, instance.TargetPixelError);
                    auto* p = graph.AllocateParameter<StreamingTestReadback>();
                    p->Visibility = output->VirtualGeometryVisibilityTexture;
                    p->Depth = output->VirtualGeometryDepthTexture;
                    p->Color = rdg[0];
                    p->Flags = output->GPUSelections[0].ClusterFlags;
                    p->Arguments = output->GPUSelections[0].IndirectArguments;
                    std::array<void*, 5> mapped{};
                    const auto read = [&](RHICommandListBase& cmd)
                    {
                        mapped[0] = GRHIApi->MapReadTexture(cmd, p->Visibility->GetRHITexture(), {});
                        mapped[1] = GRHIApi->MapReadTexture(cmd, p->Depth->GetRHITexture(), {});
                        mapped[2] = GRHIApi->MapReadTexture(cmd, p->Color->GetRHITexture(), {});
                        mapped[3] = GRHIApi->MapReadBuffer(cmd, p->Flags->GetRHIBuffer(), {});
                        mapped[4] = GRHIApi->MapReadBuffer(cmd, p->Arguments->GetRHIBuffer(), {});
                    };
                    const bool gl = GShaderPlatform == ERHIShaderPlatform::OpenGL;
                    graph.AddPass("StreamingTest.Readback", StreamingTestReadback::GetMetaData(), p, EPassFlag::Graphic,
                        [&](RHICommandListBase& cmd)
                    {
                        if (!gl)
                        {
                            read(cmd);
                        }
                    });
                    graph.Execute();
                    stream->EndFrame();
                    const auto fence = targets[0]->GetTracker().GetLastAccessFence();
                    GRHIApi->GetQueue(fence.QueueType)->WaitValue(fence.Value);
                    if (gl)
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
                    for (const auto mapping : mapped)
                    {
                        CheckStream(mapping != nullptr, "GPU readback map failed");
                    }
                    const auto* flags = static_cast<const uint32_t*>(mapped[3]);
                    const auto* arguments = static_cast<const RHIDrawIndexedIndirectArguments*>(mapped[4]);
                    std::vector<uint32_t> actual;
                    for (uint32_t c = 0; c < g.Clusters.size(); ++c)
                    {
                        if (flags[c] & 1)
                        {
                            actual.push_back(c);
                            CheckStream(stream->GetResidency()[c] != 0, "selected cluster page is not resident");
                        }
                        if (arguments[c].IndexCount)
                        {
                            const auto& physical = stream->GetPhysicalClusters()[c];
                            CheckStream(arguments[c].FirstIndex == physical.IndexOffset && arguments[c].VertexOffset == physical.VertexOffset,
                                        "indirect draw did not use physical page addresses");
                        }
                    }
                    ValidateCut(g, actual);
                    CheckStream(actual == reference, "GPU residency cut differs from CPU closure reference");
                    sawFallback |= actual != desired;
                    sawConverged |= !limited && actual == desired && instance.TargetPixelError == 0;
                    const auto* ids = static_cast<const uint32_t*>(mapped[0]);
                    const auto* depths = static_cast<const float*>(mapped[1]);
                    const auto* colors = static_cast<const uint8_t*>(mapped[2]);
                    std::vector<uint8_t> mask(96 * 96);
                    uint32_t pixels = 0;
                    for (uint32_t pixel = 0; pixel < mask.size(); ++pixel)
                    {
                        mask[pixel] = ids[pixel * 2] != UINT32_MAX;
                        if (mask[pixel])
                        {
                            ++pixels;
                            const auto draw = ids[pixel * 2];
                            CheckStream(draw < output->Draws.size(), "invalid streamed DrawID");
                            const auto cluster = output->Draws[draw].ClusterID;
                            CheckStream(std::binary_search(actual.begin(), actual.end(), cluster) &&
                                ids[pixel * 2 + 1] < g.Clusters[cluster].TriangleCount && depths[pixel] < 1,
                                "streamed visibility references invalid geometry/depth");
                            CheckStream(std::abs(int(colors[pixel * 4]) - 64) <= 1 && std::abs(int(colors[pixel * 4 + 1]) - 128) <= 1 &&
                                std::abs(int(colors[pixel * 4 + 2]) - 191) <= 1, "streamed material reconstruction differs");
                        }
                    }
                    CheckStream(pixels > 5000, "streamed fallback lost most of the surface");
                    if (baselineMask.empty())
                    {
                        baselineMask = mask;
                    }
                    CheckStream(mask == baselineMask, "streaming introduced holes or changed the planar silhouette");
                    for (const auto mapping : mapped)
                    {
                        GRHIApi->Unmap(mapping);
                    }
                    const auto& stats = stream->GetStatistics();
                    CheckStream(stats.CacheBytes <= stats.BudgetBytes && stats.ResidentPages <= slots, "GPU cache exceeded budget");
                    CheckStream(stats.PageHits <= stats.PageReferences, "page hit accounting exceeds references");
                    CheckStream(failure || stats.FailedLoads == 0, "asynchronous page load failed");
                    sawFailedLoad |= failure && stats.FailedLoads > 0;
                    sawEviction |= stats.Evictions > 0;
                    std::cout << "STREAM_FRAME limited=" << limited << " failed_input=" << failure << " frame=" << frame << " cut=" << actual.size()
                              << " desired=" << desired.size() << " resident=" << stats.ResidentPages << " slots=" << slots
                              << " loads=" << stats.Loads << " evictions=" << stats.Evictions << " pixels=" << pixels << std::endl;
                }
                const auto& stats = stream->GetStatistics();
                CheckStream(!rootBudget || (stats.BudgetBlocked > 0 && stats.Loads == 0 && stats.ResidentPages == pinned),
                            "root-only budget must preserve fallback and report blocked refinement");
                std::cout << "STREAM_STATS phase=" << phase << " bytes=" << stats.CacheBytes << " budget=" << stats.BudgetBytes
                          << " hit=" << stats.PageHits << "/" << stats.PageReferences << " blocked=" << stats.BudgetBlocked
                          << " failures=" << stats.FailedLoads << " uploaded=" << stats.UploadedBytes
                          << " io_decode_ms=" << stats.LoadMilliseconds << std::endl;
                // Several submitted graphs remain in flight; page feedback, eviction and writes must stay GPU ordered.
                RHIFence last;
                for (uint32_t frame = 0; frame < 6; ++frame)
                {
                    RenderGraphBuilder graph;
                    Renderer::VirtualGeometryInstance instance;
                    instance.RenderData = data;
                    instance.Streaming = stream;
                    instance.bGPUSelection = true;
                    instance.bFrustumCulling = false;
                    instance.TargetPixelError = frame % 2 ? 0 : 1000;
                    auto instances = std::vector{instance, instance}; // Shared-resource instances merge requests, not caches.
                    instances[1].LocalToWorld(0, 3) = 0.1f;
                    auto result = Renderer::AddVirtualGeometryRasterPass(graph, view, instances, 96, 96);
                    graph.Execute();
                    stream->EndFrame();
                    last = result->VirtualGeometryDepthTexture->GetTracker().GetLastAccessFence();
                }
                GRHIApi->GetQueue(last.QueueType)->WaitValue(last.Value);
                stream.reset();
                if (failure)
                {
                    std::fstream file(path, std::ios::in | std::ios::out | std::ios::binary);
                    file.seekp(storage->GetPages()[pinned].FileOffset);
                    file.put(originalByte);
                }
            }
            CheckStream(sawFallback && sawConverged && sawEviction && sawFailedLoad,
                        "test did not exercise fallback, convergence, eviction and asynchronous failure");
            g.ReleaseResources();
            std::error_code ignored;
            std::filesystem::remove(path, ignored);
            std::cout << "VIRTUAL_GEOMETRY_STREAMING_TEST_OK" << std::endl;
        }

        void Teardown() override
        {
            Engine::GMeshMaterialShaderMap.Clear();
            Engine::GMaterialShaderMap.Clear();
            GShaderMap.Clear();
            RHIPipelineStateCache::ClearAll();
            GTransientResourceAllocator.ReleaseRHI();
            ReleaseGlobalRenderResource();
            GRHIApi->Shutdown();
            delete GRHIApi;
            GRHIApi = nullptr;
        }
    };
    REGISTER_RENDER_TEST("VirtualGeometryStreamingTest", VirtualGeometryStreamingTest);

    class VirtualGeometryStreamingDAGTest final : public VirtualGeometryStreamingTest
    {
    public:
        void Run() override
        {
            // Graph fixture, not a geometric Build result: split Parents cross consumer Groups, with three roots.
            auto data = std::make_shared<StaticMeshRenderData>();
            auto& g = data->VirtualGeometryData;
            g.Clusters.resize(12);
            for (uint32_t c = 0; c < 12; ++c)
            {
                auto& cluster = g.Clusters[c];
                cluster.VertexOffset = uint32_t(g.Positions.size());
                cluster.VertexCount = 384; // Prevent all logical layers from sharing one Page.
                cluster.IndexOffset = uint32_t(g.Indices.size());
                cluster.TriangleCount = 1;
                cluster.Bounds.Box = {{-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}};
                cluster.Bounds.Sphere.Center = {0, 0, 0};
                cluster.Bounds.Sphere.Radius = 1;
                for (uint32_t v = 0; v < cluster.VertexCount; ++v)
                {
                    g.Positions.push_back({float(v % 3) * 0.1f, float(v % 2) * 0.1f, 0});
                    g.Normals.push_back({0, 0, 1});
                    g.Tangents.push_back({1, 0, 0, 1});
                    g.TexCoords.push_back({0, 0});
                    g.Colors.push_back({1, 1, 1, 1});
                }
                g.Indices.insert(g.Indices.end(), {0, 1, 2});
            }
            const auto group = [&](std::initializer_list<uint32_t> children, std::initializer_list<uint32_t> parents,
                                   float childError, float parentError)
            {
                const uint32_t index = uint32_t(g.Groups.size());
                VirtualGeometryGroup record;
                record.ChildClusterOffset = uint32_t(g.GroupChildClusterIndices.size());
                record.ChildClusterCount = uint32_t(children.size());
                record.ParentClusterOffset = uint32_t(g.GroupParentClusterIndices.size());
                record.ParentClusterCount = uint32_t(parents.size());
                record.ChildMetric.Bounds = record.ParentMetric.Bounds = g.Clusters.front().Bounds;
                record.ChildMetric.Error = childError;
                record.ParentMetric.Error = parentError;
                for (const auto child : children)
                {
                    g.Clusters[child].GroupIndex = index;
                    g.GroupChildClusterIndices.push_back(child);
                }
                for (const auto parent : parents)
                {
                    g.Clusters[parent].GeneratingGroupIndex = index;
                    g.GroupParentClusterIndices.push_back(parent);
                }
                g.Groups.push_back(record);
                if (!parents.size())
                {
                    g.RootGroupIndices.push_back(index);
                }
            };
            group({0, 1}, {6, 7}, 0, 0.01f);
            group({2, 3}, {8, 9}, 0, 0.02f);
            group({7, 8}, {10}, 0.02f, 0.04f);
            group({6, 9, 10}, {11}, 0.04f, 0.08f);
            group({4}, {}, 0, 0);
            group({5}, {}, 0, 0);
            group({11}, {}, 0.08f, 0.08f);
            const auto path = std::filesystem::temp_directory_path() /
                ("RenderingLearn-stream-dag-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".vgpages");
            auto storage = std::make_shared<VirtualGeometryPageStorage>();
            std::string error;
            CheckStream(storage->Build(g, path, error), error.c_str());
            auto stream = std::make_shared<Renderer::VirtualGeometryStreaming>(data, storage, 2 * 1024 * 1024);
            SceneView view;
            view.Viewport = {0, 0, 96, 96};
            view.ProjectionMatrix = Core::Mat4::Identity();
            view.ProjectionMatrix(2, 2) = 0.01f;
            view.ProjectionMatrix(2, 3) = 0.5f;
            view.ViewProjectionMatrix = view.ProjectionMatrix;
            bool coarse = false, fine = false;
            for (uint32_t frame = 0; frame < 32; ++frame)
            {
                const float target = frame % 8 == 7 ? 100 : 0;
                RenderGraphBuilder graph;
                stream->Prepare(graph);
                const auto selection = Renderer::AddVirtualGeometryGPUSelectionPass(graph, g, Core::Mat4::Identity(), view,
                                                                                     target, false, stream.get());
                stream->AddFeedbackReadback(graph);
                const auto feedback = stream->GetFeedbackBuffer();
                auto* p = graph.AllocateParameter<StreamingTestReadback>();
                p->Flags = selection.ClusterFlags;
                void* mapped = nullptr;
                const bool gl = GShaderPlatform == ERHIShaderPlatform::OpenGL;
                graph.AddPass("StreamDAG.Readback", StreamingTestReadback::GetMetaData(), p, EPassFlag::Graphic,
                    [&](RHICommandListBase& cmd)
                {
                    if (!gl)
                    {
                        mapped = GRHIApi->MapReadBuffer(cmd, p->Flags->GetRHIBuffer(), {});
                    }
                });
                graph.Execute();
                stream->EndFrame();
                // Flags are transient; their allocation tracker is distinct from the graph wrapper's tracker.
                // The cache's external feedback buffer records a real fence after the same finalization work.
                const auto fence = feedback->GetTracker().GetLastAccessFence();
                CheckStream(fence.Value != 0, "cross-DAG feedback has no submission fence");
                GRHIApi->GetQueue(fence.QueueType)->WaitValue(fence.Value);
                if (gl)
                {
                    auto* queue = GRHIApi->GetQueue(EQueueType::Graphics);
                    auto* context = queue->AcquireCommandContext();
                    RHICommandListBase cmd(context);
                    cmd.SetImmediate(true);
                    cmd.Begin();
                    mapped = GRHIApi->MapReadBuffer(cmd, p->Flags->GetRHIBuffer(), {});
                    cmd.End();
                    queue->WaitValue(queue->ExecuteContext(context));
                }
                CheckStream(mapped != nullptr, "cross-DAG feedback map failed");
                std::vector<uint32_t> actual;
                const auto* flags = static_cast<const uint32_t*>(mapped);
                for (uint32_t c = 0; c < g.Clusters.size(); ++c)
                {
                    if (flags[c] & 1)
                    {
                        actual.push_back(c);
                        CheckStream(stream->GetResidency()[c] != 0, "cross-DAG cut includes nonresident cluster");
                    }
                }
                GRHIApi->Unmap(mapped);
                ValidateCut(g, actual);
                CheckStream(actual == stream->SelectResidentReference(Core::Mat4::Identity(), view, target), "cross-DAG CPU/GPU difference");
                CheckStream(std::binary_search(actual.begin(), actual.end(), 4u) && std::binary_search(actual.begin(), actual.end(), 5u),
                            "cross-DAG lost a stalled root island");
                coarse |= actual != Renderer::SelectVirtualGeometryClusters(g, Core::Mat4::Identity(), view, target);
                fine |= target == 0 && actual == std::vector<uint32_t>({0, 1, 2, 3, 4, 5});
            }
            CheckStream(coarse && fine, "cross-DAG never exercised missing-page fallback or convergence");
            stream.reset();
            g.ReleaseResources();
            std::error_code ignored;
            std::filesystem::remove(path, ignored);
            std::cout << "VIRTUAL_GEOMETRY_STREAMING_DAG_TEST_OK" << std::endl;
        }
    };
    REGISTER_RENDER_TEST("VirtualGeometryStreamingDAGTest", VirtualGeometryStreamingDAGTest);
}
