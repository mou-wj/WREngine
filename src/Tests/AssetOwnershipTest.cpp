#include "TestBase.h"
#include "AssetManager.h"
#include "Material.h"
#include "StaticMeshComponent.h"
#include "PathInfo.h"
#include <atomic>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace Test {
namespace {
void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

float Roughness(Engine::MaterialInterface* material)
{
    float result = 0;
    Require(material->GetRenderProxy()->GetValue<Engine::EMaterialParameterSemantic::Roughness, Engine::FScalarType>(
        "Roughness", result), "Missing roughness parameter");
    return result;
}

struct LoadGate
{
    std::promise<void> Started;
    std::promise<void> Release;
    std::shared_future<void> Ready = Release.get_future().share();
    std::atomic<int> Loads = 0;
};
LoadGate Gate;

class CountingAsset : public Engine::IAsset
{
public:
    explicit CountingAsset(const std::string& path) : Path(path) {}
    const std::string& GetPath() const override { return Path; }
    const std::string& GetName() const override { return Name; }
    bool Load() override
    {
        if (++Gate.Loads == 1) Gate.Started.set_value();
        Gate.Ready.wait();
        return true;
    }
private:
    std::string Path;
    std::string Name = "CountingAsset";
};

class WrongTypeAsset : public CountingAsset
{
public:
    using CountingAsset::CountingAsset;
};
}

class AssetOwnershipTest : public TestBase
{
public:
    void Setup() override
    {
        const auto directory = std::filesystem::path(Core::GetProjectDir()) /
            "out/diagnostics/asset-ownership-fixture";
        std::filesystem::create_directories(directory);
        std::ofstream file(directory / "material.json");
        file << R"({
            "version": 1,
            "name": "AssetOwnershipFixture",
            "blendMode": "Opaque",
            "shadingModel": "Lit",
            "scalarParameters": { "roughness": 0.5, "metallic": 0.0 },
            "vectorParameters": { "baseColor": [1.0, 1.0, 1.0, 1.0] }
        })";
        if (!file) throw std::runtime_error("Cannot write asset ownership fixture");
    }
    void Teardown() override { Engine::AssetManager::Get().Clear(); }

    void Run() override
    {
        using namespace Engine;
        auto& manager = AssetManager::Get();
        const auto path = Core::GetProjectDir() + "/out/diagnostics/asset-ownership-fixture/material.json";
        auto asset = manager.LoadSync<MaterialAsset>(path);
        auto* base = asset->GetMaterial();
        Require(std::abs(Roughness(base) - 0.5f) < 0.0001f, "Grouped material JSON was not applied");
        Require(manager.LoadSync<MaterialAsset>(path + "/../material.json") == asset,
                "Normalized paths did not share an asset");
        Require(manager.GetByName<MaterialAsset>("AssetOwnershipFixture") == asset.get(), "Asset name index is incorrect");

        StaticMeshComponent first;
        StaticMeshComponent second;
        first.SetMaterial(0, base);
        second.SetMaterial(0, base);
        Require(first.GetMaterial(0) == base && second.GetMaterial(0) == base,
                "Components should initially share the base material");
        Require(!first.GetMaterialInstance(0) && !second.GetMaterialInstance(0),
                "Material instances must be lazy");

        first.SetMaterialParameterValue<EMaterialParameterSemantic::Roughness, FScalarType>(0, "Roughness", 0.9f);
        auto* instance = first.GetMaterialInstance(0);
        Require(instance && first.GetOrCreateMaterialInstance(0) == instance, "Instance was not reused");
        Require(!second.GetMaterialInstance(0) && std::abs(Roughness(second.GetMaterial(0)) - 0.5f) < 0.0001f,
                "An instance edit changed another component");
        Require(std::abs(Roughness(first.GetMaterial(0)) - 0.9f) < 0.0001f, "Instance override was not applied");
        base->SetParameterValue<EMaterialParameterSemantic::Metallic, FScalarType>("Metallic", 0.25f);
        float metallic = 0;
        Require(instance->GetRenderProxy()->GetValue<EMaterialParameterSemantic::Metallic, FScalarType>("Metallic", metallic)
                && std::abs(metallic - 0.25f) < 0.0001f, "Parent changes did not invalidate the instance proxy");
        Require(std::abs(Roughness(instance) - 0.9f) < 0.0001f, "Parent update lost the instance override");
        first.ResetMaterialInstance(0);
        Require(!first.GetMaterialInstance(0) && first.GetMaterial(0) == base, "Reset did not restore the base material");

        std::weak_ptr<MaterialAsset> lifetime = asset;
        first.SetMaterialParameterValue<EMaterialParameterSemantic::Roughness, FScalarType>(0, "Roughness", 0.7f);
        manager.Clear();
        asset.reset();
        Require(!lifetime.expired() && std::abs(Roughness(first.GetMaterial(0)) - 0.7f) < 0.0001f,
                "Clear destroyed a material still in use");
        asset = manager.LoadSync<MaterialAsset>(path);
        Require(asset.get() == lifetime.lock().get(), "A live asset was duplicated after Clear");
        manager.Unload(path);
        asset.reset();
        first.SetMaterial(0, nullptr);
        Require(!lifetime.expired(), "Second component lost its material owner");
        second.SetMaterial(0, nullptr);
        Require(lifetime.expired(), "Material dependency leaked after the last component released it");

        auto started = Gate.Started.get_future();
        std::shared_ptr<CountingAsset> results[2];
        std::atomic<int> callbacks = 0;
        const auto countingPath = Core::GetProjectDir() + "/out/diagnostics/counting.asset";
        for (int i = 0; i < 2; ++i)
        {
            manager.LoadAsync<CountingAsset>(countingPath, [&, i](std::shared_ptr<CountingAsset> value) {
                results[i] = value;
                Require(manager.GetAssetShared<CountingAsset>(countingPath) == value,
                        "Load callback cannot reenter the manager");
                ++callbacks;
            });
            if (i == 0) started.wait();
        }
        Gate.Release.set_value();
        manager.WaitForLoads();
        Require(callbacks == 2 && Gate.Loads == 1 && results[0] == results[1],
                "Concurrent requests were not coalesced or a callback was lost");
        bool rejected = false;
        try { manager.LoadSync<WrongTypeAsset>(countingPath); }
        catch (const std::invalid_argument&) { rejected = true; }
        Require(rejected, "A cached asset was returned as the wrong type");
        std::cout << "Asset ownership, lazy material instances, JSON, indexing and request coalescing passed" << std::endl;
    }
};

REGISTER_RENDER_TEST("AssetOwnershipTest", AssetOwnershipTest);
}
