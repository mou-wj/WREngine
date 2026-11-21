#include "TestBase.h"
#include "../Framework/Renderer/Private/GI/ScreenProbeFilter.h"
#include "GlobalShader.h"
#include "RHIPipelineStateCache.h"
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace Core;
using namespace Renderer;
using namespace RenderCore;
using namespace RHI;

namespace Test
{
    namespace
    {
        constexpr uint32_t Width = 16;
        constexpr uint32_t Height = 12;
        using Pixels = std::array<Float4, Width * Height>;

        BEGIN_SHADER_PARAMETER_STRUCT(ScreenProbeFilterTestUpload)
            RDG_TEXTURE_ACCESS(Raw, ERHIResourceAccess::TransferDest)
            RDG_TEXTURE_ACCESS(Color, ERHIResourceAccess::TransferDest)
            RDG_TEXTURE_ACCESS(Normal, ERHIResourceAccess::TransferDest)
            RDG_TEXTURE_ACCESS(Material, ERHIResourceAccess::TransferDest)
            RDG_TEXTURE_ACCESS(Depth, ERHIResourceAccess::TransferDest)
        END_SHADER_PARAMETER_STRUCT(ScreenProbeFilterTestUpload)

        BEGIN_SHADER_PARAMETER_STRUCT(ScreenProbeFilterTestReadback)
            RDG_TEXTURE_ACCESS(Output, ERHIResourceAccess::TransferSrc)
        END_SHADER_PARAMETER_STRUCT(ScreenProbeFilterTestReadback)

        void Verify(bool condition, const char* message)
        {
            if (!condition)
            {
                throw std::runtime_error(std::string("ScreenProbeFilterTest: ") + message);
            }
        }

        Pixels Filled(const Float4& value)
        {
            Pixels pixels;
            pixels.fill(value);
            return pixels;
        }

        Pixels Checker(uint32_t phase)
        {
            Pixels pixels;
            for (uint32_t y = 0; y < Height; ++y)
            {
                for (uint32_t x = 0; x < Width; ++x)
                {
                    const float value = (x + y + phase) % 2 == 0 ? 0.24f : 0.36f;
                    pixels[y * Width + x] = {value, value, value, 1};
                }
            }
            return pixels;
        }

        struct FilterFixture
        {
            Pixels Color = Filled({1, 1, 1, 0});
            Pixels Normal = Filled({0.5f, 0.5f, 1, 1});
            Pixels Material = Filled({0, 1, 1.0f / 255.0f, 1});
            Pixels Depth = Filled({0.25f, 0, 0, 0});
            RenderTexture Output;
            ScreenProbeFilter Filter;

            explicit FilterFixture(const RHITextureDesc& desc) : Output(desc)
            {
                Output.InitRHIResource();
            }

            Pixels Render(const Pixels& raw, EScreenProbeFilterView view, float cameraX = 0.0f)
            {
                RenderGraphBuilder graph;
                RenderGraphTextureDesc desc;
                desc.Width = Width;
                desc.Height = Height;
                desc.Format = ERHIFormat::R32G32B32A32_Float;
                desc.Usage = ERHITextureCreateFlag::ShaderResource | ERHITextureCreateFlag::TransferDest;
                auto* upload = graph.AllocateParameter<ScreenProbeFilterTestUpload>();
                upload->Raw = graph.CreateTexture("FilterTest.Raw", desc);
                upload->Color = graph.CreateTexture("FilterTest.Color", desc);
                upload->Normal = graph.CreateTexture("FilterTest.Normal", desc);
                upload->Material = graph.CreateTexture("FilterTest.Material", desc);
                upload->Depth = graph.CreateTexture("FilterTest.Depth", desc);
                graph.AddPass("FilterTestUpload", ScreenProbeFilterTestUpload::GetMetaData(), upload, EPassFlag::Graphic,
                    [&](RHICommandListBase& cmd)
                    {
                        const auto update = [&](RenderGraphTextureRef texture, const Pixels& pixels)
                        {
                            GRHIApi->UpdateTexture(cmd, texture->GetRHITexture(), pixels.data(), RHIUpdateTextureRegion::Create2DRegion(Width, Height));
                        };
                        update(upload->Raw, raw);
                        update(upload->Color, Color);
                        update(upload->Normal, Normal);
                        update(upload->Material, Material);
                        update(upload->Depth, Depth);
                    });

                ScreenProbeFilterInput input;
                input.RawGI = upload->Raw;
                input.Output = graph.RegisterExternalTexture("FilterTest.Output", &Output);
                input.GBuffer.GBufferA = upload->Color;
                input.GBuffer.GBufferB = upload->Normal;
                input.GBuffer.GBufferC = upload->Material;
                input.GBuffer.Depth = upload->Depth;
                input.GBuffer.PointSampler = GlobalNearestSampler.get();
                input.View = view;
                input.ViewProjection(0, 3) = -cameraX;
                input.InverseViewProjection(0, 3) = cameraX;
                input.CameraPosition = {cameraX, 0, 0};
                Filter.AddPasses(graph, input);

                void* mapped = nullptr;
                const auto map = [&](RHICommandListBase& cmd)
                {
                    mapped = GRHIApi->MapReadTexture(cmd, input.Output->GetRHITexture(), {});
                };
                auto* read = graph.AllocateParameter<ScreenProbeFilterTestReadback>();
                read->Output = input.Output;
                const bool bOpenGL = GShaderPlatform == ERHIShaderPlatform::OpenGL;
                graph.AddPass("FilterTestReadback", ScreenProbeFilterTestReadback::GetMetaData(), read, EPassFlag::Graphic,
                    [&](RHICommandListBase& cmd)
                    {
                        if (!bOpenGL)
                        {
                            map(cmd);
                        }
                    });
                graph.Execute();
                const auto fence = Output.GetTracker().GetLastAccessFence();
                auto* queue = GRHIApi->GetQueue(fence.QueueType);
                queue->WaitValue(fence.Value);
                if (bOpenGL)
                {
                    auto* context = queue->AcquireCommandContext();
                    RHICommandListBase cmd(context);
                    cmd.SetImmediate(true);
                    cmd.Begin();
                    map(cmd);
                    cmd.End();
                    queue->WaitValue(queue->ExecuteContext(context));
                }
                Verify(mapped != nullptr, "output readback");
                Pixels result;
                std::copy_n(static_cast<const Float4*>(mapped), result.size(), result.begin());
                GRHIApi->Unmap(mapped);
                return result;
            }
        };
    }

    class ScreenProbeFilterTest final : public TestBase
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
                return entry.first != "ScreenProbeTemporalCS" && entry.first != "ScreenProbeSpatialCS";
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
            RHITextureDesc desc;
            desc.Width = Width;
            desc.Height = Height;
            desc.Format = ERHIFormat::R32G32B32A32_Float;
            desc.Usage = ERHITextureCreateFlag::ShaderResource | ERHITextureCreateFlag::UAV | ERHITextureCreateFlag::TransferSrc;
            FilterFixture fixture(desc);
            constexpr uint32_t samplePixel = 5 * Width + 4;
            Pixels temporal;
            for (uint32_t phase = 0; phase < 12; ++phase)
            {
                temporal = fixture.Render(Checker(phase), EScreenProbeFilterView::Temporal);
            }
            float squaredError = 0.0f;
            for (const auto& pixel : temporal)
            {
                squaredError += (pixel.X - 0.30f) * (pixel.X - 0.30f);
            }
            const float rms = std::sqrt(squaredError / float(temporal.size()));
            std::cout << "Screen Probe static RMS: raw=0.06 temporal=" << rms << std::endl;
            Verify(rms < 0.02f, "temporal accumulation reduces alternating sampling noise");

            fixture.Filter.Invalidate();
            auto gradient = Filled({0, 0, 0, 1});
            for (uint32_t i = 0; i < gradient.size(); ++i)
            {
                const float value = 0.24f + 0.008f * float(i % Width);
                gradient[i] = {value, value, value, 1};
            }
            const auto flat = Filled({0.29f, 0.29f, 0.29f, 1});
            fixture.Render(gradient, EScreenProbeFilterView::Raw);
            const auto moved = fixture.Render(flat, EScreenProbeFilterView::Temporal, 0.25f);
            std::cout << "Screen Probe reprojected radiance at x=4: " << moved[samplePixel].X << std::endl;
            Verify(std::abs(moved[samplePixel].X - 0.28945f) < 0.001f, "camera reprojection samples the correct previous pixel");
            Verify(std::abs(moved[5 * Width + Width - 1].X - 0.29f) < 1e-4f, "off-screen history is rejected");

            fixture.Filter.Invalidate();
            fixture.Render(flat, EScreenProbeFilterView::Raw);
            fixture.Depth[samplePixel].X = 0.75f;
            fixture.Normal[samplePixel + 2] = {1, 0.5f, 0.5f, 1};
            const auto validity = fixture.Render(flat, EScreenProbeFilterView::HistoryValidity);
            Verify(validity[samplePixel].X > 0.7f, "depth disocclusion rejects history");
            Verify(validity[samplePixel + 2].X > 0.7f, "normal discontinuity rejects history");
            Verify(validity[samplePixel + 4].Y > 0.8f, "unchanged geometry keeps history");

            const auto dark = Filled({0.05f, 0.05f, 0.05f, 1});
            const auto changed = fixture.Render(dark, EScreenProbeFilterView::Temporal);
            Verify(std::abs(changed[samplePixel + 4].X - 0.05f) < 0.005f, "abrupt lighting change rejects stale radiance");
            fixture.Filter.Invalidate();
            const auto weights = fixture.Render(flat, EScreenProbeFilterView::HistoryWeight);
            Verify(weights[samplePixel].X == 0.0f, "explicit invalidation removes the old history weight");

            fixture.Depth = Filled({0.25f, 0, 0, 0});
            fixture.Normal = Filled({0.5f, 0.5f, 1, 1});
            auto checker = Checker(0);
            for (uint32_t i = 0; i < checker.size(); ++i)
            {
                if (i % Width >= Width / 2)
                {
                    checker[i] = {0.6f, 0.6f, 0.6f, 1};
                    fixture.Depth[i].X = 0.65f;
                }
            }
            fixture.Filter.Invalidate();
            const auto spatial = fixture.Render(checker, EScreenProbeFilterView::Spatial);
            const uint32_t rightEdge = 5 * Width + Width / 2;
            Verify(std::abs(spatial[samplePixel].X - 0.30f) < std::abs(checker[samplePixel].X - 0.30f), "spatial filter reduces interior noise");
            Verify(std::abs(spatial[rightEdge].X - 0.6f) < 1e-4f, "spatial filter preserves a depth edge");
            std::cout << "SCREEN_PROBE_FILTER_TEST_OK" << std::endl;
        }
    };
    REGISTER_RENDER_TEST("ScreenProbeFilterTest", ScreenProbeFilterTest);
}
