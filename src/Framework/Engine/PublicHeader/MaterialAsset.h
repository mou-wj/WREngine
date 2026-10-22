#pragma once
#include "Asset.h"
#include <vector>

namespace Engine {
class Material;
class TextureAsset;

class ENGINE_API MaterialAsset : public IAsset
{
public:
    explicit MaterialAsset(const std::string& path);
    ~MaterialAsset() override;
    const std::string& GetName() const override { return Name; }
    const std::string& GetPath() const override { return Path; }
    bool Load() override;
    Material* GetMaterial() const { return MaterialPtr.get(); }

    // Embedded/imported materials still have an asset owner; they need no separate file.
    static std::shared_ptr<MaterialAsset> Create(
        const std::string& path, const std::string& name, std::unique_ptr<Material> material);

private:
    std::string Name;
    std::string Path;
    std::vector<std::shared_ptr<TextureAsset>> Textures;
    std::unique_ptr<Material> MaterialPtr;
};

}
