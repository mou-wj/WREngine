#include "TestBase.h"
#include "Camera.h"
#include "DistanceFieldMgr.h"
#include "EnvironmentLightResourceManager.h"
#include "LightComponent.h"
#include "MaterialAsset.h"
#include "MaterialShaderParameter.h"
#include "RenderGraphBuilder.h"
#include "RenderModule.h"
#include "RenderResource.h"
#include "RHIApi.h"
#include "RHIPipelineStateCache.h"
#include "SceneInterface.h"
#include "SceneView.h"
#include "Shape.h"
#include "StaticMeshAsset.h"
#include "StaticMeshComponent.h"
#include "TransparencyRender.h"
#include "../Framework/Renderer/Private/Volume/AtmosphereRender.h"
#include "../Framework/Renderer/Private/Volume/WaterRender.h"
#include "../Framework/Renderer/Private/Volume/VolumeRendering.h"
#include "TextureAsset.h"
#include "Viewport.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace Test
{
    namespace
    {
        using namespace Engine;
        using namespace RenderCore;
        using namespace RHI;
        constexpr uint32_t Width = 384;
        constexpr uint32_t Height = 128;
        using Pixels = std::vector<Core::Float4>;

        class MaterialTestTarget final : public RenderTarget
        {
        public:
            MaterialTestTarget() : m_Color(MakeDesc()) { m_Color.InitRHIResource(); }
            RenderTexture* GetRenderTarget() override { return &m_Color; }

        private:
            static RHITextureDesc MakeDesc()
            {
                RHITextureDesc desc{};
                desc.Width = Width;
                desc.Height = Height;
                desc.Format = ERHIFormat::R32G32B32A32_Float;
                desc.Usage = ERHITextureCreateFlag::RenderTarget | ERHITextureCreateFlag::ShaderResource |
                    ERHITextureCreateFlag::UAV | ERHITextureCreateFlag::TransferSrc | ERHITextureCreateFlag::TransferDest;
                return desc;
            }

            RenderTexture m_Color;
        };

        void Require(bool condition, const char* message)
        {
            if (!condition)
            {
                throw std::runtime_error(message);
            }
        }

        float CenterDifference(const Pixels& a, const Pixels& b)
        {
            float difference = 0;
            for (uint32_t y = Height / 2 - 17; y < Height / 2 + 17; ++y)
            {
                for (uint32_t x = Width / 2 - 17; x < Width / 2 + 17; ++x)
                {
                    const auto index = y * Width + x;
                    const auto first = a[index], second = b[index];
                    Require(std::isfinite(first.X) && std::isfinite(first.Y) && std::isfinite(first.Z) &&
                        std::isfinite(second.X) && std::isfinite(second.Y) && std::isfinite(second.Z),
                        "Material lighting contains a non-finite RGB value");
                    difference = std::max({difference, std::abs(first.X - second.X), std::abs(first.Y - second.Y),
                        std::abs(first.Z - second.Z)});
                }
            }
            return difference;
        }

        std::shared_ptr<MaterialAsset> MakeMaterial(EShadingModel model)
        {
            auto material = std::make_unique<Material>();
            material->SetShadingModel(model);
            material->SetTwoSided(true);
            material->SetParameter("BaseColor", std::array<float, 4>{0.55f, 0.36f, 0.26f, 1.0f});
            material->SetParameter("Metallic", 0.0f);
            material->SetParameter("Roughness", 0.36f);
            return MaterialAsset::Create("", GetShadingModelName(model), std::move(material));
        }

        void SaveComparison(const Pixels& pixels, const char* path = "MaterialShadingModels.ppm")
        {
            std::ofstream file(path, std::ios::binary);
            file << "P6\n" << Width << " " << Height << "\n255\n";
            for (const auto& pixel : pixels)
            {
                for (float channel : {pixel.X, pixel.Y, pixel.Z})
                {
                    const float radiance = std::max(0.0f, channel);
                    const float display = std::pow(radiance / (1.0f + radiance), 1.0f / 2.2f);
                    file.put(static_cast<char>(uint8_t(display * 255.0f + 0.5f)));
                }
            }
        }
    }

    class MaterialSceneTestBase : public TestBase
    {
    public:
        ~MaterialSceneTestBase() override
        {
            if (m_bReady)
            {
                Teardown();
            }
        }

        void Setup() override
        {
            GRHIApi = CreateTestRHIApi();
            Require(GRHIApi->Init(), "Cannot initialize material comparison RHI");
            GDistanceFieldMgr.Initialize();
            m_Renderer = std::dynamic_pointer_cast<Renderer::RenderModule>(Core::ModuleManager::Get().GetModule("Renderer"));
            Require(bool(m_Renderer), "Material comparison requires Renderer");
            m_Renderer->StartupModule();
            m_bReady = true;
        }

        void Teardown() override
        {
            m_bReady = false;
            m_Renderer->ShutdownModule();
            RHIPipelineStateCache::ClearAll();
            GDistanceFieldMgr.Release();
            GRHIApi->Shutdown();
            delete GRHIApi;
            GRHIApi = nullptr;
        }

    protected:
        std::shared_ptr<Renderer::RenderModule> m_Renderer;

    private:
        bool m_bReady = false;
    };

    class MaterialShadingModelTest final : public MaterialSceneTestBase
    {
    public:
        void Run() override
        {
            MaterialTestTarget target;
            const auto sphere = StaticMeshAsset::CreateShape(EBasicMeshShape::Sphere);
            const auto lit = MakeMaterial(EShadingModel::Lit);
            const auto skin = MakeMaterial(EShadingModel::Subsurface);
            const auto lacquer = MakeMaterial(EShadingModel::ClearCoat);
            const auto brushed = MakeMaterial(EShadingModel::Anisotropic);
            const auto fabric = MakeMaterial(EShadingModel::Cloth);
            StaticMeshComponent mesh;
            mesh.SetStaticMesh(sphere->GetMesh());
            mesh.SetMaterial(0, lit->GetMaterial());
            DirectionalLightComponent directional;
            directional.SetDirection({0.80f, -0.30f, -0.50f});
            directional.SetIntensity(3);
            directional.SetCastShadow(false);
            PointLightComponent point;
            point.SetWorldLocation({-2, 1, 2});
            point.SetIntensity(12);
            point.SetAttenuationRadius(8);
            point.SetCastShadow(false);
            SpotLightComponent spot;
            spot.SetWorldLocation({2, 1, 2});
            spot.SetDirection({-2, -1, -2});
            spot.SetIntensity(16);
            spot.SetAttenuationRadius(9);
            spot.SetInnerConeAngle(18);
            spot.SetOuterConeAngle(34);
            spot.SetCastShadow(false);
            SkyLightComponent sky;
            auto& environments = EnvironmentLightResourceManager::Get();
            RHITextureDesc environmentDesc{};
            environmentDesc.Width = 16;
            environmentDesc.Height = 8;
            environmentDesc.Format = ERHIFormat::R8G8B8A8_UNorm;
            environmentDesc.Usage = ERHITextureCreateFlag::ShaderResource | ERHITextureCreateFlag::TransferDest;
            auto environmentImage = std::make_shared<RenderTexture>(environmentDesc);
            environmentImage->InitRHIResource();
            std::array<std::array<uint8_t, 4>, 16 * 8> texels;
            for (uint32_t y = 0; y < 8; ++y)
            {
                for (uint32_t x = 0; x < 16; ++x)
                {
                    texels[y * 16 + x] = x < 8 ? std::array<uint8_t, 4>{224, 180, 128, 255} :
                        std::array<uint8_t, 4>{28, 40, 72, 255};
                }
            }
            environmentImage->UploadData(texels.data(), 0, 0);
            auto environmentAsset = TextureAsset::Create("ShadingModelEnvironment", environmentImage);
            environments.ApplyEnvironment(sky, environments.Build(environmentAsset));
            std::unique_ptr<SceneInterface> scene(m_Renderer->AllocateScene());
            scene->AddPrimitive(&mesh);
            for (LightComponent* light : std::array<LightComponent*, 4>{&directional, &point, &spot, &sky})
            {
                scene->AddLight(light);
            }

            Camera camera;
            camera.SetPosition({0, 0, 5});
            camera.SetTarget({0, 0, 0});
            camera.SetUp({0, 1, 0});
            camera.SetPerspective(Core::DegToRad(39.0f), float(Width) / Height, 0.1f, 40);
            SceneView view;
            view.Viewport = {0, 0, Width, Height};
            view.RenderTargetWidth = float(Width);
            view.RenderTargetHeight = float(Height);
            view.CameraWorldPos = camera.GetPosition();
            view.CameraWorldDir = {0, 0, -1};
            view.NearClip = camera.GetNearPlane();
            view.FarClip = camera.GetFarPlane();
            view.ViewMatrix = camera.GetViewMatrix();
            view.ProjectionMatrix = camera.GetProjectionMatrix();
            view.ViewProjectionMatrix = camera.GetViewProjectionMatrix();
            view.InvViewProjectionMatrix = camera.GetInverseViewProjectionMatrix();
            view.Flags = VF_EnableShadows;
            const auto render = [&]()
            {
                scene->FlushPendingUpdates();
                SceneViewFamily family;
                family.Scene = scene.get();
                family.RenderTarget = &target;
                family.AddView(view);
                family.BuildAllViews();
                m_Renderer->BeginRender(&family);
                Pixels pixels(Width * Height);
                target.GetRenderTarget()->ReadData(pixels.data(), 0, 0);
                return pixels;
            };

            const std::array<LightComponent*, 4> lights{&directional, &point, &spot, &sky};
            for (auto* light : lights)
            {
                for (auto* candidate : lights)
                {
                    candidate->SetEnabled(candidate == light);
                }
                mesh.SetMaterial(0, lit->GetMaterial());
                const auto defaultLit = render();
                mesh.SetMaterial(0, skin->GetMaterial());
                const auto wrapped = render();
                const float difference = CenterDifference(defaultLit, wrapped);
                Require(difference > 0.002f, "Subsurface had no observable effect under one of the test lights");

                auto* instance = mesh.GetOrCreateMaterialInstance(0);
                instance->SetParameter("SubsurfaceStrength", 0.0f);
                RenderGraphBuilder parametersGraph;
                MaterialShaderParameters parameters{};
                BuildShaderParameters(instance->GetRenderProxy(), parametersGraph, parameters);
                Require(parameters.UniformData.ShadingModelData.X == 0.0f, "Instance override was not bound to the material");
                const auto noStrength = render();
                instance->ResetParameter("SubsurfaceStrength", EMaterialParameterType::Scalar);
                instance->SetParameter("SubsurfaceRadius", 0.0f);
                const float overrideDifference = CenterDifference(wrapped, noStrength);
                constexpr auto center = Height / 2 * Width + Width / 2;
                std::cout << " light=" << int(light->GetLightType()) << " difference=" << difference
                    << " override=" << overrideDifference << " center=" << defaultLit[center].X
                    << "/" << wrapped[center].X << "/" << noStrength[center].X << std::endl;
                Require(overrideDifference > 0.002f, "Instance Subsurface override did not affect lighting");
                Require(CenterDifference(noStrength, render()) < 0.001f, "Zero strength and zero radius must both remove scattering");
                mesh.ResetMaterialInstance(0);
                Require(CenterDifference(wrapped, render()) < 0.001f, "Clearing instance override must restore the shared response");
                mesh.SetMaterial(0, lacquer->GetMaterial());
                auto* coat = mesh.GetOrCreateMaterialInstance(0);
                const auto coated = render();
                coat->SetParameter("ClearCoatRoughness", 0.65f);
                const auto roughCoat = render();
                coat->SetParameter("ClearCoatStrength", 0.0f);
                const auto bare = render();
                const float coatEffect = CenterDifference(coated, bare);
                const float roughnessEffect = CenterDifference(coated, roughCoat);
                Require(coatEffect > 0.001f, "Clear coat layer did not respond to strength");
                Require(roughnessEffect > 0.0002f, "Clear coat lobe did not respond to roughness");
                mesh.ResetMaterialInstance(0);
                Require(CenterDifference(coated, render()) < 0.001f, "Clearing coat overrides did not restore the material");
                std::cout << " coatEffect=" << coatEffect << " coatRoughnessEffect=" << roughnessEffect << std::endl;

                mesh.SetMaterial(0, brushed->GetMaterial());
                auto* grain = mesh.GetOrCreateMaterialInstance(0);
                grain->SetParameter("Anisotropy", 0.85f);
                const auto alongTangent = render();
                grain->SetParameter("Anisotropy", -0.85f);
                const auto alongBitangent = render();
                grain->SetParameter("Anisotropy", 0.0f);
                const auto isotropic = render();
                const float directionEffect = CenterDifference(alongTangent, alongBitangent);
                const float grainEffect = CenterDifference(isotropic, alongTangent);
                Require(directionEffect > 0.0005f && grainEffect > 0.0005f, "Anisotropy did not respond to direction or strength");
                std::cout << " anisotropyDirectionEffect=" << directionEffect << " anisotropyStrengthEffect=" << grainEffect << std::endl;

                mesh.SetMaterial(0, fabric->GetMaterial());
                auto* cloth = mesh.GetOrCreateMaterialInstance(0);
                const auto sheen = render();
                cloth->SetParameter("SheenColor", std::array<float, 4>{0.1f, 0.35f, 1.0f, 1.0f});
                const auto dyedFibers = render();
                cloth->SetParameter("SheenStrength", 0.0f);
                const auto matte = render();
                const float clothEffect = CenterDifference(sheen, matte);
                const float sheenColorEffect = CenterDifference(sheen, dyedFibers);
                Require(clothEffect > 0.001f && sheenColorEffect > 0.0005f, "Cloth did not respond to sheen color and strength");
                mesh.ResetMaterialInstance(0);
                Require(CenterDifference(sheen, render()) < 0.001f, "Clearing sheen overrides did not restore the material");
                std::cout << " clothEffect=" << clothEffect << " sheenColorEffect=" << sheenColorEffect << std::endl;
            }

            for (auto* light : lights)
            {
                light->SetEnabled(light == &directional);
            }

            auto normalDesc = environmentDesc;
            normalDesc.Width = normalDesc.Height = 1;
            auto normalImage = std::make_shared<RenderTexture>(normalDesc);
            normalImage->InitRHIResource();
            const std::array<uint8_t, 4> angledNormal{212, 128, 222, 255};
            normalImage->UploadData(angledNormal.data(), 0, 0);
            const auto normalAsset = TextureAsset::Create("AngledTestNormal", normalImage);

            const auto cube = StaticMeshAsset::CreateShape(EBasicMeshShape::Cube);
            StaticMeshComponent blocker;
            blocker.SetStaticMesh(cube->GetMesh());
            blocker.SetWorldLocation({-1.6f, 0.6f, 1.45f});
            blocker.SetWorldScale({0.85f, 0.85f, 0.85f});
            blocker.SetMaterial(0, lit->GetMaterial());
            scene->AddPrimitive(&blocker);
            for (auto model : {lit, skin, lacquer, brushed, fabric})
            {
                directional.SetCastShadow(false);
                mesh.SetMaterial(0, model->GetMaterial());
                const auto unshadowed = render();
                auto* instance = mesh.GetOrCreateMaterialInstance(0);
                instance->SetParameter("Normal", normalAsset);
                const float normalEffect = CenterDifference(unshadowed, render());
                mesh.ResetMaterialInstance(0);
                directional.SetCastShadow(true);
                const float shadowEffect = CenterDifference(unshadowed, render());
                std::cout << " model=" << GetShadingModelName(model->GetMaterial()->GetShadingModel())
                    << " normalEffect=" << normalEffect << " shadowEffect=" << shadowEffect << std::endl;
                Require(normalEffect > 0.001f, "Lit model ignored the normal texture");
                Require(shadowEffect > 0.001f, "Lit model ignored directional shadows");
            }
            directional.SetCastShadow(false);
            scene->RemovePrimitive(&blocker);
            for (auto* light : lights)
            {
                light->SetEnabled(true);
            }
            scene->RemovePrimitive(&mesh);
            std::vector<std::shared_ptr<MaterialAsset>> materials;
            std::vector<std::unique_ptr<StaticMeshComponent>> meshes;
            const auto count = uint32_t(EShadingModel::COUNT_MAX);
            for (uint32_t id = 0; id < count; ++id)
            {
                materials.push_back(MakeMaterial(static_cast<EShadingModel>(id)));
                auto& object = meshes.emplace_back(std::make_unique<StaticMeshComponent>());
                object->SetStaticMesh(sphere->GetMesh());
                object->SetWorldScale({0.75f, 0.75f, 0.75f});
                object->SetMaterial(0, materials.back()->GetMaterial());
                object->SetWorldLocation({(float(id) - 0.5f * float(count - 1)) * 1.65f, 0, 0});
                scene->AddPrimitive(object.get());
            }
            SaveComparison(render());
            for (const auto& object : meshes)
            {
                scene->RemovePrimitive(object.get());
            }
            for (auto* light : lights)
            {
                scene->RemoveLight(light);
            }
            scene->FlushPendingUpdates();
            environments.ApplyEnvironment(sky, {});
            environments.ReleaseUnused();
            std::cout << "\nMATERIAL_SHADING_MODEL_TEST_OK visibleModels=" << count << std::endl;
        }

    };

    REGISTER_RENDER_TEST("MaterialShadingModelTest", MaterialShadingModelTest);

    class MaterialTransparencyTest final : public MaterialSceneTestBase
    {
    public:
        void Run() override
        {
            constexpr auto center = Height / 2 * Width + Width / 2;
            const auto unlit = [](Core::Float3 color, EBlendMode mode, float opacity)
            {
                auto asset = MakeMaterial(EShadingModel::Unlit);
                auto* material = asset->GetMaterial();
                material->SetBlendMode(mode);
                material->SetParameter("BaseColor", std::array<float, 4>{color.X, color.Y, color.Z, 1});
                material->SetParameter("Opacity", opacity);
                return asset;
            };
            const auto nearColor = unlit({1, 0, 0}, EBlendMode::Translucent, 0.5f);
            const auto farColor = unlit({0, 0, 1}, EBlendMode::Translucent, 0.5f);
            const auto background = unlit({0.2f, 0.4f, 0.6f}, EBlendMode::Opaque, 1);
            const auto plane = StaticMeshAsset::CreateShape(EBasicMeshShape::Plane);
            StaticMeshComponent farPlane, nearPlane, wall;
            for (auto* component : {&farPlane, &nearPlane, &wall})
            {
                component->SetStaticMesh(plane->GetMesh());
                component->SetWorldRotation({90.0f, 0, 0});
            }
            nearPlane.SetMaterial(0, nearColor->GetMaterial());
            farPlane.SetMaterial(0, farColor->GetMaterial());
            wall.SetMaterial(0, background->GetMaterial());
            wall.SetWorldLocation({0, 0, -1});
            wall.SetWorldScale({5, 5, 5});
            nearPlane.SetWorldLocation({0, 0, 1});
            farPlane.SetWorldLocation({0, 0, 0});

            Camera camera;
            camera.SetPosition({0, 0, 5});
            camera.SetTarget({0, 0, 0});
            camera.SetUp({0, 1, 0});
            camera.SetPerspective(Core::DegToRad(39.0f), float(Width) / Height, 0.1f, 40);
            SceneView view;
            view.Viewport = {0, 0, Width, Height};
            view.RenderTargetWidth = float(Width);
            view.RenderTargetHeight = float(Height);
            view.CameraWorldPos = camera.GetPosition();
            view.CameraWorldDir = {0, 0, -1};
            view.NearClip = camera.GetNearPlane();
            view.FarClip = camera.GetFarPlane();
            view.ViewMatrix = camera.GetViewMatrix();
            view.ProjectionMatrix = camera.GetProjectionMatrix();
            view.ViewProjectionMatrix = camera.GetViewProjectionMatrix();
            view.InvViewProjectionMatrix = camera.GetInverseViewProjectionMatrix();
            view.Flags = VF_EnableShadows;

            MaterialTestTarget target;
            std::unique_ptr<SceneInterface> scene(m_Renderer->AllocateScene());
            scene->AddPrimitive(&wall);
            const auto render = [&]()
            {
                scene->FlushPendingUpdates();
                SceneViewFamily family;
                family.Scene = scene.get();
                family.RenderTarget = &target;
                family.AddView(view);
                family.BuildAllViews();
                m_Renderer->BeginRender(&family);
                Pixels pixels(Width * Height);
                target.GetRenderTarget()->ReadData(pixels.data(), 0, 0);
                return pixels;
            };

            const auto check = [](const Core::Float4& value, Core::Float3 expected, const char* label)
            {
                std::cout << label << " actual rgb=" << value.X << "," << value.Y << "," << value.Z << std::endl;
                Require(std::abs(value.X - expected.X) < 0.008f && std::abs(value.Y - expected.Y) < 0.008f &&
                    std::abs(value.Z - expected.Z) < 0.008f, label);
            };

            const auto baseline = render()[center];
            // The existing deferred IBL path effectively doubles an Unlit surface's color.
            check(baseline, {0.4f, 0.8f, 1.2f}, "Opaque background");

            const auto cutoutMaterial = unlit({0.4f, 0.1f, 0.2f}, EBlendMode::Masked, 1);
            RHITextureDesc maskDesc{};
            maskDesc.Width = 2;
            maskDesc.Height = 1;
            maskDesc.Format = ERHIFormat::R8G8B8A8_UNorm;
            maskDesc.Usage = ERHITextureCreateFlag::ShaderResource | ERHITextureCreateFlag::TransferDest;
            auto maskTexture = std::make_shared<RenderTexture>(maskDesc);
            maskTexture->InitRHIResource();
            const std::array<uint8_t, 8> cutoutPixels{0, 0, 0, 255, 255, 255, 255, 255};
            maskTexture->UploadData(cutoutPixels.data(), 0, 0);
            cutoutMaterial->GetMaterial()->SetParameter("Opacity", TextureAsset::Create("TransparencyTestMask", maskTexture));
            StaticMeshComponent cutout;
            cutout.SetStaticMesh(plane->GetMesh());
            cutout.SetWorldLocation({0, 0, 2});
            cutout.SetWorldRotation({90.0f, 0, 0});
            cutout.SetMaterial(0, cutoutMaterial->GetMaterial());
            scene->AddPrimitive(&cutout);
            const auto masked = render();
            check(masked[center - 35], {baseline.X, baseline.Y, baseline.Z}, "Alpha-cut hole reveals the wall");
            check(masked[center + 35], {0.8f, 0.2f, 0.4f}, "Alpha-cut solid section covers the wall");

            scene->AddPrimitive(&nearPlane);
            scene->AddPrimitive(&farPlane);
            const auto throughCutout = render();
            Require(throughCutout[center - 35].X > baseline.X, "Cutout hole wrote opaque depth");
            check(throughCutout[center + 35], {0.8f, 0.2f, 0.4f}, "Opaque cutout surface rejects transparent draws behind it");
            scene->RemovePrimitive(&cutout);
            const auto sorted = render()[center];
            check(sorted, {0.60f, 0.20f, 0.55f}, "Sort independently of registration order");
            Require(std::abs(sorted.W - baseline.W) < 0.001f, "Alpha blending modified scene alpha");
            nearPlane.SetWorldLocation({0, 0, -0.5f});
            check(render()[center], {0.35f, 0.20f, 0.80f}, "Moving mesh changes sort order");

            auto& settings = scene->GetSceneSetting();
            settings.TransparencyMethod = ETransparencyMethod::Weighted;
            nearPlane.SetWorldLocation({0, 0, 1});
            farPlane.SetWorldLocation({0, 0, 1});
            farPlane.SetWorldRotation({90.0f, 25.0f, 0.0f});
            const auto weighted = render();
            check(weighted[center], {0.475f, 0.20f, 0.675f}, "Weighted overlap accumulates color and coverage");
            scene->RemovePrimitive(&nearPlane);
            scene->RemovePrimitive(&farPlane);
            scene->FlushPendingUpdates();
            scene->AddPrimitive(&farPlane);
            scene->AddPrimitive(&nearPlane);
            Require(CenterDifference(weighted, render()) < 0.004f, "Intersecting weighted surfaces depend on draw order");
            farPlane.SetWorldRotation({90.0f, 0, 0});
            farPlane.SetWorldLocation({0, 0, 0});
            nearColor->GetMaterial()->SetBlendMode(EBlendMode::Additive);
            check(render()[center], {0.70f, 0.40f, 1.10f}, "Weighted fallback preserves additive blend");
            settings.TransparencyMethod = ETransparencyMethod::Sorted;
            nearPlane.SetWorldLocation({0, 0, -2});
            farPlane.SetWorldLocation({0, 0, -3});
            check(render()[center], {baseline.X, baseline.Y, baseline.Z}, "Opaque depth blocks both blends");

            scene->RemovePrimitive(&farPlane);
            nearPlane.SetWorldLocation({0, 0, 1});
            nearColor->GetMaterial()->SetBlendMode(EBlendMode::Translucent);
            auto* optics = nearPlane.GetOrCreateMaterialInstance(0);
            optics->SetParameter("Opacity", 1.0f);
            optics->SetParameter("Transmission", 1.0f);
            optics->SetParameter("Thickness", 1.0f);
            optics->SetParameter("IndexOfRefraction", 1.0f);
            optics->SetParameter("Absorption", std::array<float, 4>{std::log(2.0f), 0, 0, 0});
            check(render()[center], {0.20f, 0.80f, 1.20f}, "Beer-Lambert per channel");

            optics->SetParameter("IndexOfRefraction", 1.6f);
            nearPlane.SetWorldRotation({78.0f, 0.0f, 0.0f});
            farPlane.SetWorldLocation({0, 0, 0});
            scene->AddPrimitive(&farPlane);
            settings.TransparencyMethod = ETransparencyMethod::LayeredRefraction;
            check(render()[center], {0.10f, 0.40f, 1.10f}, "Layered glass transmits the previously blended surface");
            settings.TransparencyMethod = ETransparencyMethod::Sorted;
            check(render()[center], {0.20f, 0.80f, 1.20f}, "Sorted glass uses the opaque background");
            scene->RemovePrimitive(&farPlane);
            settings.TransparencyView = ETransparencyView::Refraction;
            const auto displaced = render()[center];
            optics->SetParameter("RefractionStrength", 0.0f);
            const auto centered = render()[center];
            Require(displaced.Y > 0.02f && centered.Y < 0.001f, "Refraction strength did not change projected screen displacement");
            settings.TransparencyView = ETransparencyView::SortOrder;
            Require(render()[center].X > 0.9f, "Sort order debug view did not display the nearest draw");
            settings.TransparencyView = ETransparencyView::Classification;
            Require(render()[center].Z > 0.9f, "Classification debug view did not identify Alpha");
            settings.TransparencyView = ETransparencyView::Composite;

            const auto stripeColor = unlit({0.9f, 0.14f, 0.18f}, EBlendMode::Opaque, 1);
            StaticMeshComponent stripe;
            stripe.SetStaticMesh(plane->GetMesh());
            stripe.SetWorldRotation({90.0f, 0, 0});
            stripe.SetWorldLocation({0.5f, 0, -0.5f});
            stripe.SetWorldScale({0.5f, 1, 2});
            stripe.SetMaterial(0, stripeColor->GetMaterial());
            scene->AddPrimitive(&stripe);
            nearPlane.SetWorldRotation({90.0f, 0, 0});
            optics->SetParameter("IndexOfRefraction", 1.0f);
            optics->SetParameter("Absorption", std::array<float, 4>{0, 0, 0, 0});
            optics->SetParameter("RefractionStrength", 1.0f);
            optics->SetParameter("Roughness", 0.0f);
            const auto sharp = render();
            optics->SetParameter("Roughness", 1.0f);
            const auto blurred = render();
            Require(CenterDifference(sharp, blurred) > 0.05f, "Roughness did not filter the transmitted edge");
            optics->SetParameter("Roughness", 0.0f);
            optics->SetParameter("IndexOfRefraction", 2.5f);
            nearPlane.SetWorldRotation({90.0f, 35.0f, 0.0f});
            optics->SetParameter("Thickness", 24.0f);
            optics->SetParameter("RefractionStrength", 0.0f);
            const auto unshifted = render();
            optics->SetParameter("RefractionStrength", 1.0f);
            settings.TransparencyView = ETransparencyView::Refraction;
            const auto edgeDisplacement = render();
            Require(edgeDisplacement[center].X > 0.7f, "Offscreen refraction fixture needs a large displacement");
            settings.TransparencyView = ETransparencyView::Composite;
            const auto borderFallback = render();
            check(borderFallback[center], {unshifted[center].X, unshifted[center].Y, unshifted[center].Z},
                "Offscreen refraction falls back to the unshifted pixel");
            scene->RemovePrimitive(&stripe);
            nearPlane.SetWorldRotation({78.0f, 0.0f, 0.0f});
            optics->SetParameter("Thickness", 1.0f);

            auto depthDesc = RHITextureDesc{};
            depthDesc.Width = Width;
            depthDesc.Height = Height;
            depthDesc.Format = ERHIFormat::R32G32B32A32_Float;
            depthDesc.Usage = ERHITextureCreateFlag::ShaderResource | ERHITextureCreateFlag::TransferDest;
            RenderTexture occluderDepth(depthDesc);
            occluderDepth.InitRHIResource();
            const Pixels nearDepth(Width * Height, {0.25f, 0, 0, 0});
            occluderDepth.UploadData(nearDepth.data(), 0, 0);
            view.Flags |= VF_ExternalTransparencyPass;
            view.DrawExtensions.push_back({[&](RenderGraphBuilder& graph, const SceneView& currentView,
                RenderGraphTextureRef color, RenderGraphTextureRef depth)
            {
                auto effective = graph.RegisterExternalTexture("Test.EffectiveDepth", &occluderDepth);
                Renderer::AddTransparencyPasses(graph, *scene, currentView, color, depth, effective);
            }});
            check(render()[center], {baseline.X, baseline.Y, baseline.Z}, "Effective water depth blocks farther geometry");
            view.DrawExtensions.clear();

            // Run the same ordering as Volume Viewer with actual water, aerial perspective and froxel passes.
            view.DrawExtensions.push_back({[&](RenderGraphBuilder& graph, const SceneView& currentView,
                RenderGraphTextureRef color, RenderGraphTextureRef depth)
            {
                Renderer::AtmosphereRenderSettings atmosphere{};
                atmosphere.bEnabled = true;
                Renderer::WaterRenderSettings water{};
                water.bEnabled = true;
                water.Position.Y = -0.4f;
                Renderer::VolumeRenderSettings volume{};
                volume.View = Renderer::EVolumeView::Composite;
                const auto light = Renderer::PrepareAtmosphere(graph, atmosphere, scene.get());
                const auto effective = Renderer::AddWaterPass(graph, currentView, color, depth, water, light, scene.get());
                Renderer::AddAtmospherePass(graph, currentView, color, effective, atmosphere, light);
                Renderer::AddTransparencyPasses(graph, *scene, currentView, color, depth, effective);
                Renderer::AddVolumePasses(graph, currentView, effective, color, volume, scene.get());
            }});
            const auto integrated = render();
            Require(std::isfinite(integrated[center].X) && integrated[center].X > 0, "Water/atmosphere/volume composition is invalid");
            SaveComparison(integrated, "MaterialTransparency.ppm");
            view.DrawExtensions.clear();
            view.Flags &= ~VF_ExternalTransparencyPass;

            const auto plastic = MakeMaterial(EShadingModel::Lit);
            plastic->GetMaterial()->SetBlendMode(EBlendMode::Translucent);
            plastic->GetMaterial()->SetParameter("Opacity", 0.85f);
            nearPlane.SetMaterial(0, plastic->GetMaterial());
            nearPlane.SetWorldRotation({90.0f, 0.0f, 0.0f});
            DirectionalLightComponent directional;
            directional.SetDirection({0.8f, -0.3f, -0.5f});
            directional.SetIntensity(3);
            PointLightComponent point;
            point.SetWorldLocation({-2, 1, 3});
            point.SetIntensity(8);
            point.SetAttenuationRadius(9);
            SpotLightComponent spot;
            spot.SetWorldLocation({2, 1, 3});
            spot.SetDirection({-2, -1, -2});
            spot.SetIntensity(10);
            spot.SetAttenuationRadius(9);
            SkyLightComponent sky;
            auto& environments = EnvironmentLightResourceManager::Get();
            RHITextureDesc environmentDesc{};
            environmentDesc.Width = 4;
            environmentDesc.Height = 2;
            environmentDesc.Format = ERHIFormat::R8G8B8A8_UNorm;
            environmentDesc.Usage = ERHITextureCreateFlag::ShaderResource | ERHITextureCreateFlag::TransferDest;
            auto environment = std::make_shared<RenderTexture>(environmentDesc);
            environment->InitRHIResource();
            std::array<std::array<uint8_t, 4>, 8> skyPixels;
            skyPixels.fill({156, 124, 100, 255});
            environment->UploadData(skyPixels.data(), 0, 0);
            const auto skyAsset = TextureAsset::Create("TransparencyTestSky", environment);
            environments.ApplyEnvironment(sky, environments.Build(skyAsset));
            const std::array<LightComponent*, 4> lights{&directional, &point, &spot, &sky};
            for (auto* light : lights)
            {
                light->SetEnabled(false);
                light->SetCastShadow(false);
                scene->AddLight(light);
            }
            const auto dark = render();
            for (auto* light : lights)
            {
                light->SetEnabled(true);
                const float difference = CenterDifference(dark, render());
                std::cout << "Transparent light " << int(light->GetLightType()) << " effect=" << difference << std::endl;
                Require(difference > 0.01f, "Transparent PBR surface did not respond to a scene light");
                light->SetEnabled(false);
            }

            const auto caster = StaticMeshAsset::CreateShape(EBasicMeshShape::Cube);
            StaticMeshComponent occluder;
            occluder.SetStaticMesh(caster->GetMesh());
            occluder.SetWorldLocation({1.1f, 0.55f, 2.1f});
            occluder.SetWorldScale({0.58f, 0.58f, 0.58f});
            const auto maskedCaster = unlit({0.2f, 0.4f, 0.6f}, EBlendMode::Masked, 1);
            maskedCaster->GetMaterial()->SetParameter("OpacityMask", 0.0f);
            occluder.SetMaterial(0, maskedCaster->GetMaterial());
            spot.SetEnabled(true);
            spot.SetCastShadow(true);
            const auto unshadowed = render();
            scene->AddPrimitive(&occluder);
            spot.SetCastShadow(true); // Refresh the shadow atlas for the new caster.
            const float cutoutShadow = CenterDifference(unshadowed, render());
            std::cout << "Alpha-cut hole shadow difference=" << cutoutShadow << std::endl;
            Require(cutoutShadow < 0.01f, "Alpha-cut hole casts a shadow");
            occluder.SetMaterial(0, background->GetMaterial());
            spot.SetCastShadow(true);
            const auto opaqueShadow = render();
            const float opaqueDifference = CenterDifference(unshadowed, opaqueShadow);
            std::cout << "Opaque control shadow difference=" << opaqueDifference << std::endl;
            Require(opaqueDifference > 0.01f, "Opaque control did not shadow the transparent surface");
            maskedCaster->GetMaterial()->SetParameter("OpacityMask", 1.0f);
            occluder.SetMaterial(0, maskedCaster->GetMaterial());
            spot.SetCastShadow(true);
            const auto solid = render();
            const float solidShadow = CenterDifference(unshadowed, solid);
            std::cout << "Solid alpha-cut shadow difference=" << solidShadow << std::endl;
            Require(solidShadow > 0.01f, "Solid alpha-cut caster did not shadow the transparent surface");
            Require(CenterDifference(opaqueShadow, solid) < 0.015f, "Solid cutout and opaque caster have different shadows");
            scene->RemovePrimitive(&occluder);
            for (auto* light : lights)
            {
                scene->RemoveLight(light);
            }
            scene->RemovePrimitive(&wall);
            scene->RemovePrimitive(&nearPlane);
            scene->FlushPendingUpdates();
            environments.ApplyEnvironment(sky, {});
            environments.ReleaseUnused();
            std::cout << "MATERIAL_TRANSPARENCY_TEST_OK" << std::endl;
        }
    };

    REGISTER_RENDER_TEST("MaterialTransparencyTest", MaterialTransparencyTest);
}
