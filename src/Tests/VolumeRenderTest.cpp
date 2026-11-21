#include "TestBase.h"
#include "../Framework/Renderer/Private/Volume/VolumeRendering.h"
#include "Camera.h"
#include "GlobalShader.h"
#include "RenderGraphBuilder.h"
#include "RHIPipelineStateCache.h"
#include "SceneView.h"
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace Test
{
    using namespace RenderCore;
    using namespace Renderer;
    using namespace RHI;

    namespace
    {
        constexpr uint32_t Width = 17;
        constexpr uint32_t Height = 9;
        constexpr size_t Center = (Height / 2) * Width + Width / 2;
        using Pixels = std::array<Core::Float4, Width * Height>;

        BEGIN_SHADER_PARAMETER_STRUCT(VolumeTestUpload)
            RDG_TEXTURE_ACCESS(Color, ERHIResourceAccess::TransferDest)
            RDG_TEXTURE_ACCESS(Depth, ERHIResourceAccess::TransferDest)
        END_SHADER_PARAMETER_STRUCT(VolumeTestUpload)

        BEGIN_SHADER_PARAMETER_STRUCT(VolumeTestRead)
            RDG_TEXTURE_ACCESS(Color, ERHIResourceAccess::TransferSrc)
        END_SHADER_PARAMETER_STRUCT(VolumeTestRead)

        void RequireNear(float actual, float expected, const char* message)
        {
            if (!std::isfinite(actual) || std::abs(actual - expected) > 0.003f)
            {
                throw std::runtime_error(std::string(message) + ": actual=" + std::to_string(actual) +
                    " expected=" + std::to_string(expected));
            }
        }

        Pixels Render(const VolumeRenderSettings& settings, float surfaceDistance)
        {
            Engine::Camera camera;
            camera.SetPosition({0, 0, 0});
            camera.SetTarget({0, 0, -1});
            camera.SetUp({0, 1, 0});
            camera.SetPerspective(Core::DegToRad(45.0f), float(Width) / Height, 0.1f, 100.0f);
            Engine::SceneView view;
            view.ViewMatrix = camera.GetViewMatrix();
            view.InvViewProjectionMatrix = camera.GetInverseViewProjectionMatrix();
            view.NearClip = camera.GetNearPlane();
            view.FarClip = camera.GetFarPlane();
            view.CameraWorldPos = camera.GetPosition();
            view.Viewport = {0, 0, Width, Height};

            RHITextureDesc colorDesc{};
            colorDesc.Width = Width;
            colorDesc.Height = Height;
            colorDesc.Format = ERHIFormat::R32G32B32A32_Float;
            colorDesc.Usage = ERHITextureCreateFlag::ShaderResource | ERHITextureCreateFlag::TransferDest |
                ERHITextureCreateFlag::TransferSrc;
            RenderTexture color(colorDesc);
            color.InitRHIResource();

            RenderGraphBuilder graph;
            auto* upload = graph.AllocateParameter<VolumeTestUpload>();
            upload->Color = graph.RegisterExternalTexture("VolumeTest.Color", &color);
            RenderGraphTextureDesc depthDesc{};
            depthDesc.Width = Width;
            depthDesc.Height = Height;
            depthDesc.Format = ERHIFormat::R32_Float;
            depthDesc.Usage = ERHITextureCreateFlag::ShaderResource | ERHITextureCreateFlag::TransferDest;
            upload->Depth = graph.CreateTexture("VolumeTest.Depth", depthDesc);

            Pixels background;
            background.fill({0.8f, 0.4f, 0.2f, 1.0f});
            std::array<float, Width * Height> sceneDepth;
            sceneDepth.fill(surfaceDistance > 0.0f ? camera.WorldToNdc({0, 0, -surfaceDistance}).Z : 1.0f);
            graph.AddPass("VolumeTestUpload", VolumeTestUpload::GetMetaData(), upload, EPassFlag::Graphic, [&](RHICommandListBase& cmd)
            {
                const auto region = RHIUpdateTextureRegion::Create2DRegion(Width, Height);
                GRHIApi->UpdateTexture(cmd, upload->Color->GetRHITexture(), background.data(), region);
                GRHIApi->UpdateTexture(cmd, upload->Depth->GetRHITexture(), sceneDepth.data(), region);
            });
            AddVolumePasses(graph, view, upload->Depth, upload->Color, settings);

            auto* read = graph.AllocateParameter<VolumeTestRead>();
            read->Color = upload->Color;
            void* mapped = nullptr;
            const bool bOpenGL = GShaderPlatform == ERHIShaderPlatform::OpenGL;
            const auto map = [&](RHICommandListBase& cmd) { mapped = GRHIApi->MapReadTexture(cmd, color.GetRHI(), {}); };
            graph.AddPass("VolumeTestRead", VolumeTestRead::GetMetaData(), read, EPassFlag::Graphic, [&](RHICommandListBase& cmd)
            {
                if (!bOpenGL)
                {
                    map(cmd);
                }
            });
            graph.Execute();
            const auto fence = color.GetTracker().GetLastAccessFence();
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
            if (!mapped)
            {
                throw std::runtime_error("Volume output readback failed.");
            }
            Pixels pixels;
            std::copy_n(static_cast<const Core::Float4*>(mapped), pixels.size(), pixels.begin());
            GRHIApi->Unmap(mapped);
            return pixels;
        }
    }

    class VolumeRenderTest final : public TestBase
    {
    public:
        void Setup() override
        {
            GRHIApi = CreateTestRHIApi();
            GRHIApi->Init();
            GTransientResourceAllocator.InitRHI();
            auto& shaders = ShaderType::GetRegisterMap()[ShaderType::EShaderTypeFlag::Global];
            const auto registered = shaders;
            std::erase_if(shaders, [](const auto& shader) { return !shader.first.starts_with("Volume"); });
            GShaderMap.Initialize();
            shaders = registered;
        }

        void Teardown() override
        {
            GShaderMap.Clear();
            RHIPipelineStateCache::ClearAll();
            GTransientResourceAllocator.ReleaseRHI();
            GRHIApi->Shutdown();
            delete GRHIApi;
            GRHIApi = nullptr;
        }

        void Run() override
        {
            VolumeRenderSettings settings;
            settings.MaxDistance = 20;
            settings.Medium.Density = 0.2f;
            settings.Medium.Scattering = {0.6f, 0.4f, 0.2f};
            settings.Medium.Absorption = {0.4f, 0.1f, 0.0f};
            settings.AmbientRadiance = {0.5f, 0.5f, 0.5f};
            settings.View = EVolumeView::Transmittance;
            const auto nearPixel = Render(settings, 2)[Center];
            const auto farPixel = Render(settings, 10)[Center];
            const auto skyPixel = Render(settings, 0)[Center];
            const float nearT = std::exp(-0.2f * 1.9f);
            const float farT = std::exp(-0.2f * 9.9f);
            RequireNear(nearPixel.X, nearT, "foreground depth stops fog");
            RequireNear(farPixel.X, farT, "Beer-Lambert with 10-unit geometry");
            RequireNear(farPixel.Y, std::exp(-0.1f * 9.9f), "colored extinction");
            RequireNear(skyPixel.X, std::exp(-0.2f * 19.9f), "clear sky uses max range");

            settings.View = EVolumeView::Composite;
            const auto combined = Render(settings, 10)[Center];
            RequireNear(combined.X, 0.3f * (1.0f - farT) + farT * 0.8f, "scattering plus attenuated background");
            RequireNear(combined.W, 1.0f, "composite preserves alpha");
            settings.Medium.Density = 0;
            RequireNear(Render(settings, 10)[Center].X, 0.8f, "zero density leaves scene unchanged");

            settings.View = EVolumeView::Scattering;
            settings.Medium.Density = 0.2f;
            settings.Medium.Scattering = settings.Medium.Absorption = {};
            settings.Medium.Emission = {0.5f, 0, 0};
            RequireNear(Render(settings, 2)[Center].X, 0.19f, "emission accumulates without extinction");
            std::cout << "VOLUME_RENDER_TEST_OK near=" << nearPixel.X << " far=" << farPixel.X << " sky=" << skyPixel.X << std::endl;
        }
    };

    REGISTER_RENDER_TEST("VolumeRenderTest", VolumeRenderTest);
}
