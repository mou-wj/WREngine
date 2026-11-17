#include "TestBase.h"
#include "../Framework/Renderer/Private/GI/SurfaceCacheMaterial.h"
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

        BEGIN_SHADER_PARAMETER_STRUCT(SurfaceCacheReadback)
            RDG_BUFFER_ACCESS(Results, ERHIResourceAccess::TransferSrc)
            RDG_TEXTURE_ACCESS(Debug, ERHIResourceAccess::TransferSrc)
            RDG_TEXTURE_ACCESS(Coverage, ERHIResourceAccess::TransferSrc)
            RDG_TEXTURE_ACCESS(QueryDebug, ERHIResourceAccess::TransferSrc)
        END_SHADER_PARAMETER_STRUCT(SurfaceCacheReadback)

        struct Expected
        {
            uint32_t Owner = UINT32_MAX;
            uint32_t Flags = 3;
            Float3 Color, Normal;
            float Metallic = 0, Roughness = 0;
        };

        void SaveImage(const std::string& name, const float* pixels, uint32_t width, uint32_t height)
        {
            std::ofstream image(name, std::ios::binary);
            image << "P6\n" << width << " " << height << "\n255\n";
            for (uint32_t i = 0; i < width * height; ++i)
            {
                for (uint32_t c = 0; c < 3; ++c)
                {
                    const auto byte = uint8_t(std::clamp(pixels[i * 4 + c], 0.0f, 1.0f) * 255);
                    image.write(reinterpret_cast<const char*>(&byte), 1);
                }
            }
        }
    }

    class SurfaceCacheMaterialTest final : public TestBase
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
            std::erase_if(globals, [](const auto& entry) { return !entry.first.starts_with("SurfaceCache"); });
            GShaderMap.Initialize();
            globals = registered;
            GDistanceFieldMgr.Initialize();
        }

        void Run() override
        {
            auto meshA = Box();
            auto meshB = Box(true);
            RHITextureDesc textureDesc;
            textureDesc.Width = textureDesc.Height = 64;
            textureDesc.Format = ERHIFormat::R8G8B8A8_UNorm;
            textureDesc.Usage = ERHITextureCreateFlag::ShaderResource | ERHITextureCreateFlag::TransferDest;
            auto texture = std::make_shared<RenderTexture>(textureDesc);
            texture->InitRHIResource();
            std::vector<uint8_t> texels(64 * 64 * 4);
            for (uint32_t y = 0; y < 64; ++y)
            {
                for (uint32_t x = 0; x < 64; ++x)
                {
                    const uint32_t i = (y * 64 + x) * 4;
                    texels[i] = uint8_t((x + 0.5f) / 64 * 255);
                    texels[i + 1] = uint8_t((y + 0.5f) / 64 * 255);
                    texels[i + 2] = 128;
                    texels[i + 3] = 255;
                }
            }
            texture->UploadData(texels.data(), 0, 0);
            auto asset = TextureAsset::Create("SurfaceCacheUV", texture);
            Material uvMaterial, greenMaterial, blueMaterial;
            for (auto* material : {&uvMaterial, &greenMaterial, &blueMaterial})
            {
                material->SetShadingModel(EShadingModel::Lit);
            }
            uvMaterial.SetParameter("BaseColor", asset);
            uvMaterial.SetParameter("BaseColor", std::array<float, 4>{1, 1, 1, 1});
            uvMaterial.SetParameter("Roughness", 0.6f);
            uvMaterial.SetParameter("Metallic", 0.25f);
            greenMaterial.SetParameter("BaseColor", std::array<float, 4>{0.15f, 0.8f, 0.25f, 1});
            greenMaterial.SetParameter("Roughness", 0.8f);
            greenMaterial.SetParameter("Metallic", 0.4f);
            blueMaterial.SetParameter("BaseColor", std::array<float, 4>{0.2f, 0.35f, 0.9f, 1});
            blueMaterial.SetParameter("Roughness", 0.3f);
            blueMaterial.SetParameter("Metallic", 0.7f);
            RHITextureDesc normalDesc = textureDesc;
            normalDesc.Width = normalDesc.Height = 2;
            auto normalTexture = std::make_shared<RenderTexture>(normalDesc);
            normalTexture->InitRHIResource();
            const uint8_t normalPixels[] = {179, 102, 244, 255, 179, 102, 244, 255, 179, 102, 244, 255, 179, 102, 244, 255};
            normalTexture->UploadData(normalPixels, 0, 0);
            auto normalAsset = TextureAsset::Create("SurfaceCacheNormal", normalTexture);
            blueMaterial.SetParameter("Normal", normalAsset);
            std::vector<SurfaceCacheMeshInstance> instances(3);
            const uint32_t ids[] = {11, 29, 83};
            for (uint32_t i = 0; i < 3; ++i)
            {
                instances[i].InstanceID = ids[i];
                instances[i].RenderData = i == 2 ? meshB : meshA;
                instances[i].LocalToWorld = Mat4::Identity();
                instances[i].LocalToWorld(0, 3) = (float(i) - 1) * 3.5f;
                instances[i].Materials = {uvMaterial.GetRenderProxyShared(), greenMaterial.GetRenderProxyShared()};
            }
            auto& mirrored = instances[1];
            const float c = std::cos(0.35f), s = std::sin(0.35f);
            mirrored.LocalToWorld(0, 0) = -0.7f * c;
            mirrored.LocalToWorld(1, 0) = -0.7f * s;
            mirrored.LocalToWorld(0, 1) = -1.2f * s;
            mirrored.LocalToWorld(1, 1) = 1.2f * c;
            mirrored.LocalToWorld(2, 2) = 0.8f;
            mirrored.Materials = {blueMaterial.GetRenderProxyShared(), blueMaterial.GetRenderProxyShared()};

            SurfaceCacheMaterial cache;
            cache.Initialize(instances, 64);
            Verify(cache.GetCards().size() == 18, "six Cards per instance including offscreen/back surfaces");
            std::vector<SurfaceCacheQuery> queries;
            std::vector<Expected> expected;
            for (uint32_t mode = 0; mode < 2; ++mode)
            {
                for (uint32_t instance = 0; instance < 3; ++instance)
                {
                    for (uint32_t face = 0; face < 6; ++face)
                    {
                        const auto n = Axes[face];
                        const auto up = std::abs(n.Y) > 0.5f ? Float3(0, 0, 1) : Float3(0, 1, 0);
                        const auto u = Normalize(Cross(up, n));
                        const auto v = Cross(n, u);
                        const auto& transform = instances[instance].LocalToWorld;
                        const auto position = TransformPoint(transform, n + u * 0.26f - v * 0.3f);
                        const auto normal = Normalize(TransformVector(Transpose(Inverse(transform)), n));
                        const auto start = mode ? position + normal * 1.5f : position;
                        const auto direction = mode ? -normal : normal;
                        SurfaceCacheQuery query;
                        query.OriginDistance = {start.X, start.Y, start.Z, mode ? 4.0f : 0};
                        query.Direction = {direction.X, direction.Y, direction.Z, 0};
                        query.Control = {ids[instance], mode, 0, 0};
                        queries.push_back(query);
                        Expected value;
                        value.Owner = ids[instance];
                        value.Normal = normal;
                        if (instance == 1)
                        {
                            auto tangent = TransformVector(transform, u);
                            tangent = Normalize(tangent - normal * Dot(tangent, normal));
                            const auto bitangent = Normalize(Cross(normal, tangent)) * -1.0f;
                            const auto normalTS = Normalize(Float3(179.0f / 255 * 2 - 1, 102.0f / 255 * 2 - 1, 244.0f / 255 * 2 - 1));
                            value.Normal = Normalize(tangent * normalTS.X + bitangent * normalTS.Y + normal * normalTS.Z);
                        }
                        value.Color = instance == 1 ? Float3(0.2f, 0.35f, 0.9f) :
                            face % 2 ? Float3(0.15f, 0.8f, 0.25f) : Float3(0.63f, 0.35f, 128.0f / 255);
                        value.Metallic = instance == 1 ? 0.7f : face % 2 ? 0.4f : 0.25f;
                        value.Roughness = instance == 1 ? 0.3f : face % 2 ? 0.8f : 0.6f;
                        expected.push_back(value);
                    }
                }
            }
            // No instance hint: identify the nearest mesh using the existing local SDF atlas.
            for (uint32_t instance = 0; instance < 3; ++instance)
            {
                SurfaceCacheQuery query;
                query.OriginDistance = {(float(instance) - 1) * 3.5f, 0, 4, 8};
                query.Direction = {0, 0, -1, 0};
                query.Control = {UINT32_MAX, 1, 0, 0};
                queries.push_back(query);
                auto value = expected[instance * 6 + 4];
                value.Color = instance == 1 ? Float3(0.2f, 0.35f, 0.9f) : Float3(0.5f, 0.5f, 128.0f / 255);
                expected.push_back(value);
            }
            SurfaceCacheQuery miss;
            miss.OriginDistance = {0, 10, 4, 8};
            miss.Direction = {0, 0, -1, 0};
            miss.Control = {UINT32_MAX, 1, 0, 0};
            queries.push_back(miss);
            Expected invalid;
            invalid.Flags = 0;
            expected.push_back(invalid);
            // An interior/occluded point must NOT reuse the material at the nearest projected exterior.
            SurfaceCacheQuery uncovered;
            uncovered.OriginDistance = {-3.5f, 0, 0, 0};
            uncovered.Direction = {0, 0, 1, 0};
            uncovered.Control = {11, 0, 0, 0};
            queries.push_back(uncovered);
            invalid.Owner = 11;
            invalid.Flags = 1;
            expected.push_back(invalid);

            for (uint32_t frame = 0; frame < 2; ++frame)
            {
                RenderGraphBuilder graph;
                auto parameters = frame == 0 ? cache.AddCapturePasses(graph) : cache.Register(graph);
                auto result = AddSurfaceCacheQueryPass(graph, parameters, queries);
                RHITextureDesc debugDesc;
                debugDesc.Width = parameters.AtlasSize.X;
                debugDesc.Height = parameters.AtlasSize.Y;
                debugDesc.Format = ERHIFormat::R32G32B32A32_Float;
                debugDesc.Usage = ERHITextureCreateFlag::UAV | ERHITextureCreateFlag::ShaderResource | ERHITextureCreateFlag::TransferSrc;
                RenderTexture debug(debugDesc);
                debug.InitRHIResource();
                auto target = graph.RegisterExternalTexture("SurfaceCacheTest.Debug", &debug);
                AddSurfaceCacheDebugPass(graph, parameters, target, frame == 0 ? 0 : 2);
                auto queryDesc = debugDesc;
                queryDesc.Width = 256;
                queryDesc.Height = 192;
                RenderTexture queryDebug(queryDesc);
                queryDebug.InitRHIResource();
                auto queryTarget = graph.RegisterExternalTexture("SurfaceCacheTest.QueryDebug", &queryDebug);
                AddSurfaceCacheQueryDebugPass(graph, result, uint32_t(queries.size()), queryTarget, frame);
                void* mappedResult = nullptr;
                void* mappedDebug = nullptr;
                void* mappedCoverage = nullptr;
                void* mappedQueryDebug = nullptr;
                auto* read = graph.AllocateParameter<SurfaceCacheReadback>();
                read->Results = result;
                read->Debug = target;
                read->Coverage = parameters.PositionCoverage;
                read->QueryDebug = queryTarget;
                const bool gl = GShaderPlatform == ERHIShaderPlatform::OpenGL;
                const auto map = [&](RHICommandListBase& cmd)
                {
                    mappedResult = GRHIApi->MapReadBuffer(cmd, result->GetRHIBuffer(), {});
                    mappedDebug = GRHIApi->MapReadTexture(cmd, debug.GetRHI(), {});
                    mappedCoverage = GRHIApi->MapReadTexture(cmd, cache.GetPositionAtlas()->GetRHI(), {});
                    mappedQueryDebug = GRHIApi->MapReadTexture(cmd, queryDebug.GetRHI(), {});
                };
                graph.AddPass("SurfaceCacheTestReadback", SurfaceCacheReadback::GetMetaData(), read, EPassFlag::Graphic,
                    [&](RHICommandListBase& cmd)
                {
                    if (!gl)
                    {
                        map(cmd);
                    }
                });
                graph.Execute();
                const auto fence = debug.GetTracker().GetLastAccessFence();
                Verify(fence.Value != 0, "readback has an actual external allocation fence");
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
                Verify(mappedResult && mappedDebug && mappedCoverage && mappedQueryDebug, "all GPU results mapped");
                const auto* results = static_cast<const SurfaceCacheQueryResult*>(mappedResult);
                uint32_t errors = 0;
                for (uint32_t i = 0; i < expected.size(); ++i)
                {
                    const auto& actual = results[i];
                    const auto& want = expected[i];
                    bool valid = actual.Status.X == want.Owner && actual.Status.Z == want.Flags;
                    if (want.Flags == 3)
                    {
                        const Float3 color(actual.BaseColorMetallic.X, actual.BaseColorMetallic.Y, actual.BaseColorMetallic.Z);
                        const Float3 normal(actual.NormalRoughness.X, actual.NormalRoughness.Y, actual.NormalRoughness.Z);
                        valid &= actual.Status.Y < cache.GetCards().size() && (color - want.Color).Length() < 0.075f &&
                            Dot(normal, want.Normal) > 0.995f && std::abs(actual.BaseColorMetallic.W - want.Metallic) < 0.005f &&
                            std::abs(actual.NormalRoughness.W - want.Roughness) < 0.005f;
                        valid &= i < 18 ? actual.Status.W == 0 : actual.Status.W > 0;
                    }
                    if (!valid)
                    {
                        ++errors;
                        std::cout << "SURFACE_QUERY_ERROR index=" << i << " owner=" << actual.Status.X << " card=" << actual.Status.Y
                                  << " flags=" << actual.Status.Z << " steps=" << actual.Status.W << " color="
                                  << actual.BaseColorMetallic.X << "," << actual.BaseColorMetallic.Y << "," << actual.BaseColorMetallic.Z
                                  << " normal=" << actual.NormalRoughness.X << "," << actual.NormalRoughness.Y << "," << actual.NormalRoughness.Z << std::endl;
                    }
                    const auto* queryPixels = static_cast<const float*>(mappedQueryDebug);
                    const auto center = ((i / 8 * 32 + 16) * 256 + i % 8 * 32 + 16) * 4;
                    Float3 expectedDebug(0, 0, 0);
                    if (want.Flags == 3)
                    {
                        expectedDebug = frame == 0 ? want.Color : want.Normal * 0.5f + Float3(0.5f, 0.5f, 0.5f);
                    }
                    else if (want.Flags == 1)
                    {
                        expectedDebug = {1, 0, 1};
                    }
                    const Float3 actualDebug(queryPixels[center], queryPixels[center + 1], queryPixels[center + 2]);
                    Verify((actualDebug - expectedDebug).Length() < 0.075f, "query debug image represents GPU results and validity");
                }
                const auto* coverage = static_cast<const float*>(mappedCoverage);
                uint32_t covered = 0;
                for (const auto& card : cache.GetCards())
                {
                    const uint32_t center = ((card.Tile.Y + card.Tile.W / 2) * parameters.AtlasSize.X + card.Tile.X + card.Tile.Z / 2) * 4;
                    Verify(coverage[center + 3] > 0.5f, "all six Cards have valid center coverage");
                    const uint32_t border = ((card.Tile.Y - 1) * parameters.AtlasSize.X + card.Tile.X - 1) * 4;
                    Verify(coverage[border + 3] == 0, "tile padding is invalid, no atlas bleed");
                    ++covered;
                }
                const auto suffix = std::to_string(uint32_t(GShaderPlatform)) + "-" + std::to_string(frame) + ".ppm";
                SaveImage("SurfaceCacheDebug-" + suffix, static_cast<const float*>(mappedDebug), debugDesc.Width, debugDesc.Height);
                SaveImage("SurfaceCacheQueries-" + suffix, static_cast<const float*>(mappedQueryDebug), queryDesc.Width, queryDesc.Height);
                GRHIApi->Unmap(mappedResult);
                GRHIApi->Unmap(mappedDebug);
                GRHIApi->Unmap(mappedCoverage);
                GRHIApi->Unmap(mappedQueryDebug);
                std::cout << "SURFACE_CACHE_FRAME=" << frame << " cards=" << covered << " queries=" << queries.size() << " errors=" << errors << std::endl;
                Verify(errors == 0, "SDF/direct hits, UV, material slots, transforms and offscreen cache match");
            }
            cache.Release();
            for (auto& mesh : {meshA, meshB})
            {
                GDistanceFieldMgr.ReleaseMeshDistanceField(*mesh);
                mesh->LODResources.front().ReleaseResources();
            }
            std::cout << "SURFACE_CACHE_MATERIAL_TEST_OK" << std::endl;
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
    REGISTER_RENDER_TEST("SurfaceCacheMaterialTest", SurfaceCacheMaterialTest);
}
