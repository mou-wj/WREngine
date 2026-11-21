#include "VolumeRendering.h"
#include "GlobalShader.h"
#include "LightSceneProxy.h"
#include "RenderGraphBuilder.h"
#include "RHIPipelineStateCache.h"
#include "Scene.h"
#include "SceneShaderParameters.h"
#include "SceneView.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Renderer
{
    using namespace RenderCore;
    using namespace RHI;

    BEGIN_SHADER_PARAMETER_STRUCT(VolumeGridParameters)
        SHADER_PARAMETER(Core::UInt4, FroxelGrid)
        SHADER_PARAMETER(Core::UInt4, VolumeFrame)
        SHADER_PARAMETER(Core::Float4x4, InverseViewProjection)
        SHADER_PARAMETER(Core::Float4, VolumeCameraOrigin)
        SHADER_PARAMETER(Core::Float4, VolumeCameraAxis)
    END_SHADER_PARAMETER_STRUCT(VolumeGridParameters)

    BEGIN_SHADER_PARAMETER_STRUCT(VolumeMediumParameters)
        SHADER_PARAMETER_STRUCT_REFERENCE(VolumeGridParameters, Grid)
        SHADER_PARAMETER(Core::Float4, MediumScattering)
        SHADER_PARAMETER(Core::Float4, MediumAbsorption)
        SHADER_PARAMETER(Core::Float4, MediumEmission)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, MediumOutput)
    END_SHADER_PARAMETER_STRUCT(VolumeMediumParameters)

    BEGIN_SHADER_PARAMETER_STRUCT(VolumeLightData)
        SHADER_PARAMETER(Core::Float4, ColorIntensity)
        SHADER_PARAMETER(Core::Float4, PositionRadius)
        SHADER_PARAMETER(Core::Float4, DirectionInnerCos)
        SHADER_PARAMETER(Core::Float4, ConeType)
    END_SHADER_PARAMETER_STRUCT(VolumeLightData)

    BEGIN_SHADER_PARAMETER_STRUCT(VolumeShadowSlice)
        SHADER_PARAMETER(Core::Mat4, ShadowProjection)
        SHADER_PARAMETER(Core::Float4, UVTransform)
        SHADER_PARAMETER(Core::Float4, DepthLayerMip)
    END_SHADER_PARAMETER_STRUCT(VolumeShadowSlice)

    BEGIN_SHADER_PARAMETER_STRUCT(VolumeLightingParameters)
        SHADER_PARAMETER_STRUCT_REFERENCE(VolumeGridParameters, Grid)
        SHADER_PARAMETER_STRUCT_REFERENCE(SceneLightShadowParameters, Shadows)
        SHADER_PARAMETER(uint32_t, LightCount)
        SHADER_PARAMETER_RDG_STRUCTURED_BUFFER(VolumeLightData, SceneLights)
        SHADER_PARAMETER_RDG_STRUCTURED_BUFFER(VolumeShadowSlice, VolumeShadowSlices)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, Medium)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, LightingOutput)
    END_SHADER_PARAMETER_STRUCT(VolumeLightingParameters)

    BEGIN_SHADER_PARAMETER_STRUCT(VolumeBufferUpload)
        RDG_BUFFER_ACCESS(Buffer, ERHIResourceAccess::TransferDest)
    END_SHADER_PARAMETER_STRUCT(VolumeBufferUpload)

    BEGIN_SHADER_PARAMETER_STRUCT(VolumeIntegrateParameters)
        SHADER_PARAMETER_STRUCT_REFERENCE(VolumeGridParameters, Grid)
        SHADER_PARAMETER(Core::Float3, AmbientRadiance)
        SHADER_PARAMETER(uint32_t, IntegrationMode)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SceneDepth)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, Medium)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, DirectVolumeLighting)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, ScatteringOutput)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, TransmittanceOutput)
    END_SHADER_PARAMETER_STRUCT(VolumeIntegrateParameters)

    BEGIN_SHADER_PARAMETER_STRUCT(VolumeCompositeParameters)
        SHADER_PARAMETER_STRUCT_REFERENCE(VolumeGridParameters, Grid)
        SHADER_PARAMETER(uint32_t, DrawMode)
        SHADER_PARAMETER(uint32_t, DebugSlice)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, Medium)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, VolumeScattering)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, VolumeTransmittance)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, VolumeDirect)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, SceneColor)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, CompositeOutput)
    END_SHADER_PARAMETER_STRUCT(VolumeCompositeParameters)

    BEGIN_SHADER_PARAMETER_STRUCT(VolumeCopyParameters)
        RDG_TEXTURE_ACCESS(Source, ERHIResourceAccess::TransferSrc)
        RDG_TEXTURE_ACCESS(Dest, ERHIResourceAccess::TransferDest)
    END_SHADER_PARAMETER_STRUCT(VolumeCopyParameters)

    class VolumeMediumCS : public GlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER_TYPE(VolumeMediumCS)
        static const ShaderParametersMetadata* GetShaderParameterMetadata() { return VolumeMediumParameters::GetMetaData(); }
    };

    class VolumeIntegrateCS : public GlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER_TYPE(VolumeIntegrateCS)
        static const ShaderParametersMetadata* GetShaderParameterMetadata() { return VolumeIntegrateParameters::GetMetaData(); }
    };

    class VolumeLightingCS : public GlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER_TYPE(VolumeLightingCS)
        static const ShaderParametersMetadata* GetShaderParameterMetadata() { return VolumeLightingParameters::GetMetaData(); }
    };

    class VolumeCompositeCS : public GlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER_TYPE(VolumeCompositeCS)
        static const ShaderParametersMetadata* GetShaderParameterMetadata() { return VolumeCompositeParameters::GetMetaData(); }
    };

    IMPLEMENT_GLOBAL_SHADER_TYPE(VolumeMediumCS, "VolumeMediumCS", "/volume/VolumeMedium.sf", "MainCS", ERHIShaderFrequency::Compute);
    IMPLEMENT_GLOBAL_SHADER_TYPE(VolumeIntegrateCS, "VolumeIntegrateCS", "/volume/VolumeIntegrate.sf", "MainCS", ERHIShaderFrequency::Compute);
    IMPLEMENT_GLOBAL_SHADER_TYPE(VolumeLightingCS, "VolumeLightingCS", "/volume/VolumeLighting.sf", "MainCS", ERHIShaderFrequency::Compute);
    IMPLEMENT_GLOBAL_SHADER_TYPE(VolumeCompositeCS, "VolumeCompositeCS", "/volume/VolumeComposite.sf", "MainCS", ERHIShaderFrequency::Compute);

    namespace
    {
        uint32_t DivideRoundUp(uint32_t count, uint32_t groupSize)
        {
            return (count + groupSize - 1) / groupSize;
        }

        RenderGraphTextureUAVRef MakeUAV(RenderGraphBuilder& builder, RenderGraphTextureRef texture)
        {
            RenderGraphTextureUAVDesc desc{};
            desc.Texture = texture;
            desc.Format = texture->GetDesc().Format;
            return builder.CreateTextureUAV(texture->GetName() + ".UAV", desc);
        }

        template<class Shader, class Parameters>
        void AddCompute(RenderGraphBuilder& builder, const char* name, Parameters* params, Core::UInt3 groups)
        {
            const auto* shader = GShaderMap.GetShader<Shader>(0);
            if (!shader)
            {
                throw std::runtime_error(std::string("Volume: missing shader for ") + name);
            }
            RHIComputePipelineStateDesc desc{};
            desc.ComputeShader = static_cast<RHIComputeShader*>(shader->GetRHIShader());
            const auto pipeline = RHIPipelineStateCache::GetOrCreateComputePipelineState(desc);
            builder.AddPass(name, Parameters::GetMetaData(), params, EPassFlag::Compute, [=](RHICommandListBase& list)
            {
                auto& cmd = static_cast<RHIComputeCommandList&>(list);
                cmd.SetComputePipelineState(pipeline);
                SetShaderParameters(cmd, shader, params);
                cmd.Dispatch(groups.X, groups.Y, groups.Z);
            });
        }

        Core::Float4 Pack(Core::Float3 color, float alpha)
        {
            return {color.X, color.Y, color.Z, alpha};
        }

        template<class Record>
        RenderGraphBufferSRVRef UploadVolumeRecords(RenderGraphBuilder& builder, const char* name, std::vector<Record> records)
        {
            if (records.empty())
            {
                records.emplace_back();
            }
            RenderGraphBufferDesc desc{};
            desc.Stride = sizeof(Record);
            desc.Size = records.size() * desc.Stride;
            desc.Usage = ERHIBufferUsageFlag::ShaderResource | ERHIBufferUsageFlag::Structured | ERHIBufferUsageFlag::TransferDst;
            auto* upload = builder.AllocateParameter<VolumeBufferUpload>();
            upload->Buffer = builder.CreateBuffer(name, desc);
            RenderGraphBufferSRVDesc view{};
            view.Buffer = upload->Buffer;
            view.Stride = desc.Stride;
            view.NumElements = static_cast<uint32_t>(records.size());
            builder.AddPass(name, VolumeBufferUpload::GetMetaData(), upload, EPassFlag::Graphic,
                [upload, records = std::move(records)](RHICommandListBase& cmd)
                {
                    GRHIApi->UpdateBuffer(cmd, upload->Buffer->GetRHIBuffer(), records.data(),
                        {0, uint32_t(records.size() * sizeof(Record))});
                });
            return builder.CreateBufferSRV(std::string(name) + ".SRV", view);
        }

        void BindVolumeShadow(RenderGraphBuilder& builder, const Engine::SceneView& view, const LightShadowInfo& shadow,
            VolumeLightData& light, VolumeLightingParameters& params, std::vector<VolumeShadowSlice>& slices)
        {
            const auto& allocation = shadow.Allocation;
            if (!allocation.IsValid() || !allocation.Texture)
            {
                return;
            }
            const auto index = allocation.TextureIndex;
            const auto texture = [&]()
            {
                return builder.RegisterExternalTexture("Volume.Shadow." + std::to_string(reinterpret_cast<uintptr_t>(allocation.Texture)),
                    allocation.Texture);
            };
            if (allocation.ShadowType == EShadowType::Point && index < 8)
            {
                params.Shadows.PointLightShadows[index] = texture();
                light.ConeType.Z = 1;
                light.ConeType.W = float(index);
                return;
            }
            const bool bDirectional = allocation.ShadowType == EShadowType::Directional;
            const auto count = bDirectional ? Engine::SceneView::CascadeCount : 1u;
            if ((bDirectional && index >= 4) || shadow.ShadowMatrices.size() < count || allocation.Slices.size() < count)
            {
                return;
            }
            if (bDirectional)
            {
                params.Shadows.ParrallelLightShadows[index] = texture();
            }
            else if (allocation.ShadowType == EShadowType::Spot)
            {
                params.Shadows.LightShadowAtlas = texture();
            }
            else
            {
                return;
            }
            light.ConeType.Z = float(slices.size() + 1);
            light.ConeType.W = float(index);
            for (uint32_t i = 0; i < count; ++i)
            {
                const auto& region = allocation.Slices[i];
                VolumeShadowSlice slice{};
                slice.ShadowProjection = shadow.ShadowMatrices[i];
                slice.UVTransform = {region.UVScale.X, region.UVScale.Y, region.UVOffset.X, region.UVOffset.Y};
                slice.DepthLayerMip = {bDirectional ? view.SplitDepths[i] : 0, bDirectional ? view.SplitDepths[i + 1] : 0,
                    float(region.Layer), float(region.Mip)};
                slices.push_back(slice);
            }
        }

        void BindVolumeLights(RenderGraphBuilder& builder, const Engine::SceneView& view, Scene* scene,
            const VolumeRenderSettings& settings, VolumeLightingParameters& params)
        {
            std::vector<VolumeLightData> lights;
            std::vector<VolumeShadowSlice> slices;
            params.Shadows.NearestSampler = GlobalNearestSampler.get();
            const auto emptyCube = builder.RegisterExternalTexture("Volume.EmptyCube", GlobalEmptyCubeTexture.get());
            const auto emptyArray = builder.RegisterExternalTexture("Volume.EmptyArray", GlobalEmptyTexture2DArray.get());
            params.Shadows.LightShadowAtlas = builder.RegisterExternalTexture("Volume.EmptyShadowAtlas", GlobalWhiteTexture.get());
            for (uint32_t index = 0; index < 8; ++index)
            {
                params.Shadows.PointLightShadows[index] = emptyCube;
            }
            for (uint32_t index = 0; index < 4; ++index)
            {
                params.Shadows.ParrallelLightShadows[index] = emptyArray;
            }
            if (scene)
            {
                using namespace Engine;
                const auto& shadows = scene->GetGPUResourceInfo().ShadowResourceInfo.LightShadowInfos;
                const auto mask = settings.LightMask;
                scene->ForEachLight([&](LightSceneProxy* proxy)
                {
                    VolumeLightData light{};
                    light.ColorIntensity = Pack(proxy->GetColor(), proxy->GetIntensity());
                    if (proxy->GetLightType() == ELightType::Directional && (mask & 1u))
                    {
                        light.DirectionInnerCos = Pack(static_cast<DirectionalLightSceneProxy*>(proxy)->GetDirection(), 0);
                    }
                    else if (proxy->GetLightType() == ELightType::Point && (mask & 2u))
                    {
                        light.ConeType.Y = 1.0f;
                        light.PositionRadius = Pack(proxy->GetPosition(),
                            static_cast<PointLightSceneProxy*>(proxy)->GetAttenuationRadius());
                    }
                    else if (proxy->GetLightType() == ELightType::Spot && (mask & 4u))
                    {
                        const auto* spot = static_cast<SpotLightSceneProxy*>(proxy);
                        light.ConeType = {std::cos(spot->GetOuterConeAngle()), 2.0f, 0, 0};
                        light.PositionRadius = Pack(spot->GetPosition(), spot->GetAttenuationRadius());
                        light.DirectionInnerCos = Pack(spot->GetDirection(), std::cos(spot->GetInnerConeAngle()));
                    }
                    else
                    {
                        return;
                    }
                    if (settings.bShadows && proxy->IsCastShadow())
                    {
                        const auto shadow = shadows.find(proxy);
                        if (shadow != shadows.end())
                        {
                            BindVolumeShadow(builder, view, shadow->second, light, params, slices);
                        }
                    }
                    lights.push_back(light);
                });
            }
            params.LightCount = static_cast<uint32_t>(lights.size());
            params.SceneLights = UploadVolumeRecords(builder, "Volume.SceneLights", std::move(lights));
            params.VolumeShadowSlices = UploadVolumeRecords(builder, "Volume.ShadowSlices", std::move(slices));
        }
    }

    void AddVolumePasses(RenderGraphBuilder& builder, const Engine::SceneView& view,
        RenderGraphTextureRef sceneDepth, RenderGraphTextureRef sceneColor, const VolumeRenderSettings& settings,
        Engine::SceneInterface* sceneInterface)
    {
        if (!settings.bEnabled || settings.MaxDistance <= view.NearClip)
        {
            return;
        }
        const auto& colorDesc = sceneColor->GetDesc();
        const float farDistance = std::min(settings.MaxDistance, view.FarClip);
        if (farDistance <= view.NearClip || colorDesc.Format != ERHIFormat::R32G32B32A32_Float ||
            !EnumHasAnyFlags(colorDesc.Usage, ERHITextureCreateFlag::TransferDest))
        {
            throw std::invalid_argument("Volume requires linear RGBA32F scene color with TransferDest usage and valid clip distances.");
        }

        constexpr uint32_t tileSize = 8, atlasColumns = 8;
        const Core::UInt3 froxelSize{DivideRoundUp(colorDesc.Width, tileSize), DivideRoundUp(colorDesc.Height, tileSize), 32};
        VolumeGridParameters grid{};
        grid.FroxelGrid = {froxelSize.X, froxelSize.Y, froxelSize.Z, tileSize};
        grid.VolumeFrame = {colorDesc.Width, colorDesc.Height, atlasColumns, 0};
        grid.InverseViewProjection = view.InvViewProjectionMatrix;
        grid.VolumeCameraOrigin = Pack(view.CameraWorldPos, view.NearClip);
        const auto forward = Core::Normalize(Core::Float3{-view.ViewMatrix(2, 0), -view.ViewMatrix(2, 1), -view.ViewMatrix(2, 2)});
        grid.VolumeCameraAxis = Pack(forward, farDistance);
        const Core::UInt3 volumeGroups{DivideRoundUp(froxelSize.X, 4), DivideRoundUp(froxelSize.Y, 4), DivideRoundUp(froxelSize.Z, 4)};
        const Core::UInt3 screenGroups{DivideRoundUp(colorDesc.Width, 8), DivideRoundUp(colorDesc.Height, 8), 1};

        RenderGraphTextureDesc desc{};
        desc.Width = froxelSize.X * atlasColumns;
        desc.Height = froxelSize.Y * DivideRoundUp(froxelSize.Z * 3, atlasColumns);
        desc.Format = ERHIFormat::R32G32B32A32_Float;
        desc.Usage = ERHITextureCreateFlag::UAV | ERHITextureCreateFlag::ShaderResource;
        const auto medium = builder.CreateTexture("Volume.Medium", desc);
        auto* mediumParams = builder.AllocateParameter<VolumeMediumParameters>();
        mediumParams->Grid = grid;
        mediumParams->MediumScattering = Pack(settings.Medium.Scattering, settings.Medium.Anisotropy);
        mediumParams->MediumAbsorption = Pack(settings.Medium.Absorption, settings.Medium.Density);
        mediumParams->MediumEmission = Pack(settings.Medium.Emission, 0);
        mediumParams->MediumOutput = MakeUAV(builder, medium);
        AddCompute<VolumeMediumCS>(builder, "VolumeInjectMedium", mediumParams, volumeGroups);

        desc.Height = froxelSize.Y * DivideRoundUp(froxelSize.Z, atlasColumns);
        const auto directLighting = builder.CreateTexture("Volume.DirectLighting", desc);
        auto* lightingParams = builder.AllocateParameter<VolumeLightingParameters>();
        lightingParams->Grid = grid;
        BindVolumeLights(builder, view, dynamic_cast<Scene*>(sceneInterface), settings, *lightingParams);
        lightingParams->Medium = medium;
        lightingParams->LightingOutput = MakeUAV(builder, directLighting);
        AddCompute<VolumeLightingCS>(builder, "VolumeInjectLighting", lightingParams, volumeGroups);

        desc.Width = colorDesc.Width;
        desc.Height = colorDesc.Height;
        const auto scattering = builder.CreateTexture("Volume.Scattering", desc);
        const auto transmittance = builder.CreateTexture("Volume.Transmittance", desc);
        auto* integrate = builder.AllocateParameter<VolumeIntegrateParameters>();
        integrate->Grid = grid;
        integrate->AmbientRadiance = settings.AmbientRadiance;
        integrate->IntegrationMode = static_cast<uint32_t>(settings.View);
        integrate->SceneDepth = sceneDepth;
        integrate->Medium = medium;
        integrate->DirectVolumeLighting = directLighting;
        integrate->ScatteringOutput = MakeUAV(builder, scattering);
        integrate->TransmittanceOutput = MakeUAV(builder, transmittance);
        AddCompute<VolumeIntegrateCS>(builder, "VolumeIntegrate", integrate, screenGroups);

        desc.Usage |= ERHITextureCreateFlag::TransferSrc;
        const auto composite = builder.CreateTexture("Volume.Composite", desc);
        auto* compositeParams = builder.AllocateParameter<VolumeCompositeParameters>();
        compositeParams->Grid = grid;
        compositeParams->DrawMode = static_cast<uint32_t>(settings.View);
        compositeParams->DebugSlice = std::min(settings.DebugSlice, froxelSize.Z - 1);
        compositeParams->Medium = medium;
        compositeParams->VolumeScattering = scattering;
        compositeParams->VolumeTransmittance = transmittance;
        compositeParams->VolumeDirect = directLighting;
        compositeParams->SceneColor = sceneColor;
        compositeParams->CompositeOutput = MakeUAV(builder, composite);
        AddCompute<VolumeCompositeCS>(builder, "VolumeComposite", compositeParams, screenGroups);

        auto* copy = builder.AllocateParameter<VolumeCopyParameters>();
        copy->Source = composite;
        copy->Dest = sceneColor;
        builder.AddPass("VolumeCopyComposite", VolumeCopyParameters::GetMetaData(), copy, EPassFlag::Graphic,
            [copy, width = colorDesc.Width, height = colorDesc.Height](RHICommandListBase& cmd)
            {
                RHICopyTextureDesc region{};
                region.SrcRegion = region.DstRegion = {0, 0, 0, width, height, 1};
                cmd.CopyTexture(copy->Source->GetRHITexture(), copy->Dest->GetRHITexture(), region);
            });
    }
}
