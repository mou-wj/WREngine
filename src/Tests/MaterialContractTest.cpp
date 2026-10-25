#include "TestBase.h"
#include "AssetManager.h"
#include "Material.h"
#include "TextureAsset.h"
#include "StaticMeshComponent.h"
#include "RenderResource.h"
#include "PathInfo.h"
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace Test {
namespace {
void CheckMaterial(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}
float ScalarValue(const Engine::MaterialRenderProxy& proxy, const char* name)
{
    return std::get<float>(proxy.GetParameter(name,Engine::EMaterialParameterType::Scalar)->GetValueVariant());
}
template<class F> void ExpectMaterialError(F&& action, const char* message)
{
    bool rejected = false;
    try { action(); }
    catch (const std::invalid_argument&) { rejected = true; }
    CheckMaterial(rejected,message);
}
}

class MaterialContractTest : public TestBase
{
public:
    void Setup() override {}
    void Teardown() override { Engine::AssetManager::Get().Clear(); }
    void Run() override
    {
        using namespace Engine;
        const auto directory = std::filesystem::path(Core::GetProjectDir()) / "out/diagnostics/material-contract";
        std::filesystem::create_directories(directory);
        auto asset = MaterialAsset::Create((directory/"source.json").generic_string(),"ContractFixture",std::make_unique<Material>());
        auto* material = asset->GetMaterial();
        material->SetShadingModel(EShadingModel::Lit);
        CheckMaterial(material->GetParameterDefinitions().size() == 17,"Incomplete Surface parameter declarations");
        auto original = material->GetRenderProxyShared();
        CheckMaterial(std::abs(ScalarValue(*original,"Metallic")-0.8f)<0.0001f,"Legacy metallic default changed");
        CheckMaterial(std::abs(ScalarValue(*original,"Roughness")-0.2f)<0.0001f,"Legacy roughness default changed");
        CheckMaterial(material->GetParameter("BaseColor",EMaterialParameterType::Vector) &&
                      material->GetParameter("BaseColor",EMaterialParameterType::Texture),"Typed names cannot coexist");
        const auto before = material->GetRevisions();
        material->SetParameter("roughness",0.7f);
        auto edited = material->GetRenderProxyShared();
        CheckMaterial(original != edited && std::abs(ScalarValue(*original,"Roughness")-0.2f)<0.0001f,"Editing mutated a published snapshot");
        CheckMaterial(original->GetResource() == edited->GetResource(),"A dynamic parameter edit replaced the compilation resource");
        CheckMaterial(material->GetRevisions().Compilation == before.Compilation &&
                      material->GetRevisions().Parameters == before.Parameters+1,"Dynamic edit invalidation is incorrect");
        const auto unchanged = material->GetRevision();
        CheckMaterial(!material->SetParameter("Roughness",0.7f) && material->GetRevision()==unchanged,"Identical values dirtied the material");
        ExpectMaterialError([&]{material->SetParameter("Roughnes",0.5f);},"Unknown parameter was accepted");
        ExpectMaterialError([&]{material->SetParameter("Roughness",std::array<float,4>{});},"Wrong parameter type was accepted");
        ExpectMaterialError([&]{material->SetParameterValue<EMaterialParameterSemantic::Custom,FScalarType>("Roughness",0.5f);},"Wrong semantic was accepted");
        ExpectMaterialError([&]{material->SetParameter("Roughness",std::numeric_limits<float>::infinity());},"Nonfinite value was accepted");

        StaticMeshComponent first,second;
        first.SetMaterial(0,material);
        second.SetMaterial(0,material);
        CheckMaterial(!first.GetMaterialInstance(0),"A component eagerly created an instance");
        auto* instance = first.GetOrCreateMaterialInstance(0);
        instance->SetParameter("Roughness",0.4f);
        CheckMaterial(instance->HasOverride("Roughness",EMaterialParameterType::Scalar),"Override was not recorded");
        material->SetParameter("Metallic",0.3f);
        CheckMaterial(std::abs(ScalarValue(*instance->GetRenderProxy(),"Metallic")-0.3f)<0.0001f &&
                      std::abs(ScalarValue(*instance->GetRenderProxy(),"Roughness")-0.4f)<0.0001f,"Parent inheritance or override resolution failed");
        CheckMaterial(std::abs(ScalarValue(*second.GetMaterial(0)->GetRenderProxy(),"Roughness")-0.7f)<0.0001f,"Instance changed another component");
        instance->ResetParameter("Roughness",EMaterialParameterType::Scalar);
        CheckMaterial(!instance->HasOverride("Roughness",EMaterialParameterType::Scalar) &&
                      std::abs(ScalarValue(*instance->GetRenderProxy(),"Roughness")-0.7f)<0.0001f,"Reset did not restore inheritance");

        // CPU-only texture wrapper verifies ownership without requiring an RHI device.
        auto texture = TextureAsset::Create("RuntimeTexture",std::make_shared<RenderCore::RenderTexture>(RHI::RHITextureDesc{}));
        std::weak_ptr<TextureAsset> textureLifetime = texture;
        instance->SetParameter("Normal",texture);
        auto textureSnapshot = instance->GetRenderProxyShared();
        instance->ResetAllParameters();
        texture.reset();
        CheckMaterial(!textureLifetime.expired(),"Published snapshot lost its texture asset");
        textureSnapshot.reset();
        CheckMaterial(textureLifetime.expired(),"Texture asset was leaked after all references were released");
        auto oldState = material->GetRenderProxyShared();
        const auto stateRevision = material->GetRevisions();
        material->SetTwoSided(true);
        CheckMaterial(!oldState->IsTwoSided() && material->GetRenderProxy()->IsTwoSided(),"Render-state snapshot is not immutable");
        CheckMaterial(oldState->GetResource()==material->GetResource(),"Two-sided state replaced the compilation resource");
        CheckMaterial(material->GetRevisions().Compilation==stateRevision.Compilation &&
                      material->GetRevisions().RenderState==stateRevision.RenderState+1,"PSO-only state dirtied compilation");
        using RHI::ERHICullMode;
        material->SetCullMode(ERHICullMode::Front);
        auto twoSided = material->GetRenderProxyShared();
        CheckMaterial(twoSided->ResolveCullMode()==ERHICullMode::None &&
                      twoSided->ResolveCullMode(ERHICullMode::Front)==ERHICullMode::Front &&
                      twoSided->ResolveCullMode(ERHICullMode::Back)==ERHICullMode::Back,"Pass cull override did not take priority");
        material->SetTwoSided(false);
        CheckMaterial(material->GetRenderProxy()->ResolveCullMode()==ERHICullMode::Front &&
                      oldState->ResolveCullMode()==ERHICullMode::Back,"Cull state inheritance or snapshot isolation failed");
        material->SetCullMode(ERHICullMode::None);
        CheckMaterial(material->GetRenderProxy()->ResolveCullMode()==ERHICullMode::None,"Explicit no-cull state was lost");
        material->SetCullMode(ERHICullMode::Front);
        material->SetTwoSided(true);
        material->SetBlendMode(EBlendMode::Masked);
        CheckMaterial(material->GetResource()==oldState->GetResource(),"Raster/blend changes replaced the compilation resource");
        material->SetBlendMode(EBlendMode::Opaque);
        ExpectMaterialError([&]{material->SetCullMode(static_cast<ERHICullMode>(255));},"Invalid cull mode was accepted");
        material->SetParameter("UVTiling",std::array<float,4>{2,3,0,0});
        const auto savedPath = (directory/"saved.json").generic_string();
        CheckMaterial(asset->SaveToFile(savedPath),"Material serialization failed");
        auto loaded = AssetManager::Get().LoadSync<MaterialAsset>(savedPath);
        CheckMaterial(loaded && loaded->GetMaterial()->IsTwoSided(),"Saved state did not reload");
        CheckMaterial(loaded->GetMaterial()->GetCullMode()==ERHICullMode::Front,"Saved cull mode did not reload");
        CheckMaterial(std::abs(ScalarValue(*loaded->GetMaterial()->GetRenderProxy(),"Roughness")-0.7f)<0.0001f,"Saved values did not reload");
        CheckMaterial(std::get<std::array<float,4>>(loaded->GetMaterial()->GetParameter("UVTiling",EMaterialParameterType::Vector)->GetValueVariant())[1]==3,"Saved vector did not reload");
        CheckMaterial(asset->GetPath()!=(directory/"saved.json").generic_string(),"Saving a copy changed asset identity");
        const auto compilationRevision = material->GetRevisions().Compilation;
        material->SetShadingModel(EShadingModel::Unlit);
        CheckMaterial(material->GetRevisions().Compilation==compilationRevision+1 &&
                      oldState->GetShadingModel()==EShadingModel::Lit,"Program change did not preserve the old resource");
        auto resource = material->GetResource();
        bool failed = false;
        try {resource->GetMeshShader(MeshMaterialShaderKey{});}
        catch (const std::runtime_error&) {failed=true;}
        CheckMaterial(failed && resource->GetLastCompileState()==EMaterialCompileState::Failed && !resource->GetLastError().empty(),"Compilation failure is not inspectable");
        material->ResetParameter("Roughness",EMaterialParameterType::Scalar);
        CheckMaterial(std::abs(ScalarValue(*material->GetRenderProxy(),"Roughness")-0.2f)<0.0001f,"Base reset did not restore the declaration default");
        std::cout << "Material declarations, validation, snapshots, overrides, revisions, texture ownership and JSON round-trip passed" << std::endl;
    }
};
REGISTER_RENDER_TEST("MaterialContractTest",MaterialContractTest);
}
