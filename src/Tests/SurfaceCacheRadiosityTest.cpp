#include "TestBase.h"
#include "../Framework/Renderer/Private/GI/SurfaceCacheRadiosity.h"
#include "../Framework/Renderer/Private/GI/ScreenProbeGather.h"
#include "../Framework/Renderer/Private/GI/Reflections.h"
#include "../Framework/Renderer/Private/GI/GIRayTracing.h"
#include "../Framework/Renderer/Private/GI/VoxelLighting.h"
#include "DistanceFieldMgr.h"
#include "GlobalShader.h"
#include "MaterialCore.h"
#include "RHIPipelineStateCache.h"
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace Core;
using namespace Engine;
using namespace Renderer;
using namespace RenderCore;
using namespace RHI;

namespace Test
{
    namespace
    {
        void Verify(bool condition, const char* message)
        {
            if (!condition)
            {
                throw std::runtime_error(std::string("SurfaceCacheRadiosityTest: ") + message);
            }
        }

        std::shared_ptr<StaticMeshRenderData> MakeWall(float halfWidth = 2.0f, uint32_t subdivisions = 1)
        {
            auto mesh = std::make_shared<StaticMeshRenderData>();
            mesh->Bounds = BoxSphereBounds(AABB(Float3(-halfWidth, -halfWidth, -0.15f), Float3(halfWidth, halfWidth, 0.15f)));
            auto& lod = mesh->LODResources.emplace_back();
            auto& vertices = lod.VertexBuffers;
            vertices.PositionBuffer.NumComponents = 3;
            vertices.NormalBuffer.NumComponents = 3;
            vertices.TangentBuffer.NumComponents = 4;
            vertices.UVBuffer.NumComponents = 2;
            const Float3 normals[] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
            const Float2 corners[] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
            for (const auto& n : normals)
            {
                const auto u = Normalize(Cross(std::abs(n.Y) > 0.5f ? Float3(0, 0, 1) : Float3(0, 1, 0), n));
                const auto v = Cross(n, u);
                for (uint32_t y = 0; y < subdivisions; ++y)
                {
                    for (uint32_t x = 0; x < subdivisions; ++x)
                    {
                        const uint32_t firstVertex = vertices.PositionBuffer.GetNumVertices();
                        for (const auto& corner : corners)
                        {
                            const float s = (float(x) + corner.X * 0.5f + 0.5f) / float(subdivisions);
                            const float t = (float(y) + corner.Y * 0.5f + 0.5f) / float(subdivisions);
                            const auto p = n + u * (2.0f * s - 1.0f) + v * (2.0f * t - 1.0f);
                            vertices.PositionBuffer.Vertices.insert(vertices.PositionBuffer.Vertices.end(), {p.X * halfWidth, p.Y * halfWidth, p.Z * 0.15f});
                            vertices.NormalBuffer.Vertices.insert(vertices.NormalBuffer.Vertices.end(), {n.X, n.Y, n.Z});
                            vertices.TangentBuffer.Vertices.insert(vertices.TangentBuffer.Vertices.end(), {u.X, u.Y, u.Z, 1});
                            vertices.UVBuffer.Vertices.insert(vertices.UVBuffer.Vertices.end(), {s, t});
                        }
                        lod.IndexBuffer.Indices.insert(lod.IndexBuffer.Indices.end(),
                            {firstVertex, firstVertex + 1, firstVertex + 2, firstVertex, firstVertex + 2, firstVertex + 3});
                    }
                }
            }
            auto& section = lod.Sections.emplace_back();
            section.NumIndices = static_cast<uint32_t>(lod.IndexBuffer.Indices.size());
            section.MaterialIndex = 0;
            lod.InitializeResources();
            GDistanceFieldMgr.BuildMeshDistanceField(*mesh);
            Verify(lod.DistanceFieldData.Allocation.IsValid(), "wall SDF was allocated");
            return mesh;
        }

        BEGIN_SHADER_PARAMETER_STRUCT(RadiosityTestReadback)
            RDG_TEXTURE_ACCESS(Atlas, ERHIResourceAccess::TransferSrc)
            RDG_BUFFER_ACCESS(Lookup, ERHIResourceAccess::TransferSrc)
            RDG_BUFFER_ACCESS(Hits, ERHIResourceAccess::TransferSrc)
        END_SHADER_PARAMETER_STRUCT(RadiosityTestReadback)

        BEGIN_SHADER_PARAMETER_STRUCT(WorldCacheTestUpload)
            RDG_TEXTURE_ACCESS(GBufferA, ERHIResourceAccess::TransferDest)
            RDG_TEXTURE_ACCESS(GBufferB, ERHIResourceAccess::TransferDest)
            RDG_TEXTURE_ACCESS(GBufferC, ERHIResourceAccess::TransferDest)
            RDG_TEXTURE_ACCESS(Depth, ERHIResourceAccess::TransferDest)
        END_SHADER_PARAMETER_STRUCT(WorldCacheTestUpload)

        BEGIN_SHADER_PARAMETER_STRUCT(WorldCacheTestReadback)
            RDG_TEXTURE_ACCESS(Marker, ERHIResourceAccess::TransferSrc)
            RDG_BUFFER_ACCESS(Radiance, ERHIResourceAccess::TransferSrc)
            RDG_TEXTURE_ACCESS(Atlas, ERHIResourceAccess::TransferSrc)
        END_SHADER_PARAMETER_STRUCT(WorldCacheTestReadback)

        constexpr uint32_t TestScreenSize = 32;
        constexpr uint32_t TestRayCount = TestScreenSize / 8 * (TestScreenSize / 8) * 8;

        ScreenProbeGatherPassInput MakeProbeInput(RenderGraphBuilder& graph, float normalZ)
        {
            RenderGraphTextureDesc desc;
            desc.Width = desc.Height = TestScreenSize;
            desc.Format = ERHIFormat::R32G32B32A32_Float;
            desc.Usage = ERHITextureCreateFlag::ShaderResource | ERHITextureCreateFlag::TransferDest;
            ScreenProbeGatherPassInput input;
            auto* upload = graph.AllocateParameter<WorldCacheTestUpload>();
            input.GBufferA = upload->GBufferA = graph.CreateTexture("Test.GBufferA", desc);
            input.GBufferB = upload->GBufferB = graph.CreateTexture("Test.GBufferB", desc);
            input.GBufferC = upload->GBufferC = graph.CreateTexture("Test.GBufferC", desc);
            input.Depth = upload->Depth = graph.CreateTexture("Test.Depth", desc);
            graph.AddPass("WorldCacheTestGBuffer", WorldCacheTestUpload::GetMetaData(), upload, EPassFlag::Graphic,
                [=](RHICommandListBase& cmd)
                {
                    const auto update = [&](RenderGraphTextureRef texture, const Float4& value)
                    {
                        std::array<Float4, TestScreenSize * TestScreenSize> pixels;
                        pixels.fill(value);
                        GRHIApi->UpdateTexture(cmd, texture->GetRHITexture(), pixels.data(),
                            RHIUpdateTextureRegion::Create2DRegion(TestScreenSize, TestScreenSize));
                    };
                    update(upload->GBufferA, {1, 1, 1, 0});
                    update(upload->GBufferB, {0.5f, 0.5f, normalZ * 0.5f + 0.5f, 1});
                    update(upload->GBufferC, {0, 1, 1.0f / 255.0f, 1});
                    update(upload->Depth, {0.5f, 0, 0, 0});
                });

            input.InvViewProj(0, 0) = input.InvViewProj(1, 1) = 0.5f;
            input.InvViewProj(2, 3) = -0.5f;
            input.CameraPosition = {0, 0, -1};
            input.DebugMode = 5;
            input.Environment.IBLDiffuseMap = input.Environment.IBLSpecularMap =
                graph.RegisterExternalTexture("Test.EmptyEnvironment", GlobalEmptyCubeTexture.get());
            input.Environment.IBLLut = graph.RegisterExternalTexture("Test.EnvironmentLUT", GlobalWhiteTexture.get());
            input.Environment.LinearClampIBLSampler = GlobalSampler.get();
            return input;
        }
    }

    class SurfaceCacheGITest : public TestBase
    {
    protected:
        void InitializeGI(bool bWorldCache, bool bReflections = false, bool bHardwareTracing = false)
        {
            GRHIApi = CreateTestRHIApi();
            GRHIApi->Init();
            GTransientResourceAllocator.InitRHI();
            InitGlobalRenderResource();
            auto& globals = ShaderType::GetRegisterMap()[ShaderType::EShaderTypeFlag::Global];
            const auto registered = globals;
            std::erase_if(globals, [=](const auto& entry)
            {
                return !entry.first.starts_with("SurfaceCache") && entry.first != "PositionOnlyVS" &&
                    !(bWorldCache && (entry.first.starts_with("WorldRadiance") || entry.first.starts_with("ScreenProbe") ||
                        entry.first.starts_with("VoxelLighting"))) &&
                    !(bReflections && entry.first.starts_with("Reflection")) &&
                    !(bHardwareTracing && entry.first == "GIHardwareResolveCS");
            });
            GShaderMap.Initialize();
            globals = registered;
            GDistanceFieldMgr.Initialize();
        }

    public:
        void Teardown() override
        {
            GDistanceFieldMgr.Release();
            GMeshMaterialShaderMap.Clear();
            GMaterialShaderMap.Clear();
            GShaderMap.Clear();
            RHIPipelineStateCache::ClearAll();
            GTransientResourceAllocator.ReleaseRHI();
            ReleaseGlobalRenderResource();
            GRHIApi->Shutdown();
            delete GRHIApi;
            GRHIApi = nullptr;
        }
    };

    class SurfaceCacheRadiosityTest final : public SurfaceCacheGITest
    {
    public:
        void Setup() override { InitializeGI(false); }

        void Run() override
        {
            auto wall = MakeWall();
            const std::array<std::array<float, 4>, 4> colors = {{
                {0.88f, 0.06f, 0.04f, 1}, {0.85f, 0.85f, 0.85f, 1},
                {0.42f, 0.84f, 0.72f, 1}, {0.70f, 0.56f, 0.85f, 1}
            }};
            std::array<Material, colors.size()> materials;
            std::vector<SurfaceCacheMeshInstance> instances(colors.size());
            for (uint32_t i = 0; i < instances.size(); ++i)
            {
                materials[i].SetShadingModel(EShadingModel::Lit);
                materials[i].SetParameter("BaseColor", colors[i]);
                materials[i].SetParameter("Metallic", 0.0f);
                instances[i].InstanceID = i;
                instances[i].RenderData = wall;
                instances[i].Materials = {materials[i].GetRenderProxyShared()};
                auto& transform = instances[i].LocalToWorld;
                if (i < 2)
                {
                    transform(2, 3) = i == 0 ? -2.15f : 2.15f;
                }
                else
                {
                    transform(0, 0) = transform(2, 2) = 0;
                    transform(0, 2) = 1;
                    transform(2, 0) = -1;
                    transform(0, 3) = i == 2 ? -2.15f : 2.15f;
                }
            }

            SurfaceCacheMaterial cache;
            cache.Initialize(instances, 32);
            SurfaceCacheDirectLighting lighting;
            lighting.Initialize(cache, 64, 6);
            auto radiosity = std::make_unique<SurfaceCacheRadiosity>();
            SurfaceCacheLight light;
            light.Direction = {0, 0, -1};
            light.Intensity = 3.0f;
            light.bCastShadow = false;

            float firstBounce = 0;
            float lastDelta = 0;
            float firstBounceAtRed = 0;
            float secondBounceGain = 0;
            std::vector<Float4> previous;
            RHITexture* twoAtlases[2] = {};
            constexpr uint32_t iterations = 16;
            for (uint32_t frame = 0; frame <= iterations; ++frame)
            {
                const bool reset = frame == iterations;
                RenderGraphBuilder graph;
                const auto material = frame == 0 ? cache.AddCapturePasses(graph) : cache.Register(graph);
                if (reset)
                {
                    radiosity->Invalidate();
                    light.bEnabled = false;
                    Verify(radiosity->GetAtlas() == nullptr, "invalidated history must be hidden");
                }
                const auto direct = frame == 0 || reset ? lighting.AddUpdatePasses(graph, cache, {light}) : lighting.Register(graph, cache);
                radiosity->AddIteration(graph, material, direct);
                auto* atlas = radiosity->GetAtlas();
                const auto combined = radiosity->Register(graph, direct);
                const uint32_t slot = (radiosity->GetIteration() - 1) % 2;
                if (!twoAtlases[slot])
                {
                    twoAtlases[slot] = atlas->GetRHI();
                }
                Verify(atlas->GetRHI() == twoAtlases[slot] && atlas->GetRHI() != twoAtlases[1 - slot],
                    "ping-pong uses two persistent, distinct textures");

                SurfaceCacheQuery query;
                query.OriginDistance = {0, 0, 2, 0};
                query.Direction = {0, 0, -1, 0};
                query.Control = {1, 0, 0, 0};
                auto redQuery = query;
                redQuery.OriginDistance.Z = -2;
                redQuery.Direction.Z = 1;
                redQuery.Control.X = 0;
                auto rayQuery = query;
                rayQuery.OriginDistance = {0, 0, 1.97f, 8};
                rayQuery.Control = {UINT32_MAX, 1, 1, 0};
                auto hits = AddSurfaceCacheQueryPass(graph, material, std::vector<SurfaceCacheQuery>{query, redQuery, rayQuery});
                auto total = AddSurfaceCacheLightingLookupPass(graph, material, combined, hits, 3);

                void* atlasPixels = nullptr;
                void* lookupData = nullptr;
                void* hitData = nullptr;
                const bool gl = GShaderPlatform == ERHIShaderPlatform::OpenGL;
                const auto readback = [&](RHICommandListBase& cmd)
                {
                    atlasPixels = GRHIApi->MapReadTexture(cmd, atlas->GetRHI(), {});
                    lookupData = GRHIApi->MapReadBuffer(cmd, total->GetRHIBuffer(), {});
                    hitData = GRHIApi->MapReadBuffer(cmd, hits->GetRHIBuffer(), {});
                };
                auto* params = graph.AllocateParameter<RadiosityTestReadback>();
                params->Atlas = combined.IndirectRadiance;
                params->Lookup = total;
                params->Hits = hits;
                graph.AddPass("RadiosityTestReadback", RadiosityTestReadback::GetMetaData(), params, EPassFlag::Graphic,
                    [&](RHICommandListBase& cmd)
                    {
                        if (!gl)
                        {
                            readback(cmd);
                        }
                    });
                graph.Execute();
                auto fence = atlas->GetTracker().GetLastAccessFence();
                Verify(fence.Value != 0, "readback fence was recorded");
                auto* queue = GRHIApi->GetQueue(fence.QueueType);
                queue->WaitValue(fence.Value);
                if (gl)
                {
                    auto* context = queue->AcquireCommandContext();
                    RHICommandListBase cmd(context);
                    cmd.SetImmediate(true);
                    cmd.Begin();
                    readback(cmd);
                    cmd.End();
                    queue->WaitValue(queue->ExecuteContext(context));
                }
                Verify(atlasPixels && lookupData && hitData, "atlas and combined lookup readback exists");
                const auto width = atlas->GetRHI()->GetDesc().Width;
                const auto size = width * width;
                const auto* pixels = static_cast<const Float4*>(atlasPixels);
                const auto lookup = *static_cast<const SurfaceCacheLightingQueryResult*>(lookupData);
                const auto* hitResults = static_cast<const SurfaceCacheQueryResult*>(hitData);
                const auto* lookupResults = static_cast<const SurfaceCacheLightingQueryResult*>(lookupData);
                const bool bRedWallHit = hitResults[2].Status.X == 0 && hitResults[2].Status.Y == 4 &&
                    (hitResults[2].Status.Z & 3u) == 3u;
                if (frame == 0)
                {
                    for (uint32_t i = 0; i < 3; ++i)
                    {
                        const auto& hit = hitResults[i];
                        std::cout << "RADIOSITY_QUERY=" << i << " owner=" << hit.Status.X << " card=" << hit.Status.Y
                            << " flags=" << hit.Status.Z << " hitZ=" << hit.PositionDistance.Z
                            << " normalZ=" << hit.NormalRoughness.Z << " light=" << lookupResults[i].Radiance.X << std::endl;
                    }
                }
                float maximum = 0;
                float delta = 0;
                bool valid = true;
                for (uint32_t i = 0; i < size; ++i)
                {
                    const auto& p = pixels[i];
                    valid &= std::isfinite(p.X) && std::isfinite(p.Y) && std::isfinite(p.Z) &&
                        p.X >= 0 && p.Y >= 0 && p.Z >= 0 && (p.W == 1 || (p.W == 0 && p.X == 0 && p.Y == 0 && p.Z == 0));
                    maximum = std::max({maximum, p.X, p.Y, p.Z});
                    if (!previous.empty())
                    {
                        delta = std::max({delta, std::abs(p.X - previous[i].X), std::abs(p.Y - previous[i].Y), std::abs(p.Z - previous[i].Z)});
                    }
                }
                // The red wall faces +Z (Card 4) and the unlit white wall faces -Z (Card 11).
                const auto tileSize = combined.RadiosityTileSize;
                const auto columns = width / tileSize;
                const auto center = [=](uint32_t card)
                {
                    return pixels[(card / columns * tileSize + tileSize / 2) * width + card % columns * tileSize + tileSize / 2];
                };
                const auto red = center(4);
                const auto white = center(11);
                previous.assign(pixels, pixels + size);
                GRHIApi->Unmap(atlasPixels);
                GRHIApi->Unmap(lookupData);
                GRHIApi->Unmap(hitData);
                std::cout << "RADIOSITY_FRAME=" << frame + 1 << " white=" << white.X << "," << white.Y << "," << white.Z
                    << " red=" << red.X << " lookup=" << lookup.Radiance.X
                    << " maximum=" << maximum << " delta=" << delta << std::endl;
                Verify(valid && maximum <= 4.0f, "radiance is finite, nonnegative and bounded");
                Verify(white.W == 1.0f && lookup.Radiance.W == 1.0f, "wall and combined lighting lookup are valid");
                Verify(bRedWallHit, "SDF ray across the room hits the interior red Card");
                if (frame == 0)
                {
                    firstBounce = white.X;
                    firstBounceAtRed = red.X;
                    Verify(firstBounce > 0.1f && white.X > 4 * white.Y, "first bounce transfers red onto a white wall with no direct light");
                    Verify(std::abs(lookup.Radiance.X - white.X) < 0.03f, "combined lookup returns indirect outgoing radiance");
                }
                if (frame == 1)
                {
                    secondBounceGain = red.X - firstBounceAtRed;
                    Verify(secondBounceGain > 0.025f, "second bounce returns indirect red radiance to the source wall");
                }
                if (frame == iterations - 1)
                {
                    lastDelta = delta;
                    Verify(lastDelta < 0.002f, "fixed scene converges without iteration noise");
                }
                if (reset)
                {
                    Verify(maximum == 0 && lookup.Radiance.X == 0, "light removal and invalidation discard stale history");
                }
            }
            radiosity.reset();
            lighting.Release();
            cache.Release();
            GDistanceFieldMgr.ReleaseMeshDistanceField(*wall);
            wall->LODResources.front().ReleaseResources();
            std::cout << "SURFACE_CACHE_RADIOSITY_TEST_OK first=" << firstBounce
                << " second=" << secondBounceGain << " final_delta=" << lastDelta << std::endl;
        }

    };
    REGISTER_RENDER_TEST("SurfaceCacheRadiosityTest", SurfaceCacheRadiosityTest);

    class WorldRadianceCacheTest final : public SurfaceCacheGITest
    {
    public:
        void Setup() override { InitializeGI(true); }

        void Run() override
        {
            auto mesh = MakeWall(8.0f);
            std::array<Material, 2> materials;
            std::vector<SurfaceCacheMeshInstance> instances(2);
            for (uint32_t i = 0; i < instances.size(); ++i)
            {
                materials[i].SetShadingModel(EShadingModel::Lit);
                materials[i].SetParameter("Metallic", 0.0f);
                materials[i].SetParameter("BaseColor", i == 0 ? std::array<float, 4>{0.9f, 0.05f, 0.02f, 1} :
                    std::array<float, 4>{0.9f, 0.9f, 0.9f, 1});
                instances[i].InstanceID = i;
                instances[i].RenderData = mesh;
                instances[i].Materials = {materials[i].GetRenderProxyShared()};
                instances[i].LocalToWorld(2, 3) = i == 0 ? -2.0f : 10.0f;
            }
            SurfaceCacheMaterial surface;
            surface.Initialize(instances, 32);
            SurfaceCacheDirectLighting direct;
            direct.Initialize(surface, 64, 6);
            SurfaceCacheRadiosity radiosity;
            WorldRadianceCache cache;
            VoxelLighting voxelCache;
            SurfaceCacheLight light;
            light.Direction = {0, 0, -1};
            light.Intensity = 3.0f;
            light.bCastShadow = false;

            RHITextureDesc markerDesc;
            markerDesc.Width = markerDesc.Height = TestScreenSize;
            markerDesc.Format = ERHIFormat::R32G32B32A32_Float;
            markerDesc.Usage = ERHITextureCreateFlag::UAV | ERHITextureCreateFlag::ShaderResource |
                ERHITextureCreateFlag::TransferSrc;
            RenderTexture marker(markerDesc);
            marker.InitRHIResource();
            struct Frame
            {
                std::array<ScreenProbeRadianceRecord, TestRayCount> Samples;
                std::vector<Float4> Atlas;
                uint32_t Directions;
            };
            const auto render = [&](uint32_t budget, const Float3& camera, bool capture, bool iterate, bool useCache, bool reset,
                float normalZ = 1.0f, bool bVoxels = false)
            {
                RenderGraphBuilder graph;
                auto material = capture ? surface.AddCapturePasses(graph) : surface.Register(graph);
                auto lighting = capture || reset ? direct.AddUpdatePasses(graph, surface, {light}) : direct.Register(graph, surface);
                if (iterate)
                {
                    radiosity.AddIteration(graph, material, lighting);
                }
                lighting = radiosity.Register(graph, lighting);
                auto input = MakeProbeInput(graph, normalZ);
                input.IndirectDiffuseOutput = graph.RegisterExternalTexture("Test.Marker", &marker);
                auto world = cache.Update(graph, camera, budget, material, lighting, input.Environment, input.bHasEnvironment);
                input.WorldCache = useCache ? &world : nullptr;
                VoxelLightingParameters voxels{};
                if (bVoxels)
                {
                    voxels = voxelCache.Update(graph, world);
                    input.Voxels = &voxels;
                }
                else
                {
                    voxelCache.Invalidate();
                }
                const auto screen = AddScreenProbeGatherPasses(graph, material, lighting, input);
                if (bVoxels)
                {
                    VoxelLighting::AddDebugPass(graph, world, voxels, screen.DebugVisualization, 3);
                }
                else
                {
                    WorldRadianceCache::AddDebugPass(graph, world, screen.DebugVisualization);
                }
                void* mappedRadiance = nullptr;
                void* mappedAtlas = nullptr;
                const auto map = [&](RHICommandListBase& cmd)
                {
                    mappedRadiance = GRHIApi->MapReadBuffer(cmd, screen.Radiance->GetRHIBuffer(), {});
                    mappedAtlas = GRHIApi->MapReadTexture(cmd, world.WorldRadianceAtlas->GetRHITexture(), {});
                };
                auto* read = graph.AllocateParameter<WorldCacheTestReadback>();
                read->Marker = input.IndirectDiffuseOutput;
                read->Radiance = screen.Radiance;
                read->Atlas = world.WorldRadianceAtlas;
                const bool bGL = GShaderPlatform == ERHIShaderPlatform::OpenGL;
                graph.AddPass("WorldCacheTestReadback", WorldCacheTestReadback::GetMetaData(), read, EPassFlag::Graphic,
                    [&](RHICommandListBase& cmd)
                    {
                        if (!bGL)
                        {
                            map(cmd);
                        }
                    });
                graph.Execute();
                auto fence = marker.GetTracker().GetLastAccessFence();
                auto* queue = GRHIApi->GetQueue(fence.QueueType);
                queue->WaitValue(fence.Value);
                if (bGL)
                {
                    auto* context = queue->AcquireCommandContext();
                    RHICommandListBase cmd(context);
                    cmd.SetImmediate(true);
                    cmd.Begin();
                    map(cmd);
                    cmd.End();
                    queue->WaitValue(queue->ExecuteContext(context));
                }
                Verify(mappedRadiance && mappedAtlas, "world cache and screen probe readback");
                Frame frame;
                std::copy_n(static_cast<const ScreenProbeRadianceRecord*>(mappedRadiance), TestRayCount, frame.Samples.begin());
                auto* start = static_cast<const Float4*>(mappedAtlas);
                frame.Directions = world.WorldRadianceAtlas->GetDesc().Width;
                frame.Atlas.assign(start, start + frame.Directions * world.WorldRadianceAtlas->GetDesc().Height);
                GRHIApi->Unmap(mappedRadiance);
                GRHIApi->Unmap(mappedAtlas);
                return frame;
            };

            const Float3 camera{0.1f, 0, 0};
            const auto cold = render(0, camera, true, true, true, false);
            render(128, camera, false, true, false, false);
            render(128, camera, false, true, false, false);
            render(128, camera, false, true, false, false);
            const auto reference = render(128, camera, false, false, false, false);
            const auto cached = render(0, camera, false, false, true, false);
            const auto moved = render(0, {2.1f, 0, 0}, false, false, true, false);
            Verify(std::equal(cached.Atlas.begin(), cached.Atlas.begin() + cached.Directions, moved.Atlas.begin()),
                "slot for world cell zero retains all directions after scrolling");
            uint32_t cacheHits = 0, movedHits = 0, traceSteps = 0, cachedSteps = 0;
            float reflectedRed = 0.0f, reflectedGreen = 0.0f;
            float referenceRed = 0.0f, atlasRed = 0.0f;
            uint32_t geometryDirections = 0;
            for (const auto& direction : cached.Atlas)
            {
                if (direction.W > 0)
                {
                    ++geometryDirections;
                    atlasRed = std::max(atlasRed, direction.X);
                }
            }
            for (uint32_t i = 0; i < cached.Samples.size(); ++i)
            {
                const auto& sample = cached.Samples[i];
                const auto& expected = reference.Samples[i];
                Verify(cold.Samples[i].Status.Y == 0 && expected.Status.Y == 0, "cold and disabled paths use full tracing");
                Verify(std::isfinite(sample.Radiance.X) && sample.Radiance.X >= 0.0f, "finite nonnegative radiance");
                traceSteps += expected.Status.Z;
                cachedSteps += sample.Status.Z;
                referenceRed = std::max(referenceRed, expected.Radiance.X);
                if (sample.Status.Y != 0)
                {
                    ++cacheHits;
                    if (sample.Radiance.X > reflectedRed)
                    {
                        reflectedRed = sample.Radiance.X;
                        reflectedGreen = sample.Radiance.Y;
                    }
                }
                if (sample.Status.Y != 0 && moved.Samples[i].Status.Y == sample.Status.Y &&
                    moved.Samples[i].Radiance == sample.Radiance)
                {
                    ++movedHits;
                }
            }
            std::cout << "WORLD_RADIANCE_CACHE hits=" << cacheHits << " reused=" << movedHits <<
                " sdf_steps=" << traceSteps << " cached_steps=" << cachedSteps <<
                " reflected=" << reflectedRed << "," << reflectedGreen <<
                " reference_red=" << referenceRed << " geometry_directions=" << geometryDirections << " atlas_red=" << atlasRed << std::endl;
            Verify(cacheHits > 0 && movedHits > 0 && cachedSteps < traceSteps, "world cache replaces some far SDF marching across frames");
            Verify(reflectedRed > 0.03f && reflectedRed > 4 * reflectedGreen, "world rays see the white wall's red radiosity");

            const auto voxelFrame = render(0, camera, false, false, true, false, 1.0f, true);
            const auto voxelMoved = render(0, {5.1f, 0, 0}, false, false, true, false, 1.0f, true);
            uint32_t voxelHits = 0, reusedVoxelHits = 0, voxelSteps = 0;
            float voxelMaxError = 0.0f;
            for (uint32_t i = 0; i < voxelFrame.Samples.size(); ++i)
            {
                const auto& voxel = voxelFrame.Samples[i];
                voxelSteps += voxel.Status.Z;
                if (voxel.Status.Y == 3u)
                {
                    ++voxelHits;
                    const auto& movedVoxel = voxelMoved.Samples[i];
                    reusedVoxelHits += movedVoxel.Status.Y == 3u && movedVoxel.Radiance == voxel.Radiance ? 1u : 0u;
                    const auto delta = voxel.Radiance - reference.Samples[i].Radiance;
                    voxelMaxError = std::max({voxelMaxError, std::abs(delta.X), std::abs(delta.Y), std::abs(delta.Z)});
                }
            }
            std::cout << "VOXEL_LIGHTING hits=" << voxelHits << " reused=" << reusedVoxelHits <<
                " sdf_steps=" << voxelSteps << " world_steps=" << cachedSteps << " full_steps=" << traceSteps <<
                " max_rgb_difference_vs_full=" << voxelMaxError << std::endl;
            Verify(voxelSteps <= cachedSteps && voxelMaxError < 0.15f,
                "mixed two-wall visibility safely falls back to the world cache or shortens the trace");
            const auto voxelNear = render(0, camera, false, false, true, false, -1.0f, true);

            const auto nearReference = render(0, camera, false, false, false, false, -1.0f);
            const auto nearCached = render(0, camera, false, false, true, false, -1.0f);
            for (uint32_t i = 0; i < nearCached.Samples.size(); i += 8)
            {
                const auto& referenceRay = nearReference.Samples[i];
                const auto& cachedRay = nearCached.Samples[i];
                const auto& voxelRay = voxelNear.Samples[i];
                Verify(referenceRay.Status.Z > 0 && cachedRay.Status.Y == 0 &&
                    std::abs(cachedRay.Radiance.X - referenceRay.Radiance.X) < 1e-5f &&
                    cachedRay.Radiance.X > 0.5f, "the near wall occludes any cached far radiance");
                Verify(voxelRay.Status.Y == 0 && std::abs(voxelRay.Radiance.X - referenceRay.Radiance.X) < 1e-5f,
                    "voxel lighting preserves the near-wall geometry hit");
            }

            cache.Invalidate();
            radiosity.Invalidate();
            light.bEnabled = false;
            const auto reset = render(0, camera, false, false, true, true, 1.0f, true);
            for (const auto& sample : reset.Samples)
            {
                Verify(sample.Status.Y == 0 && sample.Radiance.X <= 0.0251f, "invalidation suppresses radiance from old lights");
            }
            direct.Release();
            surface.Release();
            GDistanceFieldMgr.ReleaseMeshDistanceField(*mesh);
            mesh->LODResources.front().ReleaseResources();
            std::cout << "WORLD_RADIANCE_CACHE_TEST_OK" << std::endl;
        }
    };
    REGISTER_RENDER_TEST("WorldRadianceCacheTest", WorldRadianceCacheTest);

    namespace
    {
        BEGIN_SHADER_PARAMETER_STRUCT(VoxelTestUpload)
            RDG_TEXTURE_ACCESS(Atlas, ERHIResourceAccess::TransferDest)
        END_SHADER_PARAMETER_STRUCT(VoxelTestUpload)

        BEGIN_SHADER_PARAMETER_STRUCT(VoxelTestReadback)
            RDG_TEXTURE_ACCESS(Debug, ERHIResourceAccess::TransferSrc)
            RDG_BUFFER_ACCESS(Candidates, ERHIResourceAccess::TransferSrc)
        END_SHADER_PARAMETER_STRUCT(VoxelTestReadback)
    }

    class VoxelLightingTest final : public SurfaceCacheGITest
    {
    public:
        void Setup() override { InitializeGI(true); }

        void Run() override
        {
            constexpr int size = 6, directions = 36, coarseSlots = size * size * size;
            constexpr size_t rayCount = 5;
            constexpr uint32_t debugWidth = 216, debugHeight = 144;
            const std::array<Float3, 6> axisColors = {{
                {0.8f, 0.05f, 0.05f}, {0.05f, 0.05f, 0.8f}, {0.05f, 0.8f, 0.05f},
                {0.4f, 0.2f, 0.05f}, {0.2f, 0.4f, 0.6f}, {0.05f, 0.3f, 0.2f}
            }};
            const auto getFace = [](int index)
            {
                float x = float(2 * (index % size) + 1 - size) / size;
                float y = float(2 * (index / size) + 1 - size) / size;
                const float z = 1.0f - std::abs(x) - std::abs(y);
                if (z < 0)
                {
                    const float foldedX = std::copysign(1.0f - std::abs(y), x);
                    y = std::copysign(1.0f - std::abs(x), y);
                    x = foldedX;
                }
                const auto ax = std::abs(x), ay = std::abs(y), az = std::abs(z);
                if (ax + 1e-6f >= std::max(ay, az))
                {
                    return x >= 0 ? 0 : 1;
                }
                if (ay + 1e-6f >= az)
                {
                    return y >= 0 ? 2 : 3;
                }
                return z >= 0 ? 4 : 5;
            };
            const auto wrap = [](int x) { return (x % size + size) % size; };
            const auto expectedColor = [&](int face, float worldX, float scale)
            {
                return (axisColors[face] + Float3(0, worldX * 0.01f + 0.15f, 0)) * scale;
            };
            struct Frame
            {
                std::array<WorldRadianceCandidate, rayCount> Candidates;
                Float4 DebugCenter;
            };
            RHITextureDesc debugDesc{};
            debugDesc.Width = debugWidth;
            debugDesc.Height = debugHeight;
            debugDesc.Format = ERHIFormat::R32G32B32A32_Float;
            debugDesc.Usage = ERHITextureCreateFlag::ShaderResource | ERHITextureCreateFlag::UAV | ERHITextureCreateFlag::TransferSrc;
            RenderTexture debug(debugDesc);
            debug.InitRHIResource();
            VoxelLighting voxels;

            const auto render = [&](int originX, float scale, bool bUpdated, bool bValid, bool bBlocker, bool bVoxels)
            {
                std::array<WorldRadianceProbeRecord, 2 * coarseSlots> probes{};
                std::vector<Float4> colors(2 * coarseSlots * directions);
                for (int z = -3; z < 3; ++z)
                {
                    for (int y = -3; y < 3; ++y)
                    {
                        for (int x = originX; x < originX + size; ++x)
                        {
                            const int slot = ((size + wrap(z)) * size + wrap(y)) * size + wrap(x);
                            const bool valid = bValid && x < 3;
                            probes[slot].PositionSpacing = {float(x * 5), float(y * 5), float(z * 5), 5.0f};
                            probes[slot].Status = {valid ? 1u : 0u, bUpdated && valid ? 1u : 0u, 0u, 1u};
                            for (int ray = 0; ray < directions; ++ray)
                            {
                                const int face = getFace(ray);
                                const float distance = bBlocker && ((x == 0 && face == 0) || (x == 1 && face == 1)) ? 0.5f : 12.0f;
                                const auto color = expectedColor(face, float(x * 5), scale);
                                colors[slot * directions + ray] = {color.X, color.Y, color.Z, distance};
                            }
                        }
                    }
                }

                RenderGraphBuilder graph;
                WorldRadianceCacheParameters source{};
                source.LevelOrigin0 = {-3, -3, -3};
                source.LevelOrigin1 = {originX, -3, -3};
                source.GridSize = source.DirectionSize = size;
                source.CellSize = {2, 5};
                source.TraceDistance = 100;
                RenderGraphTextureDesc atlasDesc{};
                atlasDesc.Width = directions;
                atlasDesc.Height = 2 * coarseSlots;
                atlasDesc.Format = ERHIFormat::R32G32B32A32_Float;
                atlasDesc.Usage = ERHITextureCreateFlag::ShaderResource | ERHITextureCreateFlag::TransferDest;
                auto* upload = graph.AllocateParameter<VoxelTestUpload>();
                source.WorldRadianceAtlas = upload->Atlas = graph.CreateTexture("VoxelTest.Source", atlasDesc);
                graph.AddPass("VoxelTestSource", VoxelTestUpload::GetMetaData(), upload, EPassFlag::Graphic, [&](RHICommandListBase& cmd)
                {
                    GRHIApi->UpdateTexture(cmd, upload->Atlas->GetRHITexture(), colors.data(),
                        RHIUpdateTextureRegion::Create2DRegion(directions, 2 * coarseSlots));
                });

                RenderGraphBufferDesc desc{};
                desc.Stride = sizeof(WorldRadianceProbeRecord);
                desc.Size = sizeof(probes);
                desc.Usage = ERHIBufferUsageFlag::Structured | ERHIBufferUsageFlag::ShaderResource | ERHIBufferUsageFlag::TransferDst;
                auto probeBuffer = graph.CreateBuffer("VoxelTest.Probes", desc);
                graph.AddUploadBuffer(probeBuffer, probes.data(), sizeof(probes));
                RenderGraphBufferSRVDesc view{};
                view.Buffer = probeBuffer;
                view.Stride = desc.Stride;
                view.NumElements = uint32_t(probes.size());
                source.WorldProbes = graph.CreateBufferSRV("VoxelTest.ProbesSRV", view);

                const std::array<Float3, rayCount> rayDirections = {{
                    {1, 0, 0}, {-1, 0, 0}, Normalize(Float3(1, 1, 0)), {0, 0, 1}, {0, 0, 1}
                }};
                const float positions[] = {0, 0, 0, 2.5f, 12.5f};
                std::array<SurfaceCacheQuery, rayCount> rays{};
                for (size_t i = 0; i < rayCount; ++i)
                {
                    const auto start = Float3(positions[i], 0, 0) - rayDirections[i] * 4.0f;
                    rays[i].OriginDistance = {start.X, start.Y, start.Z, 25};
                    rays[i].Direction = {rayDirections[i].X, rayDirections[i].Y, rayDirections[i].Z, 0};
                    rays[i].Control = {UINT32_MAX, 1, UINT32_MAX, 0};
                }
                desc.Stride = sizeof(SurfaceCacheQuery);
                desc.Size = sizeof(rays);
                auto rayBuffer = graph.CreateBuffer("VoxelTest.Queries", desc);
                graph.AddUploadBuffer(rayBuffer, rays.data(), sizeof(rays));

                const auto voxel = voxels.Update(graph, source);
                auto lookup = WorldRadianceCache::AddRayLookupPass(graph, source, rayBuffer, uint32_t(rays.size()),
                    bVoxels ? &voxel : nullptr);
                auto* read = graph.AllocateParameter<VoxelTestReadback>();
                read->Candidates = lookup.Candidates;
                read->Debug = graph.RegisterExternalTexture("VoxelTest.Debug", &debug);
                VoxelLighting::AddDebugPass(graph, source, voxel, read->Debug, 3);

                void* mapped = nullptr;
                void* mappedDebug = nullptr;
                const auto map = [&](RHICommandListBase& cmd)
                {
                    mapped = GRHIApi->MapReadBuffer(cmd, read->Candidates->GetRHIBuffer(), {});
                    mappedDebug = GRHIApi->MapReadTexture(cmd, read->Debug->GetRHITexture(), {});
                };
                const bool bGL = GShaderPlatform == ERHIShaderPlatform::OpenGL;
                graph.AddPass("VoxelTestReadback", VoxelTestReadback::GetMetaData(), read, EPassFlag::Graphic, [&](RHICommandListBase& cmd)
                {
                    if (!bGL)
                    {
                        map(cmd);
                    }
                });
                graph.Execute();
                const auto fence = debug.GetTracker().GetLastAccessFence();
                auto* queue = GRHIApi->GetQueue(fence.QueueType);
                queue->WaitValue(fence.Value);
                if (bGL)
                {
                    auto* context = queue->AcquireCommandContext();
                    RHICommandListBase cmd(context);
                    cmd.SetImmediate(true);
                    cmd.Begin();
                    map(cmd);
                    cmd.End();
                    queue->WaitValue(queue->ExecuteContext(context));
                }
                Verify(mapped && mappedDebug, "voxel candidate and debug readbacks exist");
                Frame frame{};
                std::copy_n(static_cast<const WorldRadianceCandidate*>(mapped), rayCount, frame.Candidates.begin());
                frame.DebugCenter = static_cast<const Float4*>(mappedDebug)[(3 * 12 + 6) * debugWidth + (3 - (originX + 3)) * 12 + 6];
                GRHIApi->Unmap(mapped);
                GRHIApi->Unmap(mappedDebug);
                return frame;
            };
            const auto check = [](const WorldRadianceCandidate& sample, const Float3& expected)
            {
                const auto& value = sample.RadianceLevel;
                return value.W == 3.0f && std::abs(value.X - expected.X) < 1e-4f &&
                    std::abs(value.Y - expected.Y) < 1e-4f && std::abs(value.Z - expected.Z) < 1e-4f;
            };
            const auto initial = render(-3, 1, true, true, false, true);
            const bool directional = check(initial.Candidates[0], expectedColor(0, 0, 1)) &&
                check(initial.Candidates[1], expectedColor(1, 0, 1)) &&
                check(initial.Candidates[2], (expectedColor(0, 0, 1) + expectedColor(2, 0, 1)) * 0.5f);
            const auto midpoint = expectedColor(4, 2.5f, 1);
            std::cout << "VOXEL_DIRECTION values=" << initial.Candidates[0].RadianceLevel.X << "," <<
                initial.Candidates[1].RadianceLevel.Z << "," << initial.Candidates[2].RadianceLevel.X <<
                " source=" << initial.Candidates[2].RadianceLevel.W << " interpolated_y=" << initial.Candidates[3].RadianceLevel.Y <<
                " debug=" << initial.DebugCenter.X << std::endl;
            Verify(directional && check(initial.Candidates[3], midpoint), "axis signs, angular blend, and trilinear interpolation");
            Verify(std::abs(initial.DebugCenter.X - 0.8f / 1.8f) < 1e-4f, "debug view shows the +X lobe");

            const auto worldOnly = render(-3, 1, false, true, false, false);
            Verify(worldOnly.Candidates[0].RadianceLevel.W == 2.0f && std::abs(worldOnly.Candidates[0].RadianceLevel.X -
                initial.Candidates[0].RadianceLevel.X) < 1e-4f, "homogeneous axis matches the finer direction source");
            const auto moved = render(-2, 1, false, true, false, true);
            Verify(check(moved.Candidates[3], midpoint) && moved.Candidates[4].RadianceLevel.W == 0.0f,
                "overlapping slots persist across scrolling and entering cells reject stale data");
            const auto blocked = render(-2, 1, true, true, true, true);
            Verify(blocked.Candidates[3].RadianceLevel.W != 3.0f, "short clearance rejects cross-wall interpolation");
            const auto invalid = render(-2, 0.5f, false, false, false, true);
            Verify(invalid.Candidates[0].RadianceLevel.W == 0.0f, "invalid probe data suppresses obsolete voxel light");
            const auto refreshed = render(-2, 0.5f, true, true, false, true);
            Verify(check(refreshed.Candidates[0], expectedColor(0, 0, 0.5f)), "refresh injects the new lighting values");
            std::cout << "VOXEL_LIGHTING_TEST_OK" << std::endl;
        }
    };

    REGISTER_RENDER_TEST("VoxelLightingTest", VoxelLightingTest);

    namespace
    {
        BEGIN_SHADER_PARAMETER_STRUCT(ReflectionsTestUpload)
            RDG_TEXTURE_ACCESS(Color, ERHIResourceAccess::TransferDest)
            RDG_TEXTURE_ACCESS(Normal, ERHIResourceAccess::TransferDest)
            RDG_TEXTURE_ACCESS(Material, ERHIResourceAccess::TransferDest)
            RDG_TEXTURE_ACCESS(Depth, ERHIResourceAccess::TransferDest)
            RDG_TEXTURE_ACCESS(Deferred, ERHIResourceAccess::TransferDest)
            RDG_TEXTURE_ACCESS(Environment, ERHIResourceAccess::TransferDest)
            RDG_TEXTURE_ACCESS(Lut, ERHIResourceAccess::TransferDest)
        END_SHADER_PARAMETER_STRUCT(ReflectionsTestUpload)

        BEGIN_SHADER_PARAMETER_STRUCT(ReflectionsTestReadback)
            RDG_TEXTURE_ACCESS(Output, ERHIResourceAccess::TransferSrc)
        END_SHADER_PARAMETER_STRUCT(ReflectionsTestReadback)
    }

    class ReflectionsTest final : public SurfaceCacheGITest
    {
    public:
        void Setup() override { InitializeGI(true, true, true); }

        void Run() override
        {
            constexpr uint32_t size = 32;
            constexpr uint32_t receiver = 15 * size + 15;
            const Float4 screenColor{0.1f, 0.8f, 0.2f, 1};
            const Float4 baseLighting{0.07f, 0.06f, 0.05f, 1};
            const Float4 sky{0.2f, 0.4f, 0.6f, 1};
            const Float4 lut{0.75f, 0.1f, 0, 1};
            using Image = std::array<Float4, size * size>;
            auto wall = MakeWall(4.0f);
            Material red;
            red.SetShadingModel(EShadingModel::Lit);
            red.SetParameter("BaseColor", std::array<float, 4>{0.85f, 0.04f, 0.03f, 1.0f});
            red.SetParameter("Metallic", 0.0f);
            SurfaceCacheMeshInstance instance;
            instance.InstanceID = 1;
            instance.RenderData = wall;
            instance.LocalToWorld(2, 3) = 2.0f;
            instance.Materials = {red.GetRenderProxyShared()};
            SurfaceCacheMaterial surface;
            surface.Initialize({instance}, 32);
            GIRayTracingScene hardware;
            SurfaceCacheDirectLighting direct;
            direct.Initialize(surface, 64, 6);
            WorldRadianceCache world;
            SurfaceCacheLight light;
            light.Direction = {0, 0, 1};
            light.Intensity = 3.0f;
            light.bCastShadow = false;

            RHITextureDesc textureDesc;
            textureDesc.Width = textureDesc.Height = size;
            textureDesc.Format = ERHIFormat::R32G32B32A32_Float;
            textureDesc.Usage = ERHITextureCreateFlag::UAV | ERHITextureCreateFlag::ShaderResource | ERHITextureCreateFlag::TransferSrc;
            RenderTexture output(textureDesc);
            output.InitRHIResource();
            textureDesc.Width = textureDesc.Height = 1;
            textureDesc.Usage = ERHITextureCreateFlag::ShaderResource | ERHITextureCreateFlag::TransferDest;
            RenderTexture brdf(textureDesc);
            brdf.InitRHIResource();
            textureDesc.Type = ERHITextureType::TextureCube;
            textureDesc.ArraySize = 6;
            RenderTexture environment(textureDesc);
            environment.InitRHIResource();

            bool capture = true;
            const auto render = [&](EReflectionView view, bool bScreenObject, bool bAwayFromWall,
                float roughness = 0.02f, bool bUseWorld = false, bool bDisableCards = false, bool bHardware = false)
            {
                Image colors, normals, materials, depths, deferred;
                colors.fill({0.85f, 0.85f, 0.85f, 0.95f});
                normals.fill({0.5f, 0.5f, 0.0f, roughness});
                materials.fill({0, 1, 1.0f / 255.0f, 1});
                depths.fill({1, 0, 0, 0});
                deferred.fill(baseLighting);
                for (uint32_t y = 3; y < 29; ++y)
                {
                    for (uint32_t x = 4; x <= 15; ++x)
                    {
                        const uint32_t index = y * size + x;
                        depths[index].X = 0.3f;
                        if (!bAwayFromWall)
                        {
                            normals[index] = {0.9472136f, 0.5f, 0.2763932f, roughness};
                        }
                    }
                    if (bScreenObject)
                    {
                        for (uint32_t x = 19; x <= 29; ++x)
                        {
                            const uint32_t index = y * size + x;
                            depths[index].X = 0.55f;
                            deferred[index] = screenColor;
                        }
                    }
                }

                RenderGraphBuilder graph;
                auto material = capture ? surface.AddCapturePasses(graph) : surface.Register(graph);
                if (bDisableCards)
                {
                    // Keep the genuine SDF hit while simulating a region without Card coverage.
                    material.CardCount = 0;
                }
                const auto lighting = capture ? direct.AddUpdatePasses(graph, surface, {light}) : direct.Register(graph, surface);
                RenderGraphTextureDesc desc;
                desc.Width = desc.Height = size;
                desc.Format = ERHIFormat::R32G32B32A32_Float;
                desc.Usage = ERHITextureCreateFlag::ShaderResource | ERHITextureCreateFlag::TransferDest;
                auto* upload = graph.AllocateParameter<ReflectionsTestUpload>();
                upload->Color = graph.CreateTexture("ReflectionsTest.Color", desc);
                upload->Normal = graph.CreateTexture("ReflectionsTest.Normal", desc);
                upload->Material = graph.CreateTexture("ReflectionsTest.Material", desc);
                upload->Depth = graph.CreateTexture("ReflectionsTest.Depth", desc);
                upload->Deferred = graph.CreateTexture("ReflectionsTest.Deferred", desc);
                const auto env = graph.RegisterExternalTexture("ReflectionsTest.Environment", &environment);
                const auto lutTexture = graph.RegisterExternalTexture("ReflectionsTest.BRDF", &brdf);
                upload->Environment = capture ? env : nullptr;
                upload->Lut = capture ? lutTexture : nullptr;
                graph.AddPass("ReflectionsTestUpload", ReflectionsTestUpload::GetMetaData(), upload, EPassFlag::Graphic,
                    [&](RHICommandListBase& cmd)
                    {
                        const auto update = [&](RenderGraphTextureRef texture, const Image& pixels)
                        {
                            GRHIApi->UpdateTexture(cmd, texture->GetRHITexture(), pixels.data(), RHIUpdateTextureRegion::Create2DRegion(size, size));
                        };
                        update(upload->Color, colors);
                        update(upload->Normal, normals);
                        update(upload->Material, materials);
                        update(upload->Depth, depths);
                        update(upload->Deferred, deferred);
                        if (upload->Environment)
                        {
                            auto region = RHIUpdateTextureRegion::Create2DRegion(1, 1);
                            GRHIApi->UpdateTexture(cmd, brdf.GetRHI(), &lut, region);
                            for (region.ArraySlice = 0; region.ArraySlice < 6; ++region.ArraySlice)
                            {
                                GRHIApi->UpdateTexture(cmd, environment.GetRHI(), &sky, region);
                            }
                        }
                    });

                ReflectionsInput input;
                input.GBuffer.GBufferA = upload->Color;
                input.GBuffer.GBufferB = upload->Normal;
                input.GBuffer.GBufferC = upload->Material;
                input.GBuffer.Depth = upload->Depth;
                input.GBuffer.PointSampler = GlobalNearestSampler.get();
                input.DeferredColor = upload->Deferred;
                input.CameraPosition = {-0.03125f, 0.03125f, -1};
                input.View = view;
                input.Environment.IBLDiffuseMap = input.Environment.IBLSpecularMap = env;
                input.Environment.IBLLut = lutTexture;
                input.Environment.LinearClampIBLSampler = GlobalSampler.get();
                input.bHasEnvironment = true;
                SurfaceCachePunctualLightParameters punctual{};
                if (bHardware)
                {
                    punctual = direct.RegisterPunctualLights(graph);
                    input.HardwareLights = &punctual;
                    input.HardwareScene = &hardware;
                }
                WorldRadianceCacheParameters worldParameters{};
                if (bUseWorld)
                {
                    worldParameters = world.Update(graph, input.CameraPosition, 128, material, lighting, input.Environment, true);
                    input.WorldCache = &worldParameters;
                }
                input.Output = graph.RegisterExternalTexture("ReflectionsTest.Output", &output);
                AddReflectionsPasses(graph, input, material, lighting);

                void* mapped = nullptr;
                const auto map = [&](RHICommandListBase& cmd)
                {
                    mapped = GRHIApi->MapReadTexture(cmd, output.GetRHI(), {});
                };
                auto* read = graph.AllocateParameter<ReflectionsTestReadback>();
                read->Output = input.Output;
                const bool bGL = GShaderPlatform == ERHIShaderPlatform::OpenGL;
                graph.AddPass("ReflectionsTestReadback", ReflectionsTestReadback::GetMetaData(), read, EPassFlag::Graphic,
                    [&](RHICommandListBase& cmd)
                    {
                        if (!bGL)
                        {
                            map(cmd);
                        }
                    });
                graph.Execute();
                const auto fence = output.GetTracker().GetLastAccessFence();
                auto* queue = GRHIApi->GetQueue(fence.QueueType);
                queue->WaitValue(fence.Value);
                if (bGL)
                {
                    auto* context = queue->AcquireCommandContext();
                    RHICommandListBase cmd(context);
                    cmd.SetImmediate(true);
                    cmd.Begin();
                    map(cmd);
                    cmd.End();
                    queue->WaitValue(queue->ExecuteContext(context));
                }
                Verify(mapped != nullptr, "reflection pixels were read back");
                Image pixels;
                std::copy_n(static_cast<const Float4*>(mapped), pixels.size(), pixels.begin());
                GRHIApi->Unmap(mapped);
                capture = false;
                return pixels;
            };

            const auto ray = render(EReflectionView::RayDirection, true, false)[receiver];
            const auto screen = render(EReflectionView::Radiance, true, false)[receiver];
            const auto screenHit = render(EReflectionView::ScreenHit, true, false)[receiver];
            const auto sdf = render(EReflectionView::Radiance, false, false)[receiver];
            const auto sdfHit = render(EReflectionView::SDFHit, false, false)[receiver];
            const auto fallback = render(EReflectionView::Radiance, false, true)[receiver];
            std::cout << "Reflection ray=" << ray.X << "," << ray.Z << " screen=" << screen.Y << " confidence=" << screenHit.Z
                << " SDF=" << sdf.X << "," << sdf.Y << " validity=" << sdfHit.Y << " environment=" << fallback.Z << std::endl;
            Verify(std::abs(ray.X - 0.9f) < 1e-4f && std::abs(ray.Z - 0.8f) < 1e-4f, "a smooth receiver reflects along the mirror direction");
            Verify(screenHit.Z > 0.9f && std::abs(screen.Y - screenColor.Y) < 1e-4f, "screen trace hits the visible green target");
            Verify(sdfHit.Y > 0.75f && sdf.X > 0.7f && sdf.X > 3.0f * sdf.Y, "miss continues to off-screen SDF and red Card lighting");
            Verify(std::abs(fallback.Z - sky.Z) < 1e-4f && std::abs(fallback.X - sky.X) < 1e-4f, "a miss restores prefiltered sky radiance");
            const auto uncoveredSource = render(EReflectionView::Source, false, false, 0.02f, false, true)[receiver];
            const auto uncovered = render(EReflectionView::Radiance, false, false, 0.02f, false, true)[receiver];
            Verify(uncoveredSource.X > 0.95f && uncoveredSource.Y < 0.35f && std::abs(uncovered.Z - sky.Z) < 1e-4f,
                "SDF geometry without a valid Card is distinguished from a miss and falls back to sky");

            const auto contribution = render(EReflectionView::Contribution, true, false)[receiver];
            const auto composite = render(EReflectionView::Composite, true, false)[receiver];
            Verify(contribution.Y > 0.0f && contribution.Y < screen.Y &&
                std::abs(composite.Y - (baseLighting.Y + contribution.Y)) < 1e-5f, "composition adds exactly the BRDF-weighted specular term");

            const auto smoothRays = render(EReflectionView::RayDirection, false, true);
            const auto roughRays = render(EReflectionView::RayDirection, false, true, 0.65f);
            uint32_t bentRays = 0;
            for (uint32_t i = 0; i < roughRays.size(); ++i)
            {
                const Float3 delta{roughRays[i].X - smoothRays[i].X, roughRays[i].Y - smoothRays[i].Y, roughRays[i].Z - smoothRays[i].Z};
                if (roughRays[i].Z > 0.0f && delta.LengthSquared() > 0.01f)
                {
                    ++bentRays;
                }
            }
            Verify(bentRays > 80, "roughness broadens the reflection direction distribution");
            Image sources;
            for (uint32_t i = 0; i < 4; ++i)
            {
                sources = render(EReflectionView::Source, false, true, 0.65f, true);
            }
            const auto worldHits = std::ranges::count_if(sources, [](const Float4& color)
            {
                return color.X > 0.75f && color.Z > 0.95f;
            });
            Verify(worldHits > 0, "rough misses use available world radiance probes");
            if (GIRayTracingScene::IsSupported())
            {
                Verify(hardware.Update({instance}), "reflection fixture builds a hardware scene");
                const auto hardwareSource = render(EReflectionView::Source, false, false, 0.02f, false, true, true)[receiver];
                const auto hardwareRadiance = render(EReflectionView::Radiance, false, false, 0.02f, false, true, true)[receiver];
                const auto hardwareMiss = render(EReflectionView::Radiance, false, true, 0.02f, false, false, true)[receiver];
                std::cout << "REFLECTION_HARDWARE source=" << hardwareSource.X << "," << hardwareSource.Y << "," << hardwareSource.Z
                    << " direct=" << hardwareRadiance.X << "," << hardwareRadiance.Y << "," << hardwareRadiance.Z << std::endl;
                Verify(hardwareSource.Y > 0.85f && hardwareSource.Z > 0.9f &&
                    hardwareRadiance.X > 0.65f && hardwareRadiance.X > 3.0f * hardwareRadiance.Y,
                    "an offscreen hardware hit shades its red material and light beyond Card coverage");
                Verify(std::abs(hardwareMiss.Z - sky.Z) < 1e-4f, "hardware miss restores environment");
            }
            std::cout << "REFLECTIONS_TEST_OK roughDirectionChanges=" << bentRays << " worldHits=" << worldHits << std::endl;
            direct.Release();
            surface.Release();
            GDistanceFieldMgr.ReleaseMeshDistanceField(*wall);
            wall->LODResources.front().ReleaseResources();
        }
    };
    REGISTER_RENDER_TEST("ReflectionsTest", ReflectionsTest);

    class GIHardwareTracingTest final : public SurfaceCacheGITest
    {
    public:
        void Setup() override { InitializeGI(true, false, true); }

        void Run() override
        {
            if (!GIRayTracingScene::IsSupported())
            {
                std::cout << "GI_HARDWARE_TRACING_SKIPPED backend unavailable" << std::endl;
                return;
            }

            // 768 triangles make the hardware metadata upload larger than a 64 KiB graph arena page.
            auto wall = MakeWall(1.0f, 8);
            Material red, green;
            red.SetShadingModel(EShadingModel::Lit);
            green.SetShadingModel(EShadingModel::Lit);
            red.SetParameter("BaseColor", std::array<float, 4>{0.85f, 0.04f, 0.03f, 1.0f});
            green.SetParameter("BaseColor", std::array<float, 4>{0.03f, 0.82f, 0.04f, 1.0f});
            red.SetParameter("Metallic", 0.0f);
            green.SetParameter("Metallic", 0.0f);
            SurfaceCacheMeshInstance nearInstance, farInstance;
            nearInstance.InstanceID = 47;
            farInstance.InstanceID = 834;
            nearInstance.RenderData = farInstance.RenderData = wall;
            nearInstance.LocalToWorld(2, 3) = 2.0f;
            farInstance.LocalToWorld(2, 3) = 4.0f;
            nearInstance.Materials = {red.GetRenderProxyShared()};
            farInstance.Materials = {green.GetRenderProxyShared()};
            GIRayTracingScene hardware;

            const auto verifyFrame = [&](const std::vector<SurfaceCacheMeshInstance>& instances, uint32_t expectedOwner,
                float expectedZ, uint64_t revision, bool bGather)
            {
                Verify(hardware.Update(instances), "hardware scene uses Vulkan pipeline and geometry");
                Verify(hardware.GetBLASCount() == 1 && hardware.GetInstanceCount() == instances.size() &&
                    hardware.GetTLASRevision() == revision, "instances share one BLAS and rebuild TLAS on changes");
                SurfaceCacheMaterial surface;
                surface.Initialize(instances, 32);
                SurfaceCacheDirectLighting direct;
                direct.Initialize(surface, 64, 6);
                RenderGraphBuilder graph;
                const auto material = surface.AddCapturePasses(graph);
                SurfaceCacheLight light;
                light.Direction = {0, 0, 1};
                light.Intensity = 3;
                light.bCastShadow = false;
                const auto lighting = direct.AddUpdatePasses(graph, surface, {light});

                std::array<SurfaceCacheQuery, 3> rays{};
                for (auto& ray : rays)
                {
                    ray.OriginDistance = {0.12f, 0.08f, 0, 12};
                    ray.Direction = {0, 0, 1, 0};
                    ray.Control = {UINT32_MAX, 1, UINT32_MAX, 0};
                }
                rays[1].OriginDistance.X = -5;
                rays[2].Control.Y = 2;
                RenderGraphBufferDesc queryDesc{};
                queryDesc.Size = sizeof(rays);
                queryDesc.Stride = sizeof(SurfaceCacheQuery);
                queryDesc.Usage = ERHIBufferUsageFlag::Structured | ERHIBufferUsageFlag::ShaderResource | ERHIBufferUsageFlag::TransferDst;
                auto queryBuffer = graph.CreateBuffer("GIHardwareTest.Queries", queryDesc);
                graph.AddUploadBuffer(queryBuffer, rays.data(), sizeof(rays));
                auto hits = hardware.AddTracePasses(graph, material, queryBuffer, uint32_t(rays.size()));
                RenderGraphBufferRef probeRadiance = nullptr;
                if (bGather)
                {
                    auto input = MakeProbeInput(graph, 1);
                    input.HardwareScene = &hardware;
                    input.DebugMode = 6;
                    probeRadiance = AddScreenProbeGatherPasses(graph, material, lighting, input).Radiance;
                }
                void* mapped = nullptr;
                void* mappedProbes = nullptr;
                auto* readback = graph.AllocateParameter<RadiosityTestReadback>();
                readback->Atlas = lighting.DirectRadiance;
                readback->Hits = hits;
                readback->Lookup = probeRadiance;
                graph.AddPass("GIHardwareTestReadback", RadiosityTestReadback::GetMetaData(), readback, EPassFlag::Graphic,
                    [&](RHICommandListBase& cmd)
                    {
                        mapped = GRHIApi->MapReadBuffer(cmd, hits->GetRHIBuffer(), {});
                        if (probeRadiance)
                        {
                            mappedProbes = GRHIApi->MapReadBuffer(cmd, probeRadiance->GetRHIBuffer(), {});
                        }
                    });
                graph.Execute();
                const auto fence = direct.GetAtlas()->GetTracker().GetLastAccessFence();
                GRHIApi->GetQueue(fence.QueueType)->WaitValue(fence.Value);
                Verify(mapped != nullptr, "hardware result readback exists");
                std::array<SurfaceCacheQueryResult, 3> results;
                std::copy_n(static_cast<const SurfaceCacheQueryResult*>(mapped), results.size(), results.begin());
                GRHIApi->Unmap(mapped);
                const auto& hit = results[0];
                std::cout << "GI_HARDWARE owner=" << hit.Status.X << " flags=" << hit.Status.Z << " primitive=" << hit.Status.W
                    << " z=" << hit.PositionDistance.Z << " normal=" << hit.NormalRoughness.Z << " revision=" << revision << std::endl;
                Verify(hit.Status.X == expectedOwner && (hit.Status.Z & 27u) == 27u &&
                    std::abs(hit.PositionDistance.Z - expectedZ) < 0.001f && hit.NormalRoughness.Z < -0.999f,
                    "nearest hit returns exact distance, normal, scene instance and a valid Surface Card");
                Verify((expectedOwner == 47 ? hit.BaseColorMetallic.X : hit.BaseColorMetallic.Y) > 0.8f,
                    "hardware hit maps to the material of the correct mesh instance");
                Verify(results[1].Status.Z == 0 && results[2].Status.Z == 0, "miss and skipped rays do not publish hits");
                if (bGather)
                {
                    Verify(mappedProbes != nullptr, "probe radiance readback exists");
                    auto* probes = static_cast<const ScreenProbeRadianceRecord*>(mappedProbes);
                    const auto litHardwareRays = std::count_if(probes, probes + TestRayCount, [](const auto& probe)
                    {
                        return (probe.Status.W & 11u) == 11u && probe.Radiance.X > 0.25f;
                    });
                    GRHIApi->Unmap(mappedProbes);
                    std::cout << "GI_HARDWARE_SCREEN_PROBES litRays=" << litHardwareRays << std::endl;
                    Verify(litHardwareRays > 0, "screen-probe rays receive red Surface Cache light from hardware geometry");
                }
                direct.Release();
                surface.Release();
            };

            verifyFrame({nearInstance, farInstance}, 47, 1.85f, 1, true);
            verifyFrame({nearInstance, farInstance}, 47, 1.85f, 1, false);
            nearInstance.LocalToWorld(0, 3) = 5.0f;
            verifyFrame({nearInstance, farInstance}, 834, 3.85f, 2, false);
            verifyFrame({farInstance}, 834, 3.85f, 3, false);
            std::cout << "GI_HARDWARE_TRACING_TEST_OK" << std::endl;
            GDistanceFieldMgr.ReleaseMeshDistanceField(*wall);
            wall->LODResources.front().ReleaseResources();
        }
    };

    REGISTER_RENDER_TEST("GIHardwareTracingTest", GIHardwareTracingTest);
}
