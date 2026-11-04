#include "EnvironmentLightResourceManager.h"
#include "TextureAsset.h"
#include "LightComponent.h"
#include "RenderResource.h"
#include "EngineGlobal.h"
#include <stdexcept>
#include <array>

namespace Engine
{
    EnvironmentLightResourceManager& EnvironmentLightResourceManager::Get()
    {
        static EnvironmentLightResourceManager instance;
        return instance;
    }

    std::shared_ptr<const EnvironmentLightResource> EnvironmentLightResourceManager::Build(const std::shared_ptr<TextureAsset>& input)
    {
        if (!input || !input->GetTexture() || !input->GetTexture()->GetRHI())
            throw std::invalid_argument("Environment lighting requires a loaded texture asset");
        const auto& sourceDesc = input->GetTexture()->GetRHI()->GetDesc();
        if (sourceDesc.Type != RHI::ERHITextureType::Texture2D || sourceDesc.SampleCount != 1 ||
            !sourceDesc.Usage.HasAny(RHI::ERHITextureCreateFlag::ShaderResource))
            throw std::invalid_argument("Environment lighting requires a shader-readable single-sample 2D texture");
        if (const auto found = m_Resources.find(input.get()); found != m_Resources.end()) return found->second;
        auto resource = BuildResource(input->GetTexture(), input);
        m_Resources.emplace(input.get(), resource);
        return resource;
    }

    std::shared_ptr<const EnvironmentLightResource> EnvironmentLightResourceManager::GetDefaultEnvironment()
    {
        if (m_DefaultEnvironment) return m_DefaultEnvironment;
        // A manager-owned neutral input, with no source file or TextureAsset.
        RHI::RHITextureDesc desc;
        desc.Width = 1; desc.Height = 1; desc.Format = RHI::ERHIFormat::R8G8B8A8_UNorm;
        desc.Usage = RHI::ERHITextureCreateFlag::ShaderResource | RHI::ERHITextureCreateFlag::TransferDest;
        m_DefaultInput = std::make_shared<RenderCore::RenderTexture>(desc);
        m_DefaultInput->InitRHIResource();
        const std::array<uint8_t,4> pixel{128,128,128,255};
        m_DefaultInput->UploadData(pixel.data(),0,0);
        m_DefaultEnvironment = BuildResource(m_DefaultInput.get(),nullptr);
        return m_DefaultEnvironment;
    }

    std::shared_ptr<const EnvironmentLightResource> EnvironmentLightResourceManager::BuildResource(
        RenderCore::RenderTexture* input, const std::shared_ptr<TextureAsset>& inputAsset)
    {
        auto* renderer = GetRenderModuleInstance();
        if (!renderer) throw std::runtime_error("Initialize the renderer before building environment lighting");

        auto resource = std::shared_ptr<EnvironmentLightResource>(new EnvironmentLightResource);
        resource->m_InputTexture = inputAsset;
        RHI::RHITextureDesc desc;
        desc.Format = RHI::ERHIFormat::B8G8R8A8_UNorm;
        desc.Width = 512; desc.Height = 512; desc.ArraySize = 6;
        desc.Type = RHI::ERHITextureType::TextureCube;
        desc.Usage = RHI::ERHITextureCreateFlag::ShaderResource | RHI::ERHITextureCreateFlag::UAV;
        desc.MipLevels = 1;
        resource->m_DiffuseIrradiance = std::make_shared<RenderCore::RenderTexture>(desc);
        resource->m_DiffuseIrradiance->InitRHIResource();
        desc.MipLevels = 6;
        resource->m_SpecularPrefilter = std::make_shared<RenderCore::RenderTexture>(desc);
        resource->m_SpecularPrefilter->InitRHIResource();
        renderer->PreComputeIBL(input, resource->m_DiffuseIrradiance.get(), resource->m_SpecularPrefilter.get());

        const auto fence = resource->m_SpecularPrefilter->GetTracker().GetLastAccessFence();
        const auto diffuseFence = resource->m_DiffuseIrradiance->GetTracker().GetLastAccessFence();
        const auto inputFence = input->GetTracker().GetLastAccessFence();
        if (!fence.Value || fence.QueueType != RHI::EQueueType::Compute ||
            diffuseFence.QueueType != fence.QueueType || diffuseFence.Value != fence.Value ||
            inputFence.QueueType != fence.QueueType || inputFence.Value != fence.Value)
            throw std::runtime_error("Environment IBL precomputation did not submit all outputs");
        // Publish a completed initialization so failed snapshot loading can
        // release unused results without destroying views still used by compute.
        RHI::GRHIApi->GetQueue(fence.QueueType)->WaitValue(fence.Value);
        return resource;
    }

    void EnvironmentLightResourceManager::ApplyEnvironment(SkyLightComponent& sky,
        std::shared_ptr<const EnvironmentLightResource> environment)
    {
        sky.SetEnvironmentResource(std::move(environment));
    }

    void EnvironmentLightResourceManager::ReleaseUnused()
    {
        if (m_DefaultEnvironment && m_DefaultEnvironment.use_count() == 1) {
            m_DefaultEnvironment.reset(); m_DefaultInput.reset();
        }
        for (auto it = m_Resources.begin(); it != m_Resources.end();)
            if (it->second.use_count() == 1) it = m_Resources.erase(it);
            else ++it;
    }

    void EnvironmentLightResourceManager::Clear()
    {
        m_DefaultEnvironment.reset(); m_DefaultInput.reset();
        m_Resources.clear();
    }
}
