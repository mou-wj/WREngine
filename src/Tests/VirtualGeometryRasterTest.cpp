#include "TestBase.h"
#include "../Framework/Renderer/VirtualGeometry/VirtualGeometryRasterPass.h"
#include "ShaderCompiler.h"
#include "GlobalShader.h"
#include "MaterialCore.h"
#include "RHIPipelineStateCache.h"
#include "RHIApi.h"
#include "StaticMesh.h"
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <set>
#include <stdexcept>

namespace Test
{
    namespace
    {
        void Verify(bool condition, const char* message)
        {
            if (!condition)
            {
                throw std::runtime_error(std::string("Virtual geometry raster test: ") + message);
            }
        }

        std::shared_ptr<Engine::StaticMesh> Grid()
        {
            auto data = std::make_shared<Engine::StaticMeshRenderData>();
            data->LODResources.emplace_back();
            auto& lod = data->LODResources.front();
            auto& positions = lod.VertexBuffers.PositionBuffer;
            positions.bValid = true;
            positions.NumComponents = 3;
            for (int z = 0; z <= 12; ++z)
            {
                for (int x = 0; x <= 12; ++x)
                {
                    positions.Vertices.insert(positions.Vertices.end(),
                                              {float(x) / 12 * 1.6f - 0.8f, 0, float(z) / 12 * 1.6f - 0.8f});
                }
            }
            for (uint32_t z = 0; z < 12; ++z)
            {
                for (uint32_t x = 0; x < 12; ++x)
                {
                    uint32_t a = z * 13 + x;
                    lod.IndexBuffer.Indices.insert(lod.IndexBuffer.Indices.end(),
                                                   {a, a + 13, a + 1, a + 1, a + 13, a + 14});
                }
            }
            auto mesh = std::make_shared<Engine::StaticMesh>(data);
            data->VirtualGeometryData.Build(mesh.get());
            data->VirtualGeometryData.InitializeResources();
            return mesh;
        }

        BEGIN_SHADER_PARAMETER_STRUCT(ReadbackParameters)
        RDG_TEXTURE_ACCESS(VisibilityRead, RHI::ERHIResourceAccess::TransferSrc)
        RDG_TEXTURE_ACCESS(ColorRead, RHI::ERHIResourceAccess::TransferSrc)
        END_SHADER_PARAMETER_STRUCT(ReadbackParameters)
    } // namespace

    class VirtualGeometryRasterTest final : public TestBase
    {
    public:
        void Setup() override
        {
            RHI::GRHIApi = CreateTestRHIApi();
            RHI::GRHIApi->Init();
            RenderCore::GTransientResourceAllocator.InitRHI();
            // Compile only the global shaders consumed by this isolated visibility test.
            auto& types = RenderCore::ShaderType::GetRegisterMap()[RenderCore::ShaderType::EShaderTypeFlag::Global];
            const auto registered = types;
            std::erase_if(types, [](const auto& entry)
            {
                return entry.first != "VirtualGeometryClearVS" && entry.first != "VirtualGeometryClearPS" &&
                       entry.first != "VirtualGeometryDebugPS";
            });
            RenderCore::GShaderMap.Initialize();
            types = registered;
        }

        void Run() override
        {
            auto mesh = Grid();
            auto data = mesh->GetRenderData();
            const auto& geometry = data->VirtualGeometryData;
            size_t leaves = 0;
            for (const auto& cluster : geometry.Clusters)
            {
                leaves += cluster.GeneratingGroupIndex == Engine::INVALID_VIRTUAL_GEOMETRY_INDEX;
            }
            Verify(leaves > 1 && geometry.Clusters.size() > leaves, "fixture has multiple leaves and coarse clusters");
            for (int scenario = 0; scenario < 4; ++scenario)
            {
                auto separateMesh = scenario == 3 ? Grid() : nullptr;
                auto separateData = separateMesh ? separateMesh->GetRenderData() : nullptr;
                RenderCore::RenderGraphBuilder graph;
                Engine::SceneView view;
                view.Viewport.X = view.Viewport.Y = 0;
                view.Viewport.Width = view.Viewport.Height = 64;
                view.ViewProjectionMatrix = Core::Mat4{};
                view.ViewProjectionMatrix(0, 0) = 1;
                view.ViewProjectionMatrix(1, 2) = 1;
                view.ViewProjectionMatrix(2, 1) = 1;
                view.ViewProjectionMatrix(3, 3) = 1;
                Renderer::VirtualGeometryInstance farInstance{data, Core::Mat4::Identity(), 17};
                farInstance.LocalToWorld(1, 3) = 0.75f;
                Renderer::VirtualGeometryInstance nearInstance{data, Core::Mat4::Identity(), 23};
                nearInstance.LocalToWorld(0, 0) = nearInstance.LocalToWorld(2, 2) = 0.5f;
                nearInstance.LocalToWorld(1, 3) = 0.25f;
                if (separateData)
                {
                    nearInstance.RenderData = separateData;
                }
                std::vector<Renderer::VirtualGeometryInstance> instances;
                if (scenario == 0 || scenario == 3)
                {
                    instances = {farInstance, nearInstance};
                }
                if (scenario == 1)
                {
                    instances = {nearInstance, farInstance};
                }
                auto result = Renderer::AddVirtualGeometryRasterPass(graph, view, instances, 64, 64);
                Verify(result->Draws.size() == instances.size() * leaves, "only leaf clusters generate draws");
                const size_t expectedResources = instances.empty() ? 0 : (separateData ? 2 : 1);
                Verify(result->Resources.size() == expectedResources,
                       "resource table preserves actual resource identity");
                RHI::RHITextureDesc colorDesc;
                colorDesc.Width = colorDesc.Height = 64;
                colorDesc.Format = RHI::ERHIFormat::R8G8B8A8_UNorm;
                colorDesc.Usage = RHI::ERHITextureCreateFlag::RenderTarget |
                                  RHI::ERHITextureCreateFlag::ShaderResource | RHI::ERHITextureCreateFlag::TransferSrc;
                RenderCore::RenderTexture color(colorDesc);
                color.InitRHIResource();
                auto target = graph.RegisterExternalTexture("VirtualGeometryTestColor", &color);
                Renderer::AddVirtualGeometryVisibilityDebugPass(graph, result, target, view);
                const bool readAfterSubmit = RHI::GShaderPlatform == RHI::ERHIShaderPlatform::OpenGL;
                void* visibilityReadback = nullptr;
                void* colorReadback = nullptr;
                auto* parameters = graph.AllocateParameter<ReadbackParameters>();
                parameters->VisibilityRead = result->VirtualGeometryVisibilityTexture;
                parameters->ColorRead = target;
                graph.AddPass("VirtualGeometryReadback", ReadbackParameters::GetMetaData(), parameters,
                                           RenderCore::EPassFlag::Graphic, [&](RHI::RHICommandListBase& cmd)
                {
                    if (!readAfterSubmit)
                    {
                        visibilityReadback = RHI::GRHIApi->MapReadTexture(
                            cmd, result->VirtualGeometryVisibilityTexture->GetRHITexture(), {});
                        colorReadback = RHI::GRHIApi->MapReadTexture(cmd, color.GetRHI(), {});
                    }
                });


                graph.Execute();
                const auto fence = color.GetTracker().GetLastAccessFence();
                RHI::GRHIApi->GetQueue(fence.QueueType)->WaitValue(fence.Value);
                if (readAfterSubmit)
                {
                    auto* queue = RHI::GRHIApi->GetQueue(RHI::EQueueType::Graphics);
                    auto* context = queue->AcquireCommandContext();
                    RHI::RHICommandListBase commands(context);
                    commands.SetImmediate(true);
                    commands.Begin();
                    visibilityReadback = RHI::GRHIApi->MapReadTexture(
                        commands, result->VirtualGeometryVisibilityTexture->GetRHITexture(), {});
                    colorReadback = RHI::GRHIApi->MapReadTexture(commands, color.GetRHI(), {});
                    commands.End();
                    const auto readFence = queue->ExecuteContext(context);
                    queue->WaitValue(readFence);
                }
                Verify(visibilityReadback && colorReadback, "both texture readbacks are mapped");
                const auto* ids = static_cast<const uint32_t*>(visibilityReadback);
                const auto* colors = static_cast<const uint8_t*>(colorReadback);
                size_t foreground = 0, background = 0, nearCount = 0, farCount = 0;
                bool nonzeroTriangle = false, offsetCluster = false;
                std::set<uint32_t> visibleClusters;
                bool valid = true;
                uint32_t colorErrors = 0, depthErrors = 0, idErrors = 0;
                for (uint32_t y = 0; y < 64; ++y)
                {
                    for (uint32_t x = 0; x < 64; ++x)
                    {
                        const auto pixel = y * 64 + x;
                        const uint32_t drawID = ids[pixel * 2], triangleID = ids[pixel * 2 + 1];
                        if (drawID == UINT32_MAX)
                        {
                            ++background;
                            valid &= triangleID == UINT32_MAX;
                            valid &= colors[pixel * 4 + 3] == 255;
                            continue;
                        }
                        ++foreground;
                        if (drawID >= result->Draws.size())
                        {
                            valid = false;
                            ++idErrors;
                            continue;
                        }
                        const auto& draw = result->Draws[drawID];
                        const uint32_t expectedResource = separateData && draw.InstanceID == 23 ? 1 : 0;
                        valid &= draw.ResourceIndex == expectedResource && draw.ClusterID < geometry.Clusters.size();
                        valid &=
                            result->Resources.at(draw.ResourceIndex) == (expectedResource == 1 ? separateData : data);
                        const auto& cluster = geometry.Clusters.at(draw.ClusterID);
                        valid &= triangleID < cluster.TriangleCount;
                        nonzeroTriangle |= triangleID > 0;
                        offsetCluster |= cluster.VertexOffset > 0 && cluster.IndexOffset > 0;
                        visibleClusters.insert(draw.ClusterID);
                        nearCount += draw.InstanceID == 23;
                        farCount += draw.InstanceID == 17;
                        const float px = (float(x) + 0.5f) / 64 * 2 - 1;
                        const float py = (float(y) + 0.5f) / 64 * 2 - 1;
                        const bool center = std::abs(px) < 0.4f && std::abs(py) < 0.4f;
                        if (draw.InstanceID != (center ? 23 : 17))
                        {
                            if (++depthErrors <= 4)
                            {
                                std::cout << "depth mismatch " << x << "," << y << " instance=" << draw.InstanceID
                                          << std::endl;
                            }
                            valid = false;
                        }
                        uint32_t hash = (draw.ClusterID + 1u) * 1664525u + 1013904223u;
                        for (uint32_t channel = 0; channel < 3; ++channel)
                        {
                            int expected = int(std::lround(51.0f + 0.8f * float((hash >> (channel * 8)) & 255u)));
                            if (std::abs(int(colors[pixel * 4 + channel]) - expected) > 1)
                            {
                                if (++colorErrors <= 6)
                                {
                                    std::cout << "color mismatch " << x << "," << y << " cluster=" << draw.ClusterID
                                              << " ch=" << channel << " got=" << int(colors[pixel * 4 + channel])
                                              << " expected=" << expected << std::endl;
                                }
                                valid = false;
                            }
                        }
                        valid &= colors[pixel * 4 + 3] == 255;
                    }
                }
                RHI::GRHIApi->Unmap(visibilityReadback);
                RHI::GRHIApi->Unmap(colorReadback);
                std::cout << "Readback counts fg=" << foreground << " bg=" << background << " near=" << nearCount
                          << " far=" << farCount << " colorErrors=" << colorErrors << " depthErrors=" << depthErrors
                          << " idErrors=" << idErrors << std::endl;
                Verify(valid, "visibility IDs, depth ordering and cluster debug colors match");
                Verify(background > 0, "invalid background pixels survive integer clear");
                if (!instances.empty())
                {
                    Verify(foreground > 2000 && nearCount > 500 && farCount > 1000,
                           "both instances cover expected areas");
                    Verify(nonzeroTriangle && offsetCluster && visibleClusters.size() == leaves,
                           "primitive IDs and cluster index/base-vertex offsets work");
                    Verify(geometry.PositionBuffer->GetTracker().GetLastAccess() ==
                                   RHI::ERHIResourceAccess::VertexOrIndexBuffer &&
                               geometry.PositionBuffer->GetTracker().GetLastAccessFence().Value > 0,
                           "RDG records vertex fetch state and submission fence");
                }
                else
                {
                    Verify(foreground == 0 && background == 4096, "empty scene is fully invalid");
                }
                if (separateData)
                {
                    const auto& otherGeometry = separateData->VirtualGeometryData;
                    Verify(otherGeometry.PositionBuffer->GetTracker().GetLastAccess() ==
                                   RHI::ERHIResourceAccess::VertexOrIndexBuffer &&
                               otherGeometry.IndexBuffer->GetTracker().GetLastAccess() ==
                                   RHI::ERHIResourceAccess::VertexOrIndexBuffer &&
                               otherGeometry.PositionBuffer->GetTracker().GetLastAccessFence().Value > 0 &&
                               otherGeometry.IndexBuffer->GetTracker().GetLastAccessFence().Value > 0,
                           "separate resources retain independent RDG vertex/index state and fences");
                }
                std::cout << "VirtualGeometryRaster scenario=" << scenario << " draws=" << result->Draws.size()
                          << " foreground=" << foreground << " near=" << nearCount << " far=" << farCount << std::endl;
            }
            data->VirtualGeometryData.ReleaseResources();
            std::cout << "VIRTUAL_GEOMETRY_VISIBILITY_TEST_OK" << std::endl;
        }

        void Teardown() override
        {
            Engine::GMeshMaterialShaderMap.Clear();
            RenderCore::GShaderMap.Clear();
            RHI::RHIPipelineStateCache::ClearAll();
            RenderCore::GTransientResourceAllocator.ReleaseRHI();
            RHI::GRHIApi->Shutdown();
            delete RHI::GRHIApi;
            RHI::GRHIApi = nullptr;
        }
    };

    const bool RegisteredVirtualGeometryRasterTest = []()
    {
        RenderTestRegistry::Register("VirtualGeometryRasterTest", []()
        {
            return std::make_unique<VirtualGeometryRasterTest>();
        });
        return true;
    }();
} // namespace Test
