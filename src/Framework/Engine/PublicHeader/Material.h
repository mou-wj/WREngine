#pragma once
#include "MaterialCore.h"
#include "MaterialParameter.h"
#include <optional>

namespace Engine {
class Material;
class MaterialAsset;

struct MaterialRevisions
{
    uint64_t Parameters = 0;
    uint64_t RenderState = 0;
    uint64_t Compilation = 0;
    bool operator==(const MaterialRevisions&) const = default;
};

struct MaterialRenderState
{
    EBlendMode BlendMode = EBlendMode::Opaque;
    RHI::ERHICullMode CullMode = RHI::ERHICullMode::Back;
    bool bTwoSided = false;
};

// Draw batches share immutable snapshots of resolved material parameters and state.
class ENGINE_API MaterialRenderProxy
{
public:
    MaterialRenderProxy(std::shared_ptr<MaterialResource> resource, MaterialRenderState renderState,
                        MaterialRevisions revisions, MaterialParameterMap parameters);
    MaterialRenderProxy(const MaterialRenderProxy&) = delete;
    MaterialRenderProxy& operator=(const MaterialRenderProxy&) = delete;
    EBlendMode GetBlendMode() const;
    EShadingModel GetShadingModel() const;
    bool IsTwoSided() const;
    const MaterialRenderState& GetRenderState() const { return m_RenderState; }
    RHI::ERHICullMode ResolveCullMode(std::optional<RHI::ERHICullMode> passOverride = std::nullopt) const;
    void ValidateSurfacePass() const;
    const std::shared_ptr<MaterialResource>& GetResource() const { return m_Resource; }
    const MaterialRevisions& GetRevisions() const { return m_Revisions; }
    const MaterialParameter* GetParameter(const std::string& name, EMaterialParameterType type) const;

    template <EMaterialParameterSemantic Semantic, typename T>
    bool GetValue(const std::string& name, typename T::Type& value) const
    {
        const auto* parameter = GetParameter(name, T::ValueType);
        const auto* typed = dynamic_cast<const TMaterialParameter<Semantic,T>*>(parameter);
        if (!typed) return false;
        value = typed->GetValue();
        return true;
    }
private:
    std::shared_ptr<MaterialResource> m_Resource;
    MaterialRenderState m_RenderState;
    MaterialRevisions m_Revisions;
    MaterialParameterMap m_Parameters;
};

class ENGINE_API MaterialInterface
{
public:
    virtual ~MaterialInterface() = default;
    virtual const Material* GetMaterial() const = 0;
    virtual std::shared_ptr<MaterialAsset> GetAsset() const = 0;
    virtual uint64_t GetRevision() const = 0;
    virtual MaterialRevisions GetRevisions() const = 0;
    virtual EBlendMode GetBlendMode() const = 0;
    virtual EShadingModel GetShadingModel() const = 0;
    virtual std::shared_ptr<const MaterialRenderProxy> GetRenderProxyShared() const = 0;
    const MaterialRenderProxy* GetRenderProxy() const { return GetRenderProxyShared().get(); }
    const std::vector<MaterialParameterDefinition>& GetParameterDefinitions() const { return GetSurfaceMaterialParameterDefinitions(); }
    const MaterialParameter* GetParameter(const std::string& name, EMaterialParameterType type) const { return GetRenderProxy()->GetParameter(name,type); }

    // Runtime-typed entry for property inspectors; validation is shared with the template API.
    bool SetParameter(const std::string& name, const MaterialParameterValue& value);
    template <EMaterialParameterSemantic Semantic, typename T>
    bool SetParameterValue(const std::string& name, const typename T::Type& value)
    {
        static_assert(TIsValidCombination<Semantic,T>::value, "Invalid material parameter semantic/type");
        return SetParameterValueImpl(std::make_unique<TMaterialParameter<Semantic,T>>(CanonicalMaterialParameterName(name),value));
    }
protected:
    virtual bool SetParameterValueImpl(std::unique_ptr<MaterialParameter> parameter) = 0;
};

class ENGINE_API Material : public MaterialInterface
{
public:
    Material();
    Material(const Material&) = delete;
    Material& operator=(const Material&) = delete;
    const Material* GetMaterial() const override { return this; }
    std::shared_ptr<MaterialAsset> GetAsset() const override { return m_OwnerAsset.lock(); }
    uint64_t GetRevision() const override { return m_Revision; }
    MaterialRevisions GetRevisions() const override { return m_Revisions; }
    EBlendMode GetBlendMode() const override { return m_RenderState.BlendMode; }
    EShadingModel GetShadingModel() const override { return m_ShadingModel; }
    bool IsTwoSided() const { return m_RenderState.bTwoSided; }
    RHI::ERHICullMode GetCullMode() const { return m_RenderState.CullMode; }
    const MaterialRenderState& GetRenderState() const { return m_RenderState; }
    void SetBlendMode(EBlendMode mode);
    void SetShadingModel(EShadingModel model);
    void SetTwoSided(bool bTwoSided);
    void SetCullMode(RHI::ERHICullMode mode);
    bool ResetParameter(const std::string& name, EMaterialParameterType type);
    const MaterialParameterMap& GetDefaultParameters() const { return m_DefaultParameters; }
    std::shared_ptr<const MaterialRenderProxy> GetRenderProxyShared() const override;
    std::shared_ptr<MaterialResource> GetResource() const;
protected:
    bool SetParameterValueImpl(std::unique_ptr<MaterialParameter> parameter) override;
private:
    friend class MaterialAsset;
    std::weak_ptr<MaterialAsset> m_OwnerAsset;
    uint64_t m_Revision = 0;
    MaterialRevisions m_Revisions;
    MaterialRenderState m_RenderState;
    EShadingModel m_ShadingModel = EShadingModel::Unlit;
    MaterialParameterMap m_DefaultParameters;
    mutable std::shared_ptr<MaterialResource> m_Resource;
    mutable std::shared_ptr<const MaterialRenderProxy> m_CachedProxy;
};

class ENGINE_API MaterialInstance : public MaterialInterface
{
public:
    explicit MaterialInstance(std::shared_ptr<MaterialAsset> parent);
    MaterialInstance(const MaterialInstance&) = delete;
    MaterialInstance& operator=(const MaterialInstance&) = delete;
    const Material* GetMaterial() const override;
    std::shared_ptr<MaterialAsset> GetAsset() const override { return m_ParentAsset; }
    uint64_t GetRevision() const override;
    MaterialRevisions GetRevisions() const override;
    EBlendMode GetBlendMode() const override;
    EShadingModel GetShadingModel() const override;
    std::shared_ptr<const MaterialRenderProxy> GetRenderProxyShared() const override;
    bool HasOverride(const std::string& name, EMaterialParameterType type) const;
    bool ResetParameter(const std::string& name, EMaterialParameterType type);
    void ResetAllParameters();
protected:
    bool SetParameterValueImpl(std::unique_ptr<MaterialParameter> parameter) override;
private:
    std::shared_ptr<MaterialAsset> m_ParentAsset;
    uint64_t m_Revision = 0;
    mutable uint64_t m_CachedParentRevision = 0;
    MaterialParameterMap m_OverriddenParameters;
    mutable std::shared_ptr<const MaterialRenderProxy> m_CachedProxy;
};
}