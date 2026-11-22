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
                    ERHITextureCreateFlag::UAV | ERHITextureCreateFlag::TransferSrc;
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

        void SaveComparison(const Pixels& pixels)
        {
            std::ofstream file("MaterialShadingModels.ppm", std::ios::binary);
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

    class MaterialShadingModelTest final : public TestBase
    {
    public:
        ~MaterialShadingModelTest() override
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

    private:
        std::shared_ptr<Renderer::RenderModule> m_Renderer;
        bool m_bReady = false;
    };

    REGISTER_RENDER_TEST("MaterialShadingModelTest", MaterialShadingModelTest);
}
