#include "TestBase.h"
#include "../Framework/Renderer/Private/PostProcess/AntiAliasingPostProcess.h"
#include "RHIPipelineStateCache.h"
#include <array>
#include <iostream>
#include <stdexcept>

namespace Test
{
    using namespace Core;
    using namespace RenderCore;
    using namespace RHI;
    using namespace Renderer;

    namespace
    {
        constexpr uint32_t RenderWidth = 8;
        constexpr uint32_t RenderHeight = 6;
        constexpr uint32_t OutputWidth = 16;
        constexpr uint32_t OutputHeight = 12;
        using SourcePixels = std::array<Float4, RenderWidth * RenderHeight>;
        using OutputPixels = std::array<uint8_t, OutputWidth * OutputHeight * 4>;

        BEGIN_SHADER_PARAMETER_STRUCT(UpsampleTestUpload)
            RDG_TEXTURE_ACCESS(Color, ERHIResourceAccess::TransferDest)
            RDG_TEXTURE_ACCESS(Depth, ERHIResourceAccess::TransferDest)
            RDG_TEXTURE_ACCESS(Opaque, ERHIResourceAccess::TransferDest)
        END_SHADER_PARAMETER_STRUCT(UpsampleTestUpload)

        BEGIN_SHADER_PARAMETER_STRUCT(UpsampleTestReadback)
            RDG_TEXTURE_ACCESS(Output, ERHIResourceAccess::TransferSrc)
        END_SHADER_PARAMETER_STRUCT(UpsampleTestReadback)

        void Verify(bool condition, const char* message)
        {
            if (!condition)
            {
                throw std::runtime_error(std::string("SuperResolutionTest: ") + message);
            }
        }
    }

    class SuperResolutionTest final : public TestBase
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
            std::erase_if(globals, [](const auto& entry) { return entry.first != "taaTAA"; });
            GShaderMap.Initialize();
            globals = registered;
        }

        void Teardown() override
        {
            GShaderMap.Clear();
            RHIPipelineStateCache::ClearAll();
            GTransientResourceAllocator.ReleaseRHI();
            ReleaseGlobalRenderResource();
            GRHIApi->Shutdown();
            delete GRHIApi;
            GRHIApi = nullptr;
        }

        void Run() override
        {
            Engine::SuperResolutionSettings settings;
            settings.Preset = Engine::ESuperResolutionPreset::Performance;
            settings.Sharpening = 0.0f;
            Verify(TemporalUpsampling::GetRenderSize({OutputWidth, OutputHeight}, settings) == UInt2(RenderWidth, RenderHeight),
                "performance preset uses half the output dimensions");

            RHITextureDesc outputDesc;
            outputDesc.Width = OutputWidth;
            outputDesc.Height = OutputHeight;
            outputDesc.Format = ERHIFormat::R8G8B8A8_UNorm;
            outputDesc.Usage = ERHITextureCreateFlag::ShaderResource | ERHITextureCreateFlag::UAV | ERHITextureCreateFlag::TransferSrc;
            RenderTexture output(outputDesc);
            output.InitRHIResource();
            TemporalUpsampling temporal;

            SourcePixels color, depth, opaque;
            for (uint32_t y = 0; y < RenderHeight; ++y)
            {
                for (uint32_t x = 0; x < RenderWidth; ++x)
                {
                    const size_t pixel = y * RenderWidth + x;
                    const bool bLeft = x < RenderWidth / 2;
                    color[pixel] = bLeft ? Float4{0.8f, 0, 0, 1} : Float4{0, 0.6f, 0, 1};
                    depth[pixel] = {bLeft ? 0.25f : 0.75f, 0, 0, 0};
                }
            }
            opaque = color;
            const auto render = [&](Engine::ESuperResolutionView debug)
            {
                RenderGraphBuilder graph;
                RenderGraphTextureDesc sourceDesc;
                sourceDesc.Width = RenderWidth;
                sourceDesc.Height = RenderHeight;
                sourceDesc.Format = ERHIFormat::R32G32B32A32_Float;
                sourceDesc.Usage = ERHITextureCreateFlag::ShaderResource | ERHITextureCreateFlag::TransferDest;
                auto* upload = graph.AllocateParameter<UpsampleTestUpload>();
                upload->Color = graph.CreateTexture("UpsampleTest.Color", sourceDesc);
                upload->Depth = graph.CreateTexture("UpsampleTest.Depth", sourceDesc);
                upload->Opaque = graph.CreateTexture("UpsampleTest.Opaque", sourceDesc);
                graph.AddPass("UpsampleTest.Upload", UpsampleTestUpload::GetMetaData(), upload, EPassFlag::Graphic,
                    [&](RHICommandListBase& cmd)
                    {
                        const auto region = RHIUpdateTextureRegion::Create2DRegion(RenderWidth, RenderHeight);
                        GRHIApi->UpdateTexture(cmd, upload->Color->GetRHITexture(), color.data(), region);
                        GRHIApi->UpdateTexture(cmd, upload->Depth->GetRHITexture(), depth.data(), region);
                        GRHIApi->UpdateTexture(cmd, upload->Opaque->GetRHITexture(), opaque.data(), region);
                    });
                Engine::SceneView view;
                view.Viewport = {0, 0, RenderWidth, RenderHeight};
                view.CameraWorldPos = {0, 0, 0};
                settings.View = debug;
                const auto target = graph.RegisterExternalTexture("UpsampleTest.Output", &output);
                temporal.AddPass(graph, view, view, {0, 0}, settings, upload->Color, upload->Depth, target, upload->Opaque);

                void* mapped = nullptr;
                const auto read = [&](RHICommandListBase& cmd)
                {
                    mapped = GRHIApi->MapReadTexture(cmd, output.GetRHI(), {});
                };
                const bool bOpenGL = GShaderPlatform == ERHIShaderPlatform::OpenGL;
                auto* access = graph.AllocateParameter<UpsampleTestReadback>();
                access->Output = target;
                graph.AddPass("UpsampleTest.Readback", UpsampleTestReadback::GetMetaData(), access, EPassFlag::Graphic,
                    [&](RHICommandListBase& cmd)
                    {
                        if (!bOpenGL)
                        {
                            read(cmd);
                        }
                    });
                graph.Execute();
                const auto fence = output.GetTracker().GetLastAccessFence();
                auto* queue = GRHIApi->GetQueue(fence.QueueType);
                queue->WaitValue(fence.Value);
                if (bOpenGL)
                {
                    auto* context = queue->AcquireCommandContext();
                    RHICommandListBase cmd(context);
                    cmd.SetImmediate(true);
                    cmd.Begin();
                    read(cmd);
                    cmd.End();
                    queue->WaitValue(queue->ExecuteContext(context));
                }
                Verify(mapped != nullptr, "read back output");
                OutputPixels pixels;
                std::copy_n(static_cast<const uint8_t*>(mapped), pixels.size(), pixels.begin());
                GRHIApi->Unmap(mapped);
                return pixels;
            };

            const auto image = render(Engine::ESuperResolutionView::Final);
            constexpr size_t left = (OutputWidth * 5 + 7) * 4;
            constexpr size_t right = (OutputWidth * 5 + 8) * 4;
            Verify(image[left] >= 203 && image[left + 1] == 0 && image[right] == 0 && image[right + 1] >= 152,
                "upsampled colors remain separated at the depth edge");
            const auto accumulation = render(Engine::ESuperResolutionView::HistoryWeight);
            Verify(accumulation[left - 8] > 200, "static surface accumulates history");

            for (size_t i = 0; i < depth.size(); ++i)
            {
                if (i % RenderWidth < RenderWidth / 2)
                {
                    depth[i].X = 0.55f;
                    color[i] = {0, 0, 1, 1};
                }
            }
            const auto disocclusion = render(Engine::ESuperResolutionView::Disocclusion);
            Verify(disocclusion[left - 8] >= 250 && disocclusion[right + 8] < 3, "changed depth rejects only affected history");
            const auto reactive = render(Engine::ESuperResolutionView::Reactive);
            Verify(reactive[left - 8] >= 250 && reactive[right + 8] < 3, "opaque reference detects changed composition");
            temporal.Invalidate();
            const auto reset = render(Engine::ESuperResolutionView::HistoryWeight);
            Verify(reset[right + 8] == 0, "history reset discards previous frame");
            std::cout << "SUPER_RESOLUTION_TEST_OK" << std::endl;
        }
    };

    REGISTER_RENDER_TEST("SuperResolutionTest", SuperResolutionTest);
}
