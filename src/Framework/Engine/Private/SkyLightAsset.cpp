#include "SkyLightAsset.h"
#include "RenderResource.h"
#include "EngineGlobal.h"
#include "PathInfo.h"
namespace Engine {
SkyLightAsset::SkyLightAsset(const std::string& path) : Name(Core::GetFileName(path)), Path(path) {}
    bool SkyLightAsset::Load(){
		uint32_t cubeWidth = 512;
        uint32_t cubeHeight = 512;
        HDRTexture = RenderCore::CreateTexture(Path);
        if (!HDRTexture) return false;
        RHI::RHITextureDesc Desc;
        Desc.Format = RHI::ERHIFormat::B8G8R8A8_UNorm;
        Desc.Width = cubeWidth;
        Desc.Height = cubeHeight;
		Desc.ArraySize = 6;
        Desc.MipLevels = 1;
        Desc.Usage = RHI::ERHITextureCreateFlag::ShaderResource | RHI::ERHITextureCreateFlag::UAV;
		Desc.Type = RHI::ERHITextureType::TextureCube;
        DiffuseIrradiance = std::make_shared<RenderCore::RenderTexture>(Desc);
        DiffuseIrradiance->InitRHIResource();
        Desc.MipLevels = 6;
        SpecularPrefilter = std::make_shared<RenderCore::RenderTexture>(Desc);
        SpecularPrefilter->InitRHIResource();
        GetRenderModuleInstance()->PreComputeIBL(HDRTexture.get(), DiffuseIrradiance.get(), SpecularPrefilter.get());
        return true;
    }
}
