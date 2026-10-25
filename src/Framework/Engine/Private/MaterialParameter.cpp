#include "MaterialParameter.h"
#include "TextureAsset.h"
#include <cmath>
#include <stdexcept>

namespace Engine {
std::string CanonicalMaterialParameterName(const std::string& name)
{
    if (name.empty()) throw std::invalid_argument("Material parameter name cannot be empty");
    auto canonical = name;
    if (canonical[0] >= 'a' && canonical[0] <= 'z') canonical[0] -= 'a' - 'A';
    return canonical;
}
const std::vector<MaterialParameterDefinition>& GetSurfaceMaterialParameterDefinitions()
{
    using S = EMaterialParameterSemantic;
    using T = EMaterialParameterType;
    static const std::vector<MaterialParameterDefinition> definitions = {
        {"BaseColor", S::BaseColor, T::Vector, "Surface", "BaseColor", std::array<float,4>{1,1,1,1}},
        {"EmissiveColor", S::Custom, T::Vector, "Surface", "EmissiveColor", std::array<float,4>{0,0,0,0}},
        {"Opacity", S::Custom, T::Scalar, "Surface", "Opacity", 1.0f},
        {"OpacityMask", S::Custom, T::Scalar, "Surface", "OpacityMask", 1.0f},
        {"Metallic", S::Metallic, T::Scalar, "PBR", "Metallic", 0.8f},
        {"Roughness", S::Roughness, T::Scalar, "PBR", "Roughness", 0.2f},
        {"Specular", S::Custom, T::Scalar, "PBR", "Specular", 0.5f},
        {"AmbientOcclusion", S::Custom, T::Scalar, "PBR", "AmbientOcclusion", 1.0f},
        {"NormalIntensity", S::Custom, T::Scalar, "Normal", "NormalIntensity", 1.0f},
        {"UVTiling", S::Custom, T::Vector, "UV", "UVTiling", std::array<float,4>{1,1,0,0}},
        {"UVOffset", S::Custom, T::Vector, "UV", "UVOffset", std::array<float,4>{0,0,0,0}},
        {"AlphaCutoff", S::Custom, T::Scalar, "Surface", "AlphaCutoff", 0.5f},
        {"BaseColor", S::BaseColor, T::Texture, "Textures", "BaseColorTexture", std::shared_ptr<TextureAsset>{}},
        {"Normal", S::Normal, T::Texture, "Textures", "NormalTexture", std::shared_ptr<TextureAsset>{}},
        {"Opacity", S::Custom, T::Texture, "Textures", "OpacityTexture", std::shared_ptr<TextureAsset>{}},
        {"ORM", S::Custom, T::Texture, "Textures", "ORMTexture", std::shared_ptr<TextureAsset>{}},
        {"Emissive", S::Custom, T::Texture, "Textures", "EmissiveTexture", std::shared_ptr<TextureAsset>{}}
    };
    return definitions;
}
const MaterialParameterDefinition& FindMaterialParameterDefinition(const std::string& name, EMaterialParameterType type)
{
    const auto canonical = CanonicalMaterialParameterName(name);
    for (const auto& definition : GetSurfaceMaterialParameterDefinitions())
        if (definition.Name == canonical && definition.Type == type) return definition;
    throw std::invalid_argument("Unknown material parameter or incompatible type: " + name);
}
std::unique_ptr<MaterialParameter> CreateMaterialParameter(const std::string& name, const MaterialParameterValue& value)
{
    const auto type = static_cast<EMaterialParameterType>(value.index());
    const auto& definition = FindMaterialParameterDefinition(name, type);
    using S = EMaterialParameterSemantic;
    if (type == EMaterialParameterType::Scalar)
    {
        const auto scalar = std::get<float>(value);
        if (definition.Semantic == S::Roughness) return std::make_unique<TMaterialParameter<S::Roughness,FScalarType>>(definition.Name,scalar);
        if (definition.Semantic == S::Metallic) return std::make_unique<TMaterialParameter<S::Metallic,FScalarType>>(definition.Name,scalar);
        return std::make_unique<TMaterialParameter<S::Custom,FScalarType>>(definition.Name,scalar);
    }
    if (type == EMaterialParameterType::Vector)
    {
        const auto vector = std::get<std::array<float,4>>(value);
        if (definition.Semantic == S::BaseColor) return std::make_unique<TMaterialParameter<S::BaseColor,FVectorType>>(definition.Name,vector);
        return std::make_unique<TMaterialParameter<S::Custom,FVectorType>>(definition.Name,vector);
    }
    if (type == EMaterialParameterType::Texture)
    {
        const auto texture = std::get<std::shared_ptr<TextureAsset>>(value);
        if (definition.Semantic == S::BaseColor) return std::make_unique<TMaterialParameter<S::BaseColor,FTextureType>>(definition.Name,texture);
        if (definition.Semantic == S::Normal) return std::make_unique<TMaterialParameter<S::Normal,FTextureType>>(definition.Name,texture);
        return std::make_unique<TMaterialParameter<S::Custom,FTextureType>>(definition.Name,texture);
    }
    throw std::invalid_argument("The built-in Surface material has no integer parameter: " + name);
}
void ValidateMaterialParameter(const MaterialParameter& parameter, bool bInstanceOverride)
{
    const auto& definition = FindMaterialParameterDefinition(parameter.GetName(), parameter.GetType());
    if (definition.Semantic != parameter.GetSemantic()) throw std::invalid_argument("Material parameter semantic mismatch: " + parameter.GetName());
    if (bInstanceOverride && !definition.bInstanceEditable) throw std::invalid_argument("Material parameter cannot be overridden: " + parameter.GetName());
    const auto value = parameter.GetValueVariant();
    if (const auto* scalar = std::get_if<float>(&value); scalar && !std::isfinite(*scalar))
        throw std::invalid_argument("Material scalar must be finite: " + parameter.GetName());
    if (const auto* vector = std::get_if<std::array<float,4>>(&value))
        for (float element : *vector) if (!std::isfinite(element)) throw std::invalid_argument("Material vector must be finite: " + parameter.GetName());
    if (const auto* texture = std::get_if<std::shared_ptr<TextureAsset>>(&value); texture && *texture && !(*texture)->GetTexture())
        throw std::invalid_argument("Material texture asset is not loaded: " + parameter.GetName());
}
}
