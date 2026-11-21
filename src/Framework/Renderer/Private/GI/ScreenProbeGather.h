#pragma once

#include "SurfaceCacheMaterial.h"
#include "SurfaceCacheDirectLighting.h"
#include "../GBufferInfo.h"
#include "WorldRadianceCache.h"

namespace Renderer
{
    BEGIN_SHADER_PARAMETER_STRUCT(ScreenProbeRecord)
        SHADER_PARAMETER(Core::Float4, WorldPositionDepth)
        SHADER_PARAMETER(Core::Float4, WorldNormal)
        SHADER_PARAMETER(Core::Float4, ScreenPosition)
        SHADER_PARAMETER(Core::UInt4, Status)
    END_SHADER_PARAMETER_STRUCT(ScreenProbeRecord)

    BEGIN_SHADER_PARAMETER_STRUCT(ScreenProbeRadianceRecord)
        SHADER_PARAMETER(Core::Float4, Radiance)
        SHADER_PARAMETER(Core::UInt4, Status)
    END_SHADER_PARAMETER_STRUCT(ScreenProbeRadianceRecord)

    BEGIN_SHADER_PARAMETER_STRUCT(ScreenProbePlacementParameters)
        SHADER_PARAMETER_STRUCT_REFERENCE(GBufferInputParameters, GBuffer)
        SHADER_PARAMETER(Core::Float4x4, InvViewProj)
        SHADER_PARAMETER(Core::Float3, CameraPosition)
        SHADER_PARAMETER(Core::UInt2, ScreenSize)
        SHADER_PARAMETER(Core::UInt2, ProbeGridSize)
        SHADER_PARAMETER(uint32_t, TileSize)
        SHADER_PARAMETER(uint32_t, RayCount)
        SHADER_PARAMETER(float, MaxTraceDistance)
        SHADER_PARAMETER_RDG_RWSTRUCTURED_BUFFER(ScreenProbeRecord, Probes)
        SHADER_PARAMETER_RDG_RWSTRUCTURED_BUFFER(SurfaceCacheQuery, Queries)
    END_SHADER_PARAMETER_STRUCT(ScreenProbePlacementParameters)

    BEGIN_SHADER_PARAMETER_STRUCT(ScreenProbeGatherParameters)
        SHADER_PARAMETER_STRUCT_REFERENCE(SceneEvnIBLLightParameters, Environment)
        SHADER_PARAMETER(uint32_t, QueryCount)
        SHADER_PARAMETER(uint32_t, bHasEnvironment)
        SHADER_PARAMETER(uint32_t, bUseWorldCache)
        SHADER_PARAMETER_RDG_STRUCTURED_BUFFER(SurfaceCacheQuery, Queries)
        SHADER_PARAMETER_RDG_STRUCTURED_BUFFER(SurfaceCacheQueryResult, MaterialResults)
        SHADER_PARAMETER_RDG_STRUCTURED_BUFFER(SurfaceCacheLightingQueryResult, LightingResults)
        SHADER_PARAMETER_RDG_STRUCTURED_BUFFER(WorldRadianceCandidate, WorldCandidates)
        SHADER_PARAMETER_RDG_RWSTRUCTURED_BUFFER(ScreenProbeRadianceRecord, Radiance)
    END_SHADER_PARAMETER_STRUCT(ScreenProbeGatherParameters)

    BEGIN_SHADER_PARAMETER_STRUCT(ScreenProbeIntegrationParameters)
        SHADER_PARAMETER_STRUCT_REFERENCE(GBufferInputParameters, GBuffer)
        SHADER_PARAMETER(Core::Float4x4, InvViewProj)
        SHADER_PARAMETER(Core::Float3, CameraPosition)
        SHADER_PARAMETER(Core::UInt2, ScreenSize)
        SHADER_PARAMETER(Core::UInt2, ProbeGridSize)
        SHADER_PARAMETER(uint32_t, TileSize)
        SHADER_PARAMETER(uint32_t, RayCount)
        SHADER_PARAMETER(uint32_t, DebugMode)
        SHADER_PARAMETER_RDG_STRUCTURED_BUFFER(ScreenProbeRecord, Probes)
        SHADER_PARAMETER_RDG_STRUCTURED_BUFFER(SurfaceCacheQuery, Queries)
        SHADER_PARAMETER_RDG_STRUCTURED_BUFFER(ScreenProbeRadianceRecord, Radiance)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputGI)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, DebugOutput)
    END_SHADER_PARAMETER_STRUCT(ScreenProbeIntegrationParameters)

    struct ScreenProbeGatherPassInput
    {
        RenderCore::RenderGraphTextureRef GBufferA = nullptr;
        RenderCore::RenderGraphTextureRef GBufferB = nullptr;
        RenderCore::RenderGraphTextureRef GBufferC = nullptr;
        RenderCore::RenderGraphTextureRef Depth = nullptr;
        Core::Float4x4 InvViewProj = Core::Float4x4::Identity();
        Core::Float3 CameraPosition = {0, 0, 0};
        SceneEvnIBLLightParameters Environment{};
        const WorldRadianceCacheParameters* WorldCache = nullptr;
        RenderCore::RenderGraphTextureRef IndirectDiffuseOutput = nullptr;
        RenderCore::RenderGraphTextureRef DebugOutput = nullptr;
        bool bHasEnvironment = false;
        uint32_t TileSize = 8;
        uint32_t RaysPerProbe = 8;
        float MaxTraceDistance = 100.0f;
        uint32_t DebugMode = 0;
    };

    struct ScreenProbeGatherOutput
    {
        RenderCore::RenderGraphTextureRef IndirectDiffuse = nullptr;
        RenderCore::RenderGraphTextureRef DebugVisualization = nullptr;
        RenderCore::RenderGraphBufferRef Probes = nullptr;
        RenderCore::RenderGraphBufferRef Queries = nullptr;
        RenderCore::RenderGraphBufferRef Radiance = nullptr;
        Core::UInt2 ProbeGridSize = {0, 0};
        uint32_t RayCount = 0;
    };

    RENDERER_API ScreenProbeGatherOutput AddScreenProbeGatherPasses(RenderCore::RenderGraphBuilder& builder,
        const SurfaceCacheMaterialParameters& material, const SurfaceCacheLightingParameters& lighting,
        const ScreenProbeGatherPassInput& input);
}
