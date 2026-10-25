#include "Material.h"
#include "MaterialAsset.h"
#include "TextureAsset.h"
#include <algorithm>
#include <stdexcept>

namespace Engine {
MeshMaterialShaderMap GMeshMaterialShaderMap;
MaterialShaderMap GMaterialShaderMap;

namespace {
MaterialParameterMap CloneParameters(const MaterialParameterMap& source)
{
    MaterialParameterMap result;
    for (const auto& [key,parameter] : source) result.emplace(key,parameter->Clone());
    return result;
}
bool StoreParameter(MaterialParameterMap& parameters, std::unique_ptr<MaterialParameter> parameter)
{
    const auto key = parameter->GetKey();
    auto found = parameters.find(key);
    if (found != parameters.end() && found->second->GetValueVariant() == parameter->GetValueVariant()) return false;
    parameters[key] = std::move(parameter);
    return true;
}
MaterialParameterKey ParameterKey(const std::string& name, EMaterialParameterType type)
{
    const auto& definition = FindMaterialParameterDefinition(name,type);
    return {definition.Name,definition.Semantic,definition.Type};
}
}

MaterialRenderProxy::MaterialRenderProxy(std::shared_ptr<MaterialResource> resource, MaterialRenderState renderState,
                                       MaterialRevisions revisions, MaterialParameterMap parameters)
    : m_Resource(std::move(resource)), m_RenderState(renderState), m_Revisions(revisions), m_Parameters(std::move(parameters)) {}
EBlendMode MaterialRenderProxy::GetBlendMode() const { return m_RenderState.BlendMode; }
EShadingModel MaterialRenderProxy::GetShadingModel() const { return m_Resource->GetShadingModel(); }
bool MaterialRenderProxy::IsTwoSided() const { return m_RenderState.bTwoSided; }
RHI::ERHICullMode MaterialRenderProxy::ResolveCullMode(std::optional<RHI::ERHICullMode> passOverride) const
{
    return passOverride.value_or(m_RenderState.bTwoSided ? RHI::ERHICullMode::None : m_RenderState.CullMode);
}
void MaterialRenderProxy::ValidateSurfacePass() const
{
    if (m_RenderState.BlendMode != EBlendMode::Opaque)
        throw std::runtime_error("BuiltinSurface currently supports opaque rendering only; Masked/Translucent passes are not implemented");
}
const MaterialParameter* MaterialRenderProxy::GetParameter(const std::string& name, EMaterialParameterType type) const
{
    const auto found = m_Parameters.find(ParameterKey(name,type));
    return found == m_Parameters.end() ? nullptr : found->second.get();
}
bool MaterialInterface::SetParameter(const std::string& name, const MaterialParameterValue& value)
{
    return SetParameterValueImpl(CreateMaterialParameter(name,value));
}
Material::Material()
{
    for (const auto& definition : GetParameterDefinitions())
    {
        auto parameter = CreateMaterialParameter(definition.Name,definition.DefaultValue);
        const auto key = parameter->GetKey();
        m_DefaultParameters.emplace(key,std::move(parameter));
    }
}
void Material::SetBlendMode(EBlendMode mode)
{
    if (mode != EBlendMode::Opaque && mode != EBlendMode::Masked && mode != EBlendMode::Translucent)
        throw std::invalid_argument("Invalid material blend mode");
    if (m_RenderState.BlendMode == mode) return;
    m_RenderState.BlendMode = mode;
    ++m_Revision; ++m_Revisions.RenderState;
    m_CachedProxy.reset();
}
void Material::SetShadingModel(EShadingModel model)
{
    if (model != EShadingModel::Lit && model != EShadingModel::Unlit) throw std::invalid_argument("Invalid material shading model");
    if (m_ShadingModel == model) return;
    m_ShadingModel = model;
    ++m_Revision; ++m_Revisions.Compilation; ++m_Revisions.RenderState;
    m_Resource.reset(); m_CachedProxy.reset();
}
void Material::SetTwoSided(bool bTwoSided)
{
    if (m_RenderState.bTwoSided == bTwoSided) return;
    m_RenderState.bTwoSided = bTwoSided;
    ++m_Revision; ++m_Revisions.RenderState;
    m_CachedProxy.reset();
}
void Material::SetCullMode(RHI::ERHICullMode mode)
{
    if (mode != RHI::ERHICullMode::None && mode != RHI::ERHICullMode::Front && mode != RHI::ERHICullMode::Back)
        throw std::invalid_argument("Unsupported material cull mode");
    if (m_RenderState.CullMode == mode) return;
    m_RenderState.CullMode = mode;
    ++m_Revision; ++m_Revisions.RenderState;
    m_CachedProxy.reset();
}
bool Material::SetParameterValueImpl(std::unique_ptr<MaterialParameter> parameter)
{
    ValidateMaterialParameter(*parameter,false);
    if (!StoreParameter(m_DefaultParameters,std::move(parameter))) return false;
    ++m_Revision; ++m_Revisions.Parameters;
    m_CachedProxy.reset();
    return true;
}
bool Material::ResetParameter(const std::string& name, EMaterialParameterType type)
{
    return SetParameter(name,FindMaterialParameterDefinition(name,type).DefaultValue);
}
std::shared_ptr<MaterialResource> Material::GetResource() const
{
    if (!m_Resource) m_Resource = std::make_shared<MaterialResource>(m_ShadingModel);
    return m_Resource;
}
std::shared_ptr<const MaterialRenderProxy> Material::GetRenderProxyShared() const
{
    if (!m_CachedProxy) m_CachedProxy = std::make_shared<MaterialRenderProxy>(GetResource(),m_RenderState,m_Revisions,CloneParameters(m_DefaultParameters));
    return m_CachedProxy;
}
MaterialInstance::MaterialInstance(std::shared_ptr<MaterialAsset> parent) : m_ParentAsset(std::move(parent))
{
    if (!m_ParentAsset || !m_ParentAsset->GetMaterial()) throw std::invalid_argument("A material instance requires a loaded parent MaterialAsset");
}
const Material* MaterialInstance::GetMaterial() const { return m_ParentAsset->GetMaterial(); }
EBlendMode MaterialInstance::GetBlendMode() const { return GetMaterial()->GetBlendMode(); }
EShadingModel MaterialInstance::GetShadingModel() const { return GetMaterial()->GetShadingModel(); }
uint64_t MaterialInstance::GetRevision() const { return m_Revision + GetMaterial()->GetRevision(); }
MaterialRevisions MaterialInstance::GetRevisions() const
{
    auto revisions = GetMaterial()->GetRevisions();
    revisions.Parameters += m_Revision;
    return revisions;
}
std::shared_ptr<const MaterialRenderProxy> MaterialInstance::GetRenderProxyShared() const
{
    const auto parentRevision = GetMaterial()->GetRevision();
    if (!m_CachedProxy || m_CachedParentRevision != parentRevision)
    {
        auto parameters = CloneParameters(GetMaterial()->GetDefaultParameters());
        for (const auto& [key,parameter] : m_OverriddenParameters) parameters[key] = parameter->Clone();
        m_CachedProxy = std::make_shared<MaterialRenderProxy>(GetMaterial()->GetResource(),GetMaterial()->GetRenderState(),GetRevisions(),std::move(parameters));
        m_CachedParentRevision = parentRevision;
    }
    return m_CachedProxy;
}
bool MaterialInstance::SetParameterValueImpl(std::unique_ptr<MaterialParameter> parameter)
{
    ValidateMaterialParameter(*parameter,true);
    if (!StoreParameter(m_OverriddenParameters,std::move(parameter))) return false;
    ++m_Revision; m_CachedProxy.reset();
    return true;
}
bool MaterialInstance::HasOverride(const std::string& name, EMaterialParameterType type) const
{
    return m_OverriddenParameters.contains(ParameterKey(name,type));
}
bool MaterialInstance::ResetParameter(const std::string& name, EMaterialParameterType type)
{
    if (!m_OverriddenParameters.erase(ParameterKey(name,type))) return false;
    ++m_Revision; m_CachedProxy.reset();
    return true;
}
void MaterialInstance::ResetAllParameters()
{
    if (m_OverriddenParameters.empty()) return;
    m_OverriddenParameters.clear(); ++m_Revision; m_CachedProxy.reset();
}
}