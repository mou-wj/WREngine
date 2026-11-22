#pragma once

#include "RenderGraphResource.h"
#include "ShaderParameter.h"

namespace Engine { class SceneView; class SceneInterface; }
namespace RenderCore { class RenderGraphBuilder; }

namespace Renderer
{
    enum class EAtmosphereView : uint32_t { Composite, InScattering, Transmittance };

    struct AtmosphereRenderSettings
    {
        float PlanetRadiusKm = 6360.0f;
        float AtmosphereHeightKm = 100.0f;
        float RayleighHeightKm = 8.0f;
        float MieHeightKm = 1.2f;
        Core::Float3 RayleighScattering{0.005802f, 0.013558f, 0.033100f};
        Core::Float3 MieScattering{0.003996f, 0.003996f, 0.003996f};
        Core::Float3 Absorption{0.000650f, 0.001881f, 0.000085f};
        float MieAnisotropy = 0.8f;
        float Density = 1.0f;
        float WorldToKm = 0.001f;
        float SeaLevel = 0.0f;
        Core::Float3 SunDirection{0.0f, 0.7f, -0.7f}; // Toward the sun. Used when no directional light is available.
        float SunIntensity = 5.0f;
        EAtmosphereView View = EAtmosphereView::Composite;
        bool bEnabled = false;
        bool bAerialPerspective = true;
    };

    BEGIN_SHADER_PARAMETER_STRUCT(VolumeAtmosphereModel)
        SHADER_PARAMETER(Core::Float4, Planet)
        SHADER_PARAMETER(Core::Float4, Rayleigh)
        SHADER_PARAMETER(Core::Float4, Mie)
        SHADER_PARAMETER(Core::Float4, AtmosphereAbsorption)
    END_SHADER_PARAMETER_STRUCT(VolumeAtmosphereModel)

    BEGIN_SHADER_PARAMETER_STRUCT(VolumeAtmosphereLight)
        SHADER_PARAMETER_STRUCT_REFERENCE(VolumeAtmosphereModel, Model)
        SHADER_PARAMETER(Core::Float4, AtmosphereSun)
        SHADER_PARAMETER(Core::Float4, AtmosphereIrradiance)
        SHADER_PARAMETER_SAMPLER(AtmosphereSampler)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, AtmosphereTransmittance)
    END_SHADER_PARAMETER_STRUCT(VolumeAtmosphereLight)

    // A graph-local snapshot shared by the sky pass and optional reflection consumers.
    RENDERER_API VolumeAtmosphereLight PrepareAtmosphere(RenderCore::RenderGraphBuilder& builder,
        const AtmosphereRenderSettings& settings, Engine::SceneInterface* scene);
    RENDERER_API void AddAtmospherePass(RenderCore::RenderGraphBuilder& builder, const Engine::SceneView& view,
        RenderCore::RenderGraphTextureRef color, RenderCore::RenderGraphTextureRef depth,
        const AtmosphereRenderSettings& settings, const VolumeAtmosphereLight& atmosphere);
}
