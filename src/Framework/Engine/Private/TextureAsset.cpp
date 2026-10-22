#include "TextureAsset.h"
#include "RenderResource.h"
#include "PathInfo.h"

namespace Engine {
TextureAsset::TextureAsset(const std::string& path) : Name(Core::GetFileName(path)), Path(path) {}
bool TextureAsset::Load()
{
    Texture = RenderCore::CreateTexture(Path);
    return Texture != nullptr;
}
}
