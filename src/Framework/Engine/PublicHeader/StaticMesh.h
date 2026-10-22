// StaticMesh.h
// Minimal StaticMesh asset wrapper holding render data (LODs, sections, materials, bounds).
#pragma once
#include "EngineExport.h"
#include "StaticMeshResources.h"
#include <memory>
namespace Engine {
// Forward
class PrimitiveSceneProxy;
class MaterialAsset;
class MaterialInterface;
class StaticMeshAsset;

class ENGINE_API StaticMesh {
public:
    StaticMesh() = default;
    explicit StaticMesh(std::shared_ptr<StaticMeshRenderData> inRenderData) : m_RenderData(std::move(inRenderData)) {}
    ~StaticMesh() = default;

    // Accessors
    size_t GetLODCount() const { return m_RenderData ? m_RenderData->GetLODCount() : 0; }
    const LODResource& GetLODResource(size_t index) const { return m_RenderData->GetLODResource(index); }
    const Core::BoxSphereBounds& GetBounds() const { static Core::BoxSphereBounds empty; return m_RenderData ? m_RenderData->GetBounds() : empty; }

    std::shared_ptr<StaticMeshRenderData> GetRenderData() const { return m_RenderData; }

    // Materials    
    void AddMaterial(std::shared_ptr<MaterialAsset> material) { m_Materials.push_back(std::move(material)); }
    MaterialInterface* GetMaterial(size_t index) const;
    std::shared_ptr<MaterialAsset> GetMaterialAsset(size_t index) const
    { return index < m_Materials.size() ? m_Materials[index] : nullptr; }
    size_t GetNumMaterials() const { return m_Materials.size(); }
    std::shared_ptr<StaticMeshAsset> GetAsset() const { return m_OwnerAsset.lock(); }



private:
    std::shared_ptr<StaticMeshRenderData> m_RenderData; // shared ownership of underlying render data
    friend class StaticMeshAsset;
    void SetAsset(const std::shared_ptr<StaticMeshAsset>& asset) { m_OwnerAsset = asset; }
    std::weak_ptr<StaticMeshAsset> m_OwnerAsset;
    std::vector<std::shared_ptr<MaterialAsset>> m_Materials;
};

using StaticMeshSP = std::shared_ptr<StaticMesh>;
} // namespace Engine