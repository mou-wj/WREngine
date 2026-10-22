#pragma once
#include "Asset.h"

namespace RenderCore { class RenderTexture; }
namespace Engine {

class ENGINE_API TextureAsset : public IAsset
{
public:
    explicit TextureAsset(const std::string& path);
    const std::string& GetName() const override { return m_Name; }
    const std::string& GetPath() const override { return m_Path; }
    bool Load() override;
    RenderCore::RenderTexture* GetTexture() const { return m_Texture.get(); }

private:
    std::string m_Name;
    std::string m_Path;
    std::shared_ptr<RenderCore::RenderTexture> m_Texture;
};

}
