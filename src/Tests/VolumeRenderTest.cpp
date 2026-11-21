#include "TestBase.h"
#include "../Framework/Renderer/Private/Volume/VolumeRendering.h"
#include "../Framework/Renderer/Private/Scene.h"
#include "Camera.h"
#include "DistanceFieldMgr.h"
#include "EnvironmentLightResourceManager.h"
#include "GlobalShader.h"
#include "LightComponent.h"
#include "LightSceneProxy.h"
#include "RenderGraphBuilder.h"
#include "RenderModule.h"
#include "RHIPipelineStateCache.h"
#include "SceneView.h"
#include "Shape.h"
#include "StaticMeshAsset.h"
#include "StaticMeshComponent.h"
#include "Viewport.h"
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

        Pixels ExecuteAndRead(RenderGraphBuilder& graph, RenderGraphTextureRef target, RenderTexture& color)
        {
            auto* read = graph.AllocateParameter<VolumeTestRead>();
            read->Color = target;
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

        Pixels Render(const VolumeRenderSettings& settings, float surfaceDistance, Engine::SceneInterface* scene = nullptr,
            double timeSeconds = 0)
        {
            Engine::Camera camera;
            camera.SetPosition({0, 0, 0});
            camera.SetTarget({0, 0, -1});
            camera.SetUp({0, 1, 0});
            camera.SetPerspective(Core::DegToRad(45.0f), float(Width) / Height, 0.1f, 100.0f);
            Engine::SceneView view;
            view.ViewMatrix = camera.GetViewMatrix();
            view.ProjectionMatrix = camera.GetProjectionMatrix();
            view.InvViewProjectionMatrix = camera.GetInverseViewProjectionMatrix();
            view.NearClip = camera.GetNearPlane();
            view.FarClip = camera.GetFarPlane();
            view.CameraWorldPos = camera.GetPosition();
            view.Viewport = {0, 0, Width, Height};
            view.BuildSplitDepths();
            view.FrameTimeSeconds = timeSeconds;

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
            AddVolumePasses(graph, view, upload->Depth, upload->Color, settings, scene);

            return ExecuteAndRead(graph, upload->Color, color);
        }

        std::unique_ptr<RenderTexture> MakeShadowTexture(ERHITextureType type, uint32_t layers,
            uint32_t blockerLayer, float blockerDepth, float clearDepth)
        {
            constexpr uint32_t size = 4;
            RHITextureDesc desc{};
            desc.Width = desc.Height = size;
            desc.ArraySize = layers;
            desc.Format = ERHIFormat::R32_Float;
            desc.Type = type;
            desc.Usage = ERHITextureCreateFlag::ShaderResource | ERHITextureCreateFlag::TransferDest;
            auto texture = std::make_unique<RenderTexture>(desc);
            texture->InitRHIResource();
            RenderGraphBuilder graph;
            auto* upload = graph.AllocateParameter<VolumeTestUpload>();
            upload->Color = graph.RegisterExternalTexture("VolumeTest.Shadow", texture.get());
            graph.AddPass("VolumeTestShadow", VolumeTestUpload::GetMetaData(), upload, EPassFlag::Graphic,
                [&](RHICommandListBase& cmd)
                {
                    for (uint32_t layer = 0; layer < layers; ++layer)
                    {
                        auto region = RHIUpdateTextureRegion::Create2DRegion(size, size);
                        region.ArraySlice = layer;
                        std::array<float, size * size> pixels;
                        pixels.fill(layer == blockerLayer || blockerLayer >= layers ? blockerDepth : clearDepth);
                        GRHIApi->UpdateTexture(cmd, texture->GetRHI(), pixels.data(), region);
                    }
                });
            graph.Execute();
            const auto fence = texture->GetTracker().GetLastAccessFence();
            GRHIApi->GetQueue(fence.QueueType)->WaitValue(fence.Value);
            return texture;
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
            InitGlobalRenderResource();
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
            ReleaseGlobalRenderResource();
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

            Engine::DirectionalLightComponent directional;
            directional.SetDirection({0, 0, 1});
            directional.SetColor({1, 0, 0});
            directional.SetIntensity(6.0f);
            Engine::PointLightComponent point;
            point.SetWorldLocation({0, 0, -4});
            point.SetColor({0, 1, 0});
            point.SetIntensity(12.0f);
            point.SetAttenuationRadius(3.0f);
            Engine::SpotLightComponent spot;
            spot.SetWorldLocation({0, 0, -0.2f});
            spot.SetDirection({0, 0, -1});
            spot.SetColor({0, 0, 1});
            spot.SetIntensity(24.0f);
            spot.SetAttenuationRadius(7.0f);
            spot.SetInnerConeAngle(23.0f);
            spot.SetOuterConeAngle(32.0f);
            Renderer::Scene scene;
            scene.AddLight(&directional);
            scene.AddLight(&point);
            scene.AddLight(&spot);
            scene.FlushPendingUpdates();

            settings.View = EVolumeView::DirectLighting;
            settings.Medium.Scattering = {1, 1, 1};
            settings.Medium.Emission = {};
            settings.LightMask = 1;
            const auto isotropic = Render(settings, 6, &scene)[Center];
            RequireNear(isotropic.X, (6.0f / (4.0f * 3.14159265f)) * (1.0f - std::exp(-0.2f * 5.9f)),
                "directional light has isotropic 1/(4 pi) phase");
            settings.Medium.Anisotropy = 0.55f;
            const auto forward = Render(settings, 6, &scene)[Center];
            if (forward.X < isotropic.X * 2.0f)
            {
                throw std::runtime_error("Forward anisotropy must increase the light along the view ray.");
            }
            settings.Medium.Anisotropy = 0;
            settings.LightMask = 2;
            const auto radial = Render(settings, 6, &scene)[Center];
            RequireNear(Render(settings, 0.4f, &scene)[Center].Y, 0.0f, "point light has finite radius");
            settings.LightMask = 4;
            const auto cone = Render(settings, 6, &scene);
            const float outsideCone = cone[Center + Width / 2].Z;
            std::cout << "VOLUME_LIGHTING_READBACK isotropic=" << isotropic.X << " forward=" << forward.X <<
                " point=" << radial.Y << " spot=" << cone[Center].Z << " edge=" << outsideCone << std::endl;
            if (radial.Y < 0.05f || cone[Center].Z < 0.1f || cone[Center].Z <= outsideCone * 1.5f)
            {
                throw std::runtime_error("Local lights did not produce a point falloff and a bounded spot cone.");
            }
            RequireNear(radial.X, 0.0f, "point mask ignores directional light");
            RequireNear(cone[Center].Y, 0.0f, "spot mask ignores point light");
            std::cout << "VOLUME_LIGHTING_TEST_OK" << std::endl;

            auto csm = MakeShadowTexture(ERHITextureType::Texture2DArray, 4, 2, 0.1f, 1.0f);
            auto cube = MakeShadowTexture(ERHITextureType::TextureCube, 6, 6, 0.01f, 3.0f);
            auto atlas = MakeShadowTexture(ERHITextureType::Texture2D, 1, 0, 0.1f, 1.0f);
            scene.ForEachLight([&](Engine::LightSceneProxy* proxy)
            {
                auto& shadow = scene.GetLightShadowInfo(proxy);
                auto& allocation = shadow.Allocation;
                auto projection = Core::Float4x4::Identity();
                projection(0, 0) = projection(1, 1) = 0.1f;
                projection(2, 2) = -0.04f;
                projection(2, 3) = 0.2f;
                switch (proxy->GetLightType())
                {
                case Engine::ELightType::Directional:
                    allocation.ShadowType = EShadowType::Directional;
                    allocation.Texture = csm.get();
                    allocation.Slices.resize(4);
                    allocation.Slices[0].Layer = 2;
                    shadow.ShadowMatrices.assign(4, projection);
                    break;
                case Engine::ELightType::Point:
                    allocation.ShadowType = EShadowType::Point;
                    allocation.Texture = cube.get();
                    allocation.Slices.resize(6);
                    break;
                case Engine::ELightType::Spot:
                    allocation.ShadowType = EShadowType::Spot;
                    allocation.Texture = atlas.get();
                    allocation.Slices.resize(1);
                    allocation.Slices[0].UVScale = {0.5f, 0.5f};
                    allocation.Slices[0].UVOffset = {0.5f, 0.5f};
                    shadow.ShadowMatrices.assign(1, projection);
                    break;
                default:
                    break;
                }
            });

            settings.LightMask = 7;
            const auto blocked = Render(settings, 6, &scene)[Center];
            std::cout << "VOLUME_SHADOW_READBACK " << blocked.X << ',' << blocked.Y << ',' << blocked.Z << std::endl;
            RequireNear(blocked.X, 0.0f, "selected CSM array layer blocks the directional light");
            RequireNear(blocked.Y, 0.0f, "point shadow cube blocks the point light");
            RequireNear(blocked.Z, 0.0f, "spot atlas blocks the spotlight");
            settings.View = EVolumeView::LightVisibility;
            RequireNear(Render(settings, 6, &scene)[Center].X, 0.0f, "visibility slice shows occluded froxels");
            settings.View = EVolumeView::Transmittance;
            const auto shadowedT = Render(settings, 6, &scene)[Center];
            settings.bShadows = false;
            RequireNear(Render(settings, 6, &scene)[Center].X, shadowedT.X, "shadows leave extinction unchanged");
            settings.View = EVolumeView::DirectLighting;
            const auto unblocked = Render(settings, 6, &scene)[Center];
            RequireNear(unblocked.X, isotropic.X, "shadow toggle restores directional radiance");
            RequireNear(unblocked.Y, radial.Y, "shadow toggle restores point radiance");
            RequireNear(unblocked.Z, cone[Center].Z, "shadow toggle restores spot radiance");
            std::cout << "VOLUME_SHADOW_TEST_OK" << std::endl;

            Renderer::LocalVolume box;
            box.Position = {0, 0, -3};
            box.RotationDegrees = {20, 40, 0};
            box.HalfExtent = {8, 7, 6};
            box.Medium.Density = 0.2f;
            box.Medium.Scattering = {0.5f, 0.5f, 0.5f};
            box.Medium.Absorption = {0.5f, 0.5f, 0.5f};
            auto sphere = box;
            sphere.Shape = EVolumeShape::Sphere;
            sphere.RotationDegrees = {0, 10, 0};
            sphere.Medium.Density = 0.3f;
            sphere.Medium.Scattering = {0.25f, 0.25f, 0.25f};
            sphere.Medium.Absorption = {0.25f, 0.25f, 0.25f};
            settings.LocalVolumes = {box, sphere};
            settings.Medium.Density = 0.1f;
            settings.Medium.Scattering = {0.5f, 0.5f, 0.5f};
            settings.View = EVolumeView::Transmittance;
            const auto overlapT = Render(settings, 6, &scene)[Center];
            RequireNear(overlapT.X, std::exp(-0.4f * 5.9f), "overlapping box, ellipsoid and global extinction add");
            settings.View = EVolumeView::LocalOverlap;
            RequireNear(Render(settings, 6, &scene)[Center].X, 0.535f, "overlap slice contains two local volumes");
            settings.View = EVolumeView::DirectLighting;
            const auto localLit = Render(settings, 6, &scene)[Center];
            if (localLit.X <= 0.05f || localLit.Y <= 0.01f || localLit.Z <= 0.03f)
            {
                throw std::runtime_error("Local media must participate in direct scattering from all three lights.");
            }
            settings.bShadows = true;
            RequireNear(Render(settings, 6, &scene)[Center].X, 0, "local media respect the existing light shadow");
            settings.View = EVolumeView::Transmittance;
            settings.LocalVolumes.pop_back();
            RequireNear(Render(settings, 6, &scene)[Center].X, std::exp(-0.25f * 5.9f), "removing the sphere leaves the box");
            settings.LocalVolumes.front().Position.X = 100;
            RequireNear(Render(settings, 6, &scene)[Center].X, std::exp(-0.05f * 5.9f), "moving the box leaves only global fog");
            std::cout << "VOLUME_LOCAL_TEST_OK overlap=" << overlapT.X << std::endl;

            auto constantDensity = Create3DTexture(2, 2, 2, ERHIFormat::R32_Float,
                ERHITextureCreateFlag::ShaderResource | ERHITextureCreateFlag::TransferDest, "VolumeTest.ConstantDensity");
            std::array<float, 8> constantSamples;
            constantSamples.fill(0.5f);
            constantDensity->UploadData(constantSamples.data(), 0, 0);
            VolumeRenderSettings densityTest;
            densityTest.MaxDistance = 20;
            densityTest.Medium.Density = 0;
            densityTest.View = EVolumeView::Transmittance;
            auto textureVolume = box;
            textureVolume.DensityField.Mode = EVolumeDensityMode::Texture;
            textureVolume.DensityField.Texture = constantDensity.get();
            textureVolume.DensityField.Scale = 1.5f;
            textureVolume.DensityField.Bias = 0.1f;
            textureVolume.DensityField.Threshold = 0.2f;
            densityTest.LocalVolumes = {textureVolume};
            const auto textureT = Render(densityTest, 6)[Center].X;
            RequireNear(textureT, std::exp(-0.13f * 5.9f), "3D texture coefficient follows scale, bias and threshold");

            auto varyingTexture = CreateVolumePreviewDensityTexture();
            auto movingTexture = textureVolume;
            movingTexture.DensityField.Texture = varyingTexture.get();
            movingTexture.DensityField.Velocity = {0.41f, 0.28f, 0.13f};
            movingTexture.DensityField.Threshold = 0;
            auto movingNoise = sphere;
            movingNoise.DensityField.Mode = EVolumeDensityMode::Noise;
            movingNoise.DensityField.Velocity = {0.31f, -0.12f, 0.07f};
            densityTest.LocalVolumes = {box, movingNoise, textureVolume, movingTexture};
            densityTest.View = EVolumeView::DensitySources;
            const auto firstFields = Render(densityTest, 6);
            const auto secondFields = Render(densityTest, 6, nullptr, 1.75);
            RequireNear(firstFields[Center].Y, 1.0f - std::exp(-4 * box.Medium.Density),
                "uniform local density coexists with noise and textures");
            float noiseChange = 0.0f;
            float textureChange = 0.0f;
            for (size_t i = 0; i < firstFields.size(); ++i)
            {
                RequireNear(secondFields[i].Y, firstFields[i].Y, "uniform density stays stable as fields move");
                noiseChange = std::max(noiseChange, std::abs(secondFields[i].X - firstFields[i].X));
                textureChange = std::max(textureChange, std::abs(secondFields[i].Z - firstFields[i].Z));
            }
            if (firstFields[Center].X <= 0.03f || firstFields[Center].Z <= 1.0f - std::exp(-4 * 0.13f) ||
                noiseChange < 0.01f || textureChange < 0.01f)
            {
                throw std::runtime_error("Animated procedural and texture density must change while both contribute.");
            }
            densityTest.View = EVolumeView::Transmittance;
            const auto animatedT0 = Render(densityTest, 6)[Center].X;
            const auto animatedT1 = Render(densityTest, 6, nullptr, 1.75)[Center].X;
            if (std::abs(animatedT1 - animatedT0) < 0.005f)
            {
                throw std::runtime_error("Animated density must change extinction.");
            }
            densityTest.View = EVolumeView::DirectLighting;
            densityTest.bShadows = false;
            const auto densityLit = Render(densityTest, 6, &scene, 1.75)[Center];
            densityTest.bShadows = true;
            if (densityLit.X <= 0.05f)
            {
                throw std::runtime_error("Nonuniform media must receive direct light.");
            }
            RequireNear(Render(densityTest, 6, &scene, 1.75)[Center].X, 0.0f, "nonuniform media receive shadowed incident light");
            std::cout << "VOLUME_DENSITY_TEST_OK textureT=" << textureT << " noiseMaxDelta=" << noiseChange <<
                " textureMaxDelta=" << textureChange << " transmittance=" << animatedT0 << "->" << animatedT1 << std::endl;

            // The fixture textures are borrowed sampler inputs, outside the scene allocator.
            scene.ForEachLight([&](Engine::LightSceneProxy* proxy) { scene.GetLightShadowInfo(proxy).Allocation = {}; });
            scene.RemoveLight(&directional);
            scene.RemoveLight(&point);
            scene.RemoveLight(&spot);
            scene.FlushPendingUpdates();
        }
    };

    REGISTER_RENDER_TEST("VolumeRenderTest", VolumeRenderTest);

    class VolumePipelineTest final : public TestBase
    {
    public:
        void Setup() override
        {
            GRHIApi = CreateTestRHIApi();
            GRHIApi->Init();
            Engine::GDistanceFieldMgr.Initialize();
            m_Renderer = std::dynamic_pointer_cast<Renderer::RenderModule>(Core::ModuleManager::Get().GetModule("Renderer"));
            if (!m_Renderer)
            {
                throw std::runtime_error("Volume preview test requires the Renderer module.");
            }
            m_Renderer->StartupModule();
        }

        void Teardown() override
        {
            m_Renderer->ShutdownModule();
            RHIPipelineStateCache::ClearAll();
            Engine::GDistanceFieldMgr.Release();
            GRHIApi->Shutdown();
            delete GRHIApi;
            GRHIApi = nullptr;
        }

        void Run() override
        {
            class PreviewTarget final : public Engine::RenderTarget
            {
            public:
                explicit PreviewTarget(const RHITextureDesc& desc) : Color(desc) { Color.InitRHIResource(); }
                RenderTexture* GetRenderTarget() override { return &Color; }
                RenderTexture Color;
            };
            RHITextureDesc desc{};
            desc.Width = Width;
            desc.Height = Height;
            desc.Format = ERHIFormat::R32G32B32A32_Float;
            desc.Usage = ERHITextureCreateFlag::ShaderResource | ERHITextureCreateFlag::RenderTarget |
                ERHITextureCreateFlag::UAV | ERHITextureCreateFlag::TransferDest | ERHITextureCreateFlag::TransferSrc;
            PreviewTarget target(desc);
            auto cube = Engine::StaticMeshAsset::CreateShape(Engine::EBasicMeshShape::Cube);
            Engine::StaticMeshComponent mesh;
            mesh.SetStaticMesh(cube->GetMesh());
            Engine::DirectionalLightComponent directional;
            directional.SetDirection({0.4f, -1.0f, -0.6f});
            directional.SetIntensity(2);
            Engine::PointLightComponent point;
            point.SetWorldLocation({1, 2, 1.5f});
            point.SetAttenuationRadius(8);
            Engine::SpotLightComponent spot;
            spot.SetWorldLocation({-2, 3, 2});
            spot.SetDirection({0.45f, -1, -0.4f});
            spot.SetAttenuationRadius(10);
            spot.SetInnerConeAngle(15);
            spot.SetOuterConeAngle(30);
            Engine::SkyLightComponent sky;
            auto& environments = Engine::EnvironmentLightResourceManager::Get();
            environments.ApplyEnvironment(sky, environments.GetDefaultEnvironment());
            std::unique_ptr<Engine::SceneInterface> scene(m_Renderer->AllocateScene());
            scene->AddPrimitive(&mesh);
            scene->AddLight(&directional);
            scene->AddLight(&point);
            scene->AddLight(&spot);
            scene->AddLight(&sky);
            scene->FlushPendingUpdates();

            Engine::Camera camera;
            camera.SetPosition({0, 0, 5});
            camera.SetTarget({0, 0, 0});
            camera.SetUp({0, 1, 0});
            camera.SetPerspective(Core::DegToRad(45.0f), float(Width) / Height, 0.1f, 60.0f);
            Engine::SceneView view;
            view.Viewport = {0, 0, Width, Height};
            view.CameraWorldPos = camera.GetPosition();
            view.CameraWorldDir = {0, 0, -1};
            view.NearClip = camera.GetNearPlane();
            view.FarClip = camera.GetFarPlane();
            view.RenderTargetWidth = float(Width);
            view.RenderTargetHeight = float(Height);
            view.ViewMatrix = camera.GetViewMatrix();
            view.ProjectionMatrix = camera.GetProjectionMatrix();
            view.ViewProjectionMatrix = camera.GetViewProjectionMatrix();
            view.InvViewProjectionMatrix = camera.GetInverseViewProjectionMatrix();
            view.Flags = Engine::VF_EnableShadows;

            const auto render = [&](const VolumeRenderSettings* settings)
            {
                auto drawView = view;
                if (settings)
                {
                    drawView.DrawExtensions.push_back(MakeVolumeDrawExtension(*settings, scene.get()));
                }
                Engine::SceneViewFamily family;
                family.Scene = scene.get();
                family.RenderTarget = &target;
                family.AddView(std::move(drawView));
                family.BuildAllViews();
                m_Renderer->BeginRender(&family);
                RenderGraphBuilder readback;
                const auto color = readback.RegisterExternalTexture("VolumePipeline.Output", &target.Color);
                return ExecuteAndRead(readback, color, target.Color);
            };

            const auto base = render(nullptr);
            VolumeRenderSettings settings;
            settings.Medium.Density = 0.42f;
            settings.Medium.Scattering = {0.5f, 0.6f, 0.8f};
            settings.AmbientRadiance = {};
            settings.LocalVolumes.emplace_back();
            settings.LocalVolumes.back().HalfExtent = {2, 2, 2};
            const auto density = CreateVolumePreviewDensityTexture();
            settings.LocalVolumes.back().DensityField.Mode = EVolumeDensityMode::Texture;
            settings.LocalVolumes.back().DensityField.Texture = density.get();
            const auto composite = render(&settings);
            settings.View = EVolumeView::Transmittance;
            const auto transmittance = render(&settings);
            settings.View = EVolumeView::IndirectLighting;
            const auto indirect = render(&settings);
            settings.bUseSkyEnvironment = false;
            const auto withoutIBL = render(&settings);
            if (!std::isfinite(composite[Center].X) || std::abs(composite[Center].X - base[Center].X) < 0.02f ||
                transmittance[Center].X <= transmittance[0].X + 0.05f || indirect[Center].X <= 0.02f)
            {
                throw std::runtime_error("Deferred volume preview did not compose, stop at geometry or receive the sky.");
            }
            RequireNear(withoutIBL[Center].X, 0.0f, "disabling sky input with zero ambient removes indirect fog light");
            RequireNear(render(nullptr)[Center].X, base[Center].X, "view without an extension preserves the original render");
            std::cout << "VOLUME_PIPELINE_TEST_OK baseline=" << base[Center].X << " composite=" << composite[Center].X <<
                " foregroundT=" << transmittance[Center].X << " skyT=" << transmittance[0].X <<
                " indirect=" << indirect[Center].X << std::endl;

            scene->RemovePrimitive(&mesh);
            scene->RemoveLight(&directional);
            scene->RemoveLight(&point);
            scene->RemoveLight(&spot);
            scene->RemoveLight(&sky);
            scene->FlushPendingUpdates();
            environments.ApplyEnvironment(sky, {});
            environments.ReleaseUnused();
        }

    private:
        std::shared_ptr<Renderer::RenderModule> m_Renderer;
    };

    REGISTER_RENDER_TEST("VolumePipelineTest", VolumePipelineTest);
}
