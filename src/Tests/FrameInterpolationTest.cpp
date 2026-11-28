#include "TestBase.h"
#include "../Framework/Renderer/Private/PostProcess/FrameInterpolation.h"
#include "GlobalShader.h"
#include "RenderGraphBuilder.h"
#include "RHIPipelineStateCache.h"
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace Test
{
    using namespace Core;
    using namespace Renderer;
    using namespace RenderCore;
    using namespace RHI;

    namespace
    {
        constexpr uint32_t Width = 48;
        constexpr uint32_t Height = 32;
        using Pixels = std::array<Float4, Width * Height>;

        BEGIN_SHADER_PARAMETER_STRUCT(InterpolationTestUpload)
            RDG_TEXTURE_ACCESS(Color, ERHIResourceAccess::TransferDest)
            RDG_TEXTURE_ACCESS(Depth, ERHIResourceAccess::TransferDest)
        END_SHADER_PARAMETER_STRUCT(InterpolationTestUpload)

        BEGIN_SHADER_PARAMETER_STRUCT(InterpolationTestRead)
            RDG_TEXTURE_ACCESS(Output, ERHIResourceAccess::TransferSrc)
        END_SHADER_PARAMETER_STRUCT(InterpolationTestRead)

        void Verify(bool result, const char* message)
        {
            if (!result)
            {
                throw std::runtime_error(std::string("FrameInterpolationTest: ") + message);
            }
        }

        float Pattern(float x, float y)
        {
            return 0.5f + 0.20f * std::sin(0.44f * x + 0.31f * y) +
                0.23f * std::cos(0.35f * y - 0.29f * x);
        }
    }

    class FrameInterpolationTest final : public TestBase
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
                return entry.first != "FrameCaptureCS" && entry.first != "FrameLuminanceCS" &&
                    entry.first != "FrameFlowCS" && entry.first != "FrameInterpolateCS";
            });
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
            RHITextureDesc outputDesc;
            outputDesc.Width = Width;
            outputDesc.Height = Height;
            outputDesc.Format = ERHIFormat::R8G8B8A8_UNorm;
            outputDesc.Usage = ERHITextureCreateFlag::ShaderResource | ERHITextureCreateFlag::UAV | ERHITextureCreateFlag::TransferSrc;
            RenderTexture output(outputDesc);
            output.InitRHIResource();
            FrameInterpolation interpolation;
            std::array<uint8_t, Width * Height * 4> result{};
            Pixels depth;
            depth.fill({0.4f, 0, 0, 0});
            const auto render = [&](float shift, double time, bool bExpected)
            {
                Pixels color;
                for (uint32_t y = 0; y < Height; ++y)
                {
                    for (uint32_t x = 0; x < Width; ++x)
                    {
                        const float luma = Pattern(float(x) - shift, float(y));
                        color[y * Width + x] = {luma, luma, luma, 1};
                    }
                }
                RenderGraphBuilder graph;
                RenderGraphTextureDesc desc;
                desc.Width = Width;
                desc.Height = Height;
                desc.Format = ERHIFormat::R32G32B32A32_Float;
                desc.Usage = ERHITextureCreateFlag::ShaderResource | ERHITextureCreateFlag::TransferDest;
                auto* upload = graph.AllocateParameter<InterpolationTestUpload>();
                upload->Color = graph.CreateTexture("InterpolationTest.Color", desc);
                upload->Depth = graph.CreateTexture("InterpolationTest.Depth", desc);
                graph.AddPass("InterpolationTest.Upload", InterpolationTestUpload::GetMetaData(), upload, EPassFlag::Graphic,
                    [&](RHICommandListBase& cmd)
                    {
                        const auto region = RHIUpdateTextureRegion::Create2DRegion(Width, Height);
                        GRHIApi->UpdateTexture(cmd, upload->Color->GetRHITexture(), color.data(), region);
                        GRHIApi->UpdateTexture(cmd, upload->Depth->GetRHITexture(), depth.data(), region);
                    });
                Engine::SceneView view;
                view.CameraWorldPos = {};
                const auto target = graph.RegisterExternalTexture("InterpolationTest.Output", &output);
                const bool bGenerated = interpolation.AddPasses(graph, upload->Color, upload->Depth, target, view, {}, time);
                Verify(bGenerated == bExpected, "pair readiness matches capture and timing");
                if (!bGenerated)
                {
                    graph.Execute();
                    return;
                }
                void* mapped = nullptr;
                const auto read = [&](RHICommandListBase& cmd) { mapped = GRHIApi->MapReadTexture(cmd, output.GetRHI(), {}); };
                const bool bOpenGL = GShaderPlatform == ERHIShaderPlatform::OpenGL;
                auto* access = graph.AllocateParameter<InterpolationTestRead>();
                access->Output = target;
                graph.AddPass("InterpolationTest.Read", InterpolationTestRead::GetMetaData(), access, EPassFlag::Graphic,
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
                Verify(mapped != nullptr, "output pixel readback");
                std::copy_n(static_cast<const uint8_t*>(mapped), result.size(), result.begin());
                GRHIApi->Unmap(mapped);
            };

            render(0.0f, 1.0, false);
            render(4.0f, 1.016, true);
            float sum = 0.0f;
            size_t samples = 0;
            for (uint32_t y = 6; y < Height - 6; ++y)
            {
                for (uint32_t x = 9; x < Width - 9; ++x)
                {
                    sum += std::abs(float(result[4 * (y * Width + x)]) / 255.0f - Pattern(float(x) - 2.0f, float(y)));
                    ++samples;
                }
            }
            const float error = sum / static_cast<float>(samples);
            std::cout << "Frame interpolation midpoint MAE=" << error << std::endl;
            Verify(error < 0.065f, "midpoint reaches the moving test pattern");
            render(8.0f, 1.40, false);
            render(12.0f, 1.416, true);
            interpolation.Invalidate();
            render(16.0f, 1.432, false);
            std::cout << "FRAME_INTERPOLATION_TEST_OK" << std::endl;
        }
    };

    REGISTER_RENDER_TEST("FrameInterpolationTest", FrameInterpolationTest);
}
