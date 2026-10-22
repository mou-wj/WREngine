#pragma once
#include "Asset.h"

namespace RenderCore { class RenderTexture; }
namespace Engine {

class ENGINE_API SkyLightAsset : public IAsset
{
public:
    explicit SkyLightAsset(const std::string& path);
    const std::string& GetName() const override { return m_Name; }
    const std::string& GetPath() const override { return m_Path; }
    bool Load() override;
    RenderCore::RenderTexture* GetHDRTexture() const { return m_HDRTexture.get(); }
    RenderCore::RenderTexture* GetDiffuseIrradiance() const { return m_DiffuseIrradiance.get(); }
    RenderCore::RenderTexture* GetSpecularPrefilter() const { return m_SpecularPrefilter.get(); }

private:
    std::string m_Name;
    std::string m_Path;
    std::shared_ptr<RenderCore::RenderTexture> m_HDRTexture;
    std::shared_ptr<RenderCore::RenderTexture> m_DiffuseIrradiance;
    std::shared_ptr<RenderCore::RenderTexture> m_SpecularPrefilter;
};

}
