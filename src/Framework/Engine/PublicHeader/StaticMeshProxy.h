// StaticMeshProxy.h
// SceneProxy specialization for StaticMesh. RenderThread-read-only representation.
#pragma once

#include "PrimitiveSceneProxy.h"
#include "EngineExport.h"
#include <memory>
namespace Engine {

class StaticMeshComponent;
class ENGINE_API StaticMeshProxy : public PrimitiveSceneProxy {
public:
    DECLARE_TYPE_ID_DERIVED_TYPE(StaticMeshProxy, PrimitiveSceneProxy)
    // Construct from shared render data and component transform.
    StaticMeshProxy(const StaticMeshComponent* inComponent);
    ~StaticMeshProxy() override;

    
    bool HasStaticGeometry() const override { return true; }
    bool IsDynamic() const override { return false; }
    const StaticMeshComponent* GetStaticMeshComponent() const { return m_MeshComponent; }
private:
    // Keep shared ownership of render data to ensure lifetime while proxy exists on RenderThread.
    const StaticMeshComponent* m_MeshComponent;
};
} // namespace Engine