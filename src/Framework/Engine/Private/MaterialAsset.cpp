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

        const auto cull = data.value("cullMode",std::string("Back"));
        if (cull == "None") material->SetCullMode(RHI::ERHICullMode::None);
        else if (cull == "Front") material->SetCullMode(RHI::ERHICullMode::Front);
        else if (cull == "Back") material->SetCullMode(RHI::ERHICullMode::Back);
        else throw std::runtime_error("Unknown cullMode: " + cull);

        if (data.contains("staticParameters"))
            for (const auto& [name, value] : data["staticParameters"].items())
                if (name == "twoSided") material->SetTwoSided(value.get<bool>());
                else if (value != false)
                    throw std::runtime_error("Unsupported material static parameter: " + name);

        // Accept the previous parameter block as well as the version-1 grouped schema.
        if (data.contains("parameters"))
            for (const auto& [name, value] : data["parameters"].items())
                if (value.is_array()) material->SetParameter(name, value.get<std::array<float, 4>>());
                else material->SetParameter(name, value.get<float>());
        if (data.contains("scalarParameters"))
            for (const auto& [name, value] : data["scalarParameters"].items())
                material->SetParameter(name, value.get<float>());
        if (data.contains("vectorParameters"))
            for (const auto& [name, value] : data["vectorParameters"].items())
                material->SetParameter(name, value.get<std::array<float, 4>>());

        if (data.contains("textureParameters"))
            for (const auto& [name, value] : data["textureParameters"].items())
            {
                if (value.is_null())
                {
                    material->SetParameter(name, std::shared_ptr<TextureAsset>{});
                    continue;
                }
                auto path = std::filesystem::path(value.get<std::string>());
                if (path.is_relative()) path = std::filesystem::path(m_Path).parent_path() / path;
                auto texture = AssetManager::Get().LoadSync<TextureAsset>(path.generic_string());
                if (!texture) throw std::runtime_error("Cannot load material texture: " + path.generic_string());
                material->SetParameter(name, texture);
            }
        material->m_OwnerAsset = std::static_pointer_cast<MaterialAsset>(shared_from_this());
        m_Name = data.value("name", m_Name);
        m_MaterialPtr = std::move(material);
        return true;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "Material asset '" << m_Path << "': " << ex.what() << std::endl;
        return false;
    }
}
bool MaterialAsset::SaveToFile(const std::string& path) const
{
    try
    {
        if (!m_MaterialPtr) throw std::runtime_error("Cannot save an unloaded material");
        if (path.find('#') != std::string::npos) throw std::runtime_error("Save embedded materials to a standalone file");
        nlohmann::json data;
        data["version"] = 1;
        data["name"] = m_Name;
        switch (m_MaterialPtr->GetBlendMode())
        {
        case EBlendMode::Opaque: data["blendMode"] = "Opaque"; break;
        case EBlendMode::Masked: data["blendMode"] = "Masked"; break;
        case EBlendMode::Translucent: data["blendMode"] = "Translucent"; break;
        }
        data["shadingModel"] = m_MaterialPtr->GetShadingModel() == EShadingModel::Lit ? "Lit" : "Unlit";
        data["staticParameters"]["twoSided"] = m_MaterialPtr->IsTwoSided();
        switch (m_MaterialPtr->GetCullMode())
        {
        case RHI::ERHICullMode::None: data["cullMode"] = "None"; break;
        case RHI::ERHICullMode::Front: data["cullMode"] = "Front"; break;
        case RHI::ERHICullMode::Back: data["cullMode"] = "Back"; break;
        }

        for (const auto& definition : m_MaterialPtr->GetParameterDefinitions())
        {
            const auto value = m_MaterialPtr->GetParameter(definition.Name,definition.Type)->GetValueVariant();
            if (const auto* scalar = std::get_if<float>(&value)) data["scalarParameters"][definition.Name] = *scalar;
            else if (const auto* vector = std::get_if<std::array<float,4>>(&value)) data["vectorParameters"][definition.Name] = *vector;
            else if (const auto* texture = std::get_if<std::shared_ptr<TextureAsset>>(&value))
            {
                if (!*texture) data["textureParameters"][definition.Name] = nullptr;
                else
                {
                    const auto texturePath = std::filesystem::path((*texture)->GetPath());
                    if (!texturePath.is_absolute()) throw std::runtime_error("A runtime/built-in texture cannot be serialized as a file dependency");
                    data["textureParameters"][definition.Name] = texturePath.generic_string();
                }
            }
        }
        // Saving a copy does not rename the registered asset or change its identity.
        std::ofstream file(path);
        if (!file || !(file << data.dump(2) << '\n')) throw std::runtime_error("Cannot write material file");
        file.close();
        if (!file) throw std::runtime_error("Cannot flush material file");
        return true;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Save material '" << path << "': " << error.what() << std::endl;
        return false;
    }
}

}
