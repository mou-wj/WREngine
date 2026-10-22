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
    MaterialParameter::MaterialParameter(const std::string& InName, EMaterialParameterSemantic InSemantic)
        : Name(InName), Semantic(InSemantic) {
    }

    MaterialRenderProxy::MaterialRenderProxy(const MaterialInterface* parent) : Parent(parent) {

    }

    // ============================================================================
    // MaterialRenderProxy 实现
    // ============================================================================
    void MaterialRenderProxy::AddParameter(std::unique_ptr<MaterialParameter> Param) {
        if (Param) {
            Parameters.push_back(std::move(Param));
        }
    }

    // ============================================================================
    // Material 实现
    // ============================================================================
    void Material::SetBlendMode(EBlendMode InMode) {
        if (BlendMode != InMode) {
            BlendMode = InMode;
            bProxyDirty = true;
            ++Revision;
        }
    }
    void Material::SetShadingModel(EShadingModel InModel) {
		if (ShadingModel != InModel) {
			ShadingModel = InModel;
			bProxyDirty = true;
            ++Revision;
		}
    }

    MaterialRenderProxy* Material::GetRenderProxy() const {
        if (bProxyDirty || !CachedProxy) {
            CachedProxy = std::make_unique<MaterialRenderProxy>(this);
            // 将资产内持有的基础默认数据全部 Clone 进只读代理
            for (const auto& [Name, Param] : DefaultParameters) {
                CachedProxy->AddParameter(Param->Clone());
            }
            bProxyDirty = false;
        }
        return CachedProxy.get();
    }

    void Material::SetParameterValueImpl(EMaterialParameterSemantic Semantic, const std::string& Name, std::unique_ptr<MaterialParameter> Param) {
        const auto key = Name + ":" + std::to_string(static_cast<int>(Semantic)) + ":" + typeid(*Param).name();
        DefaultParameters[key] = std::move(Param);
        ++Revision;
        bProxyDirty = true;
    }

    // ============================================================================
    // MaterialInstance 实现
    // ============================================================================
    MaterialInstance::MaterialInstance(std::shared_ptr<MaterialAsset> InParent)
        : ParentAsset(std::move(InParent))
    {
        if (!ParentAsset || !ParentAsset->GetMaterial())
            throw std::invalid_argument("A material instance requires a loaded parent MaterialAsset");
    }

    const Material* MaterialInstance::GetMaterial() const { return ParentAsset->GetMaterial(); }
    EBlendMode MaterialInstance::GetBlendMode() const { return GetMaterial()->GetBlendMode(); }
    EShadingModel MaterialInstance::GetShadingModel() const { return GetMaterial()->GetShadingModel(); }
    uint64_t MaterialInstance::GetRevision() const { return Revision + GetMaterial()->GetRevision(); }

    MaterialRenderProxy* MaterialInstance::GetRenderProxy() const {
        const auto parentRevision = GetMaterial()->GetRevision();
        if (bProxyDirty || !CachedProxy || CachedParentRevision != parentRevision) {
            CachedParentRevision = parentRevision;
            CachedProxy = std::make_unique<MaterialRenderProxy>(this);

            const Material* BaseMaterial = GetMaterial();
            if (BaseMaterial) {
                // 1. 倒装父材质的默认参数
                for (const auto& [Name, Param] : BaseMaterial->GetDefaultParameters()) {
                    if (!OverriddenParameters.contains(Name)) {
                        CachedProxy->AddParameter(Param->Clone());
                    }
                }
            }

            // 2. 倒装实例自己重写（Override）的参数覆盖默认数据
            for (const auto& [Name, Param] : OverriddenParameters) {
                CachedProxy->AddParameter(Param->Clone());
            }

            bProxyDirty = false;
        }
        return CachedProxy.get();
    }

    void MaterialInstance::SetParameterValueImpl(EMaterialParameterSemantic Semantic, const std::string& Name, std::unique_ptr<MaterialParameter> Param) {
        const auto key = Name + ":" + std::to_string(static_cast<int>(Semantic)) + ":" + typeid(*Param).name();
        OverriddenParameters[key] = std::move(Param);
        ++Revision;
        bProxyDirty = true;
    }

    

} // namespace Engine