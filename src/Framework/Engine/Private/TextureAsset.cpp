#include "TextureAsset.h"
#include "RenderResource.h"
#include "PathInfo.h"

namespace Engine {
TextureAsset::TextureAsset(const std::string& path) : m_Name(Core::GetFileName(path)), m_Path(path) {}
bool TextureAsset::Load()
{
    m_Texture = RenderCore::CreateTexture(m_Path);
    return m_Texture != nullptr;
}
}
