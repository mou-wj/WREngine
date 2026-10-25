#include "TestBase.h"
#include "Material.h"
#include "MaterialShaderParameter.h"
#include "TextureAsset.h"
#include "RenderResource.h"
#include "RenderGraphBuilder.h"
#include "RHIApi.h"
#include "VertexFactory.h"
#include "LocalVertexFactory.h"
#include <iostream>
#include <stdexcept>

namespace Test {
class MaterialBindingTest : public TestBase
{
public:
    ~MaterialBindingTest() override
    {
        if (m_Api) Teardown();
    }
    void Setup() override
    {
        m_Api.reset(CreateTestRHIApi());
        RHI::GRHIApi = m_Api.get();
        if (!m_Api->Init()) throw std::runtime_error("Cannot initialize RHI");
        RenderCore::InitGlobalRenderResource();
    }
    void Run() override
    {
        using namespace Engine;
        using namespace RenderCore;
        std::array<uint8_t,4> white{}, normalDefault{};
        GlobalWhiteTexture->ReadData(white.data(),0,0);
        GlobalFlatNormalTexture->ReadData(normalDefault.data(),0,0);
        if (white != std::array<uint8_t,4>{255,255,255,255} ||
            normalDefault != std::array<uint8_t,4>{128,128,255,255})
            throw std::runtime_error("Global fallback texture pixels are incorrect");
        Material material;
        material.SetParameter("UVTiling",std::array<float,4>{2,3,0,0});
        material.SetParameter("NormalIntensity",0.4f);
        RenderGraphBuilder builder;
        MaterialShaderParameters parameters{};
        auto snapshot = material.GetRenderProxyShared();
        BuildShaderParameters(snapshot.get(),builder,parameters);
        if (parameters.TextureData.TextureMask != 0 || !parameters.TextureData.BaseColorTexture->GetRHITexture() ||
            parameters.TextureData.BaseColorTexture == parameters.TextureData.NormalTexture)
            throw std::runtime_error("Material fallback textures are missing or aliased");
        if (parameters.UniformData.UVTiling.X != 2 || parameters.UniformData.UVTiling.Y != 3 ||
            parameters.UniformData.NormalIntensity != 0.4f || parameters.UniformData.Opacity != 1)
            throw std::runtime_error("Declared uniform parameters did not reach shader parameters");
        RHI::RHITextureDesc desc{};
        desc.Width = 1; desc.Height = 1;
        desc.Format = RHI::ERHIFormat::R8G8B8A8_UNorm;
        desc.Usage = RHI::ERHITextureCreateFlag::ShaderResource | RHI::ERHITextureCreateFlag::TransferDest;
        auto firstTexture = std::make_shared<RenderTexture>(desc);
        auto secondTexture = std::make_shared<RenderTexture>(desc);
        firstTexture->InitRHIResource(); secondTexture->InitRHIResource();
        const uint8_t normal[4] = {128,128,255,255};
        firstTexture->UploadData(normal,0,0); secondTexture->UploadData(normal,0,0);
        // Identical display names must not alias two different external textures in one graph.
        auto firstAsset = TextureAsset::Create("same-name",firstTexture);
        auto secondAsset = TextureAsset::Create("same-name",secondTexture);
        material.SetParameter("Normal",firstAsset);
        auto firstSnapshot = material.GetRenderProxyShared();
        BuildShaderParameters(firstSnapshot.get(),builder,parameters);
        auto* firstRHI = parameters.TextureData.NormalTexture->GetRHITexture();
        if (parameters.TextureData.TextureMask != (1u<<2) || firstRHI != firstTexture->GetRHI())
            throw std::runtime_error("Normal texture uses the wrong mask bit or binding");
        material.SetParameter("Normal",secondAsset);
        auto secondSnapshot = material.GetRenderProxyShared();
        BuildShaderParameters(secondSnapshot.get(),builder,parameters);
        if (parameters.TextureData.NormalTexture->GetRHITexture() != secondTexture->GetRHI() ||
            parameters.TextureData.NormalTexture->GetRHITexture() == firstRHI)
            throw std::runtime_error("Texture replacement reused a different resource with the same name");
        std::cout << "Material uniforms, GPU-initialized defaults, normal mask and texture replacement bindings passed" << std::endl;
    }
    void Teardown() override
    {
        Engine::GMeshMaterialShaderMap.Clear();
        Engine::GMaterialShaderMap.Clear();
        RenderCore::ReleaseGlobalRenderResource();
        m_Api->Shutdown();
        RHI::GRHIApi = nullptr;
        m_Api.reset();
    }
private:
    std::unique_ptr<RHI::RHIApi> m_Api;
};
REGISTER_RENDER_TEST("MaterialBindingTest",MaterialBindingTest);

class MaterialShaderCompilationTest : public MaterialBindingTest
{
public:
    void Run() override
    {
        using namespace Engine;
        using namespace RenderCore;
        auto& shaderTypes = ShaderType::GetRegisterMap()[ShaderType::EShaderTypeFlag::MeshMaterial];
        auto& factories = VertexFactoryType::GetRegisterMap();
        auto factory = factories.find("LocalVertexFactory");
        if (factory == factories.end()) throw std::runtime_error("LocalVertexFactory is not registered");
        Material material;
        for (auto model : { EShadingModel::Lit, EShadingModel::Unlit })
        {
            material.SetShadingModel(model);
            auto resource = material.GetResource();
            for (const char* name : { "StaticMeshMaterialShaderVS", "StaticMeshMaterialShaderPS", "StaticMeshMaterialGBufferShaderPS" })
            {
                auto type = shaderTypes.find(name);
                if (type == shaderTypes.end()) throw std::runtime_error(std::string("Missing material shader: ")+name);
                MeshMaterialShaderKey key;
                key.ShaderType = static_cast<MeshMaterialShaderType*>(type->second);
                key.VF = factory->second;
                LocalVertexFactoryFeatureFlags flags{};
                flags.SupportsTexCoord = true;
                flags.SupportsNormal = true;
                key.VertexFactoryFlags = flags.PackedFlags;
                auto* shader = resource->GetMeshShader(key);
                if (!shader || !shader->GetRHIShader() || resource->GetLastCompileState()!=EMaterialCompileState::Ready)
                    throw std::runtime_error("Material shader compilation did not produce an RHI shader");
                material.SetParameter("Roughness",0.6f);
                if (material.GetResource()!=resource || resource->GetMeshShader(key)!=shader)
                    throw std::runtime_error("A dynamic parameter edit did not reuse the compiled shader");
            }
        }
        material.SetBlendMode(EBlendMode::Masked);
        bool rejected = false;
        try { material.GetRenderProxy()->ValidateSurfacePass(); }
        catch (const std::runtime_error&) { rejected=true; }
        if (!rejected) throw std::runtime_error("Unsupported masked rendering was accepted");
        GMeshMaterialShaderMap.Clear();
        std::cout << "Lit/Unlit forward and GBuffer shader compilation, RHI shader creation and dynamic-value cache reuse passed" << std::endl;
    }
};
REGISTER_RENDER_TEST("MaterialShaderCompilationTest",MaterialShaderCompilationTest);
}
