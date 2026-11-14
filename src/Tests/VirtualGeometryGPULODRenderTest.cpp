#include "TestBase.h"
#include "../Framework/Renderer/VirtualGeometry/VirtualGeometryMaterialResolvePass.h"
#include "../Framework/Renderer/VirtualGeometry/VirtualGeometryLODSelection.h"
#include "StaticMesh.h"
#include "MaterialAsset.h"
#include "TextureAsset.h"
#include "GlobalShader.h"
#include "Transform.hpp"
#include "RHIPipelineStateCache.h"
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace Test
{
    namespace
    {
        void VerifyRender(bool condition, const char* message)
        {
            if (!condition)
            {
                throw std::runtime_error(std::string("Virtual geometry LOD render test: ") + message);
            }
        }

        BEGIN_SHADER_PARAMETER_STRUCT(LODReadbackParameters)
        RDG_TEXTURE_ACCESS(Visibility, RHI::ERHIResourceAccess::TransferSrc)
        RDG_TEXTURE_ACCESS(A, RHI::ERHIResourceAccess::TransferSrc)
        RDG_TEXTURE_ACCESS(B, RHI::ERHIResourceAccess::TransferSrc)
        RDG_TEXTURE_ACCESS(C, RHI::ERHIResourceAccess::TransferSrc)
        END_SHADER_PARAMETER_STRUCT(LODReadbackParameters)

        float GradientSample(float value)
        {
            const float coordinate = value * 64 - 0.5f;
            const int first = int(std::floor(coordinate));
            const float fraction = coordinate - first;
            const int a = (first % 64 + 64) % 64, b = (a + 1) % 64;
            return uint8_t((a + 0.5f) / 64 * 255) * (1 - fraction) + uint8_t((b + 0.5f) / 64 * 255) * fraction;
        }
    }

    class VirtualGeometryGPULODRenderTest final : public TestBase
    {
    public:
        void Setup() override
        {
            RHI::GRHIApi = CreateTestRHIApi();
            RHI::GRHIApi->Init();
            RenderCore::GTransientResourceAllocator.InitRHI();
            RenderCore::InitGlobalRenderResource();
            auto& types = RenderCore::ShaderType::GetRegisterMap()[RenderCore::ShaderType::EShaderTypeFlag::Global];
            const auto registered = types;
            std::erase_if(types, [](const auto& entry)
            {
                return entry.first != "VirtualGeometryClearVS" && entry.first != "VirtualGeometryClearPS" &&
                       entry.first != "VirtualGeometryResolveClearCS" && entry.first != "VirtualGeometrySelectGroupsCS" &&
                       entry.first != "VirtualGeometrySelectClustersCS";
            });
            RenderCore::GShaderMap.Initialize();
            types = registered;
        }

        void Run() override
        {
            using namespace Engine;
            using namespace RenderCore;
            using namespace RHI;
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
                    const auto pixel = (y * 64 + x) * 4;
                    texels[pixel] = uint8_t((x + 0.5f) / 64 * 255);
                    texels[pixel + 1] = uint8_t((y + 0.5f) / 64 * 255);
                    texels[pixel + 2] = 128;
                    texels[pixel + 3] = 255;
                }
            }
            texture->UploadData(texels.data(), 0, 0);
            auto input = TextureAsset::Create("LOD.UV", texture);
            std::array<std::shared_ptr<MaterialAsset>, 2> materials;
            for (uint32_t index = 0; index < 2; ++index)
            {
                auto material = std::make_unique<Material>();
                material->SetShadingModel(EShadingModel::Lit);
                material->SetParameter("BaseColor", input);
                const float tint = index ? 0.5f : 1;
                material->SetParameter("BaseColor", std::array<float, 4>{tint, tint, tint, 1});
                material->SetParameter("Metallic", index ? 0.8f : 0.2f);
                material->SetParameter("Roughness", 0.5f);
                materials[index] = MaterialAsset::Create("", "LOD material", std::move(material));
            }
            auto data = std::make_shared<StaticMeshRenderData>();
            auto& lod = data->LODResources.emplace_back();
            auto& vertex = lod.VertexBuffers;
            vertex.PositionBuffer.bValid = vertex.NormalBuffer.bValid = vertex.UVBuffer.bValid = true;
            vertex.PositionBuffer.NumComponents = vertex.NormalBuffer.NumComponents = 3;
            vertex.UVBuffer.NumComponents = 2;
            for (uint32_t y = 0; y <= 16; ++y)
            {
                for (uint32_t x = 0; x <= 16; ++x)
                {
                    vertex.PositionBuffer.Vertices.insert(vertex.PositionBuffer.Vertices.end(), {float(x) / 8 - 1, float(y) / 8 - 1, 0});
                    vertex.NormalBuffer.Vertices.insert(vertex.NormalBuffer.Vertices.end(), {0, 0, 1});
                    vertex.UVBuffer.Vertices.insert(vertex.UVBuffer.Vertices.end(), {float(x) / 16, float(y) / 16});
                }
            }
            for (uint32_t material = 0; material < 2; ++material)
            {
                SectionInfo section;
                section.FirstIndex = uint32_t(lod.IndexBuffer.Indices.size());
                section.MaterialIndex = material;
                for (uint32_t y = 0; y < 16; ++y)
                {
                    for (uint32_t x = material * 8; x < (material + 1) * 8; ++x)
                    {
                        const auto a = y * 17 + x;
                        lod.IndexBuffer.Indices.insert(lod.IndexBuffer.Indices.end(), {a, a + 1, a + 17, a + 1, a + 18, a + 17});
                    }
                }
                section.NumIndices = uint32_t(lod.IndexBuffer.Indices.size()) - section.FirstIndex;
                lod.Sections.push_back(section);
            }
            StaticMesh mesh(data);
            mesh.AddMaterial(materials[0]);
            mesh.AddMaterial(materials[1]);
            auto& geometry = data->VirtualGeometryData;
            geometry.Build(&mesh);
            geometry.InitializeResources();
            bool sawCoarse = false;
            for (const float threshold : {0.0f, 1.0f, 1000.0f})
            {
                SceneView view;
                view.Viewport = {0, 0, 128, 128};
                view.CameraWorldPos = {0, 0, 4};
                view.ViewMatrix = Core::LookAtRH(view.CameraWorldPos, {0, 0, 0}, {0, 1, 0});
                view.ProjectionMatrix = Core::PerspectiveRhZo(Core::DegToRad(60.0f), 1, 0.1f, 100);
                view.ViewProjectionMatrix = view.ProjectionMatrix * view.ViewMatrix;
                std::vector<Renderer::VirtualGeometryInstance> instances;
                for (uint32_t index = 0; index < 2; ++index)
                {
                    Renderer::VirtualGeometryInstance instance;
                    instance.RenderData = data;
                    instance.bGPUSelection = true;
                    instance.InstanceID = 41 + index;
                    instance.LocalToWorld(0, 0) = index ? -0.45f : 0.45f;
                    instance.LocalToWorld(1, 1) = 0.65f;
                    instance.LocalToWorld(0, 3) = index ? 0.6f : -0.6f;
                    instance.LocalToWorld(2, 3) = index ? 0.3f : 0;
                    instance.TargetPixelError = index ? threshold : 0; // Fine and coarse instances coexist.
                    for (const auto& material : materials)
                    {
                        instance.Materials.push_back(material->GetMaterial()->GetRenderProxyShared());
                    }
                    instances.push_back(instance);
                }
                RenderGraphBuilder graph;
                auto visibility = Renderer::AddVirtualGeometryRasterPass(graph, view, instances, 128, 128);
                for (uint32_t index = 0; index < 2; ++index)
                {
                    const auto selected = Renderer::SelectVirtualGeometryClusters(geometry, instances[index].LocalToWorld,
                                                                                   view, instances[index].TargetPixelError);
                    std::vector<uint32_t> actual;
                    for (const auto& draw : visibility->Draws)
                    {
                        if (draw.InstanceID == instances[index].InstanceID)
                        {
                            actual.push_back(draw.ClusterID);
                        }
                    }
                    VerifyRender(actual.size() == geometry.Clusters.size(), "GPU Draw Metadata must retain stable candidate IDs");
                    for (uint32_t c = 0; c < actual.size(); ++c)
                    {
                        VerifyRender(actual[c] == c, "GPU Draw Metadata Cluster ID mapping changed");
                    }
                }
                std::array<std::unique_ptr<RenderTexture>, 3> targets;
                std::array<RenderGraphTextureRef, 3> rdg{};
                for (uint32_t index = 0; index < 3; ++index)
                {
                    RHITextureDesc desc;
                    desc.Width = desc.Height = 128;
                    desc.Format = index == 1 ? ERHIFormat::R16G16B16A16_Float : ERHIFormat::R8G8B8A8_UNorm;
                    desc.Usage = ERHITextureCreateFlag::UAV | ERHITextureCreateFlag::ShaderResource | ERHITextureCreateFlag::TransferSrc;
                    targets[index] = std::make_unique<RenderTexture>(desc);
                    targets[index]->InitRHIResource();
                    rdg[index] = graph.RegisterExternalTexture("LOD.Target" + std::to_string(index), targets[index].get());
                }
                Renderer::AddVirtualGeometryMaterialResolvePass(graph, view, visibility, rdg[0], rdg[1], rdg[2]);
                auto* params = graph.AllocateParameter<LODReadbackParameters>();
                params->Visibility = visibility->VirtualGeometryVisibilityTexture;
                params->A = rdg[0];
                params->B = rdg[1];
                params->C = rdg[2];
                const bool immediateRead = GShaderPlatform == ERHIShaderPlatform::OpenGL;
                std::array<void*, 4> mapped{};
                const auto read = [&](RHICommandListBase& commands)
                {
                    mapped[0] = GRHIApi->MapReadTexture(commands, visibility->VirtualGeometryVisibilityTexture->GetRHITexture(), {});
                    for (uint32_t index = 0; index < 3; ++index)
                    {
                        mapped[index + 1] = GRHIApi->MapReadTexture(commands, targets[index]->GetRHI(), {});
                    }
                };
                graph.AddPass("LOD.Readback", LODReadbackParameters::GetMetaData(), params, EPassFlag::Graphic,
                              [&](RHICommandListBase& commands)
                {
                    if (!immediateRead)
                    {
                        read(commands);
                    }
                });
                graph.Execute();
                const auto fence = targets[0]->GetTracker().GetLastAccessFence();
                GRHIApi->GetQueue(fence.QueueType)->WaitValue(fence.Value);
                if (immediateRead)
                {
                    auto* queue = GRHIApi->GetQueue(EQueueType::Graphics);
                    auto* context = queue->AcquireCommandContext();
                    RHICommandListBase commands(context);
                    commands.SetImmediate(true);
                    commands.Begin();
                    read(commands);
                    commands.End();
                    queue->WaitValue(queue->ExecuteContext(context));
                }
                std::array<std::vector<uint32_t>, 2> selectedReference;
                for (uint32_t i = 0; i < 2; ++i)
                {
                    selectedReference[i] = Renderer::SelectVirtualGeometryClusters(geometry, instances[i].LocalToWorld,
                                                                                  view, instances[i].TargetPixelError);
                }
                const auto* ids = static_cast<const uint32_t*>(mapped[0]);
                const auto* color = static_cast<const uint8_t*>(mapped[1]);
                const auto* normal = static_cast<const uint16_t*>(mapped[2]);
                const auto* pbr = static_cast<const uint8_t*>(mapped[3]);
                VerifyRender(ids && color && normal && pbr, "readback mapping failed");
                uint32_t foreground = 0, errors = 0, coarsePixels = 0;
                std::array<uint32_t, 2> instancePixels{};
                for (uint32_t y = 0; y < 128; ++y)
                {
                    for (uint32_t x = 0; x < 128; ++x)
                    {
                        const auto pixel = y * 128 + x;
                        if (ids[pixel * 2] == UINT32_MAX)
                        {
                            errors += ids[pixel * 2 + 1] != UINT32_MAX || color[pixel * 4] != 0;
                            continue;
                        }
                        ++foreground;
                        if (ids[pixel * 2] >= visibility->Draws.size())
                        {
                            ++errors;
                            continue;
                        }
                        const auto& draw = visibility->Draws[ids[pixel * 2]];
                        const auto& cluster = geometry.Clusters[draw.ClusterID];
                        const auto& instance = instances[draw.InstanceIndex];
                        ++instancePixels[draw.InstanceIndex];
                        const bool coarse = cluster.GeneratingGroupIndex != INVALID_VIRTUAL_GEOMETRY_INDEX;
                        coarsePixels += coarse;
                        sawCoarse |= coarse;
                        errors += ids[pixel * 2 + 1] >= cluster.TriangleCount;
                        const auto& selected = selectedReference[draw.InstanceIndex];
                        errors += !std::binary_search(selected.begin(), selected.end(), draw.ClusterID);
                        const float w = 4 - instance.LocalToWorld(2, 3);
                        const float ndcX = (x + 0.5f) / 64 - 1;
                        const float ySign = GShaderPlatform == ERHIShaderPlatform::D3D12 ? -1.0f : 1.0f;
                        const float ndcY = ((y + 0.5f) / 64 - 1) * ySign;
                        const float localX = (ndcX * w / view.ProjectionMatrix(0, 0) - instance.LocalToWorld(0, 3)) /
                                             instance.LocalToWorld(0, 0);
                        const float localY = ndcY * w / view.ProjectionMatrix(1, 1) / instance.LocalToWorld(1, 1);
                        const float tint = cluster.MaterialIndex ? 0.5f : 1;
                        const std::array<int, 4> expected = {int(GradientSample((localX + 1) * 0.5f) * tint),
                            int(GradientSample((localY + 1) * 0.5f) * tint), int(128 * tint), cluster.MaterialIndex ? 204 : 51};
                        for (uint32_t channel = 0; channel < 4; ++channel)
                        {
                            errors += std::abs(int(color[pixel * 4 + channel]) - expected[channel]) > 3;
                        }
                        errors += normal[pixel * 4] != 0x3800 || normal[pixel * 4 + 1] != 0x3800 ||
                                  normal[pixel * 4 + 2] != 0x3c00 || normal[pixel * 4 + 3] != 0x3800;
                        errors += std::abs(int(pbr[pixel * 4]) - 128) > 1 || pbr[pixel * 4 + 1] != 255 || pbr[pixel * 4 + 2] != 1;
                        if (std::abs(localX) > 0.001f)
                        {
                            errors += cluster.MaterialIndex != (localX < 0 ? 0u : 1u);
                        }
                    }
                }
                for (auto* pointer : mapped)
                {
                    GRHIApi->Unmap(pointer);
                }
                std::cout << "LOD_RENDER target=" << threshold << " draws=" << visibility->Draws.size()
                          << " pixels=" << foreground << " coarse_pixels=" << coarsePixels << " errors=" << errors << std::endl;
                VerifyRender(errors == 0, "coarse triangle IDs/UVs/normals/material parameters differ from CPU reference");
                VerifyRender(instancePixels[0] > 100 && instancePixels[1] > 100, "both transformed instances must be visible");
            }
            VerifyRender(sawCoarse, "GPU test never rendered generated Clusters");
            geometry.ReleaseResources();
            std::cout << "VIRTUAL_GEOMETRY_GPU_LOD_RENDER_TEST_OK" << std::endl;
        }

        void Teardown() override
        {
            Engine::GMeshMaterialShaderMap.Clear();
            Engine::GMaterialShaderMap.Clear();
            RenderCore::GShaderMap.Clear();
            RHI::RHIPipelineStateCache::ClearAll();
            RenderCore::GTransientResourceAllocator.ReleaseRHI();
            RenderCore::ReleaseGlobalRenderResource();
            RHI::GRHIApi->Shutdown();
            delete RHI::GRHIApi;
            RHI::GRHIApi = nullptr;
        }
    };

    REGISTER_RENDER_TEST("VirtualGeometryGPULODRenderTest", VirtualGeometryGPULODRenderTest);
}
