#pragma once
#include "EngineExport.h"
#include "MaterialCore.h" // 包含底层的 ShaderMap
#include <string>
#include <unordered_map>
#include <memory>

// 前置声明你自己的资产类型和底层的渲染资源
namespace RenderCore {
    class RenderTexture;
    class Shader;
    class RenderGraphBuilder;
}

namespace Engine {
    class Material;
    class MaterialRenderProxy;
    class MaterialInterface;
    class MaterialAsset;
    class TextureAsset; // 使用你定义的资产类
    // ============================================================================
// 2. 基础数据类型标签与核心语义定义
// ============================================================================
    struct FScalarType { using Type = float; };
    struct FVectorType { using Type = std::array<float, 4>; };
    struct FTextureType { using Type = RenderCore::RenderTexture*; };
    struct FIntType { using Type = int; };

    enum class EMaterialParameterSemantic : uint8_t {
        BaseColor,
        Roughness,
        Metallic,
        Normal,
        Custom
    };

    // ============================================================================
    // 3. 编译期合法性检查白名单（模板特化限制）
    // ============================================================================
    template <EMaterialParameterSemantic Semantic, typename TParamType>
    struct TIsValidCombination : std::false_type {};

    template <> struct TIsValidCombination<EMaterialParameterSemantic::Roughness, FScalarType> : std::true_type {};
    template <> struct TIsValidCombination<EMaterialParameterSemantic::Metallic, FScalarType> : std::true_type {};
    template <> struct TIsValidCombination<EMaterialParameterSemantic::BaseColor, FVectorType> : std::true_type {};
    template <> struct TIsValidCombination<EMaterialParameterSemantic::BaseColor, FTextureType> : std::true_type {};
    template <> struct TIsValidCombination<EMaterialParameterSemantic::Normal, FTextureType> : std::true_type {};

    // Custom 语义作为万能后备，全类型支持
    template <> struct TIsValidCombination<EMaterialParameterSemantic::Custom, FScalarType> : std::true_type {};
    template <> struct TIsValidCombination<EMaterialParameterSemantic::Custom, FVectorType> : std::true_type {};
    template <> struct TIsValidCombination<EMaterialParameterSemantic::Custom, FTextureType> : std::true_type {};
    template <> struct TIsValidCombination<EMaterialParameterSemantic::Custom, FIntType> : std::true_type {};

    // ============================================================================
    // 4. 材质参数多态核心类
    // ============================================================================
    class ENGINE_API MaterialParameter {
    public:
        MaterialParameter(const std::string& InName, EMaterialParameterSemantic InSemantic);
        virtual ~MaterialParameter() = default;

        const std::string& GetName() const { return Name; }
        EMaterialParameterSemantic GetSemantic() const { return Semantic; }

        virtual std::unique_ptr<MaterialParameter> Clone() const = 0;

    private:
        std::string Name;
        EMaterialParameterSemantic Semantic;
    };

    template <EMaterialParameterSemantic InSemantic, typename TParamType>
    class TMaterialParameter : public MaterialParameter
    {
        static_assert(
            TIsValidCombination<InSemantic, TParamType>::value,
            "Error: Invalid combination of EMaterialParameterSemantic and ParamType!");

    public:
        using UnderlyingType = typename TParamType::Type;

        TMaterialParameter(const std::string& InName, const UnderlyingType& InValue)
            : MaterialParameter(InName, InSemantic), Value(InValue)
        {
        }

        const UnderlyingType& GetValue() const
        {
            return Value;
        }

        void SetValue(const UnderlyingType& InValue)
        {
            Value = InValue;
        }

        std::unique_ptr<MaterialParameter> Clone() const override
        {
            return std::make_unique<
                TMaterialParameter<InSemantic, TParamType>>(
                    GetName(),
                    Value);
        }

    private:
        UnderlyingType Value;
    };
    
    
    // ============================================================================
    // 5. MaterialRenderProxy (渲染线程数据快照)
    // ============================================================================
    class ENGINE_API MaterialRenderProxy {
    public:
        MaterialRenderProxy(const MaterialInterface* parent);
        virtual ~MaterialRenderProxy() = default;

        MaterialRenderProxy(const MaterialRenderProxy&) = delete;
        MaterialRenderProxy& operator=(const MaterialRenderProxy&) = delete;

		const MaterialInterface* GetParent() const { return Parent; }
        void AddParameter(std::unique_ptr<MaterialParameter> Param);

        template <EMaterialParameterSemantic Semantic, typename TParamType>
        bool GetValue(const std::string& Name, typename TParamType::Type& OutValue) const {
            for (const auto& Param : Parameters) {
                if (Param->GetSemantic() == Semantic && Param->GetName() == Name) {
                    using TargetParamClass = TMaterialParameter<Semantic, TParamType>;
                    auto* TypedParam = dynamic_cast<const TargetParamClass*>(Param.get());
                    if (TypedParam) {
                        OutValue = TypedParam->GetValue();
                        return true;
                    }
                }
            }
            return false;
        }

    private:
        std::vector<std::unique_ptr<MaterialParameter>> Parameters;
        const MaterialInterface* Parent;
    };

    // ============================================================================
    // 6. 游戏线程材质框架接口与子类
    // ============================================================================
    class Material;

    class ENGINE_API MaterialInterface {
    public:
        MaterialInterface() = default;
        virtual ~MaterialInterface() = default;

        virtual const Material* GetMaterial() const = 0;
        virtual MaterialRenderProxy* GetRenderProxy() const = 0;
        virtual std::shared_ptr<MaterialAsset> GetAsset() const = 0;
        virtual uint64_t GetRevision() const = 0;
        virtual EBlendMode GetBlendMode() const = 0;
        virtual EShadingModel GetShadingModel() const = 0;

        template <EMaterialParameterSemantic Semantic, typename TParamType>
        void SetParameterValue(const std::string& Name, const typename TParamType::Type& Value) {
            static_assert(TIsValidCombination<Semantic, TParamType>::value,
                "Error: Pre-compiler checked invalid Semantic and Type combination!");
            SetParameterValueImpl(Semantic, Name, std::make_unique<TMaterialParameter<Semantic, TParamType>>(Name, Value));
        }

    protected:
        virtual void SetParameterValueImpl(EMaterialParameterSemantic Semantic, const std::string& Name, std::unique_ptr<MaterialParameter> Param) = 0;
    };

    class ENGINE_API Material : public MaterialInterface {
    public:
        Material() = default;
        ~Material() override = default;

        const Material* GetMaterial() const override { return this; }
        std::shared_ptr<MaterialAsset> GetAsset() const override { return OwnerAsset.lock(); }
        uint64_t GetRevision() const override { return Revision; }
        EBlendMode GetBlendMode() const override { return BlendMode; }
        void SetBlendMode(EBlendMode InMode);
        EShadingModel GetShadingModel() const { return ShadingModel; }
        void SetShadingModel(EShadingModel InModel);

        MaterialRenderProxy* GetRenderProxy() const override;
        const std::unordered_map<std::string, std::unique_ptr<MaterialParameter>>& GetDefaultParameters() const { return DefaultParameters; }

    protected:
        void SetParameterValueImpl(EMaterialParameterSemantic Semantic, const std::string& Name, std::unique_ptr<MaterialParameter> Param) override;

    private:
        friend class MaterialAsset;
        std::weak_ptr<MaterialAsset> OwnerAsset;
        uint64_t Revision = 0;
        EBlendMode BlendMode = EBlendMode::Opaque;
		EShadingModel ShadingModel = EShadingModel::Unlit;
        std::unordered_map<std::string, std::unique_ptr<MaterialParameter>> DefaultParameters;

        mutable std::unique_ptr<MaterialRenderProxy> CachedProxy;
        mutable bool bProxyDirty = true;
    };

    class ENGINE_API MaterialInstance : public MaterialInterface {
    public:
        explicit MaterialInstance(std::shared_ptr<MaterialAsset> InParent);
        ~MaterialInstance() override = default;

        const Material* GetMaterial() const override;
        std::shared_ptr<MaterialAsset> GetAsset() const override { return ParentAsset; }
        uint64_t GetRevision() const override;
        EBlendMode GetBlendMode() const override;
        EShadingModel GetShadingModel() const override;


        MaterialRenderProxy* GetRenderProxy() const override;

    protected:
        void SetParameterValueImpl(EMaterialParameterSemantic Semantic, const std::string& Name, std::unique_ptr<MaterialParameter> Param) override;

    private:
        std::shared_ptr<MaterialAsset> ParentAsset;
        uint64_t Revision = 0;
        mutable uint64_t CachedParentRevision = 0;
        std::unordered_map<std::string, std::unique_ptr<MaterialParameter>> OverriddenParameters;

        mutable std::unique_ptr<MaterialRenderProxy> CachedProxy;
        mutable bool bProxyDirty = true;
    };
    class MaterialShaderParameters;

}