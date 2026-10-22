#include "Material.h"
#include "RenderResource.h" // 包含你的基类以及 RenderTexture 的定义
#include "MaterialAsset.h"
#include <stdexcept>
#include "Shader.h"
#include "MaterialShaderParameter.h"
#include "RenderGraphBuilder.h"
using namespace RenderCore;
namespace Engine {
    MeshMaterialShaderMap GMeshMaterialShaderMap;
    MaterialShaderMap GMaterialShaderMap;

    // ============================================================================
// MaterialParameter 实现
// ============================================================================
    MaterialParameter::MaterialParameter(const std::string& inName, EMaterialParameterSemantic inSemantic)
        : m_Name(inName), m_Semantic(inSemantic) {
    }

    MaterialRenderProxy::MaterialRenderProxy(const MaterialInterface* parent) : m_Parent(parent) {

    }

    // ============================================================================
    // MaterialRenderProxy 实现
    // ============================================================================
    void MaterialRenderProxy::AddParameter(std::unique_ptr<MaterialParameter> param) {
        if (param) {
            m_Parameters.push_back(std::move(param));
        }
    }

    // ============================================================================
    // Material 实现
    // ============================================================================
    void Material::SetBlendMode(EBlendMode inMode) {
        if (m_BlendMode != inMode) {
            m_BlendMode = inMode;
            m_bProxyDirty = true;
            ++m_Revision;
        }
    }
    void Material::SetShadingModel(EShadingModel inModel) {
		if (m_ShadingModel != inModel) {
			m_ShadingModel = inModel;
			m_bProxyDirty = true;
            ++m_Revision;
		}
    }

    MaterialRenderProxy* Material::GetRenderProxy() const {
        if (m_bProxyDirty || !m_CachedProxy) {
            m_CachedProxy = std::make_unique<MaterialRenderProxy>(this);
            // 将资产内持有的基础默认数据全部 Clone 进只读代理
            for (const auto& [Name, Param] : m_DefaultParameters) {
                m_CachedProxy->AddParameter(Param->Clone());
            }
            m_bProxyDirty = false;
        }
        return m_CachedProxy.get();
    }

    void Material::SetParameterValueImpl(EMaterialParameterSemantic semantic, const std::string& name, std::unique_ptr<MaterialParameter> param) {
        const auto key = name + ":" + std::to_string(static_cast<int>(semantic)) + ":" + typeid(*param).name();
        m_DefaultParameters[key] = std::move(param);
        ++m_Revision;
        m_bProxyDirty = true;
    }

    // ============================================================================
    // MaterialInstance 实现
    // ============================================================================
    MaterialInstance::MaterialInstance(std::shared_ptr<MaterialAsset> inParent)
        : m_ParentAsset(std::move(inParent))
    {
        if (!m_ParentAsset || !m_ParentAsset->GetMaterial())
            throw std::invalid_argument("A material instance requires a loaded parent MaterialAsset");
    }

    const Material* MaterialInstance::GetMaterial() const { return m_ParentAsset->GetMaterial(); }
    EBlendMode MaterialInstance::GetBlendMode() const { return GetMaterial()->GetBlendMode(); }
    EShadingModel MaterialInstance::GetShadingModel() const { return GetMaterial()->GetShadingModel(); }
    uint64_t MaterialInstance::GetRevision() const { return m_Revision + GetMaterial()->GetRevision(); }

    MaterialRenderProxy* MaterialInstance::GetRenderProxy() const {
        const auto parentRevision = GetMaterial()->GetRevision();
        if (m_bProxyDirty || !m_CachedProxy || m_CachedParentRevision != parentRevision) {
            m_CachedParentRevision = parentRevision;
            m_CachedProxy = std::make_unique<MaterialRenderProxy>(this);

            const Material* baseMaterial = GetMaterial();
            if (baseMaterial) {
                // 1. 倒装父材质的默认参数
                for (const auto& [Name, Param] : baseMaterial->GetDefaultParameters()) {
                    if (!m_OverriddenParameters.contains(Name)) {
                        m_CachedProxy->AddParameter(Param->Clone());
                    }
                }
            }

            // 2. 倒装实例自己重写（Override）的参数覆盖默认数据
            for (const auto& [Name, Param] : m_OverriddenParameters) {
                m_CachedProxy->AddParameter(Param->Clone());
            }

            m_bProxyDirty = false;
        }
        return m_CachedProxy.get();
    }

    void MaterialInstance::SetParameterValueImpl(EMaterialParameterSemantic semantic, const std::string& name, std::unique_ptr<MaterialParameter> param) {
        const auto key = name + ":" + std::to_string(static_cast<int>(semantic)) + ":" + typeid(*param).name();
        m_OverriddenParameters[key] = std::move(param);
        ++m_Revision;
        m_bProxyDirty = true;
    }

    

} // namespace Engine