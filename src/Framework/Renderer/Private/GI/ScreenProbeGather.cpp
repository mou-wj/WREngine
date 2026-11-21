#include "ScreenProbeGather.h"

#include "GlobalShader.h"
#include "RHIPipelineStateCache.h"
#include "RenderResource.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

using namespace RenderCore;
using namespace RHI;

namespace Renderer
{
    class ScreenProbePlacementCS : public GlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER_TYPE(ScreenProbePlacementCS)
        static bool ShouldCompilePermutation(const ShaderPermutationParameters&) { return true; }
        static void ModifyShaderCompilerEnvironment(const ShaderPermutationParameters&, ShaderCompilerEnvironment&) {}
        static const ShaderParametersMetadata* GetShaderParameterMetadata() { return ScreenProbePlacementParameters::GetMetaData(); }
    };

    class ScreenProbeGatherCS : public GlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER_TYPE(ScreenProbeGatherCS)
        static bool ShouldCompilePermutation(const ShaderPermutationParameters&) { return true; }
        static void ModifyShaderCompilerEnvironment(const ShaderPermutationParameters&, ShaderCompilerEnvironment&) {}
        static const ShaderParametersMetadata* GetShaderParameterMetadata() { return ScreenProbeGatherParameters::GetMetaData(); }
    };

    class ScreenProbeIntegrationCS : public GlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER_TYPE(ScreenProbeIntegrationCS)
        static bool ShouldCompilePermutation(const ShaderPermutationParameters&) { return true; }
        static void ModifyShaderCompilerEnvironment(const ShaderPermutationParameters&, ShaderCompilerEnvironment&) {}
        static const ShaderParametersMetadata* GetShaderParameterMetadata() { return ScreenProbeIntegrationParameters::GetMetaData(); }
    };

    IMPLEMENT_GLOBAL_SHADER_TYPE(ScreenProbePlacementCS, "ScreenProbePlacementCS", "/GI/ScreenProbePlacement.sf", "MainCS", ERHIShaderFrequency::Compute);
    IMPLEMENT_GLOBAL_SHADER_TYPE(ScreenProbeGatherCS, "ScreenProbeGatherCS", "/GI/ScreenProbeGather.sf", "MainCS", ERHIShaderFrequency::Compute);
    IMPLEMENT_GLOBAL_SHADER_TYPE(ScreenProbeIntegrationCS, "ScreenProbeIntegrationCS", "/GI/ScreenProbeIntegration.sf", "MainCS", ERHIShaderFrequency::Compute);

    namespace
    {
        void Require(bool value, const char* message)
        {
            if (!value)
            {
                throw std::invalid_argument(std::string("Screen Probe Gather: ") + message);
            }
        }

        RenderGraphBufferSRVRef CreateBufferSRV(RenderGraphBuilder& builder, RenderGraphBufferRef buffer)
        {
            RenderGraphBufferSRVDesc desc;
            desc.Buffer = buffer;
            desc.Stride = buffer->GetDesc().Stride;
            desc.NumElements = buffer->GetDesc().Size / desc.Stride;
            return builder.CreateBufferSRV(buffer->GetName() + ".SRV", desc);
        }

        RenderGraphTextureUAVRef CreateTextureUAV(RenderGraphBuilder& builder, RenderGraphTextureRef texture)
        {
            RenderGraphTextureUAVDesc desc;
            desc.Texture = texture;
            desc.Format = texture->GetDesc().Format;
            return builder.CreateTextureUAV(texture->GetName() + ".UAV", desc);
        }

        template<class T, class P>
        void AddComputePass(RenderGraphBuilder& builder, const char* name, P* params, uint32_t groupsX, uint32_t groupsY = 1)
        {
            auto shader = GShaderMap.GetShader<T>(0);
            Require(shader != nullptr, "required compute shader is unavailable");
            RHIComputePipelineStateDesc desc;
            desc.ComputeShader = static_cast<RHIComputeShader*>(shader->GetRHIShader());
            auto pipeline = RHIPipelineStateCache::GetOrCreateComputePipelineState(desc);
            builder.AddPass(name, P::GetMetaData(), params, EPassFlag::Compute, [=](RHICommandListBase& list)
            {
                auto& cmd = static_cast<RHIComputeCommandList&>(list);
                cmd.SetComputePipelineState(pipeline);
                SetShaderParameters(cmd, shader, params);
                cmd.Dispatch(groupsX, groupsY, 1);
            });
        }

        RenderGraphBufferRef CreateStructuredBuffer(RenderGraphBuilder& builder, const char* name,
            uint32_t stride, uint64_t count, bool bUAV)
        {
            RenderGraphBufferDesc desc;
            desc.Stride = stride;
            desc.Size = uint64_t(stride) * count;
            desc.Usage = ERHIBufferUsageFlag::Structured | ERHIBufferUsageFlag::ShaderResource |
                ERHIBufferUsageFlag::TransferSrc;
            if (bUAV)
            {
                desc.Usage |= ERHIBufferUsageFlag::UnorderedAccess;
            }
            return builder.CreateBuffer(name, desc);
        }

        RenderGraphBufferUAVRef CreateBufferUAV(RenderGraphBuilder& builder, RenderGraphBufferRef buffer, uint32_t count)
        {
            RenderGraphBufferUAVDesc desc;
            desc.Buffer = buffer;
            desc.Stride = buffer->GetDesc().Stride;
            desc.NumElements = count;
            return builder.CreateBufferUAV(buffer->GetName() + ".UAV", desc);
        }

        RenderGraphTextureRef CreateOutputTexture(RenderGraphBuilder& builder, const char* name, uint32_t width, uint32_t height)
        {
            RenderGraphTextureDesc desc;
            desc.Width = width;
            desc.Height = height;
            desc.Format = ERHIFormat::R32G32B32A32_Float;
            desc.Usage = ERHITextureCreateFlag::UAV | ERHITextureCreateFlag::ShaderResource |
                ERHITextureCreateFlag::TransferSrc;
            return builder.CreateTexture(name, desc);
        }
    }

    ScreenProbeGatherOutput AddScreenProbeGatherPasses(RenderGraphBuilder& builder,
        const SurfaceCacheMaterialParameters& material, const SurfaceCacheLightingParameters& lighting,
        const ScreenProbeGatherPassInput& input)
    {
        Require(input.GBufferA && input.GBufferB && input.GBufferC && input.Depth, "GBuffer and depth are required");
        Require(input.TileSize >= 4 && input.TileSize <= 64 && input.RaysPerProbe >= 1 && input.RaysPerProbe <= 64,
            "tile size or ray count is outside the supported range");
        Require(std::isfinite(input.MaxTraceDistance) && input.MaxTraceDistance > 0, "invalid trace distance");
        Require(input.DebugMode <= 5, "debug mode must be between 0 and 5");

        const auto& gbufferDesc = input.GBufferA->GetDesc();
        Require(gbufferDesc.Width > 0 && gbufferDesc.Height > 0 &&
            input.GBufferB->GetDesc().Width == gbufferDesc.Width && input.GBufferB->GetDesc().Height == gbufferDesc.Height &&
            input.GBufferC->GetDesc().Width == gbufferDesc.Width && input.GBufferC->GetDesc().Height == gbufferDesc.Height &&
            input.Depth->GetDesc().Width == gbufferDesc.Width && input.Depth->GetDesc().Height == gbufferDesc.Height,
            "GBuffer and depth dimensions must match");

        ScreenProbeGatherOutput output;
        output.ProbeGridSize = {
            (gbufferDesc.Width + input.TileSize - 1) / input.TileSize,
            (gbufferDesc.Height + input.TileSize - 1) / input.TileSize
        };
        output.RayCount = input.RaysPerProbe;
        const uint64_t probeCount = uint64_t(output.ProbeGridSize.X) * output.ProbeGridSize.Y;
        const uint64_t queryCount = probeCount * input.RaysPerProbe;
        Require(probeCount > 0 && queryCount <= 65535ull * 64ull, "probe ray count exceeds Surface Cache query capacity");

        output.Probes = CreateStructuredBuffer(builder, "ScreenProbe.Probes", sizeof(ScreenProbeRecord), probeCount, true);
        output.Queries = CreateStructuredBuffer(builder, "ScreenProbe.Queries", sizeof(SurfaceCacheQuery), queryCount, true);
        output.Radiance = CreateStructuredBuffer(builder, "ScreenProbe.Radiance", sizeof(ScreenProbeRadianceRecord), queryCount, true);

        auto* placement = builder.AllocateParameter<ScreenProbePlacementParameters>();
        placement->GBuffer.GBufferA = input.GBufferA;
        placement->GBuffer.GBufferB = input.GBufferB;
        placement->GBuffer.GBufferC = input.GBufferC;
        placement->GBuffer.Depth = input.Depth;
        placement->GBuffer.PointSampler = GlobalNearestSampler.get();
        placement->InvViewProj = input.InvViewProj;
        placement->CameraPosition = input.CameraPosition;
        placement->ScreenSize = {gbufferDesc.Width, gbufferDesc.Height};
        placement->ProbeGridSize = output.ProbeGridSize;
        placement->TileSize = input.TileSize;
        placement->RayCount = input.RaysPerProbe;
        placement->SamplePhase = input.SamplePhase;
        placement->MaxTraceDistance = input.MaxTraceDistance;
        placement->Probes = CreateBufferUAV(builder, output.Probes, uint32_t(probeCount));
        placement->Queries = CreateBufferUAV(builder, output.Queries, uint32_t(queryCount));
        AddComputePass<ScreenProbePlacementCS>(builder, "ScreenProbePlacementAndRayGeneration", placement,
            uint32_t((probeCount + 63) / 64));

        const auto world = input.WorldCache ? WorldRadianceCache::AddRayLookupPass(builder, *input.WorldCache,
            output.Queries, uint32_t(queryCount)) : WorldRadianceCacheRayOutput{};
        auto queryResults = AddSurfaceCacheQueryPass(builder, material, world.Queries ? world.Queries : output.Queries,
            uint32_t(queryCount));
        auto lightingResults = AddSurfaceCacheLightingLookupPass(builder, material, lighting, queryResults, uint32_t(queryCount));

        auto* gather = builder.AllocateParameter<ScreenProbeGatherParameters>();
        gather->Environment = input.Environment;
        gather->QueryCount = uint32_t(queryCount);
        gather->bHasEnvironment = input.bHasEnvironment ? 1u : 0u;
        gather->bUseWorldCache = world.Candidates ? 1u : 0u;
        gather->Queries = CreateBufferSRV(builder, output.Queries);
        gather->MaterialResults = CreateBufferSRV(builder, queryResults);
        gather->LightingResults = CreateBufferSRV(builder, lightingResults);
        // Keep a valid descriptor with world-cache lookup disabled. The shader does not read it.
        auto candidates = world.Candidates ? world.Candidates : CreateStructuredBuffer(builder, "ScreenProbe.EmptyWorldCandidates",
            sizeof(WorldRadianceCandidate), 1, false);
        gather->WorldCandidates = CreateBufferSRV(builder, candidates);
        gather->Radiance = CreateBufferUAV(builder, output.Radiance, uint32_t(queryCount));
        AddComputePass<ScreenProbeGatherCS>(builder, "ScreenProbeRadianceGather", gather,
            uint32_t((queryCount + 63) / 64));

        output.IndirectDiffuse = input.IndirectDiffuseOutput ? input.IndirectDiffuseOutput :
            CreateOutputTexture(builder, "ScreenProbe.IndirectDiffuse", gbufferDesc.Width, gbufferDesc.Height);
        const bool bWriteDebug = input.DebugOutput || input.SamplePhase == 0;
        output.DebugVisualization = input.DebugOutput ? input.DebugOutput :
            CreateOutputTexture(builder, "ScreenProbe.DebugVisualization",
                bWriteDebug ? gbufferDesc.Width : 1u, bWriteDebug ? gbufferDesc.Height : 1u);
        auto* integration = builder.AllocateParameter<ScreenProbeIntegrationParameters>();
        integration->GBuffer.GBufferA = input.GBufferA;
        integration->GBuffer.GBufferB = input.GBufferB;
        integration->GBuffer.GBufferC = input.GBufferC;
        integration->GBuffer.Depth = input.Depth;
        integration->GBuffer.PointSampler = GlobalNearestSampler.get();
        integration->InvViewProj = input.InvViewProj;
        integration->ScreenSize = {gbufferDesc.Width, gbufferDesc.Height};
        integration->ProbeGridSize = output.ProbeGridSize;
        integration->TileSize = input.TileSize;
        integration->RayCount = input.RaysPerProbe;
        integration->DebugMode = input.DebugMode;
        integration->bStoreConfidence = input.SamplePhase != 0 ? 1u : 0u;
        integration->bWriteDebug = bWriteDebug ? 1u : 0u;
        integration->Probes = CreateBufferSRV(builder, output.Probes);
        integration->Queries = CreateBufferSRV(builder, output.Queries);
        integration->Radiance = CreateBufferSRV(builder, output.Radiance);
        integration->OutputGI = CreateTextureUAV(builder, output.IndirectDiffuse);
        integration->DebugOutput = CreateTextureUAV(builder, output.DebugVisualization);
        AddComputePass<ScreenProbeIntegrationCS>(builder, "ScreenProbeIntegration", integration,
            (gbufferDesc.Width + 7) / 8, (gbufferDesc.Height + 7) / 8);
        return output;
    }
}
