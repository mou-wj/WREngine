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
    explicit StaticMesh(std::shared_ptr<StaticMeshRenderData> InRenderData) : RenderData(std::move(InRenderData)) {}
    ~StaticMesh() = default;

    // Accessors
    size_t GetLODCount() const { return RenderData ? RenderData->GetLODCount() : 0; }
    const LODResource& GetLODResource(size_t Index) const { return RenderData->GetLODResource(Index); }
    const Core::BoxSphereBounds& GetBounds() const { static Core::BoxSphereBounds Empty; return RenderData ? RenderData->GetBounds() : Empty; }

    std::shared_ptr<StaticMeshRenderData> GetRenderData() const { return RenderData; }

    // Materials    
    void AddMaterial(std::shared_ptr<MaterialAsset> material) { Materials.push_back(std::move(material)); }
    MaterialInterface* GetMaterial(size_t index) const;
    std::shared_ptr<MaterialAsset> GetMaterialAsset(size_t index) const
    { return index < Materials.size() ? Materials[index] : nullptr; }
    size_t GetNumMaterials() const { return Materials.size(); }
    std::shared_ptr<StaticMeshAsset> GetAsset() const { return OwnerAsset.lock(); }



private:
    std::shared_ptr<StaticMeshRenderData> RenderData; // shared ownership of underlying render data
    friend class StaticMeshAsset;
    void SetAsset(const std::shared_ptr<StaticMeshAsset>& asset) { OwnerAsset = asset; }
    std::weak_ptr<StaticMeshAsset> OwnerAsset;
    std::vector<std::shared_ptr<MaterialAsset>> Materials;
};

using StaticMeshSP = std::shared_ptr<StaticMesh>;
} // namespace Engine