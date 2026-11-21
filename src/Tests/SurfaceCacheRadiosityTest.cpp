#include "TestBase.h"
#include "../Framework/Renderer/Private/GI/SurfaceCacheRadiosity.h"
#include "../Framework/Renderer/Private/GI/ScreenProbeGather.h"
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

        std::shared_ptr<StaticMeshRenderData> MakeWall(float halfWidth = 2.0f)
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
                const uint32_t firstVertex = vertices.PositionBuffer.GetNumVertices();
                for (const auto& corner : corners)
                {
                    const auto p = n + u * corner.X + v * corner.Y;
                    vertices.PositionBuffer.Vertices.insert(vertices.PositionBuffer.Vertices.end(), {p.X * halfWidth, p.Y * halfWidth, p.Z * 0.15f});
                    vertices.NormalBuffer.Vertices.insert(vertices.NormalBuffer.Vertices.end(), {n.X, n.Y, n.Z});
                    vertices.TangentBuffer.Vertices.insert(vertices.TangentBuffer.Vertices.end(), {u.X, u.Y, u.Z, 1});
                    vertices.UVBuffer.Vertices.insert(vertices.UVBuffer.Vertices.end(), {corner.X * 0.5f + 0.5f, corner.Y * 0.5f + 0.5f});
                }
                lod.IndexBuffer.Indices.insert(lod.IndexBuffer.Indices.end(),
                    {firstVertex, firstVertex + 1, firstVertex + 2, firstVertex, firstVertex + 2, firstVertex + 3});
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
        void InitializeGI(bool bWorldCache)
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
                    !(bWorldCache && (entry.first.starts_with("WorldRadiance") || entry.first.starts_with("ScreenProbe")));
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
                float normalZ = 1.0f)
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
                const auto screen = AddScreenProbeGatherPasses(graph, material, lighting, input);
                WorldRadianceCache::AddDebugPass(graph, world, screen.DebugVisualization);
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

            const auto nearReference = render(0, camera, false, false, false, false, -1.0f);
            const auto nearCached = render(0, camera, false, false, true, false, -1.0f);
            for (uint32_t i = 0; i < nearCached.Samples.size(); i += 8)
            {
                const auto& referenceRay = nearReference.Samples[i];
                const auto& cachedRay = nearCached.Samples[i];
                Verify(referenceRay.Status.Z > 0 && cachedRay.Status.Y == 0 &&
                    std::abs(cachedRay.Radiance.X - referenceRay.Radiance.X) < 1e-5f &&
                    cachedRay.Radiance.X > 0.5f, "the near wall occludes any cached far radiance");
            }

            cache.Invalidate();
            radiosity.Invalidate();
            light.bEnabled = false;
            const auto reset = render(0, camera, false, false, true, true);
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
}
