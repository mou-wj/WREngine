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

    void StaticMeshComponent::SetStaticMesh(StaticMesh* InMesh)
    {
        auto asset = InMesh ? InMesh->GetAsset() : nullptr;
        if (InMesh && !asset)
            throw std::invalid_argument("StaticMeshComponent requires an asset-owned mesh");
        if (Mesh != InMesh)
        {
            MaterialInstances.clear();
            OverrideMaterials.clear();
        }
        MeshAsset = std::move(asset);
        Mesh = InMesh;

        // Bounds may change
        UpdateWorldTransform();

        // Need recreate SceneProxy
        MarkRenderStateDirty();
    }

    void StaticMeshComponent::SetMaterial(
        uint32_t SlotIndex,
        MaterialInterface* Material)
    {
        auto asset = Material ? Material->GetAsset() : nullptr;
        if (Material && (!asset || Material != asset->GetMaterial()))
            throw std::invalid_argument("SetMaterial requires a MaterialAsset's base material");
        if (SlotIndex >= OverrideMaterials.size()) OverrideMaterials.resize(SlotIndex + 1);
        if (SlotIndex >= MaterialInstances.size()) MaterialInstances.resize(SlotIndex + 1);
        MaterialInstances[SlotIndex].reset();
        OverrideMaterials[SlotIndex] = std::move(asset);
        MarkRenderStateDirty();
    }

    MaterialInterface* StaticMeshComponent::GetMaterial(uint32_t slotIndex) const
    {
        if (auto* instance = GetMaterialInstance(slotIndex)) return instance;
        if (slotIndex < OverrideMaterials.size() && OverrideMaterials[slotIndex])
            return OverrideMaterials[slotIndex]->GetMaterial();
        return Mesh ? Mesh->GetMaterial(slotIndex) : nullptr;
    }

    uint32_t StaticMeshComponent::GetNumMaterials() const
    {
        return static_cast<uint32_t>(std::max(OverrideMaterials.size(), Mesh ? Mesh->GetNumMaterials() : size_t(0)));
    }

    MaterialInstance* StaticMeshComponent::GetMaterialInstance(uint32_t slotIndex) const
    {
        return slotIndex < MaterialInstances.size() ? MaterialInstances[slotIndex].get() : nullptr;
    }

    MaterialInstance* StaticMeshComponent::GetOrCreateMaterialInstance(uint32_t slotIndex)
    {
        if (auto* instance = GetMaterialInstance(slotIndex)) return instance;
        auto* material = GetMaterial(slotIndex);
        if (!material) throw std::out_of_range("Material slot has no base material");
        auto instance = std::make_unique<MaterialInstance>(material->GetAsset());
        if (slotIndex >= MaterialInstances.size()) MaterialInstances.resize(slotIndex + 1);
        auto* result = instance.get();
        MaterialInstances[slotIndex] = std::move(instance);
        MarkRenderStateDirty();
        return result;
    }

    void StaticMeshComponent::ResetMaterialInstance(uint32_t slotIndex)
    {
        if (slotIndex < MaterialInstances.size()) MaterialInstances[slotIndex].reset();
        MarkRenderStateDirty();
    }

    PrimitiveSceneProxy*
        StaticMeshComponent::CreateSceneProxy() const
    {
        if (!Mesh)
        {
            return nullptr;
        }

        auto RenderData = Mesh->GetRenderData();

        if (!RenderData)
        {
            return nullptr;
        }

        return new StaticMeshProxy(this);
    }
    const Core::BoxSphereBounds& StaticMeshComponent::GetBounds() const {
        return Bounds;
    }
    Core::BoxSphereBounds
        StaticMeshComponent::CalcBounds(
            const Core::Mat4& LocalToWorld)
    {
        if (!Mesh)
        {
            return {};
        }

        auto RenderData = Mesh->GetRenderData();

        if (!RenderData)
        {
            return {};
        }
        auto bounds = Mesh->GetBounds();
        bounds.Transform(LocalToWorld);
		Bounds = bounds;
        return Bounds;
    }

} // namespace Engine