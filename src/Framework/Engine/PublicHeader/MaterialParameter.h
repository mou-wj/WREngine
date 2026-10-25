#pragma once
#include "EngineExport.h"
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>
#include "HashHelper.hpp"

namespace Engine {
class TextureAsset;
enum class EMaterialParameterType : uint8_t { Scalar, Vector, Texture, Int };
enum class EMaterialParameterSemantic : uint8_t { BaseColor, Roughness, Metallic, Normal, Custom };
struct FScalarType { using Type = float; static constexpr auto ValueType = EMaterialParameterType::Scalar; };
struct FVectorType { using Type = std::array<float, 4>; static constexpr auto ValueType = EMaterialParameterType::Vector; };
// Reference the asset; the actual texture remains owned by TextureAsset.
struct FTextureType { using Type = std::shared_ptr<TextureAsset>; static constexpr auto ValueType = EMaterialParameterType::Texture; };
struct FIntType { using Type = int; static constexpr auto ValueType = EMaterialParameterType::Int; };
using MaterialParameterValue = std::variant<float, std::array<float, 4>, std::shared_ptr<TextureAsset>, int>;

template <EMaterialParameterSemantic Semantic, typename T> struct TIsValidCombination : std::false_type {};
template <> struct TIsValidCombination<EMaterialParameterSemantic::Roughness, FScalarType> : std::true_type {};
template <> struct TIsValidCombination<EMaterialParameterSemantic::Metallic, FScalarType> : std::true_type {};
template <> struct TIsValidCombination<EMaterialParameterSemantic::BaseColor, FVectorType> : std::true_type {};
template <> struct TIsValidCombination<EMaterialParameterSemantic::BaseColor, FTextureType> : std::true_type {};
template <> struct TIsValidCombination<EMaterialParameterSemantic::Normal, FTextureType> : std::true_type {};
template <> struct TIsValidCombination<EMaterialParameterSemantic::Custom, FScalarType> : std::true_type {};
template <> struct TIsValidCombination<EMaterialParameterSemantic::Custom, FVectorType> : std::true_type {};
template <> struct TIsValidCombination<EMaterialParameterSemantic::Custom, FTextureType> : std::true_type {};
template <> struct TIsValidCombination<EMaterialParameterSemantic::Custom, FIntType> : std::true_type {};

struct MaterialParameterKey
{
    std::string Name;
    EMaterialParameterSemantic Semantic;
    EMaterialParameterType Type;
    bool operator==(const MaterialParameterKey&) const = default;
};
struct MaterialParameterKeyHash
{
    size_t operator()(const MaterialParameterKey& key) const
    {
        size_t seed = 0;
        HashCombine(seed, std::hash<std::string>{}(key.Name));
        HashCombine(seed, std::hash<uint8_t>{}(static_cast<uint8_t>(key.Semantic)));
        HashCombine(seed, std::hash<uint8_t>{}(static_cast<uint8_t>(key.Type)));
        return seed;
    }
};
class ENGINE_API MaterialParameter
{
public:
    MaterialParameter(const std::string& name, EMaterialParameterSemantic semantic) : m_Name(name), m_Semantic(semantic) {}
    virtual ~MaterialParameter() = default;
    const std::string& GetName() const { return m_Name; }
    EMaterialParameterSemantic GetSemantic() const { return m_Semantic; }
    MaterialParameterKey GetKey() const { return { m_Name, m_Semantic, GetType() }; }
    virtual EMaterialParameterType GetType() const = 0;
    virtual MaterialParameterValue GetValueVariant() const = 0;
    virtual std::unique_ptr<MaterialParameter> Clone() const = 0;
private:
    std::string m_Name;
    EMaterialParameterSemantic m_Semantic;
};
template <EMaterialParameterSemantic Semantic, typename T>
class TMaterialParameter : public MaterialParameter
{
    static_assert(TIsValidCombination<Semantic, T>::value, "Invalid material parameter semantic/type");
public:
    using UnderlyingType = typename T::Type;
    TMaterialParameter(const std::string& name, const UnderlyingType& value) : MaterialParameter(name, Semantic), m_Value(value) {}
    EMaterialParameterType GetType() const override { return T::ValueType; }
    const UnderlyingType& GetValue() const { return m_Value; }
    void SetValue(const UnderlyingType& value) { m_Value = value; }
    MaterialParameterValue GetValueVariant() const override { return m_Value; }
    std::unique_ptr<MaterialParameter> Clone() const override { return std::make_unique<TMaterialParameter>(GetName(), m_Value); }
private:
    UnderlyingType m_Value;
};
// Shared contract for editor inspection, import, runtime edits and shader bindings.
struct MaterialParameterDefinition
{
    std::string Name;
    EMaterialParameterSemantic Semantic;
    EMaterialParameterType Type;
    std::string Group;
    std::string ShaderBinding;
    MaterialParameterValue DefaultValue;
    bool bInstanceEditable = true;
};
using MaterialParameterMap = std::unordered_map<MaterialParameterKey, std::unique_ptr<MaterialParameter>, MaterialParameterKeyHash>;
ENGINE_API std::string CanonicalMaterialParameterName(const std::string& name);
ENGINE_API const std::vector<MaterialParameterDefinition>& GetSurfaceMaterialParameterDefinitions();
ENGINE_API const MaterialParameterDefinition& FindMaterialParameterDefinition(const std::string& name, EMaterialParameterType type);
ENGINE_API std::unique_ptr<MaterialParameter> CreateMaterialParameter(const std::string& name, const MaterialParameterValue& value);
ENGINE_API void ValidateMaterialParameter(const MaterialParameter& parameter, bool bInstanceOverride);
}
