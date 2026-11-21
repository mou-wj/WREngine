#include "TestBase.h"
#include "../Framework/Renderer/Private/VirtualGeometry/VirtualGeomtryRender.h"
#include "../Framework/Renderer/Private/Scene.h"
#include "GlobalShader.h"
#include "MaterialCore.h"
#include "RHIApi.h"
#include "RHIPipelineStateCache.h"
#include <array>
#include <iostream>
#include <stdexcept>

namespace Test
{
    namespace
    {
        BEGIN_SHADER_PARAMETER_STRUCT(VirtualGeometryRenderReadback)
        RDG_TEXTURE_ACCESS(Visibility, RHI::ERHIResourceAccess::TransferSrc)
        RDG_TEXTURE_ACCESS(Depth, RHI::ERHIResourceAccess::TransferSrc)
        RDG_TEXTURE_ACCESS(GBufferA, RHI::ERHIResourceAccess::TransferSrc)
        RDG_TEXTURE_ACCESS(GBufferB, RHI::ERHIResourceAccess::TransferSrc)
        RDG_TEXTURE_ACCESS(GBufferC, RHI::ERHIResourceAccess::TransferSrc)
        END_SHADER_PARAMETER_STRUCT(VirtualGeometryRenderReadback)

        void VerifyRender(bool condition, const char* message)
        {
            if (!condition)
            {
                throw std::runtime_error(std::string("Virtual geometry render test: ") + message);
            }
        }
    } // namespace

    class VirtualGeometryRenderTest final : public TestBase
    {
    public:
        void Setup() override
        {
            RHI::GRHIApi = CreateTestRHIApi();
            RHI::GRHIApi->Init();
            RenderCore::GTransientResourceAllocator.InitRHI();
            auto& types = RenderCore::ShaderType::GetRegisterMap()[RenderCore::ShaderType::EShaderTypeFlag::Global];
            const auto registered = types;
            std::erase_if(types, [](const auto& entry)
            {
                return entry.first != "VirtualGeometryClearVS" && entry.first != "VirtualGeometryClearPS" &&
                       entry.first != "VirtualGeometryResolveClearCS";
            });
            RenderCore::GShaderMap.Initialize();
            types = registered;
        }

        void Run() override
        {
            // Non-multiple-of-eight dimensions also cover the compute clear's edge threads.
            constexpr uint32_t width = 19, height = 13;
            Renderer::Scene scene;
            Engine::SceneView view;
            view.Viewport = {0, 0, width, height};
            RenderCore::RenderGraphTextureDesc desc;
            desc.Width = width;
            desc.Height = height;
            desc.Format = RHI::ERHIFormat::D32_Float;
            desc.Usage = RHI::ERHITextureCreateFlag::DepthStencil | RHI::ERHITextureCreateFlag::ShaderResource |
                         RHI::ERHITextureCreateFlag::TransferSrc;
            RenderCore::RenderTexture depthTexture(desc);
            depthTexture.InitRHIResource();
            desc.Usage = RHI::ERHITextureCreateFlag::UAV | RHI::ERHITextureCreateFlag::ShaderResource |
                         RHI::ERHITextureCreateFlag::TransferSrc;
            desc.Format = RHI::ERHIFormat::R8G8B8A8_UNorm;
            RenderCore::RenderTexture colorTexture(desc), pbrTexture(desc);
            colorTexture.InitRHIResource();
            pbrTexture.InitRHIResource();
            desc.Format = RHI::ERHIFormat::R16G16B16A16_Float;
            RenderCore::RenderTexture normalTexture(desc);
            normalTexture.InitRHIResource();
            RenderCore::RenderGraphBuilder graph;
            auto depth = graph.RegisterExternalTexture("RenderTest.Depth", &depthTexture);
            auto a = graph.RegisterExternalTexture("RenderTest.A", &colorTexture);
            auto b = graph.RegisterExternalTexture("RenderTest.B", &normalTexture);
            auto c = graph.RegisterExternalTexture("RenderTest.C", &pbrTexture);
            auto output = Renderer::AddVirtualGeometryRender(graph, &scene, view, depth, a, b, c);
            VerifyRender(output->VirtualGeometryDepthTexture == depth, "raster writes the supplied scene depth");
            VerifyRender(output->Draws.empty() && output->Instances.empty(), "empty scene produces no draws");

            const std::array<RenderCore::RenderGraphTextureRef, 5> textures = {
                output->VirtualGeometryVisibilityTexture, depth, a, b, c};
            std::array<void*, 5> readbacks{};
            const bool readAfterSubmit = RHI::GShaderPlatform == RHI::ERHIShaderPlatform::OpenGL;
            auto* params = graph.AllocateParameter<VirtualGeometryRenderReadback>();
            params->Visibility = textures[0];
            params->Depth = depth;
            params->GBufferA = a;
            params->GBufferB = b;
            params->GBufferC = c;
            graph.AddPass("VirtualGeometryRenderReadback", VirtualGeometryRenderReadback::GetMetaData(), params,
                          RenderCore::EPassFlag::Graphic, [&](RHI::RHICommandListBase& commands)
            {
                if (!readAfterSubmit)
                {
                    for (size_t index = 0; index < textures.size(); ++index)
                    {
                        readbacks[index] = RHI::GRHIApi->MapReadTexture(commands, textures[index]->GetRHITexture(), {});
                    }
                }
            });
            graph.Execute();
            const auto fence = depthTexture.GetTracker().GetLastAccessFence();
            VerifyRender(fence.Value > 0, "supplied depth receives a submission fence");
            RHI::GRHIApi->GetQueue(fence.QueueType)->WaitValue(fence.Value);
            if (readAfterSubmit)
            {
                auto* queue = RHI::GRHIApi->GetQueue(RHI::EQueueType::Graphics);
                auto* context = queue->AcquireCommandContext();
                RHI::RHICommandListBase commands(context);
                commands.SetImmediate(true);
                commands.Begin();
                for (size_t index = 0; index < textures.size(); ++index)
                {
                    readbacks[index] = RHI::GRHIApi->MapReadTexture(commands, textures[index]->GetRHITexture(), {});
                }
                commands.End();
                queue->WaitValue(queue->ExecuteContext(context));
            }
            for (void* readback : readbacks)
            {
                VerifyRender(readback != nullptr, "all render outputs are mapped");
            }
            const auto* ids = static_cast<const uint32_t*>(readbacks[0]);
            const auto* depths = static_cast<const float*>(readbacks[1]);
            bool validVisibility = true, validDepth = true, validGBuffer = true;
            for (uint32_t pixel = 0; pixel < width * height; ++pixel)
            {
                validVisibility &= ids[pixel * 2] == UINT32_MAX && ids[pixel * 2 + 1] == UINT32_MAX;
                validDepth &= depths[pixel] == 1.0f;
            }
            for (size_t target = 2; target < readbacks.size(); ++target)
            {
                const auto* bytes = static_cast<const uint8_t*>(readbacks[target]);
                const size_t size = width * height * (target == 3 ? 8 : 4);
                for (size_t index = 0; index < size; ++index)
                {
                    validGBuffer &= bytes[index] == 0;
                }
            }
            // Unmap uses the RHI's existing release path, including on a pixel mismatch.
            for (void* readback : readbacks)
            {
                RHI::GRHIApi->Unmap(readback);
            }
            VerifyRender(validVisibility, "empty scene visibility stays invalid");
            VerifyRender(validDepth, "supplied depth is cleared to far depth");
            VerifyRender(validGBuffer, "empty scene clears every GBuffer channel");
            std::cout << "VIRTUAL_GEOMETRY_RENDER_EMPTY_SCENE_OK" << std::endl;
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

    REGISTER_RENDER_TEST("VirtualGeometryRenderTest", VirtualGeometryRenderTest);
} // namespace Test
