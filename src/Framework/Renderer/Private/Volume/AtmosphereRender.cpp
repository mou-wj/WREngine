#include "AtmosphereRender.h"
#include "LightSceneProxy.h"
#include "Scene.h"
#include "VolumeViewPass.h"
#include <algorithm>

namespace Renderer
{
    using namespace RenderCore;
    using namespace RHI;
    using VolumeViewPass::Pack;

    BEGIN_SHADER_PARAMETER_STRUCT(VolumeAtmosphereLutParameters)
        SHADER_PARAMETER_STRUCT_REFERENCE(VolumeAtmosphereModel, Model)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, TransmittanceOutput)
    END_SHADER_PARAMETER_STRUCT(VolumeAtmosphereLutParameters)

    BEGIN_SHADER_PARAMETER_STRUCT(VolumeAtmosphereDrawParameters)
        SHADER_PARAMETER_STRUCT_REFERENCE(VolumeAtmosphereLight, Light)
        SHADER_PARAMETER(Core::Mat4, AtmosphereInverseViewProjection)
        SHADER_PARAMETER(Core::Float4, AtmosphereCamera)
        SHADER_PARAMETER(Core::UInt4, AtmosphereViewSettings)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, SceneColor)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SceneDepth)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, AtmosphereOutput)
    END_SHADER_PARAMETER_STRUCT(VolumeAtmosphereDrawParameters)

    class VolumeAtmosphereLutCS : public GlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER_TYPE(VolumeAtmosphereLutCS)
        static const ShaderParametersMetadata* GetShaderParameterMetadata() { return VolumeAtmosphereLutParameters::GetMetaData(); }
    };

    class VolumeAtmosphereDrawCS : public GlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER_TYPE(VolumeAtmosphereDrawCS)
        static const ShaderParametersMetadata* GetShaderParameterMetadata() { return VolumeAtmosphereDrawParameters::GetMetaData(); }
    };

    IMPLEMENT_GLOBAL_SHADER_TYPE(VolumeAtmosphereLutCS, "VolumeAtmosphereLutCS", "/volume/AtmosphereTransmittance.sf", "MainCS", ERHIShaderFrequency::Compute);
    IMPLEMENT_GLOBAL_SHADER_TYPE(VolumeAtmosphereDrawCS, "VolumeAtmosphereDrawCS", "/volume/AtmosphereDraw.sf", "MainCS", ERHIShaderFrequency::Compute);

    VolumeAtmosphereLight PrepareAtmosphere(RenderGraphBuilder& builder, const AtmosphereRenderSettings& settings,
        Engine::SceneInterface* sceneInterface)
    {
        VolumeAtmosphereLight atmosphere{};
        atmosphere.AtmosphereSampler = GlobalSampler.get();
        if (!settings.bEnabled)
        {
            atmosphere.AtmosphereTransmittance = builder.RegisterExternalTexture("Volume.AtmosphereDisabled", GlobalWhiteTexture.get());
            return atmosphere;
        }

        using Core::Float3;
        const auto nonnegative = [](Float3 v) { return Float3{std::max(v.X, 0.0f), std::max(v.Y, 0.0f), std::max(v.Z, 0.0f)}; };
        const float density = std::max(0.0f, settings.Density);
        atmosphere.Model.Planet = {std::max(1.0f, settings.PlanetRadiusKm), std::max(0.01f, settings.AtmosphereHeightKm),
            std::max(1e-6f, settings.WorldToKm), settings.SeaLevel};
        atmosphere.Model.Rayleigh = Pack(nonnegative(settings.RayleighScattering) * density, std::max(0.01f, settings.RayleighHeightKm));
        atmosphere.Model.Mie = Pack(nonnegative(settings.MieScattering) * density, std::max(0.01f, settings.MieHeightKm));
        atmosphere.Model.AtmosphereAbsorption = Pack(nonnegative(settings.Absorption) * density,
            std::clamp(settings.MieAnisotropy, -0.95f, 0.95f));
        Float3 sun = settings.SunDirection;
        const float intensity = std::max(0.0f, settings.SunIntensity);
        Float3 irradiance{intensity, intensity, intensity};
        if (auto* scene = dynamic_cast<Scene*>(sceneInterface))
        {
            const Engine::DirectionalLightSceneProxy* sunLight = nullptr;
            scene->ForEachLight([&](Engine::LightSceneProxy* light)
            {
                if (light->GetLightType() == Engine::ELightType::Directional &&
                    (!sunLight || light->GetIntensity() > sunLight->GetIntensity()))
                {
                    sunLight = static_cast<const Engine::DirectionalLightSceneProxy*>(light);
                }
            });
            if (sunLight)
            {
                sun = sunLight->GetDirection() * -1.0f;
                irradiance = nonnegative(sunLight->GetColor()) * std::max(0.0f, sunLight->GetIntensity());
            }
        }
        atmosphere.AtmosphereSun = Pack(sun.LengthSquared() > 1e-8f ? Core::Normalize(sun) : Float3{0, 1, 0}, 1.0f);
        atmosphere.AtmosphereIrradiance = Pack(irradiance, 0.0f);
        constexpr uint32_t lutWidth = 128, lutHeight = 48;
        atmosphere.AtmosphereTransmittance = VolumeViewPass::Image(builder, "Volume.AtmosphereTransmittance", lutWidth, lutHeight);
        auto* lut = builder.AllocateParameter<VolumeAtmosphereLutParameters>();
        lut->Model = atmosphere.Model;
        lut->TransmittanceOutput = VolumeViewPass::WriteImage(builder, atmosphere.AtmosphereTransmittance);
        VolumeViewPass::AddCompute<VolumeAtmosphereLutCS>(builder, "VolumeAtmosphereTransmittance", lut, lutWidth, lutHeight);
        return atmosphere;
    }

    void AddAtmospherePass(RenderGraphBuilder& builder, const Engine::SceneView& view, RenderGraphTextureRef color,
        RenderGraphTextureRef depth, const AtmosphereRenderSettings& settings, const VolumeAtmosphereLight& atmosphere)
    {
        if (!settings.bEnabled)
        {
            return;
        }
        VolumeViewPass::ValidateInputs(view, color, depth);
        const auto& desc = color->GetDesc();
        const auto output = VolumeViewPass::Image(builder, "Volume.AtmosphereColor", desc.Width, desc.Height,
            desc.Format, ERHITextureCreateFlag::TransferSrc);
        auto* parameters = builder.AllocateParameter<VolumeAtmosphereDrawParameters>();
        parameters->Light = atmosphere;
        parameters->AtmosphereInverseViewProjection = view.InvViewProjectionMatrix;
        parameters->AtmosphereCamera = Pack(view.CameraWorldPos, 0.0f);
        parameters->AtmosphereViewSettings = {desc.Width, desc.Height, uint32_t(settings.View), settings.bAerialPerspective ? 1u : 0u};
        parameters->SceneColor = color;
        parameters->SceneDepth = depth;
        parameters->AtmosphereOutput = VolumeViewPass::WriteImage(builder, output);
        VolumeViewPass::AddCompute<VolumeAtmosphereDrawCS>(builder, "VolumeAtmosphereCompose", parameters, desc.Width, desc.Height);
        VolumeViewPass::AddColorCopy(builder, "VolumeAtmosphereCopy", output, color);
    }
}
