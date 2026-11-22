#include "TestBase.h"
#include "../Framework/Renderer/Private/Volume/AtmosphereRender.h"
#include "../Framework/Renderer/Private/Volume/VolumeRendering.h"
#include "../Framework/Renderer/Private/Volume/WaterRender.h"
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
#include "VulkanQueue.h"
#include <array>
#include <chrono>
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

        struct VolumeTestFrame
        {
            uint64_t Index = 0;
            float CameraX = 0;
            bool bNearCenter = false;
            VolumeRenderHistory* History = nullptr;
            VolumeRenderStats* Stats = nullptr;
            const AtmosphereRenderSettings* Atmosphere = nullptr;
            const WaterRenderSettings* Water = nullptr;
            float CameraY = 0.0f;
            float CameraSlope = 0.0f;
            bool bColorRamp = false;
            float BackgroundDepth = 1.0f;
        };

        // Native timestamps stay in the benchmark. Split physical queues need profiling on both queues.
        class VolumeBenchmarkTimer
        {
        public:
            void Initialize()
            {
                if (GShaderPlatform != ERHIShaderPlatform::Vulkan)
                {
                    return;
                }
                auto* graphics = static_cast<RHIVulkan::VulkanQueue*>(GRHIApi->GetQueue(EQueueType::Graphics));
                auto* compute = static_cast<RHIVulkan::VulkanQueue*>(GRHIApi->GetQueue(EQueueType::Compute));
                if (graphics->GetHandle() != compute->GetHandle())
                {
                    return;
                }
                const auto physical = graphics->GetDevice()->GetPhysicalDevice();
                uint32_t families = 0;
                vkGetPhysicalDeviceQueueFamilyProperties(physical, &families, nullptr);
                std::vector<VkQueueFamilyProperties> properties(families);
                vkGetPhysicalDeviceQueueFamilyProperties(physical, &families, properties.data());
                m_ValidBits = properties.at(graphics->GetFamilyIndex()).timestampValidBits;
                if (m_ValidBits == 0)
                {
                    return;
                }
                VkPhysicalDeviceProperties deviceProperties{};
                vkGetPhysicalDeviceProperties(physical, &deviceProperties);
                m_NanosecondsPerTick = deviceProperties.limits.timestampPeriod;
                m_Device = graphics->GetDevice()->GetHandle();
                VkQueryPoolCreateInfo desc{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
                desc.queryType = VK_QUERY_TYPE_TIMESTAMP;
                desc.queryCount = 2;
                Check(vkCreateQueryPool(m_Device, &desc, nullptr, &m_Pool));
                m_Queue = graphics->GetHandle();

                VkCommandPoolCreateInfo commands{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
                commands.queueFamilyIndex = graphics->GetFamilyIndex();
                Check(vkCreateCommandPool(m_Device, &commands, nullptr, &m_Markers));
                VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
                allocate.commandPool = m_Markers;
                allocate.commandBufferCount = uint32_t(m_Buffers.size());
                allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
                Check(vkAllocateCommandBuffers(m_Device, &allocate, m_Buffers.data()));
                for (uint32_t index = 0; index < uint32_t(m_Buffers.size()); ++index)
                {
                    VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
                    Check(vkCreateFence(m_Device, &fence, nullptr, &m_Fences[index]));
                    const auto buffer = m_Buffers[index];
                    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
                    Check(vkBeginCommandBuffer(buffer, &begin));
                    if (index == 0)
                    {
                        vkCmdResetQueryPool(buffer, m_Pool, 0, 2);
                    }
                    vkCmdWriteTimestamp(buffer, index == 0 ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                        m_Pool, index);
                    Check(vkEndCommandBuffer(buffer));
                }
            }

            ~VolumeBenchmarkTimer()
            {
                if (m_Device)
                {
                    for (size_t index = 0; index < m_Fences.size(); ++index)
                    {
                        if (m_InFlight[index])
                        {
                            vkWaitForFences(m_Device, 1, &m_Fences[index], VK_TRUE, UINT64_MAX);
                        }
                        vkDestroyFence(m_Device, m_Fences[index], nullptr);
                    }
                    vkDestroyCommandPool(m_Device, m_Markers, nullptr);
                    vkDestroyQueryPool(m_Device, m_Pool, nullptr);
                }
            }

            bool IsAvailable() const { return m_Pool != VK_NULL_HANDLE; }

            void Mark(uint32_t index)
            {
                if (!IsAvailable())
                {
                    return;
                }
                Check(vkResetFences(m_Device, 1, &m_Fences[index]));
                VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
                submit.commandBufferCount = 1;
                submit.pCommandBuffers = &m_Buffers[index];
                // The standalone benchmark submits from one CPU thread.
                Check(vkQueueSubmit(m_Queue, 1, &submit, m_Fences[index]));
                m_InFlight[index] = true;
            }

            double ReadMilliseconds()
            {
                Check(vkWaitForFences(m_Device, uint32_t(m_Fences.size()), m_Fences.data(), VK_TRUE, UINT64_MAX));
                m_InFlight = {};
                std::array<uint64_t, 2> times{};
                Check(vkGetQueryPoolResults(m_Device, m_Pool, 0, 2, sizeof(times), times.data(), sizeof(uint64_t), VK_QUERY_RESULT_64_BIT));
                const uint64_t mask = m_ValidBits < 64 ? (uint64_t(1) << m_ValidBits) - 1 : UINT64_MAX;
                return double((times[1] - times[0]) & mask) * m_NanosecondsPerTick / 1e6;
            }

        private:
            static void Check(VkResult result)
            {
                if (result != VK_SUCCESS)
                {
                    throw std::runtime_error("Vulkan volume timestamp query failed: " + std::to_string(result));
                }
            }

            VkQueue m_Queue = VK_NULL_HANDLE;
            VkDevice m_Device = VK_NULL_HANDLE;
            VkQueryPool m_Pool = VK_NULL_HANDLE;
            VkCommandPool m_Markers = VK_NULL_HANDLE;
            std::array<VkCommandBuffer, 2> m_Buffers{};
            std::array<VkFence, 2> m_Fences{};
            std::array<bool, 2> m_InFlight{};
            uint32_t m_ValidBits = 0;
            float m_NanosecondsPerTick = 0;
        };

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
            double timeSeconds = 0, const VolumeTestFrame& frame = {})
        {
            Engine::Camera camera;
            camera.SetPosition({frame.CameraX, frame.CameraY, 0});
            camera.SetTarget({frame.CameraX, frame.CameraY + frame.CameraSlope, -1});
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
            view.FrameIndex = frame.Index;
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
            if (frame.bColorRamp)
            {
                for (uint32_t row = 0; row < Height; ++row)
                {
                    std::fill_n(background.begin() + row * Width, Width, Core::Float4{float(row) / Height, 0.4f, 0.2f, 1.0f});
                }
            }
            const auto forward = Core::Normalize(Core::Float3{0, frame.CameraSlope, -1});
            const auto depthAt = [&](float distance) { return camera.WorldToNdc(camera.GetPosition() + forward * distance).Z; };
            std::array<float, Width * Height> sceneDepth;
            sceneDepth.fill(surfaceDistance > 0.0f ? depthAt(surfaceDistance) : frame.BackgroundDepth);
            if (frame.bNearCenter)
            {
                for (uint32_t row = 0; row < Height; ++row)
                {
                    sceneDepth[row * Width + Width / 2] = depthAt(2);
                }
            }
            graph.AddPass("VolumeTestUpload", VolumeTestUpload::GetMetaData(), upload, EPassFlag::Graphic, [&](RHICommandListBase& cmd)
            {
                const auto region = RHIUpdateTextureRegion::Create2DRegion(Width, Height);
                GRHIApi->UpdateTexture(cmd, upload->Color->GetRHITexture(), background.data(), region);
                GRHIApi->UpdateTexture(cmd, upload->Depth->GetRHITexture(), sceneDepth.data(), region);
            });
            RenderGraphTextureRef effectiveDepth = upload->Depth;
            if ((frame.Atmosphere && frame.Atmosphere->bEnabled) || (frame.Water && frame.Water->bEnabled))
            {
                const auto atmosphereSettings = frame.Atmosphere ? *frame.Atmosphere : AtmosphereRenderSettings{};
                const auto atmosphere = PrepareAtmosphere(graph, atmosphereSettings, scene);
                if (frame.Water)
                {
                    effectiveDepth = AddWaterPass(graph, view, upload->Color, effectiveDepth, *frame.Water, atmosphere, scene);
                }
                AddAtmospherePass(graph, view, upload->Color, effectiveDepth, atmosphereSettings, atmosphere);
            }
            AddVolumePasses(graph, view, effectiveDepth, upload->Color, settings, scene, frame.History, frame.Stats);

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

    class VolumeRenderTest : public TestBase
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

            VolumeRenderHistory lightHistory;
            densityTest.View = EVolumeView::HistoryWeight;
            densityTest.bShadows = false;
            const auto historyFrame = [&](uint64_t index)
            {
                return Render(densityTest, 6, &scene, 0, {.Index = index, .History = &lightHistory})[Center].X;
            };
            RequireNear(historyFrame(0), 0, "lighting history starts empty");
            RequireNear(historyFrame(1), densityTest.HistoryWeight, "stable lights permit history");
            spot.SetIntensity(spot.GetIntensity() * 2.0f);
            scene.FlushPendingUpdates();
            RequireNear(historyFrame(2), 0, "a scene light edit resets volume lighting history");

            // The fixture textures are borrowed sampler inputs, outside the scene allocator.
            scene.ForEachLight([&](Engine::LightSceneProxy* proxy) { scene.GetLightShadowInfo(proxy).Allocation = {}; });
            scene.RemoveLight(&directional);
            scene.RemoveLight(&point);
            scene.RemoveLight(&spot);
            scene.FlushPendingUpdates();

            TestVolumeFiltering();
        }

    private:
        static void TestVolumeFiltering()
        {
            VolumeRenderSettings settings;
            settings.Medium.Density = 0.2f;
            settings.Medium.Absorption = {1, 1, 1};
            settings.Medium.Scattering = {};
            settings.MaxDistance = 20;
            settings.View = EVolumeView::Transmittance;
            const auto reference = Render(settings, 10, nullptr, 0, {.bNearCenter = true});
            settings.bSpatialReconstruction = true;
            VolumeRenderStats stats;
            const auto spatial = Render(settings, 10, nullptr, 0, {.bNearCenter = true, .Stats = &stats});
            RequireNear(spatial[Center].X, reference[Center].X, "depth reconstruction preserves an unsampled foreground stripe");
            RequireNear(spatial[Center + 1].X, reference[Center + 1].X, "foreground does not bleed into background");
            if (stats.IntegrationSize != Core::UInt2{(Width + 1) / 2, (Height + 1) / 2})
            {
                throw std::runtime_error("Spatial reconstruction must dispatch one primary integration ray per 2x2 region.");
            }

            VolumeRenderHistory history;
            settings.View = EVolumeView::HistoryWeight;
            const auto renderFrame = [&](uint64_t index, float cameraX = 0.0f, bool bNearCenter = false)
            {
                return Render(settings, 10, nullptr, 0, {index, cameraX, bNearCenter, &history, &stats});
            };
            RequireNear(renderFrame(0)[Center].X, 0, "first frame does not reuse history");
            RequireNear(renderFrame(1)[Center].X, settings.HistoryWeight, "stationary scene reuses its history");
            RequireNear(renderFrame(2, 0.24f)[Center].X, settings.HistoryWeight, "camera translation reprojects the previous depth plane");
            const auto disoccluded = renderFrame(3, 0.24f, true);
            RequireNear(disoccluded[Center].X, 0, "new foreground rejects previous background");
            RequireNear(disoccluded[Center - 2].X, settings.HistoryWeight, "surrounding history remains valid");
            settings.Medium.Density = 0.6f;
            RequireNear(renderFrame(4, 0.24f, true)[Center - 2].X, 0, "medium edit invalidates history immediately");
            RequireNear(renderFrame(5, 0.24f, true)[Center - 2].X, settings.HistoryWeight, "new medium can start accumulating");
            RequireNear(renderFrame(6, 10.0f)[Center].X, 0, "camera jump invalidates history");
            settings.SceneRevision++;
            RequireNear(renderFrame(7, 10.0f)[Center].X, 0, "external scene revision invalidates history");

            settings.View = EVolumeView::Transmittance;
            settings.Medium.Density = 0.08f;
            settings.bJitter = true;
            auto& local = settings.LocalVolumes.emplace_back();
            local.Position = {0, 0, -4};
            local.HalfExtent = {3, 2, 3};
            local.Medium.Density = 0.4;
            local.DensityField.Mode = EVolumeDensityMode::Noise;
            local.DensityField.Frequency = 2.2f;
            history.Invalidate();
            Pixels previousRaw{}, previousFiltered{};
            double rawDifference = 0, filteredDifference = 0;
            for (uint64_t i = 0; i < 16; ++i)
            {
                const auto raw = Render(settings, 10, nullptr, 0, {.Index = i});
                const auto filtered = renderFrame(i);
                if (i >= 4)
                {
                    for (size_t pixel = 0; pixel < raw.size(); ++pixel)
                    {
                        const auto dr = raw[pixel].X - previousRaw[pixel].X;
                        const auto df = filtered[pixel].X - previousFiltered[pixel].X;
                        rawDifference += dr * dr;
                        filteredDifference += df * df;
                    }
                }
                previousRaw = raw;
                previousFiltered = filtered;
            }
            const double rawRms = std::sqrt(rawDifference / (12 * Width * Height));
            const double filteredRms = std::sqrt(filteredDifference / (12 * Width * Height));
            if (rawRms < 0.005 || filteredRms > rawRms * 0.8)
            {
                throw std::runtime_error("Temporal accumulation must reduce jitter in a stationary nonuniform volume.");
            }
            std::cout << "VOLUME_FILTER_TEST_OK edgeT=" << spatial[Center].X << " rms=" << rawRms << "->" <<
                filteredRms << std::endl;
        }
    };

    REGISTER_RENDER_TEST("VolumeRenderTest", VolumeRenderTest);

    class VolumeAtmosphereTest final : public VolumeRenderTest
    {
    public:
        void Run() override
        {
            VolumeRenderSettings volume;
            volume.bEnabled = false;
            AtmosphereRenderSettings atmosphere;
            atmosphere.bEnabled = true;
            constexpr size_t upper = Width / 2;
            const auto render = [&](float depth) { return Render(volume, depth, nullptr, 0, {.Atmosphere = &atmosphere}); };
            const auto day = render(0);
            if (!(day[upper].Z > day[upper].X * 1.5f && day[upper].Z > 0.025f))
            {
                throw std::runtime_error("Daylight over the horizon must show blue Rayleigh scattering.");
            }
            const auto roundedSky = Render(volume, 0, nullptr, 0, {.Atmosphere = &atmosphere, .BackgroundDepth = std::nextafter(1.0f, 0.0f)});
            RequireNear(roundedSky[upper].Z, day[upper].Z, "far-plane skybox rounding preserves sky shading");
            atmosphere.SunDirection = {0, 0.03f, -1};
            const auto dusk = render(0);
            if (!(dusk[Center].X > 0.02f && dusk[Center].X > 1.4f * dusk[Center].Z))
            {
                throw std::runtime_error("Low sun should redden the horizon.");
            }
            atmosphere.SunDirection = {0, -0.45f, -1};
            const auto night = render(0);
            RequireNear(night[upper].Y, 0.0f, "planet occludes low night sun");

            atmosphere.SunDirection = {0, 0.7f, -0.7f};
            atmosphere.WorldToKm = 0.4f;
            atmosphere.bAerialPerspective = false;
            RequireNear(render(8)[Center].X, 0.8f, "opaque color survives with aerial perspective disabled");
            atmosphere.bAerialPerspective = true;
            atmosphere.View = EAtmosphereView::Transmittance;
            const float nearBlue = render(2)[Center].Z;
            const float farBlue = render(8)[Center].Z;
            if (!(nearBlue > farBlue + 0.04f && farBlue > 0.0f))
            {
                throw std::runtime_error("Far geometry must receive more atmospheric attenuation.");
            }
            atmosphere.bEnabled = false;
            RequireNear(render(0)[upper].X, 0.8f, "disabled atmosphere preserves scene image");
            std::cout << "VOLUME_ATMOSPHERE_TEST_OK daylightBlue=" << day[upper].Z << " duskRed=" << dusk[Center].X
                << " duskBlue=" << dusk[Center].Z << " nearT=" << nearBlue << " farT=" << farBlue << std::endl;
        }
    };

    REGISTER_RENDER_TEST("VolumeAtmosphereTest", VolumeAtmosphereTest);

    class VolumeWaterTest final : public VolumeRenderTest
    {
    public:
        void Run() override
        {
            VolumeRenderSettings volume;
            volume.bEnabled = false;
            WaterRenderSettings water;
            water.bEnabled = true;
            water.Position = {0, 0, 0};
            water.WaveAmplitude = 0;
            water.HalfExtent = {10, 12};
            water.MaxThickness = 8;
            water.RefractionStrength = 0;
            water.View = EWaterView::Thickness;
            const auto render = [&](float depth, float slope = -0.5f, bool bRamp = false, Engine::SceneInterface* scene = nullptr)
            {
                return Render(volume, depth, scene, 0, {.Water = &water, .CameraY = 2, .CameraSlope = slope, .bColorRamp = bRamp});
            };
            RequireNear(render(2)[Center].X, 0.8f, "opaque foreground prevents water from shading that pixel");
            const float shallow = render(6)[Center].X;
            const float deep = render(10)[Center].X;
            RequireNear(shallow, (6.0f - std::sqrt(20.0f)) / 8.0f, "shallow thickness stops at the opaque bed");
            RequireNear(deep, (10.0f - std::sqrt(20.0f)) / 8.0f, "deep thickness follows the opaque bed");

            water.View = EWaterView::Reflection;
            const float steepReflection = render(0, -1.5f)[Center].Z;
            const float grazingReflection = render(0, -0.26f)[Center].Z;
            if (!(steepReflection > 0.0f && grazingReflection > steepReflection * 2.0f))
            {
                throw std::runtime_error("Water Fresnel response must increase at grazing incidence.");
            }

            water.View = EWaterView::Refraction;
            const float shallowRed = render(6)[Center].X;
            const float deepRed = render(10)[Center].X;
            if (!(shallowRed > deepRed + 0.1f))
            {
                throw std::runtime_error("Longer water paths must absorb more red light.");
            }
            const auto absorption = water.Absorption;
            water.Absorption = {};
            const float straight = render(9, -0.5f, true)[Center].X;
            water.RefractionStrength = 1;
            const float refracted = render(9, -0.5f, true)[Center].X;
            if (std::abs(refracted - straight) < 0.04f)
            {
                throw std::runtime_error("Refracted rays did not sample a different submerged location.");
            }
            water.Absorption = absorption;

            water.WaveAmplitude = 0.15f;
            water.View = EWaterView::Normals;
            const auto firstNormal = render(8)[Center];
            water.TimeSeconds = 1.0f;
            const auto nextNormal = render(8)[Center];
            if (std::abs(firstNormal.X - nextNormal.X) + std::abs(firstNormal.Z - nextNormal.Z) < 0.04f)
            {
                throw std::runtime_error("Water surface normal must follow animated waves.");
            }

            water.WaveAmplitude = 0;
            water.View = EWaterView::Composite;
            volume.bEnabled = true;
            volume.View = EVolumeView::Transmittance;
            volume.Medium.Density = 0.2f;
            volume.Medium.Absorption = {1, 1, 1};
            volume.Medium.Scattering = {};
            const float beforeWater = render(10)[Center].X;
            water.bEnabled = false;
            const float beforeBed = render(10)[Center].X;
            RequireNear(beforeWater, std::exp(-0.2f * (std::sqrt(20.0f) - 0.1f)), "fog terminates at the water interface");
            RequireNear(beforeBed, std::exp(-0.2f * 9.9f), "disabled water restores opaque depth");
            std::cout << "VOLUME_WATER_TEST_OK shallow=" << shallow << " deep=" << deep << " fresnel=" << steepReflection <<
                "->" << grazingReflection << " refraction=" << straight << "->" << refracted <<
                " fogT=" << beforeWater << " bedT=" << beforeBed << std::endl;
        }
    };

    REGISTER_RENDER_TEST("VolumeWaterTest", VolumeWaterTest);

    template<bool bOptimized, uint32_t WidthInPixels = 960, uint32_t HeightInPixels = 540>
    class VolumeBenchmark final : public VolumeRenderTest
    {
    public:
        void Run() override
        {
            constexpr uint32_t width = WidthInPixels, height = HeightInPixels, frames = 80, warmupFrames = 8;
            Engine::Camera camera;
            camera.SetPosition({0, 0, 0});
            camera.SetTarget({0, 0, -1});
            camera.SetUp({0, 1, 0});
            camera.SetPerspective(Core::DegToRad(45.0f), float(width) / height, 0.1f, 100.0f);
            Engine::SceneView view;
            view.ViewMatrix = camera.GetViewMatrix();
            view.ProjectionMatrix = camera.GetProjectionMatrix();
            view.InvViewProjectionMatrix = camera.GetInverseViewProjectionMatrix();
            view.NearClip = 0.1f;
            view.FarClip = 100.0f;
            view.CameraWorldPos = camera.GetPosition();
            view.Viewport = {0, 0, width, height};

            RHITextureDesc desc{};
            desc.Width = width;
            desc.Height = height;
            desc.Usage = ERHITextureCreateFlag::ShaderResource | ERHITextureCreateFlag::TransferDest;
            desc.Format = ERHIFormat::R32G32B32A32_Float;
            RenderTexture color(desc);
            color.InitRHIResource();
            desc.Format = ERHIFormat::R32_Float;
            RenderTexture depth(desc);
            depth.InitRHIResource();
            const auto wait = [&]()
            {
                const auto fence = color.GetTracker().GetLastAccessFence();
                GRHIApi->GetQueue(fence.QueueType)->WaitValue(fence.Value);
            };

            {
                const std::vector<Core::Float4> background(width * height, {0.4f, 0.3f, 0.2f, 1.0f});
                std::vector<float> depths(width * height, 1.0f);
                const float plane = camera.WorldToNdc({0, 0, -8}).Z;
                for (uint32_t row = height / 4; row < 3 * height / 4; ++row)
                {
                    std::fill_n(depths.begin() + row * width + width / 4, width / 2, plane);
                }
                RenderGraphBuilder graph;
                auto* upload = graph.AllocateParameter<VolumeTestUpload>();
                upload->Color = graph.RegisterExternalTexture("VolumeBenchmark.Color", &color);
                upload->Depth = graph.RegisterExternalTexture("VolumeBenchmark.Depth", &depth);
                graph.AddPass("VolumeBenchmarkUpload", VolumeTestUpload::GetMetaData(), upload, EPassFlag::Graphic, [&](RHICommandListBase& cmd)
                {
                    const auto region = RHIUpdateTextureRegion::Create2DRegion(width, height);
                    GRHIApi->UpdateTexture(cmd, color.GetRHI(), background.data(), region);
                    GRHIApi->UpdateTexture(cmd, depth.GetRHI(), depths.data(), region);
                });
                graph.Execute();
                wait();
            }

            VolumeRenderSettings settings;
            settings.View = EVolumeView::Scattering;
            settings.MaxDistance = 25;
            settings.bSpatialReconstruction = bOptimized;
            settings.bJitter = bOptimized;
            auto& fog = settings.LocalVolumes.emplace_back();
            fog.Position = {0, 0, -10};
            fog.HalfExtent = {12, 8, 7};
            fog.Medium.Density = 0.4f;
            fog.DensityField.Mode = EVolumeDensityMode::Noise;
            VolumeRenderHistory history;
            VolumeRenderStats stats;
            VolumeBenchmarkTimer timer;
            timer.Initialize();
            double querySumMs = 0;
            auto start = std::chrono::steady_clock::now();
            for (uint32_t i = 0; i < frames; ++i)
            {
                if (i == warmupFrames)
                {
                    start = std::chrono::steady_clock::now();
                }
                view.FrameIndex = i;
                RenderGraphBuilder graph;
                const auto colorTarget = graph.RegisterExternalTexture("VolumeBenchmark.Color", &color);
                const auto sceneDepth = graph.RegisterExternalTexture("VolumeBenchmark.Depth", &depth);
                AddVolumePasses(graph, view, sceneDepth, colorTarget, settings, nullptr, bOptimized ? &history : nullptr, &stats);
                if (i >= warmupFrames)
                {
                    timer.Mark(0);
                }
                graph.Execute();
                if (i >= warmupFrames && timer.IsAvailable())
                {
                    timer.Mark(1);
                    querySumMs += timer.ReadMilliseconds();
                }
                else
                {
                    wait();
                }
            }
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            std::cout << "VOLUME_BENCHMARK_OK optimized=" << bOptimized << " size=" << width << 'x' << height << " frames=" << frames - warmupFrames
                << " primaryRays=" << stats.IntegrationSize.X * stats.IntegrationSize.Y
                << " transientBytes=" << stats.TransientBytes << " historyBytes=" << stats.HistoryBytes
                << " completedFrameMs=" << ms / (frames - warmupFrames);
            if (timer.IsAvailable())
            {
                std::cout << " vulkanQueueSpanMs=" << querySumMs / (frames - warmupFrames);
            }
            std::cout << std::endl;
        }
    };

    using VolumeBenchmarkReference = VolumeBenchmark<false>;
    using VolumeBenchmarkOptimized = VolumeBenchmark<true>;
    using VolumeBenchmarkReference1080p = VolumeBenchmark<false, 1920, 1080>;
    using VolumeBenchmarkOptimized1080p = VolumeBenchmark<true, 1920, 1080>;
    REGISTER_RENDER_TEST("VolumeBenchmarkReference", VolumeBenchmarkReference);
    REGISTER_RENDER_TEST("VolumeBenchmarkOptimized", VolumeBenchmarkOptimized);
    REGISTER_RENDER_TEST("VolumeBenchmarkReference1080p", VolumeBenchmarkReference1080p);
    REGISTER_RENDER_TEST("VolumeBenchmarkOptimized1080p", VolumeBenchmarkOptimized1080p);

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

            const auto render = [&](const VolumeRenderSettings* settings, VolumeRenderHistory* history = nullptr, uint64_t frame = 0,
                const AtmosphereRenderSettings* atmosphere = nullptr, const WaterRenderSettings* water = nullptr)
            {
                auto drawView = view;
                drawView.FrameIndex = frame;
                if (atmosphere)
                {
                    drawView.DrawExtensions.push_back({[&, atmosphere, water, settings, history](RenderGraphBuilder& graph,
                        const Engine::SceneView& draw, RenderGraphTextureRef color, RenderGraphTextureRef depth)
                    {
                        const auto lighting = PrepareAtmosphere(graph, *atmosphere, scene.get());
                        if (water)
                        {
                            depth = AddWaterPass(graph, draw, color, depth, *water, lighting, scene.get());
                        }
                        AddAtmospherePass(graph, draw, color, depth, *atmosphere, lighting);
                        if (settings)
                        {
                            AddVolumePasses(graph, draw, depth, color, *settings, scene.get(), history);
                        }
                    }});
                }
                else if (settings)
                {
                    drawView.DrawExtensions.push_back(MakeVolumeDrawExtension(*settings, scene.get(), history));
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

            VolumeRenderHistory history;
            settings.bUseSkyEnvironment = true;
            settings.bSpatialReconstruction = true;
            settings.View = EVolumeView::HistoryWeight;
            RequireNear(render(&settings, &history, 0)[Center].X, 0, "deferred preview starts without history");
            const float historyWeight = render(&settings, &history, 1)[Center].X;
            RequireNear(historyWeight, settings.HistoryWeight, "deferred view accumulates at unchanged opaque depth");
            settings.View = EVolumeView::Composite;
            const auto optimized = render(&settings, &history, 2);
            if (!std::isfinite(optimized[Center].X) || std::abs(optimized[Center].X - composite[Center].X) > 0.02f)
            {
                throw std::runtime_error("Depth-aware deferred volume diverged from the reference at the test surface.");
            }
            history.Release();
            settings.View = EVolumeView::HistoryWeight;
            RequireNear(render(&settings, &history, 3)[Center].X, 0, "reopened deferred preview starts with clean history");

            AtmosphereRenderSettings atmosphere;
            atmosphere.bEnabled = true;
            WaterRenderSettings water;
            water.bEnabled = true;
            water.Position.Y = -0.5f;
            water.WaveAmplitude = 0;
            water.Position.Z = 2;
            const size_t lowerEdge = (Height - 1) * Width + 1;
            const size_t upperEdge = 1;
            const auto atmosphereOnly = render(nullptr, nullptr, 0, &atmosphere);
            const auto atmosphereAndWater = render(nullptr, nullptr, 0, &atmosphere, &water);
            RequireNear(atmosphereAndWater[upperEdge].Z, atmosphereOnly[upperEdge].Z,
                "water keeps sky unchanged where the ray misses the interface");
            if (!std::isfinite(atmosphereAndWater[lowerEdge].Z) ||
                std::abs(atmosphereAndWater[lowerEdge].Z - atmosphereOnly[lowerEdge].Z) < 0.02f)
            {
                throw std::runtime_error("The deferred water surface must reflect the atmosphere and compose with the scene.");
            }
            settings.View = EVolumeView::Composite;
            const auto layered = render(&settings, nullptr, 0, &atmosphere, &water);
            if (!std::isfinite(layered[lowerEdge].Z) || std::abs(layered[lowerEdge].Z - atmosphereAndWater[lowerEdge].Z) < 0.01f)
            {
                throw std::runtime_error("Atmosphere, water, and volume must participate in the same deferred preview.");
            }
            std::cout << "VOLUME_PIPELINE_TEST_OK baseline=" << base[Center].X << " composite=" << composite[Center].X <<
                " foregroundT=" << transmittance[Center].X << " skyT=" << transmittance[0].X <<
                " indirect=" << indirect[Center].X << " historyWeight=" << historyWeight <<
                " optimizedComposite=" << optimized[Center].X <<
                " skyWaterFog=" << atmosphereOnly[lowerEdge].Z << "->" << atmosphereAndWater[lowerEdge].Z <<
                "->" << layered[lowerEdge].Z << std::endl;

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
