#pragma once

#include "EngineExport.h"
#include <memory>
#include <unordered_map>

namespace RenderCore { class RenderTexture; }
namespace Engine
{
    class TextureAsset;
    class SkyLightComponent;

    // Runtime IBL products and their input owner; this is not a file asset.
    class ENGINE_API EnvironmentLightResource
    {
    public:
        const std::shared_ptr<TextureAsset>& GetInputTexture() const { return m_InputTexture; }
        RenderCore::RenderTexture* GetDiffuseIrradiance() const { return m_DiffuseIrradiance.get(); }
        RenderCore::RenderTexture* GetSpecularPrefilter() const { return m_SpecularPrefilter.get(); }

    private:
        friend class EnvironmentLightResourceManager;
        EnvironmentLightResource() = default;
        std::shared_ptr<TextureAsset> m_InputTexture;
        std::shared_ptr<RenderCore::RenderTexture> m_DiffuseIrradiance;
        std::shared_ptr<RenderCore::RenderTexture> m_SpecularPrefilter;
    };

    // Build/apply/release run serially on the scene control thread.
    class ENGINE_API EnvironmentLightResourceManager
    {
    public:
        static EnvironmentLightResourceManager& Get();
        std::shared_ptr<const EnvironmentLightResource> GetDefaultEnvironment();
        std::shared_ptr<const EnvironmentLightResource> Build(const std::shared_ptr<TextureAsset>& input);
        void ApplyEnvironment(SkyLightComponent& sky, std::shared_ptr<const EnvironmentLightResource> environment);

        // Call after scene removal and its GPU completion point. Referenced
        // results survive; shared_ptr counts alone are not GPU synchronization.
        void ReleaseUnused();
        // Engine shutdown, after users have completed and released their work.
        void Clear();

    private:
        EnvironmentLightResourceManager() = default;
        EnvironmentLightResourceManager(const EnvironmentLightResourceManager&) = delete;
        EnvironmentLightResourceManager& operator=(const EnvironmentLightResourceManager&) = delete;
        std::shared_ptr<const EnvironmentLightResource> BuildResource(RenderCore::RenderTexture* input,
            const std::shared_ptr<TextureAsset>& inputAsset);
        std::shared_ptr<RenderCore::RenderTexture> m_DefaultInput;
        std::shared_ptr<const EnvironmentLightResource> m_DefaultEnvironment;
        std::unordered_map<const TextureAsset*, std::shared_ptr<const EnvironmentLightResource>> m_Resources;
    };
}
