#include "VolumeRendering.h"
#include "GlobalShader.h"
#include "HashHelper.hpp"
#include "LightSceneProxy.h"
#include "PrimitiveSceneProxy.h"
#include "RenderGraphBuilder.h"
#include "RHIPipelineStateCache.h"
#include "Scene.h"
#include "SceneShaderParameters.h"
#include "SceneView.h"
#include "Transform.hpp"
#include <algorithm>
#include <array>
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

    BEGIN_SHADER_PARAMETER_STRUCT(VolumeLocalData)
        SHADER_PARAMETER(Core::Mat4, WorldToLocal)
        SHADER_PARAMETER(Core::Float4, LocalScattering)
        SHADER_PARAMETER(Core::Float4, LocalAbsorption)
        SHADER_PARAMETER(Core::Float4, LocalEmission)
        SHADER_PARAMETER(Core::Float4, DensityParameters)
        SHADER_PARAMETER(Core::Float4, DensityOffsetMode)
        SHADER_PARAMETER(Core::Float4, LocalDepthRange)
    END_SHADER_PARAMETER_STRUCT(VolumeLocalData)

    BEGIN_SHADER_PARAMETER_STRUCT(VolumeMediumParameters)
        SHADER_PARAMETER_STRUCT_REFERENCE(VolumeGridParameters, Grid)
        SHADER_PARAMETER(Core::Float4, MediumScattering)
        SHADER_PARAMETER(Core::Float4, MediumAbsorption)
        SHADER_PARAMETER(Core::Float4, MediumEmission)
        SHADER_PARAMETER(uint32_t, LocalCount)
        SHADER_PARAMETER(uint32_t, bWriteDensitySources)
        SHADER_PARAMETER_RDG_STRUCTURED_BUFFER(VolumeLocalData, LocalVolumes)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, MediumOutput)
    END_SHADER_PARAMETER_STRUCT(VolumeMediumParameters)

    BEGIN_SHADER_PARAMETER_STRUCT(VolumeTexturedMediumParameters)
        SHADER_PARAMETER_STRUCT_REFERENCE(VolumeMediumParameters, Common)
        SHADER_PARAMETER_SAMPLER(VolumeDensitySampler)
        SHADER_PARAMETER_RDG_TEXTURE_ARRAY(Texture3D<float4>, VolumeDensityTextures, 8)
    END_SHADER_PARAMETER_STRUCT(VolumeTexturedMediumParameters)

    BEGIN_SHADER_PARAMETER_STRUCT(VolumeLightData)
        SHADER_PARAMETER(Core::Float4, ColorIntensity)
        SHADER_PARAMETER(Core::Float4, PositionRadius)
        SHADER_PARAMETER(Core::Float4, DirectionInnerCos)
        SHADER_PARAMETER(Core::Float4, ConeType)
        SHADER_PARAMETER(Core::Float4, DepthRange)
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

    BEGIN_SHADER_PARAMETER_STRUCT(VolumeEnvironmentParameters)
        SHADER_PARAMETER(Core::Float4, AmbientFallback)
        SHADER_PARAMETER(Core::Float4, SkySettings)
        SHADER_PARAMETER_SAMPLER(VolumeSkySampler)
        SHADER_PARAMETER_RDG_TEXTURE(TextureCube<float4>, VolumeSky)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, AmbientOutput)
    END_SHADER_PARAMETER_STRUCT(VolumeEnvironmentParameters)

    BEGIN_SHADER_PARAMETER_STRUCT(VolumeBufferUpload)
        RDG_BUFFER_ACCESS(Buffer, ERHIResourceAccess::TransferDest)
    END_SHADER_PARAMETER_STRUCT(VolumeBufferUpload)

    BEGIN_SHADER_PARAMETER_STRUCT(VolumeRayParameters)
        SHADER_PARAMETER_STRUCT_REFERENCE(VolumeGridParameters, Grid)
        SHADER_PARAMETER(uint32_t, IntegrationMode)
        SHADER_PARAMETER(uint32_t, IntegrationStride)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SceneDepth)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, Medium)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, DirectVolumeLighting)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, IndirectVolumeLighting)
    END_SHADER_PARAMETER_STRUCT(VolumeRayParameters)

    BEGIN_SHADER_PARAMETER_STRUCT(VolumeIntegrateParameters)
        SHADER_PARAMETER_STRUCT_REFERENCE(VolumeRayParameters, Ray)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, ScatteringOutput)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, TransmittanceOutput)
    END_SHADER_PARAMETER_STRUCT(VolumeIntegrateParameters)

    BEGIN_SHADER_PARAMETER_STRUCT(VolumeReconstructParameters)
        SHADER_PARAMETER_STRUCT_REFERENCE(VolumeRayParameters, Ray)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, SparseScattering)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, SparseTransmittance)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, SceneColor)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, CompositeOutput)
    END_SHADER_PARAMETER_STRUCT(VolumeReconstructParameters)

    BEGIN_SHADER_PARAMETER_STRUCT(VolumeTemporalParameters)
        SHADER_PARAMETER_STRUCT_REFERENCE(VolumeRayParameters, Ray)
        SHADER_PARAMETER(Core::Mat4, PreviousViewProjection)
        SHADER_PARAMETER(Core::Float4, TemporalSettings)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PreviousScattering)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, PreviousTransmittance)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, ScatteringOutput)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, TransmittanceOutput)
    END_SHADER_PARAMETER_STRUCT(VolumeTemporalParameters)

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

    class VolumeTexturedMediumCS : public GlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER_TYPE(VolumeTexturedMediumCS)
        static const ShaderParametersMetadata* GetShaderParameterMetadata() { return VolumeTexturedMediumParameters::GetMetaData(); }
    };

    class VolumeIntegrateCS : public GlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER_TYPE(VolumeIntegrateCS)
        static const ShaderParametersMetadata* GetShaderParameterMetadata() { return VolumeIntegrateParameters::GetMetaData(); }
    };

    class VolumeReconstructCS : public GlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER_TYPE(VolumeReconstructCS)
        static const ShaderParametersMetadata* GetShaderParameterMetadata() { return VolumeReconstructParameters::GetMetaData(); }
    };

    class VolumeTemporalCS : public GlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER_TYPE(VolumeTemporalCS)
        static const ShaderParametersMetadata* GetShaderParameterMetadata() { return VolumeTemporalParameters::GetMetaData(); }
    };

    class VolumeLightingCS : public GlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER_TYPE(VolumeLightingCS)
        static const ShaderParametersMetadata* GetShaderParameterMetadata() { return VolumeLightingParameters::GetMetaData(); }
    };

    class VolumeEnvironmentCS : public GlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER_TYPE(VolumeEnvironmentCS)
        static const ShaderParametersMetadata* GetShaderParameterMetadata() { return VolumeEnvironmentParameters::GetMetaData(); }
    };

    class VolumeCompositeCS : public GlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER_TYPE(VolumeCompositeCS)
        static const ShaderParametersMetadata* GetShaderParameterMetadata() { return VolumeCompositeParameters::GetMetaData(); }
    };

    IMPLEMENT_GLOBAL_SHADER_TYPE(VolumeMediumCS, "VolumeMediumCS", "/volume/VolumeMedium.sf", "MainCS", ERHIShaderFrequency::Compute);
    IMPLEMENT_GLOBAL_SHADER_TYPE(VolumeTexturedMediumCS, "VolumeTexturedMediumCS", "/volume/VolumeTexturedMedium.sf", "MainCS", ERHIShaderFrequency::Compute);
    IMPLEMENT_GLOBAL_SHADER_TYPE(VolumeIntegrateCS, "VolumeIntegrateCS", "/volume/VolumeIntegrate.sf", "MainCS", ERHIShaderFrequency::Compute);
    IMPLEMENT_GLOBAL_SHADER_TYPE(VolumeReconstructCS, "VolumeReconstructCS", "/volume/VolumeReconstruct.sf", "MainCS", ERHIShaderFrequency::Compute);
    IMPLEMENT_GLOBAL_SHADER_TYPE(VolumeTemporalCS, "VolumeTemporalCS", "/volume/VolumeTemporal.sf", "MainCS", ERHIShaderFrequency::Compute);
    IMPLEMENT_GLOBAL_SHADER_TYPE(VolumeLightingCS, "VolumeLightingCS", "/volume/VolumeLighting.sf", "MainCS", ERHIShaderFrequency::Compute);
    IMPLEMENT_GLOBAL_SHADER_TYPE(VolumeEnvironmentCS, "VolumeEnvironmentCS", "/volume/VolumeEnvironment.sf", "MainCS", ERHIShaderFrequency::Compute);
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

        void HashValues(size_t& hash, std::initializer_list<float> values)
        {
            for (const float value : values)
            {
                HashCombine(hash, value);
            }
        }

        void HashVector(size_t& hash, const Core::Float4& value)
        {
            HashValues(hash, {value.X, value.Y, value.Z, value.W});
        }

        void HashMatrix(size_t& hash, const Core::Mat4& matrix)
        {
            for (int row = 0; row < 4; ++row)
            {
                for (int column = 0; column < 4; ++column)
                {
                    HashCombine(hash, matrix(row, column));
                }
            }
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

        void BindVolumeLights(RenderGraphBuilder& builder, const Engine::SceneView& view, const VolumeGridParameters& grid,
            Scene* scene, const VolumeRenderSettings& settings, VolumeLightingParameters& params, size_t* inputsHash)
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
                    if (proxy->GetIntensity() <= 0.0f)
                    {
                        return;
                    }
                    VolumeLightData light{};
                    light.ColorIntensity = Pack(proxy->GetColor(), proxy->GetIntensity());
                    if (proxy->GetLightType() == ELightType::Directional && (mask & 1u))
                    {
                        light.DirectionInnerCos = Pack(Core::Normalize(static_cast<DirectionalLightSceneProxy*>(proxy)->GetDirection()), 0);
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
                        light.DirectionInnerCos = Pack(Core::Normalize(spot->GetDirection()), std::cos(spot->GetInnerConeAngle()));
                    }
                    else
                    {
                        return;
                    }
                    const bool bCastsShadow = settings.bShadows && proxy->IsCastShadow();
                    if (inputsHash)
                    {
                        HashVector(*inputsHash, light.ColorIntensity);
                        HashVector(*inputsHash, light.PositionRadius);
                        HashVector(*inputsHash, light.DirectionInnerCos);
                        HashVector(*inputsHash, light.ConeType);
                        HashCombine(*inputsHash, bCastsShadow);
                    }
                    light.DepthRange = {grid.VolumeCameraOrigin.W, grid.VolumeCameraAxis.W, 0, 0};
                    if (light.ConeType.Y != 0)
                    {
                        const auto position = Core::Float3{light.PositionRadius.X, light.PositionRadius.Y, light.PositionRadius.Z};
                        const auto forward = Core::Float3{grid.VolumeCameraAxis.X, grid.VolumeCameraAxis.Y, grid.VolumeCameraAxis.Z};
                        const float depth = Core::Dot(position - view.CameraWorldPos, forward);
                        const float radius = light.PositionRadius.W;
                        light.DepthRange.X = depth - radius;
                        light.DepthRange.Y = depth + radius;
                        if (radius <= 0 || light.DepthRange.Y < grid.VolumeCameraOrigin.W ||
                            light.DepthRange.X > grid.VolumeCameraAxis.W)
                        {
                            return;
                        }
                    }
                    if (bCastsShadow)
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

    VolumeRenderHistory::VolumeRenderHistory() = default;

    VolumeRenderHistory::~VolumeRenderHistory()
    {
        Release();
    }

    void VolumeRenderHistory::Invalidate()
    {
        m_bValid = false;
    }

    void VolumeRenderHistory::Release()
    {
        for (auto& slot : m_Slots)
        {
            for (auto* image : {slot.Scattering.get(), slot.Transmittance.get()})
            {
                if (image && GRHIApi)
                {
                    const auto fence = image->GetTracker().GetLastAccessFence();
                    if (fence.Value)
                    {
                        GRHIApi->GetQueue(fence.QueueType)->WaitValue(fence.Value);
                    }
                }
            }
            slot = {};
        }
        m_Size = {};
        m_NextSlot = 0;
        Invalidate();
    }

    void AddVolumePasses(RenderGraphBuilder& builder, const Engine::SceneView& view,
        RenderGraphTextureRef sceneDepth, RenderGraphTextureRef sceneColor, const VolumeRenderSettings& settings,
        Engine::SceneInterface* sceneInterface, VolumeRenderHistory* history, VolumeRenderStats* stats)
    {
        if (stats)
        {
            *stats = {};
        }
        if (!settings.bEnabled || settings.MaxDistance <= view.NearClip)
        {
            if (history)
            {
                history->Invalidate();
            }
            return;
        }
        if (!sceneColor || !sceneDepth)
        {
            throw std::invalid_argument("Volume requires both scene color and opaque depth.");
        }
        const auto& colorDesc = sceneColor->GetDesc();
        const auto& depthDesc = sceneDepth->GetDesc();
        const float farDistance = std::min(settings.MaxDistance, view.FarClip);
        if (view.NearClip <= 0.0f || farDistance <= view.NearClip || colorDesc.Format != ERHIFormat::R32G32B32A32_Float ||
            !EnumHasAnyFlags(colorDesc.Usage, ERHITextureCreateFlag::TransferDest) || depthDesc.Width != colorDesc.Width ||
            depthDesc.Height != colorDesc.Height || view.Viewport.X != 0 || view.Viewport.Y != 0 ||
            view.Viewport.Width != int(colorDesc.Width) || view.Viewport.Height != int(colorDesc.Height) ||
            !view.bDepthRangeZeroToOne || view.ProjectionMatrix(3, 2) != -1.0f)
        {
            throw std::invalid_argument("Volume requires a full-size perspective view, matching zero-to-one depth, and linear RGBA32F color with TransferDest.");
        }

        const uint32_t tileSize = std::clamp(settings.FroxelTileSize, 4u, 16u);
        const uint32_t depthSlices = std::clamp(settings.FroxelSliceCount, 16u, 64u);
        // These column counts keep atlas dimensions bounded for the preset slice counts.
        const uint32_t atlasColumns = depthSlices <= 16 ? 4u : 8u;
        const Core::UInt3 froxelSize{DivideRoundUp(colorDesc.Width, tileSize), DivideRoundUp(colorDesc.Height, tileSize), depthSlices};
        const bool bIntegratedView = settings.View == EVolumeView::Composite || settings.View == EVolumeView::Scattering ||
            settings.View == EVolumeView::Transmittance || settings.View == EVolumeView::HistoryWeight;
        const bool bUseHistory = history && bIntegratedView && settings.HistoryWeight > 0.0f;
        if (history && !bUseHistory)
        {
            history->Invalidate();
        }
        const bool bNeedsRays = bIntegratedView || settings.View == EVolumeView::DirectLighting ||
            settings.View == EVolumeView::IndirectLighting;
        const uint32_t stride = settings.bSpatialReconstruction && bNeedsRays ? 2u : 1u;
        const Core::UInt2 size{colorDesc.Width, colorDesc.Height};
        const Core::UInt2 integrationSize{DivideRoundUp(size.X, stride), DivideRoundUp(size.Y, stride)};
        if (stats)
        {
            stats->FroxelSize = froxelSize;
            stats->IntegrationSize = bNeedsRays ? integrationSize : Core::UInt2{};
        }
        VolumeGridParameters grid{};
        grid.FroxelGrid = {froxelSize.X, froxelSize.Y, froxelSize.Z, tileSize};
        const uint32_t phase = settings.bJitter && bIntegratedView ? uint32_t(view.FrameIndex % 64) + 1 : 0u;
        grid.VolumeFrame = {colorDesc.Width, colorDesc.Height, atlasColumns, phase};
        grid.InverseViewProjection = view.InvViewProjectionMatrix;
        grid.VolumeCameraOrigin = Pack(view.CameraWorldPos, view.NearClip);
        const auto forward = Core::Normalize(Core::Float3{-view.ViewMatrix(2, 0), -view.ViewMatrix(2, 1), -view.ViewMatrix(2, 2)});
        grid.VolumeCameraAxis = Pack(forward, farDistance);
        const Core::UInt3 volumeGroups{DivideRoundUp(froxelSize.X, 4), DivideRoundUp(froxelSize.Y, 4), DivideRoundUp(froxelSize.Z, 4)};
        const Core::UInt3 screenGroups{DivideRoundUp(colorDesc.Width, 8), DivideRoundUp(colorDesc.Height, 8), 1};

        RenderGraphTextureDesc desc{};
        desc.Width = froxelSize.X * atlasColumns;
        const bool bShowDensitySources = settings.View == EVolumeView::DensitySources;
        desc.Height = froxelSize.Y * DivideRoundUp(froxelSize.Z * (bShowDensitySources ? 4u : 3u), atlasColumns);
        desc.Format = ERHIFormat::R32G32B32A32_Float;
        desc.Usage = ERHITextureCreateFlag::UAV | ERHITextureCreateFlag::ShaderResource;
        const auto medium = builder.CreateTexture("Volume.Medium", desc);
        VolumeMediumParameters mediumParams{};
        mediumParams.Grid = grid;
        mediumParams.MediumScattering = Pack(settings.Medium.Scattering, settings.Medium.Anisotropy);
        mediumParams.MediumAbsorption = Pack(settings.Medium.Absorption, settings.Medium.Density);
        mediumParams.MediumEmission = Pack(settings.Medium.Emission, 0);
        mediumParams.bWriteDensitySources = bShowDensitySources ? 1u : 0u;
        size_t inputsHash = 0;
        float maxFlow = 0.0f;
        if (bUseHistory)
        {
            HashCombine(inputsHash, settings.SceneRevision);
            HashCombine(inputsHash, settings.LightMask);
            HashCombine(inputsHash, depthSlices);
            HashCombine(inputsHash, tileSize);
            HashCombine(inputsHash, stride);
            HashCombine(inputsHash, size.X);
            HashCombine(inputsHash, size.Y);
            HashCombine(inputsHash, settings.bJitter);
            HashVector(inputsHash, mediumParams.MediumScattering);
            HashVector(inputsHash, mediumParams.MediumAbsorption);
            HashVector(inputsHash, mediumParams.MediumEmission);
            HashValues(inputsHash, {view.NearClip, farDistance});
        }
        std::vector<VolumeLocalData> localData;
        std::vector<RenderTexture*> densityTextures;
        localData.reserve(settings.LocalVolumes.size());
        for (const auto& local : settings.LocalVolumes)
        {
            if (local.HalfExtent.X < 1e-3f || local.HalfExtent.Y < 1e-3f || local.HalfExtent.Z < 1e-3f ||
                local.Medium.Density <= 0.0f)
            {
                continue;
            }
            const auto transform = Core::MakeTransformMatrix(local.Position, local.RotationDegrees, local.HalfExtent);
            const float determinant = Core::Determinant(transform);
            if (!std::isfinite(determinant) || determinant <= 1e-9f)
            {
                continue;
            }
            VolumeLocalData packed{};
            packed.WorldToLocal = Core::Inverse(transform);
            packed.LocalScattering = Pack(local.Medium.Scattering, local.Medium.Anisotropy);
            packed.LocalAbsorption = Pack(local.Medium.Absorption, local.Medium.Density);
            packed.LocalEmission = Pack(local.Medium.Emission, static_cast<float>(local.Shape));
            const float centerDepth = Core::Dot(local.Position - view.CameraWorldPos, forward);
            const float boundingRadius = local.Shape == EVolumeShape::Box ? local.HalfExtent.Length() :
                std::max({local.HalfExtent.X, local.HalfExtent.Y, local.HalfExtent.Z});
            packed.LocalDepthRange = {centerDepth - boundingRadius, centerDepth + boundingRadius, 0, 0};
            const auto& field = local.DensityField;
            const auto offset = field.Offset + field.Velocity * static_cast<float>(view.FrameTimeSeconds);
            float mode = static_cast<float>(field.Mode);
            if (field.Mode == EVolumeDensityMode::Texture)
            {
                if (!field.Texture || !field.Texture->GetRHI() ||
                    field.Texture->GetRHI()->GetDesc().Type != ERHITextureType::Texture3D ||
                    !EnumHasAnyFlags(field.Texture->GetRHI()->GetDesc().Usage, ERHITextureCreateFlag::ShaderResource))
                {
                    throw std::invalid_argument("Volume texture density requires a sampled 3D texture.");
                }
                auto index = std::find(densityTextures.begin(), densityTextures.end(), field.Texture);
                if (index == densityTextures.end())
                {
                    if (densityTextures.size() == 8)
                    {
                        throw std::invalid_argument("Volume supports eight unique density textures per view.");
                    }
                    mode += static_cast<float>(densityTextures.size());
                    densityTextures.push_back(field.Texture);
                }
                else
                {
                    mode += static_cast<float>(index - densityTextures.begin());
                }
            }
            packed.DensityParameters = {std::max(field.Scale, 0.0f), field.Bias, std::max(field.Threshold, 0.0f),
                std::max(field.Frequency, 0.001f)};
            packed.DensityOffsetMode = Pack(offset, mode);
            if (bUseHistory)
            {
                HashMatrix(inputsHash, transform);
                HashVector(inputsHash, packed.LocalScattering);
                HashVector(inputsHash, packed.LocalAbsorption);
                HashVector(inputsHash, packed.LocalEmission);
                HashVector(inputsHash, packed.DensityParameters);
                HashVector(inputsHash, Pack(field.Offset, mode));
                HashVector(inputsHash, Pack(field.Velocity, 0));
                HashCombine(inputsHash, field.Texture);
                if (field.Mode != EVolumeDensityMode::Uniform)
                {
                    const float frequency = field.Mode == EVolumeDensityMode::Noise ? packed.DensityParameters.W : 1.0f;
                    maxFlow = std::max(maxFlow, field.Velocity.Length() * frequency);
                }
            }
            if (packed.LocalDepthRange.Y >= view.NearClip && packed.LocalDepthRange.X <= farDistance)
            {
                localData.push_back(packed);
            }
        }
        mediumParams.LocalCount = static_cast<uint32_t>(localData.size());
        mediumParams.LocalVolumes = UploadVolumeRecords(builder, "Volume.LocalMedia", std::move(localData));
        mediumParams.MediumOutput = MakeUAV(builder, medium);
        if (densityTextures.empty())
        {
            auto* params = builder.AllocateParameter<VolumeMediumParameters>();
            *params = mediumParams;
            AddCompute<VolumeMediumCS>(builder, "VolumeInjectMedium", params, volumeGroups);
        }
        else
        {
            auto* params = builder.AllocateParameter<VolumeTexturedMediumParameters>();
            params->Common = mediumParams;
            params->VolumeDensitySampler = GlobalSampler.get();
            for (size_t index = 0; index < 8; ++index)
            {
                auto* texture = index < densityTextures.size() ? densityTextures[index] : densityTextures.front();
                params->VolumeDensityTextures[index] = builder.RegisterExternalTexture(
                    "Volume.DensityInput." + std::to_string(index), texture);
            }
            AddCompute<VolumeTexturedMediumCS>(builder, "VolumeInjectTexturedMedium", params, volumeGroups);
        }

        const uint64_t mediumPixels = uint64_t(desc.Width) * desc.Height;
        desc.Height = froxelSize.Y * DivideRoundUp(froxelSize.Z, atlasColumns);
        const uint64_t lightingPixels = uint64_t(desc.Width) * desc.Height;
        const auto directLighting = builder.CreateTexture("Volume.DirectLighting", desc);
        auto* lightingParams = builder.AllocateParameter<VolumeLightingParameters>();
        lightingParams->Grid = grid;
        auto* scene = dynamic_cast<Scene*>(sceneInterface);
        BindVolumeLights(builder, view, grid, scene, settings, *lightingParams, bUseHistory ? &inputsHash : nullptr);
        lightingParams->Medium = medium;
        lightingParams->LightingOutput = MakeUAV(builder, directLighting);
        AddCompute<VolumeLightingCS>(builder, "VolumeInjectLighting", lightingParams, volumeGroups);

        const auto* resources = scene ? &scene->GetGPUResourceInfo().LightResourceInfo : nullptr;
        auto* environment = settings.bUseSkyEnvironment && resources ? resources->IBLSpecularTexture : nullptr;
        if (bUseHistory)
        {
            HashCombine(inputsHash, sceneInterface);
            HashCombine(inputsHash, environment);
            HashVector(inputsHash, Pack(settings.AmbientRadiance, settings.EnvironmentIntensity));
            if (scene)
            {
                scene->ForEachPrimitive([&](Engine::PrimitiveSceneProxy* primitive)
                {
                    HashCombine(inputsHash, primitive);
                    HashMatrix(inputsHash, primitive->GetLocalToWorld());
                });
            }
        }
        desc.Width = desc.Height = 1;
        const auto indirectLighting = builder.CreateTexture("Volume.IndirectLighting", desc);
        auto* ambientParams = builder.AllocateParameter<VolumeEnvironmentParameters>();
        ambientParams->AmbientFallback = Pack(settings.AmbientRadiance, 0);
        ambientParams->SkySettings = {environment ? 1.0f : 0.0f, std::max(0.0f, settings.EnvironmentIntensity),
            environment ? float(environment->GetRHI()->GetDesc().MipLevels - 1u) : 0.0f, 0.0f};
        ambientParams->VolumeSkySampler = GlobalSampler.get();
        ambientParams->VolumeSky = builder.RegisterExternalTexture("Volume.Environment",
            environment ? environment : GlobalEmptyCubeTexture.get());
        ambientParams->AmbientOutput = MakeUAV(builder, indirectLighting);
        AddCompute<VolumeEnvironmentCS>(builder, "VolumeGatherEnvironment", ambientParams, {1, 1, 1});

        RenderGraphTextureRef scattering, transmittance;
        VolumeRayParameters ray{};
        const Core::UInt3 integrationGroups{DivideRoundUp(integrationSize.X, 8), DivideRoundUp(integrationSize.Y, 8), 1};
        uint64_t transientPixels = mediumPixels + lightingPixels + 1 + uint64_t(size.X) * size.Y;
        if (bNeedsRays)
        {
            ray.Grid = grid;
            ray.IntegrationMode = static_cast<uint32_t>(settings.View);
            ray.IntegrationStride = stride;
            ray.SceneDepth = sceneDepth;
            ray.Medium = medium;
            ray.DirectVolumeLighting = directLighting;
            ray.IndirectVolumeLighting = indirectLighting;
            if (!bUseHistory)
            {
                desc.Width = integrationSize.X;
                desc.Height = integrationSize.Y;
                transientPixels += 2 * uint64_t(desc.Width) * desc.Height;
                scattering = builder.CreateTexture("Volume.Scattering", desc);
                transmittance = builder.CreateTexture("Volume.Transmittance", desc);
                auto* integrate = builder.AllocateParameter<VolumeIntegrateParameters>();
                integrate->Ray = ray;
                integrate->ScatteringOutput = MakeUAV(builder, scattering);
                integrate->TransmittanceOutput = MakeUAV(builder, transmittance);
                AddCompute<VolumeIntegrateCS>(builder, "VolumeIntegrate", integrate, integrationGroups);
            }
        }
        else
        {
            scattering = transmittance = builder.RegisterExternalTexture("Volume.UnusedIntegration", GlobalWhiteTexture.get());
        }

        if (bUseHistory)
        {
            if (history->m_Size != integrationSize)
            {
                history->Release();
                RHITextureDesc historyDesc{};
                historyDesc.Width = integrationSize.X;
                historyDesc.Height = integrationSize.Y;
                historyDesc.Format = ERHIFormat::R32G32B32A32_Float;
                historyDesc.Usage = ERHITextureCreateFlag::ShaderResource | ERHITextureCreateFlag::UAV;
                for (auto& slot : history->m_Slots)
                {
                    slot.Scattering = std::make_unique<RenderTexture>(historyDesc);
                    slot.Scattering->InitRHIResource();
                    slot.Transmittance = std::make_unique<RenderTexture>(historyDesc);
                    slot.Transmittance->InitRHIResource();
                }
                history->m_Size = integrationSize;
            }

            const auto index = history->m_NextSlot;
            auto& current = history->m_Slots[index];
            auto& previous = history->m_Slots[1 - index];
            const float maxJump = std::max(0.5f, farDistance * 0.12f);
            const bool bReuse = history->m_bValid && view.FrameIndex == history->m_PreviousFrame + 1 &&
                inputsHash == history->m_PreviousInputs &&
                (view.CameraWorldPos - history->m_PreviousOrigin).LengthSquared() <= maxJump * maxJump &&
                Core::Dot(history->m_PreviousForward, forward) > 0.85f;
            const auto flow = maxFlow * float(std::abs(view.FrameTimeSeconds - history->m_PreviousTime));
            const float weight = bReuse ? std::clamp(settings.HistoryWeight, 0.0f, 0.95f) *
                std::clamp(1.0f - flow * 4.0f, 0.0f, 1.0f) : 0.0f;
            auto* temporal = builder.AllocateParameter<VolumeTemporalParameters>();
            temporal->Ray = ray;
            temporal->PreviousViewProjection = history->m_PreviousViewProjection;
            temporal->TemporalSettings = {weight, 0, 0, 0};
            if (weight > 0.0f)
            {
                temporal->PreviousScattering = builder.RegisterExternalTexture("Volume.PreviousScattering", previous.Scattering.get());
                temporal->PreviousTransmittance = builder.RegisterExternalTexture("Volume.PreviousTransmittance", previous.Transmittance.get());
            }
            else
            {
                temporal->PreviousScattering = temporal->PreviousTransmittance =
                    builder.RegisterExternalTexture("Volume.EmptyHistory", GlobalWhiteTexture.get());
            }
            scattering = builder.RegisterExternalTexture("Volume.CurrentScattering", current.Scattering.get());
            transmittance = builder.RegisterExternalTexture("Volume.CurrentTransmittance", current.Transmittance.get());
            temporal->ScatteringOutput = MakeUAV(builder, scattering);
            temporal->TransmittanceOutput = MakeUAV(builder, transmittance);
            AddCompute<VolumeTemporalCS>(builder, "VolumeIntegrateTemporal", temporal, integrationGroups);
            history->m_PreviousViewProjection = view.ProjectionMatrix * view.ViewMatrix;
            history->m_PreviousOrigin = view.CameraWorldPos;
            history->m_PreviousForward = forward;
            history->m_PreviousFrame = view.FrameIndex;
            history->m_PreviousInputs = inputsHash;
            history->m_PreviousTime = view.FrameTimeSeconds;
            history->m_NextSlot = 1 - index;
            history->m_bValid = true;
            if (stats)
            {
                stats->bHistoryEligible = weight > 0.0f;
            }
        }
        if (stats)
        {
            stats->TransientBytes = transientPixels * 16;
            if (history)
            {
                stats->HistoryBytes = uint64_t(history->m_Size.X) * history->m_Size.Y * 4 * 16;
            }
        }

        desc.Width = size.X;
        desc.Height = size.Y;
        desc.Usage |= ERHITextureCreateFlag::TransferSrc;
        const auto composite = builder.CreateTexture("Volume.Composite", desc);
        if (stride != 1)
        {
            auto* reconstruct = builder.AllocateParameter<VolumeReconstructParameters>();
            reconstruct->Ray = ray;
            reconstruct->SparseScattering = scattering;
            reconstruct->SparseTransmittance = transmittance;
            reconstruct->SceneColor = sceneColor;
            reconstruct->CompositeOutput = MakeUAV(builder, composite);
            AddCompute<VolumeReconstructCS>(builder, "VolumeReconstructAndComposite", reconstruct, screenGroups);
        }
        else
        {
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
        }

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

    Engine::SceneViewDrawExtension MakeVolumeDrawExtension(VolumeRenderSettings settings, Engine::SceneInterface* scene,
        VolumeRenderHistory* history, VolumeRenderStats* stats)
    {
        return {[settings = std::move(settings), scene, history, stats](RenderGraphBuilder& graph, const Engine::SceneView& view,
            RenderGraphTextureRef color, RenderGraphTextureRef depth)
        {
            AddVolumePasses(graph, view, depth, color, settings, scene, history, stats);
        }};
    }

    std::shared_ptr<RenderTexture> CreateVolumePreviewDensityTexture()
    {
        constexpr uint32_t size = 16;
        constexpr float frequency = 6.2831853f / size;
        std::array<float, size * size * size> values{};
        for (uint32_t z = 0; z < size; ++z)
        {
            for (uint32_t y = 0; y < size; ++y)
            {
                for (uint32_t x = 0; x < size; ++x)
                {
                    const float bands = std::sin(frequency * x + 1.3f * std::sin(frequency * z)) * std::cos(frequency * y);
                    const float detail = std::cos(frequency * (2.0f * x + y - z));
                    values[(z * size + y) * size + x] = std::clamp(0.52f + 0.33f * bands + 0.15f * detail, 0.0f, 1.0f);
                }
            }
        }
        auto texture = Create3DTexture(size, size, size, ERHIFormat::R32_Float,
            ERHITextureCreateFlag::ShaderResource | ERHITextureCreateFlag::TransferDest, "VolumePreviewDensity");
        texture->UploadData(values.data(), 0, 0);
        return texture;
    }
}
