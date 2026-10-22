#pragma once
#include "Asset.h"

namespace RenderCore { class RenderTexture; }
namespace Engine {

class ENGINE_API SkyLightAsset : public IAsset
{
public:
    explicit SkyLightAsset(const std::string& path);
    const std::string& GetName() const override { return Name; }
    const std::string& GetPath() const override { return Path; }
    bool Load() override;
    RenderCore::RenderTexture* GetHDRTexture() const { return HDRTexture.get(); }
    RenderCore::RenderTexture* GetDiffuseIrradiance() const { return DiffuseIrradiance.get(); }
    RenderCore::RenderTexture* GetSpecularPrefilter() const { return SpecularPrefilter.get(); }

private:
    std::string Name;
    std::string Path;
    std::shared_ptr<RenderCore::RenderTexture> HDRTexture;
    std::shared_ptr<RenderCore::RenderTexture> DiffuseIrradiance;
    std::shared_ptr<RenderCore::RenderTexture> SpecularPrefilter;
};

}
