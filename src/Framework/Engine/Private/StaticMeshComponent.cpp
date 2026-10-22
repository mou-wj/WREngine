// StaticMeshComponent.cpp

#include "StaticMeshComponent.h"
#include "StaticMesh.h"
#include "StaticMeshProxy.h"
#include "Material.h"
#include "MaterialAsset.h"
#include "StaticMeshAsset.h"
#include <algorithm>
#include <stdexcept>

namespace Engine
{

    StaticMeshComponent::StaticMeshComponent()
    {
    }

    StaticMeshComponent::~StaticMeshComponent() = default;

    void StaticMeshComponent::SetStaticMesh(StaticMesh* inMesh)
    {
        auto asset = inMesh ? inMesh->GetAsset() : nullptr;
        if (inMesh && !asset)
            throw std::invalid_argument("StaticMeshComponent requires an asset-owned mesh");
        if (m_Mesh != inMesh)
        {
            m_MaterialInstances.clear();
            m_OverrideMaterials.clear();
        }
        m_MeshAsset = std::move(asset);
        m_Mesh = inMesh;

        // Bounds may change
        UpdateWorldTransform();

        // Need recreate SceneProxy
        MarkRenderStateDirty();
    }

    void StaticMeshComponent::SetMaterial(
        uint32_t slotIndex,
        MaterialInterface* material)
    {
        auto asset = material ? material->GetAsset() : nullptr;
        if (material && (!asset || material != asset->GetMaterial()))
            throw std::invalid_argument("SetMaterial requires a MaterialAsset's base material");
        if (slotIndex >= m_OverrideMaterials.size()) m_OverrideMaterials.resize(slotIndex + 1);
        if (slotIndex >= m_MaterialInstances.size()) m_MaterialInstances.resize(slotIndex + 1);
        m_MaterialInstances[slotIndex].reset();
        m_OverrideMaterials[slotIndex] = std::move(asset);
        MarkRenderStateDirty();
    }

    MaterialInterface* StaticMeshComponent::GetMaterial(uint32_t slotIndex) const
    {
        if (auto* instance = GetMaterialInstance(slotIndex)) return instance;
        if (slotIndex < m_OverrideMaterials.size() && m_OverrideMaterials[slotIndex])
            return m_OverrideMaterials[slotIndex]->GetMaterial();
        return m_Mesh ? m_Mesh->GetMaterial(slotIndex) : nullptr;
    }

    uint32_t StaticMeshComponent::GetNumMaterials() const
    {
        return static_cast<uint32_t>(std::max(m_OverrideMaterials.size(), m_Mesh ? m_Mesh->GetNumMaterials() : size_t(0)));
    }

    MaterialInstance* StaticMeshComponent::GetMaterialInstance(uint32_t slotIndex) const
    {
        return slotIndex < m_MaterialInstances.size() ? m_MaterialInstances[slotIndex].get() : nullptr;
    }

    MaterialInstance* StaticMeshComponent::GetOrCreateMaterialInstance(uint32_t slotIndex)
    {
        if (auto* instance = GetMaterialInstance(slotIndex)) return instance;
        auto* material = GetMaterial(slotIndex);
        if (!material) throw std::out_of_range("Material slot has no base material");
        auto instance = std::make_unique<MaterialInstance>(material->GetAsset());
        if (slotIndex >= m_MaterialInstances.size()) m_MaterialInstances.resize(slotIndex + 1);
        auto* result = instance.get();
        m_MaterialInstances[slotIndex] = std::move(instance);
        MarkRenderStateDirty();
        return result;
    }

    void StaticMeshComponent::ResetMaterialInstance(uint32_t slotIndex)
    {
        if (slotIndex < m_MaterialInstances.size()) m_MaterialInstances[slotIndex].reset();
        MarkRenderStateDirty();
    }

    PrimitiveSceneProxy*
        StaticMeshComponent::CreateSceneProxy() const
    {
        if (!m_Mesh)
        {
            return nullptr;
        }

        auto renderData = m_Mesh->GetRenderData();

        if (!renderData)
        {
            return nullptr;
        }

        return new StaticMeshProxy(this);
    }
    const Core::BoxSphereBounds& StaticMeshComponent::GetBounds() const {
        return _bounds;
    }
    Core::BoxSphereBounds
        StaticMeshComponent::CalcBounds(
            const Core::Mat4& localToWorld)
    {
        if (!m_Mesh)
        {
            return {};
        }

        auto renderData = m_Mesh->GetRenderData();

        if (!renderData)
        {
            return {};
        }
        auto bounds = m_Mesh->GetBounds();
        bounds.Transform(localToWorld);
		_bounds = bounds;
        return _bounds;
    }

} // namespace Engine