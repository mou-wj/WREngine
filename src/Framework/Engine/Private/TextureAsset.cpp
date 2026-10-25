#include "TextureAsset.h"
#include "RenderResource.h"
#include "PathInfo.h"
#include <stdexcept>

namespace Engine {
TextureAsset::TextureAsset(const std::string& path) : m_Name(Core::GetFileName(path)), m_Path(path) {}
bool TextureAsset::Load()
{
    m_Texture = RenderCore::CreateTexture(m_Path);
    return m_Texture != nullptr;
}
std::shared_ptr<TextureAsset> TextureAsset::Create(const std::string& name, std::shared_ptr<RenderCore::RenderTexture> texture)
{
    if (!texture) throw std::invalid_argument("TextureAsset cannot own a null texture");
    auto asset = std::make_shared<TextureAsset>(name);
    asset->m_Texture = std::move(texture);
    return asset;
}
}
