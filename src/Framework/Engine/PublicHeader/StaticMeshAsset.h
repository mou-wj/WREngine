#pragma once
#include "Asset.h"

namespace Engine {
class StaticMesh;
enum class EBasicMeshShape;

class ENGINE_API StaticMeshAsset : public IAsset
{
public:
    explicit StaticMeshAsset(const std::string& path);
    // Creates an in-memory asset using Engine's existing geometry generators.
    static std::shared_ptr<StaticMeshAsset> CreateShape(EBasicMeshShape shape);
    const std::string& GetName() const override { return m_Name; }
    const std::string& GetPath() const override { return m_Path; }
    bool Load() override;
    StaticMesh* GetMesh() const { return m_Mesh.get(); }

private:
    std::string m_Name;
    std::string m_Path;
    std::shared_ptr<StaticMesh> m_Mesh;
};

}
