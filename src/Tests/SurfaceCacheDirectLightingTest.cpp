#include "TestBase.h"
#include "../Framework/Renderer/Private/GI/SurfaceCacheDirectLighting.h"
#include "DistanceFieldMgr.h"
#include "GlobalShader.h"
#include "MaterialCore.h"
#include "TextureAsset.h"
#include "Transform.hpp"
#include "RHIPipelineStateCache.h"
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace Core;
using namespace RHI;
using namespace RenderCore;
using namespace Engine;
using namespace Renderer;

namespace Test
{
    namespace
    {
        void Verify(bool condition, const char* message)
        {
            if (!condition)
            {
                throw std::runtime_error(std::string("SurfaceCache test: ") + message);
            }
        }

        const Float3 Axes[] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};

        std::shared_ptr<StaticMeshRenderData> Box(bool zeroHandedness = false)
        {
            auto data = std::make_shared<StaticMeshRenderData>();
            data->Bounds = BoxSphereBounds(AABB(Float3(-1, -1, -1), Float3(1, 1, 1)));
            data->LODResources.emplace_back();
            auto& lod = data->LODResources.front();
            auto& vb = lod.VertexBuffers;
            vb.PositionBuffer.NumComponents = vb.NormalBuffer.NumComponents = 3;
            vb.UVBuffer.NumComponents = 2;
            vb.TangentBuffer.NumComponents = 4;
            vb.PositionBuffer.bValid = vb.NormalBuffer.bValid = vb.UVBuffer.bValid = vb.TangentBuffer.bValid = true;
            for (uint32_t face = 0; face < 6; ++face)
            {
                const auto n = Axes[face];
                const auto up = std::abs(n.Y) > 0.5f ? Float3(0, 0, 1) : Float3(0, 1, 0);
                const auto u = Normalize(Cross(up, n));
                const auto v = Cross(n, u);
                const Float2 corners[] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
                for (const auto& corner : corners)
                {
                    const auto p = n + u * corner.X + v * corner.Y;
                    vb.PositionBuffer.Vertices.insert(vb.PositionBuffer.Vertices.end(), {p.X, p.Y, p.Z});
                    vb.NormalBuffer.Vertices.insert(vb.NormalBuffer.Vertices.end(), {n.X, n.Y, n.Z});
                    vb.TangentBuffer.Vertices.insert(vb.TangentBuffer.Vertices.end(), {u.X, u.Y, u.Z, zeroHandedness ? 0.0f : 1.0f});
                    vb.UVBuffer.Vertices.insert(vb.UVBuffer.Vertices.end(), {corner.X * 0.5f + 0.5f, corner.Y * 0.5f + 0.5f});
                }
                const uint32_t base = face * 4;
                SectionInfo section;
                section.FirstIndex = uint32_t(lod.IndexBuffer.Indices.size());
                section.NumIndices = 6;
                section.MaterialIndex = face % 2;
                lod.Sections.push_back(section);
                lod.IndexBuffer.Indices.insert(lod.IndexBuffer.Indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
            }
            lod.InitializeResources();
            GDistanceFieldMgr.BuildMeshDistanceField(*data);
            Verify(lod.DistanceFieldData.Allocation.IsValid(), "real mesh distance field was built/uploaded");
            return data;
        }


        BEGIN_SHADER_PARAMETER_STRUCT(LightingReadback)
            RDG_BUFFER_ACCESS(Results, ERHIResourceAccess::TransferSrc)
            RDG_TEXTURE_ACCESS(Atlas, ERHIResourceAccess::TransferSrc)
            RDG_TEXTURE_ACCESS(Debug, ERHIResourceAccess::TransferSrc)
            RDG_TEXTURE_ACCESS(QueryDebug, ERHIResourceAccess::TransferSrc)
        END_SHADER_PARAMETER_STRUCT(LightingReadback)

        void SaveLighting(const std::string& path, const float* pixels, uint32_t width, uint32_t height)
        {
            std::ofstream image(path, std::ios::binary);
            image << "P6\n" << width << " " << height << "\n255\n";
            for (uint32_t i = 0; i < width * height * 4; ++i)
            {
                if (i % 4 != 3)
                {
                    const auto byte = uint8_t(std::clamp(pixels[i], 0.0f, 1.0f) * 255);
                    image.write(reinterpret_cast<const char*>(&byte), 1);
                }
            }
        }
    }

    class SurfaceCacheDirectLightingTest final : public TestBase
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
            auto mesh = Box();
            Material material;
            material.SetShadingModel(EShadingModel::Lit);
            material.SetParameter("BaseColor", std::array<float, 4>{0.8f, 0.4f, 0.2f, 1});
            material.SetParameter("Metallic", 0.25f);
            material.SetParameter("Roughness", 0.2f);
            std::vector<SurfaceCacheMeshInstance> instances(2);
            for (uint32_t i = 0; i < 2; ++i)
            {
                instances[i].InstanceID = i + 10;
                instances[i].RenderData = mesh;
                instances[i].Materials = {material.GetRenderProxyShared(), material.GetRenderProxyShared()};
            }
            for (uint32_t i = 0; i < 3; ++i)
            {
                instances[1].LocalToWorld(i, i) = 0.25f;
            }
            instances[1].LocalToWorld(2, 3) = 4;
            SurfaceCacheMaterial cache;
            cache.Initialize(instances, 64);
            SurfaceCacheDirectLighting lighting;
            lighting.Initialize(cache, 128, 16);
            auto* originalAtlas = lighting.GetAtlas()->GetRHI();
            Float3 albedo(0.8f, 0.4f, 0.2f);
            float metallic = 0.25f;
            uint32_t errors = 0;
            for (uint32_t frame = 0; frame < 17; ++frame)
            {
                // GPU users from the preceding iteration have been explicitly retired below.
                if (frame == 9 || frame == 10 || frame == 11)
                {
                    if (frame == 9)
                    {
                        albedo = {0.2f, 0.7f, 0.4f};
                        metallic = 0.5f;
                        material.SetParameter("BaseColor", std::array<float, 4>{albedo.X, albedo.Y, albedo.Z, 1});
                        material.SetParameter("Metallic", metallic);
                    }
                    if (frame == 10)
                    {
                        material.SetParameter("Roughness", 0.9f); // Diffuse cache must be view/roughness independent.
                    }
                    if (frame == 11)
                    {
                        instances[0].LocalToWorld(0, 3) = 1.5f;
                    }
                    for (auto& instance : instances)
                    {
                        instance.Materials = {material.GetRenderProxyShared(), material.GetRenderProxyShared()};
                    }
                    cache.Release();
                    cache.Initialize(instances, 64);
                    Verify(!lighting.IsValidFor(cache), "recapture invalidates old lighting revision");
                }
                SurfaceCacheLight light;
                light.Type = frame < 6 ? ESurfaceCacheLightType(frame / 2) : ESurfaceCacheLightType::Directional;
                light.Direction = {0, 0, -1};
                light.Position = {0, 0, 7};
                light.Radius = 20;
                light.bCastShadow = frame < 6 && frame % 2 == 1;
                light.bEnabled = frame != 6;
                if (frame == 7 || frame == 8)
                {
                    light.Intensity = 2;
                    light.Color = {0.3f, 0.6f, 0.9f};
                }
                if (frame == 12)
                {
                    lighting.Invalidate();
                    Verify(!lighting.IsValidFor(cache), "explicit light invalidation");
                    light.Direction = {0, 0, 1}; // Opposite side, unseen by a conventional +Z camera.
                }
                if (frame == 13 || frame == 14)
                {
                    light.Type = ESurfaceCacheLightType::Spot;
                    light.Direction = Normalize(frame == 13 ? Float3(2, 0, -1) : Float3(1, 0, -1));
                }
                if (frame == 15)
                {
                    light.Type = ESurfaceCacheLightType::Point;
                    light.Position = {0, 0, -7};
                    light.bCastShadow = true;
                }
                std::vector<SurfaceCacheLight> lights = {light};
                if (frame == 16)
                {
                    auto second = light;
                    second.Intensity = 0.5f;
                    lights.push_back(second);
                }
                RenderGraphBuilder graph;
                auto parameters = frame == 0 || (frame >= 9 && frame <= 11) ? cache.AddCapturePasses(graph) : cache.Register(graph);
                const auto lit = frame == 8 ? lighting.Register(graph, cache) : lighting.AddUpdatePasses(graph, cache, lights);
                Verify(lighting.IsValidFor(cache), "new lighting revision published");
                Verify(originalAtlas == lighting.GetAtlas()->GetRHI(), "persistent independent atlas reused across light and material updates");
                const float shift = instances[0].LocalToWorld(0, 3);
                std::vector<SurfaceCacheQuery> queries;
                const Float3 points[] = {{shift, 0, 1}, {shift + 0.8f, 0.6f, 1}, {shift, 0, -1}};
                for (uint32_t mode = 0; mode < 2; ++mode)
                {
                    for (uint32_t i = 0; i < 3; ++i)
                    {
                        const Float3 n = i == 2 ? Float3(0, 0, -1) : Float3(0, 0, 1);
                        SurfaceCacheQuery query;
                        const auto origin = points[i] + n * float(mode);
                        query.OriginDistance = {origin.X, origin.Y, origin.Z, mode == 1 ? 2.0f : 0};
                        query.Direction = mode == 1 ? Float4(-n.X, -n.Y, -n.Z, 0) : Float4(n.X, n.Y, n.Z, 0);
                        query.Control = {10, mode, 0, 0};
                        queries.push_back(query);
                    }
                }
                SurfaceCacheQuery invalid;
                invalid.OriginDistance = {shift, 0, 0, 0};
                invalid.Direction = {0, 0, 1, 0};
                invalid.Control = {10, 0, 0, 0};
                queries.push_back(invalid); // Real hit without Card coverage must not sample a nearby lit face.
                invalid.OriginDistance = {30, 30, 30, 2};
                invalid.Control = {UINT32_MAX, 1, 0, 0};
                queries.push_back(invalid);
                auto hit = AddSurfaceCacheQueryPass(graph, parameters, queries);
                auto result = AddSurfaceCacheLightingLookupPass(graph, parameters, lit, hit, uint32_t(queries.size()));
                RHITextureDesc debugDesc;
                debugDesc.Width = parameters.AtlasSize.X;
                debugDesc.Height = parameters.AtlasSize.Y;
                debugDesc.Format = ERHIFormat::R32G32B32A32_Float;
                debugDesc.Usage = ERHITextureCreateFlag::UAV | ERHITextureCreateFlag::ShaderResource | ERHITextureCreateFlag::TransferSrc;
                RenderTexture debug(debugDesc);
                debug.InitRHIResource();
                auto target = graph.RegisterExternalTexture("LightingTest.Debug", &debug);
                AddSurfaceCacheLightingDebugPass(graph, lit, target);
                auto queryDesc = debugDesc;
                queryDesc.Width = 256;
                queryDesc.Height = 32;
                RenderTexture queryDebug(queryDesc);
                queryDebug.InitRHIResource();
                auto queryTarget = graph.RegisterExternalTexture("LightingTest.QueryDebug", &queryDebug);
                AddSurfaceCacheLightingDebugPass(graph, lit, queryTarget, result, uint32_t(queries.size()));
                void* mappedResult = nullptr;
                void* mappedAtlas = nullptr;
                void* mappedDebug = nullptr;
                void* mappedQuery = nullptr;
                const bool gl = GShaderPlatform == ERHIShaderPlatform::OpenGL;
                const auto map = [&](RHICommandListBase& cmd)
                {
                    mappedResult = GRHIApi->MapReadBuffer(cmd, result->GetRHIBuffer(), {});
                    mappedAtlas = GRHIApi->MapReadTexture(cmd, lighting.GetAtlas()->GetRHI(), {});
                    mappedDebug = GRHIApi->MapReadTexture(cmd, debug.GetRHI(), {});
                    mappedQuery = GRHIApi->MapReadTexture(cmd, queryDebug.GetRHI(), {});
                };
                auto* read = graph.AllocateParameter<LightingReadback>();
                read->Results = result;
                read->Atlas = lit.DirectRadiance;
                read->Debug = target;
                read->QueryDebug = queryTarget;
                graph.AddPass("LightingTestReadback", LightingReadback::GetMetaData(), read, EPassFlag::Graphic,
                    [&](RHICommandListBase& cmd)
                {
                    if (!gl)
                    {
                        map(cmd);
                    }
                });
                graph.Execute();
                auto fence = debug.GetTracker().GetLastAccessFence();
                Verify(fence.Value != 0, "external readback fence");
                GRHIApi->GetQueue(fence.QueueType)->WaitValue(fence.Value);
                if (gl)
                {
                    auto* queue = GRHIApi->GetQueue(EQueueType::Graphics);
                    auto* context = queue->AcquireCommandContext();
                    RHICommandListBase cmd(context);
                    cmd.SetImmediate(true);
                    cmd.Begin();
                    map(cmd);
                    cmd.End();
                    queue->WaitValue(queue->ExecuteContext(context));
                }
                Verify(mappedResult && mappedAtlas && mappedDebug && mappedQuery, "GPU lighting readback");
                const auto* results = static_cast<const SurfaceCacheLightingQueryResult*>(mappedResult);
                for (uint32_t i = 0; i < queries.size(); ++i)
                {
                    const auto& actual = results[i];
                    bool valid = i < 6 ? (actual.Status.X == 10 && actual.Status.Z == 3 && actual.Radiance.W == 1) : actual.Radiance.W == 0;
                    Float3 expected(0, 0, 0);
                    if (i < 6 && light.bEnabled)
                    {
                        const uint32_t point = i % 3;
                        const auto p = points[point];
                        const auto n = point == 2 ? Float3(0, 0, -1) : Float3(0, 0, 1);
                        auto l = -light.Direction;
                        float attenuation = 1;
                        if (light.Type != ESurfaceCacheLightType::Directional)
                        {
                            const auto delta = light.Position - p;
                            const float distance = delta.Length();
                            l = delta / distance;
                            attenuation = std::pow(std::max(0.0f, 1 - distance / light.Radius), 2);
                            if (light.Type == ESurfaceCacheLightType::Spot)
                            {
                                const float inner = std::cos(light.InnerConeAngle), outer = std::cos(light.OuterConeAngle);
                                const float t = std::clamp((Dot(-light.Direction, l) - outer) / (inner - outer), 0.0f, 1.0f);
                                if (frame == 14 && point < 2)
                                {
                                    Verify(t > 0 && t < 1, "spot test point lies strictly inside the smooth transition band");
                                }
                                attenuation *= t * t * (3 - 2 * t);
                            }
                        }
                        const float cosine = std::max(0.0f, Dot(n, l));
                        const bool blocked = frame < 6 && light.bCastShadow && point == 0;
                        if (!blocked)
                        {
                            expected = albedo * ((1 - metallic) / 3.14159265359f * attenuation * cosine * light.Intensity);
                            expected.X *= light.Color.X;
                            expected.Y *= light.Color.Y;
                            expected.Z *= light.Color.Z;
                            if (frame == 16)
                            {
                                expected *= 1.5f;
                            }
                        }
                    }
                    const Float3 color(actual.Radiance.X, actual.Radiance.Y, actual.Radiance.Z);
                    valid &= (color - expected).Length() < 0.012f;
                    const auto* queryPixels = static_cast<const float*>(mappedQuery);
                    const uint32_t center = (16 * 256 + i * 32 + 16) * 4;
                    Float3 expectedDebug(expected.X / (1 + expected.X), expected.Y / (1 + expected.Y), expected.Z / (1 + expected.Z));
                    if (i == 6)
                    {
                        expectedDebug = {1, 0, 1};
                    }
                    valid &= (Float3(queryPixels[center], queryPixels[center + 1], queryPixels[center + 2]) - expectedDebug).Length() < 0.012f;
                    if (!valid)
                    {
                        ++errors;
                        std::cout << "LIGHTING_ERROR frame=" << frame << " query=" << i << " flags=" << actual.Status.Z
                            << " radiance=" << color.X << "," << color.Y << "," << color.Z
                            << " expected=" << expected.X << "," << expected.Y << "," << expected.Z << std::endl;
                    }
                }
                const auto* atlas = static_cast<const float*>(mappedAtlas);
                for (const auto& card : cache.GetCards())
                {
                    const uint32_t center = ((card.Tile.Y + card.Tile.W / 2) * parameters.AtlasSize.X + card.Tile.X + card.Tile.Z / 2) * 4;
                    const uint32_t padding = ((card.Tile.Y - 1) * parameters.AtlasSize.X + card.Tile.X - 1) * 4;
                    Verify(atlas[center + 3] == 1 && atlas[padding + 3] == 0, "all cards covered, padding invalid even when unlit");
                }
                const auto suffix = std::to_string(uint32_t(GShaderPlatform)) + "-" + std::to_string(frame) + ".ppm";
                SaveLighting("SurfaceCacheLighting-" + suffix, static_cast<const float*>(mappedDebug), debugDesc.Width, debugDesc.Height);
                SaveLighting("SurfaceCacheLightingQueries-" + suffix, static_cast<const float*>(mappedQuery), 256, 32);
                GRHIApi->Unmap(mappedResult);
                GRHIApi->Unmap(mappedAtlas);
                GRHIApi->Unmap(mappedDebug);
                GRHIApi->Unmap(mappedQuery);
                std::cout << "SURFACE_LIGHTING_FRAME=" << frame << " type=" << uint32_t(light.Type) << " shadow=" << light.bCastShadow
                    << " queries=" << queries.size() << " errors=" << errors << std::endl;
            }
            lighting.Release();
            cache.Release();
            GDistanceFieldMgr.ReleaseMeshDistanceField(*mesh);
            mesh->LODResources.front().ReleaseResources();
            Verify(errors == 0, "analytic diffuse radiance, shadow, SDF lookup, recapture and persistence");
            std::cout << "SURFACE_CACHE_DIRECT_LIGHTING_TEST_OK" << std::endl;
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
    REGISTER_RENDER_TEST("SurfaceCacheDirectLightingTest", SurfaceCacheDirectLightingTest);
}
