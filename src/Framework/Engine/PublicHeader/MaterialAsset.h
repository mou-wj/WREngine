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
    const std::string& GetName() const override { return m_Name; }
    const std::string& GetPath() const override { return m_Path; }
    bool Load() override;
    Material* GetMaterial() const { return m_MaterialPtr.get(); }

    // Embedded/imported materials still have an asset owner; they need no separate file.
    static std::shared_ptr<MaterialAsset> Create(
        const std::string& path, const std::string& name, std::unique_ptr<Material> material);

private:
    std::string m_Name;
    std::string m_Path;
    std::vector<std::shared_ptr<TextureAsset>> m_Textures;
    std::unique_ptr<Material> m_MaterialPtr;
};

}
