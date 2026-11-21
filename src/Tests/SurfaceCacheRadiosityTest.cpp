#include "TestBase.h"
#include "../Framework/Renderer/Private/GI/SurfaceCacheRadiosity.h"
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

        std::shared_ptr<StaticMeshRenderData> MakeWall()
        {
            auto mesh = std::make_shared<StaticMeshRenderData>();
            mesh->Bounds = BoxSphereBounds(AABB(Float3(-2, -2, -0.15f), Float3(2, 2, 0.15f)));
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
                    vertices.PositionBuffer.Vertices.insert(vertices.PositionBuffer.Vertices.end(), {p.X * 2, p.Y * 2, p.Z * 0.15f});
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
    }

    class SurfaceCacheRadiosityTest final : public TestBase
    {
    public:
        void Setup() override
        {
            GRHIApi = CreateTestRHIApi();
            GRHIApi->Init();
            GTransientResourceAllocator.InitRHI();
            InitGlobalRenderResource();
            auto& globals = ShaderType::GetRegisterMap()[ShaderType::EShaderTypeFlag::Global];
            const auto registered = globals;
            std::erase_if(globals, [](const auto& entry)
            {
                return !entry.first.starts_with("SurfaceCache") && entry.first != "PositionOnlyVS";
            });
            GShaderMap.Initialize();
            globals = registered;
            GDistanceFieldMgr.Initialize();
        }

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
    REGISTER_RENDER_TEST("SurfaceCacheRadiosityTest", SurfaceCacheRadiosityTest);
}
