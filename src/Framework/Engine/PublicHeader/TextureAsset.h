#pragma once
#include "Asset.h"

namespace RenderCore { class RenderTexture; }
namespace Engine {

class ENGINE_API TextureAsset : public IAsset
{
public:
    explicit TextureAsset(const std::string& path);
    const std::string& GetName() const override { return Name; }
    const std::string& GetPath() const override { return Path; }
    bool Load() override;
    RenderCore::RenderTexture* GetTexture() const { return Texture.get(); }

private:
    std::string Name;
    std::string Path;
    std::shared_ptr<RenderCore::RenderTexture> Texture;
};

}
