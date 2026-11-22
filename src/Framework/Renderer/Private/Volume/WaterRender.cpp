#include "WaterRender.h"
#include "LightSceneProxy.h"
#include "Scene.h"
#include "VolumeViewPass.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace Renderer
{
    using namespace RenderCore;
    using namespace RHI;
    using VolumeViewPass::Pack;

    BEGIN_SHADER_PARAMETER_STRUCT(VolumeWaterLight)
        SHADER_PARAMETER(Core::Float4, ColorIntensity)
        SHADER_PARAMETER(Core::Float4, PositionRadius)
        SHADER_PARAMETER(Core::Float4, DirectionInnerCos)
        SHADER_PARAMETER(Core::Float4, TypeOuterCos)
    END_SHADER_PARAMETER_STRUCT(VolumeWaterLight)

    BEGIN_SHADER_PARAMETER_STRUCT(VolumeWaterLightsUpload)
        RDG_BUFFER_ACCESS(Buffer, ERHIResourceAccess::TransferDest)
    END_SHADER_PARAMETER_STRUCT(VolumeWaterLightsUpload)

    BEGIN_SHADER_PARAMETER_STRUCT(VolumeWaterParameters)
        SHADER_PARAMETER_STRUCT_REFERENCE(VolumeAtmosphereLight, Atmosphere)
        SHADER_PARAMETER(Core::Mat4, WaterInverseViewProjection)
        SHADER_PARAMETER(Core::Mat4, WaterViewProjection)
        SHADER_PARAMETER(Core::Float4, WaterCamera)
        SHADER_PARAMETER(Core::Float4, WaterPosition)
        SHADER_PARAMETER(Core::Float4, WaterExtentAndRotation)
        SHADER_PARAMETER(Core::Float4, WaterWave)
        SHADER_PARAMETER(Core::Float4, WaterFlow)
        SHADER_PARAMETER(Core::Float4, WaterBody)
        SHADER_PARAMETER(Core::Float4, WaterAbsorption)
        SHADER_PARAMETER(Core::Float4, WaterOptics)
        SHADER_PARAMETER(Core::UInt4, WaterViewSettings)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, WaterColorOutput)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, WaterDepthOutput)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, SceneColor)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SceneDepth)
        SHADER_PARAMETER_RDG_STRUCTURED_BUFFER(VolumeWaterLight, WaterLights)
        SHADER_PARAMETER_RDG_TEXTURE(TextureCube<float4>, WaterEnvironment)
        SHADER_PARAMETER_SAMPLER(WaterSampler)
    END_SHADER_PARAMETER_STRUCT(VolumeWaterParameters)

    class VolumeWaterCS : public GlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER_TYPE(VolumeWaterCS)
        static const ShaderParametersMetadata* GetShaderParameterMetadata() { return VolumeWaterParameters::GetMetaData(); }
    };

    IMPLEMENT_GLOBAL_SHADER_TYPE(VolumeWaterCS, "VolumeWaterCS", "/volume/WaterDraw.sf", "MainCS", ERHIShaderFrequency::Compute);

    namespace
    {
        void BindWaterLights(RenderGraphBuilder& builder, Scene* scene, VolumeWaterParameters& parameters)
        {
            std::vector<VolumeWaterLight> lights;
            if (scene)
            {
                using namespace Engine;
                scene->ForEachLight([&](LightSceneProxy* light)
                {
                    if (light->GetIntensity() <= 0.0f)
                    {
                        return;
                    }
                    VolumeWaterLight data{};
                    data.ColorIntensity = Pack(light->GetColor(), light->GetIntensity());
                    switch (light->GetLightType())
                    {
                    case ELightType::Directional:
                        data.DirectionInnerCos = Pack(static_cast<const DirectionalLightSceneProxy*>(light)->GetDirection(), 0);
                        break;
                    case ELightType::Point:
                        data.PositionRadius = Pack(light->GetPosition(), static_cast<const PointLightSceneProxy*>(light)->GetAttenuationRadius());
                        data.TypeOuterCos.X = 1;
                        break;
                    case ELightType::Spot:
                    {
                        const auto* spot = static_cast<const SpotLightSceneProxy*>(light);
                        data.PositionRadius = Pack(light->GetPosition(), spot->GetAttenuationRadius());
                        data.DirectionInnerCos = Pack(spot->GetDirection(), std::cos(spot->GetInnerConeAngle()));
                        data.TypeOuterCos = {2, std::cos(spot->GetOuterConeAngle()), 0, 0};
                        break;
                    }
                    default:
                        return;
                    }
                    lights.push_back(data);
                });
            }
            parameters.WaterViewSettings.W = uint32_t(lights.size());
            if (lights.empty())
            {
                lights.emplace_back();
            }
            RenderGraphBufferDesc desc{};
            desc.Stride = sizeof(VolumeWaterLight);
            desc.Size = lights.size() * desc.Stride;
            desc.Usage = ERHIBufferUsageFlag::Structured | ERHIBufferUsageFlag::ShaderResource | ERHIBufferUsageFlag::TransferDst;
            auto* upload = builder.AllocateParameter<VolumeWaterLightsUpload>();
            upload->Buffer = builder.CreateBuffer("Volume.WaterLights", desc);
            RenderGraphBufferSRVDesc srv{};
            srv.Buffer = upload->Buffer;
            srv.Stride = desc.Stride;
            srv.NumElements = uint32_t(lights.size());
            parameters.WaterLights = builder.CreateBufferSRV("Volume.WaterLights.SRV", srv);
            builder.AddPass("VolumeWaterUploadLights", VolumeWaterLightsUpload::GetMetaData(), upload, EPassFlag::Graphic,
                [upload, lights = std::move(lights)](RHICommandListBase& list)
                {
                    GRHIApi->UpdateBuffer(list, upload->Buffer->GetRHIBuffer(), lights.data(),
                        {0, uint32_t(lights.size() * sizeof(VolumeWaterLight))});
                });
        }
    }

    RenderGraphTextureRef AddWaterPass(RenderGraphBuilder& builder, const Engine::SceneView& view, RenderGraphTextureRef color,
        RenderGraphTextureRef opaqueDepth, const WaterRenderSettings& settings, const VolumeAtmosphereLight& atmosphere,
        Engine::SceneInterface* sceneInterface)
    {
        if (!settings.bEnabled)
        {
            return opaqueDepth;
        }
        VolumeViewPass::ValidateInputs(view, color, opaqueDepth);
        const auto& desc = color->GetDesc();
        const auto shaded = VolumeViewPass::Image(builder, "Volume.WaterColor", desc.Width, desc.Height, desc.Format,
            ERHITextureCreateFlag::TransferSrc);
        // Use the same storage image type for both outputs. Depth consumers read the red channel.
        const auto effectiveDepth = VolumeViewPass::Image(builder, "Volume.WaterDepth", desc.Width, desc.Height);
        auto* parameters = builder.AllocateParameter<VolumeWaterParameters>();
        parameters->Atmosphere = atmosphere;
        parameters->WaterInverseViewProjection = view.InvViewProjectionMatrix;
        parameters->WaterViewProjection = view.ProjectionMatrix * view.ViewMatrix;
        parameters->WaterCamera = Pack(view.CameraWorldPos, view.FarClip);
        parameters->WaterPosition = Pack(settings.Position, 0);
        const float yaw = Core::DegToRad(settings.YawDegrees);
        const float direction = Core::DegToRad(settings.WaveDirectionDegrees);
        parameters->WaterExtentAndRotation = {std::max(0.01f, settings.HalfExtent.X), std::max(0.01f, settings.HalfExtent.Y),
            std::cos(yaw), std::sin(yaw)};
        parameters->WaterWave = {std::max(0.0f, settings.WaveAmplitude), std::max(0.001f, settings.WaveFrequency),
            settings.WaveSpeed, settings.TimeSeconds};
        parameters->WaterFlow = {std::cos(direction), std::sin(direction), std::max(0.0f, settings.NormalStrength), view.NearClip};
        parameters->WaterBody = Pack(settings.BodyColor, std::clamp(settings.Roughness, 0.05f, 1.0f));
        parameters->WaterAbsorption = Pack(settings.Absorption, std::max(0.01f, settings.MaxThickness));
        parameters->WaterViewSettings = {desc.Width, desc.Height, uint32_t(settings.View), 0};

        auto* scene = dynamic_cast<Scene*>(sceneInterface);
        const auto* resource = scene ? &scene->GetGPUResourceInfo().LightResourceInfo : nullptr;
        auto* environment = resource ? resource->IBLSpecularTexture : nullptr;
        parameters->WaterOptics = {std::clamp(settings.ReflectionStrength, 0.0f, 1.0f), std::clamp(settings.RefractionStrength, 0.0f, 1.0f),
            environment ? float(std::max(1u, uint32_t(environment->GetRHI()->GetDesc().MipLevels)) - 1u) : 0.0f, environment ? 1.0f : 0.0f};
        parameters->WaterSampler = GlobalSampler.get();
        parameters->WaterEnvironment = builder.RegisterExternalTexture("Volume.WaterIBL", environment ? environment : GlobalEmptyCubeTexture.get());
        BindWaterLights(builder, scene, *parameters);
        parameters->SceneColor = color;
        parameters->SceneDepth = opaqueDepth;
        parameters->WaterColorOutput = VolumeViewPass::WriteImage(builder, shaded);
        parameters->WaterDepthOutput = VolumeViewPass::WriteImage(builder, effectiveDepth);
        VolumeViewPass::AddCompute<VolumeWaterCS>(builder, "VolumeWaterSurface", parameters, desc.Width, desc.Height);
        VolumeViewPass::AddColorCopy(builder, "VolumeWaterCopy", shaded, color);
        return effectiveDepth;
    }
}
