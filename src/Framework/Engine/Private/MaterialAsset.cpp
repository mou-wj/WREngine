#include "MaterialAsset.h"
#include "AssetManager.h"
#include "Material.h"
#include "RenderResource.h"
#include "PathInfo.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

namespace Engine {
namespace {
std::string ParameterName(const std::string& name)
{
    if (name == "baseColor") return "BaseColor";
    if (name == "roughness") return "Roughness";
    if (name == "metallic") return "Metallic";
    if (name == "normal") return "Normal";
    return name;
}

void SetScalar(Material& material, const std::string& name, float value)
{
    if (name == "Roughness")
        material.SetParameterValue<EMaterialParameterSemantic::Roughness, FScalarType>(name, value);
    else if (name == "Metallic")
        material.SetParameterValue<EMaterialParameterSemantic::Metallic, FScalarType>(name, value);
    else
        material.SetParameterValue<EMaterialParameterSemantic::Custom, FScalarType>(name, value);
}

void SetVector(Material& material, const std::string& name, const std::array<float, 4>& value)
{
    if (name == "BaseColor")
        material.SetParameterValue<EMaterialParameterSemantic::BaseColor, FVectorType>(name, value);
    else
        material.SetParameterValue<EMaterialParameterSemantic::Custom, FVectorType>(name, value);
}
}

MaterialAsset::MaterialAsset(const std::string& path) : m_Name(Core::GetFileName(path)), m_Path(path) {}
MaterialAsset::~MaterialAsset() = default;

std::shared_ptr<MaterialAsset> MaterialAsset::Create(
    const std::string& path, const std::string& name, std::unique_ptr<Material> material)
{
    if (!material) throw std::invalid_argument("MaterialAsset cannot own a null material");
    auto asset = std::make_shared<MaterialAsset>(AssetManager::NormalizePath(path));
    asset->m_Name = name.empty() ? Core::GetFileName(path) : name;
    material->m_OwnerAsset = asset;
    asset->m_MaterialPtr = std::move(material);
    return asset;
}

bool MaterialAsset::Load()
{
    try
    {
        if (m_MaterialPtr) throw std::runtime_error("In-place material reload requires dependency invalidation");
        std::ifstream file(m_Path);
        if (!file) throw std::runtime_error("Cannot open material file");
        nlohmann::json data;
        file >> data;
        if (data.value("version", 1) != 1) throw std::runtime_error("Unsupported material schema version");
        auto material = std::make_unique<Material>();
        const auto blend = data.value("blendMode", std::string("Opaque"));
        if (blend == "Opaque") material->SetBlendMode(EBlendMode::Opaque);
        else if (blend == "Masked") material->SetBlendMode(EBlendMode::Masked);
        else if (blend == "Translucent") material->SetBlendMode(EBlendMode::Translucent);
        else throw std::runtime_error("Unknown blendMode: " + blend);
        const auto shading = data.value("shadingModel", std::string("Unlit"));
        if (shading == "Lit") material->SetShadingModel(EShadingModel::Lit);
        else if (shading == "Unlit") material->SetShadingModel(EShadingModel::Unlit);
        else throw std::runtime_error("Unknown shadingModel: " + shading);

        if (data.contains("staticParameters"))
            for (const auto& [name, value] : data["staticParameters"].items())
                if (value != false)
                    throw std::runtime_error("Unsupported material static parameter: " + name);

        // Accept the previous parameter block as well as the version-1 grouped schema.
        if (data.contains("parameters"))
            for (const auto& [name, value] : data["parameters"].items())
                if (value.is_array()) SetVector(*material, ParameterName(name), value.get<std::array<float, 4>>());
                else SetScalar(*material, ParameterName(name), value.get<float>());
        if (data.contains("scalarParameters"))
            for (const auto& [name, value] : data["scalarParameters"].items())
                SetScalar(*material, ParameterName(name), value.get<float>());
        if (data.contains("vectorParameters"))
            for (const auto& [name, value] : data["vectorParameters"].items())
                SetVector(*material, ParameterName(name), value.get<std::array<float, 4>>());

        std::vector<std::shared_ptr<TextureAsset>> textures;
        if (data.contains("textureParameters"))
            for (const auto& [name, value] : data["textureParameters"].items())
            {
                auto path = std::filesystem::path(value.get<std::string>());
                if (path.is_relative()) path = std::filesystem::path(m_Path).parent_path() / path;
                auto texture = AssetManager::Get().LoadSync<TextureAsset>(path.generic_string());
                const auto parameter = ParameterName(name);
                if (parameter == "BaseColor")
                    material->SetParameterValue<EMaterialParameterSemantic::BaseColor, FTextureType>(parameter, texture->GetTexture());
                else if (parameter == "Normal")
                    material->SetParameterValue<EMaterialParameterSemantic::Normal, FTextureType>(parameter, texture->GetTexture());
                else
                    material->SetParameterValue<EMaterialParameterSemantic::Custom, FTextureType>(parameter, texture->GetTexture());
                textures.push_back(std::move(texture));
            }
        material->m_OwnerAsset = std::static_pointer_cast<MaterialAsset>(shared_from_this());
        m_Name = data.value("name", m_Name);
        m_Textures = std::move(textures);
        m_MaterialPtr = std::move(material);
        return true;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "Material asset '" << m_Path << "': " << ex.what() << std::endl;
        return false;
    }
}
}
