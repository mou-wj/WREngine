#pragma once
#include "Asset.h"

namespace Engine {
class StaticMesh;
enum class EBasicMeshShape;

class ENGINE_API StaticMeshAsset : public IAsset
{
public:
    explicit StaticMeshAsset(const std::string& path);
    ~StaticMeshAsset() override;
    // Creates an in-memory asset using Engine's existing geometry generators.
    static std::shared_ptr<StaticMeshAsset> CreateShape(EBasicMeshShape shape);
    // Adopt a mesh reconstructed from project data, without loading a source file.
    static std::shared_ptr<StaticMeshAsset> Create(const std::string& name, std::shared_ptr<StaticMesh> mesh);
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
