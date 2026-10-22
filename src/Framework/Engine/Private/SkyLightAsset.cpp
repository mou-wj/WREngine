#include "SkyLightAsset.h"
#include "RenderResource.h"
#include "EngineGlobal.h"
#include "PathInfo.h"
namespace Engine {
SkyLightAsset::SkyLightAsset(const std::string& path) : m_Name(Core::GetFileName(path)), m_Path(path) {}
    bool SkyLightAsset::Load(){
		uint32_t cubeWidth = 512;
        uint32_t cubeHeight = 512;
        m_HDRTexture = RenderCore::CreateTexture(m_Path);
        if (!m_HDRTexture) return false;
        RHI::RHITextureDesc desc;
        desc.Format = RHI::ERHIFormat::B8G8R8A8_UNorm;
        desc.Width = cubeWidth;
        desc.Height = cubeHeight;
		desc.ArraySize = 6;
        desc.MipLevels = 1;
        desc.Usage = RHI::ERHITextureCreateFlag::ShaderResource | RHI::ERHITextureCreateFlag::UAV;
		desc.Type = RHI::ERHITextureType::TextureCube;
        m_DiffuseIrradiance = std::make_shared<RenderCore::RenderTexture>(desc);
        m_DiffuseIrradiance->InitRHIResource();
        desc.MipLevels = 6;
        m_SpecularPrefilter = std::make_shared<RenderCore::RenderTexture>(desc);
        m_SpecularPrefilter->InitRHIResource();
        GetRenderModuleInstance()->PreComputeIBL(m_HDRTexture.get(), m_DiffuseIrradiance.get(), m_SpecularPrefilter.get());
        return true;
    }
}
