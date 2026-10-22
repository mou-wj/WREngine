#pragma once
#include "Asset.h"

namespace Engine {
class StaticMesh;

class ENGINE_API StaticMeshAsset : public IAsset
{
public:
    explicit StaticMeshAsset(const std::string& path);
    const std::string& GetName() const override { return Name; }
    const std::string& GetPath() const override { return Path; }
    bool Load() override;
    StaticMesh* GetMesh() const { return Mesh.get(); }

private:
    std::string Name;
    std::string Path;
    std::shared_ptr<StaticMesh> Mesh;
};

}
