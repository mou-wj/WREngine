#include "Reflections.h"
#include "GIRayTracing.h"

#include "GlobalShader.h"
#include "RHIPipelineStateCache.h"
#include <stdexcept>

namespace Renderer
{
    using namespace RenderCore;
    using namespace RHI;

    BEGIN_SHADER_PARAMETER_STRUCT(ReflectionViewParameters)
        SHADER_PARAMETER(Core::Float4x4, ViewProjection)
        SHADER_PARAMETER(Core::Float4x4, InverseViewProjection)
        SHADER_PARAMETER(Core::Float3, CameraPosition)
        SHADER_PARAMETER(Core::UInt2, ReflectionSize)
        SHADER_PARAMETER_STRUCT_REFERENCE(GBufferInputParameters, GBuffer)
    END_SHADER_PARAMETER_STRUCT(ReflectionViewParameters)

    BEGIN_SHADER_PARAMETER_STRUCT(ReflectionTraceParameters)
        SHADER_PARAMETER_STRUCT_REFERENCE(ReflectionViewParameters, View)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, ScreenHit)
        SHADER_PARAMETER_RDG_RWSTRUCTURED_BUFFER(SurfaceCacheQuery, TraceQueries)
    END_SHADER_PARAMETER_STRUCT(ReflectionTraceParameters)

    BEGIN_SHADER_PARAMETER_STRUCT(ReflectionResolveParameters)
        SHADER_PARAMETER_STRUCT_REFERENCE(ReflectionViewParameters, View)
        SHADER_PARAMETER_STRUCT_REFERENCE(SceneEvnIBLLightParameters, Environment)
        SHADER_PARAMETER_STRUCT_REFERENCE(WorldRadianceCacheParameters, WorldCache)
        SHADER_PARAMETER_STRUCT_REFERENCE(SurfaceCachePunctualLightParameters, Punctual)
        SHADER_PARAMETER(uint32_t, ReflectionView)
        SHADER_PARAMETER(uint32_t, bHasEnvironment)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DeferredColor)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ScreenHit)
        SHADER_PARAMETER_RDG_STRUCTURED_BUFFER(SurfaceCacheQuery, TraceQueries)
        SHADER_PARAMETER_RDG_STRUCTURED_BUFFER(SurfaceCacheQueryResult, GeometryHits)
        SHADER_PARAMETER_RDG_STRUCTURED_BUFFER(SurfaceCacheLightingQueryResult, HitLighting)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, ReflectionOutput)
    END_SHADER_PARAMETER_STRUCT(ReflectionResolveParameters)

    class ReflectionTraceCS : public GlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER_TYPE(ReflectionTraceCS)
        static bool ShouldCompilePermutation(const ShaderPermutationParameters&) { return true; }
        static void ModifyShaderCompilerEnvironment(const ShaderPermutationParameters&, ShaderCompilerEnvironment&) {}
        static const ShaderParametersMetadata* GetShaderParameterMetadata() { return ReflectionTraceParameters::GetMetaData(); }
    };

    class ReflectionResolveCS : public GlobalShader
    {
    public:
        static constexpr char USE_WORLD_CACHE[] = "REFLECTION_WORLD_CACHE";
        static constexpr char USE_HARDWARE_LIGHTS[] = "REFLECTION_HARDWARE_LIGHTS";
        using WorldCacheDimension = FPermutationDimensionBool<USE_WORLD_CACHE>;
        using HardwareLightsDimension = FPermutationDimensionBool<USE_HARDWARE_LIGHTS>;
        using PermutationDomain = ShaderPermutationDomain<WorldCacheDimension, HardwareLightsDimension>;
        DECLARE_GLOBAL_SHADER_TYPE(ReflectionResolveCS)
        static bool ShouldCompilePermutation(const ShaderPermutationParameters& parameters)
        {
            PermutationDomain domain;
            domain.SetFromId(parameters.PermutationId);
            return !domain.GetFieldValue<HardwareLightsDimension>() || GIRayTracingScene::IsSupported();
        }
        static void ModifyShaderCompilerEnvironment(const ShaderPermutationParameters& parameters, ShaderCompilerEnvironment& environment)
        {
            PermutationDomain domain;
            domain.SetFromId(parameters.PermutationId);
            domain.ModifyCompilationEnvironment(environment);
        }
        static const ShaderParametersMetadata* GetShaderParameterMetadata() { return ReflectionResolveParameters::GetMetaData(); }
    };

    IMPLEMENT_GLOBAL_SHADER_TYPE(ReflectionTraceCS, "ReflectionTraceCS", "/GI/ReflectionTrace.sf", "MainCS", ERHIShaderFrequency::Compute);
    IMPLEMENT_GLOBAL_SHADER_TYPE(ReflectionResolveCS, "ReflectionResolveCS", "/GI/ReflectionResolve.sf", "MainCS", ERHIShaderFrequency::Compute);

    namespace
    {
        RenderGraphTextureUAVRef CreateOutputUAV(RenderGraphBuilder& builder, RenderGraphTextureRef texture)
        {
            RenderGraphTextureUAVDesc desc;
            desc.Texture = texture;
            desc.Format = texture->GetDesc().Format;
            return builder.CreateTextureUAV(texture->GetName() + ".ReflectionUAV", desc);
        }

        RenderGraphBufferSRVRef ReadBuffer(RenderGraphBuilder& builder, RenderGraphBufferRef buffer, uint32_t count)
        {
            RenderGraphBufferSRVDesc desc;
            desc.Buffer = buffer;
            desc.Stride = buffer->GetDesc().Stride;
            desc.NumElements = count;
            return builder.CreateBufferSRV(buffer->GetName() + ".ReflectionSRV", desc);
        }

        template<class ShaderType, class Parameters>
        void AddCompute(RenderGraphBuilder& builder, const char* name, Parameters* params, Core::UInt2 size, uint32_t permutation = 0)
        {
            const auto shader = GShaderMap.GetShader<ShaderType>(permutation);
            if (!shader)
            {
                throw std::runtime_error(std::string("Reflections: missing shader for ") + name);
            }
            RHIComputePipelineStateDesc desc;
            desc.ComputeShader = static_cast<RHIComputeShader*>(shader->GetRHIShader());
            const auto pipeline = RHIPipelineStateCache::GetOrCreateComputePipelineState(desc);
            builder.AddPass(name, Parameters::GetMetaData(), params, EPassFlag::Compute, [=](RHICommandListBase& list)
            {
                auto& cmd = static_cast<RHIComputeCommandList&>(list);
                cmd.SetComputePipelineState(pipeline);
                SetShaderParameters(cmd, shader, params);
                cmd.Dispatch((size.X + 7) / 8, (size.Y + 7) / 8, 1);
            });
        }
    }

    void AddReflectionsPasses(RenderGraphBuilder& builder, const ReflectionsInput& input,
        const SurfaceCacheMaterialParameters& material, const SurfaceCacheLightingParameters& lighting)
    {
        if (!input.Output || input.Output == input.DeferredColor || input.Output->GetDesc().Format != ERHIFormat::R32G32B32A32_Float)
        {
            throw std::invalid_argument("Reflections: a separate RGBA32F output is required");
        }
        const auto& output = input.Output->GetDesc();
        const Core::UInt2 size{output.Width, output.Height};
        const uint64_t pixelCount = uint64_t(size.X) * size.Y;
        if (pixelCount == 0 || pixelCount > 65535ull * 64ull)
        {
            throw std::invalid_argument("Reflections: viewport exceeds the Surface Cache query capacity");
        }
        for (const auto* texture : {input.DeferredColor, input.GBuffer.GBufferA, input.GBuffer.GBufferB,
            input.GBuffer.GBufferC, input.GBuffer.Depth})
        {
            if (!texture || texture->GetDesc().Width != size.X || texture->GetDesc().Height != size.Y)
            {
                throw std::invalid_argument("Reflections: GBuffer, depth, lighting and output dimensions must match");
            }
        }
        const uint32_t count = static_cast<uint32_t>(pixelCount);
        RenderGraphTextureDesc hitDesc;
        hitDesc.Width = size.X;
        hitDesc.Height = size.Y;
        hitDesc.Format = ERHIFormat::R32G32B32A32_Float;
        hitDesc.Usage = ERHITextureCreateFlag::ShaderResource | ERHITextureCreateFlag::UAV;
        auto screenHit = builder.CreateTexture("Reflections.ScreenHit", hitDesc);

        RenderGraphBufferDesc queryDesc;
        queryDesc.Stride = sizeof(SurfaceCacheQuery);
        queryDesc.Size = pixelCount * queryDesc.Stride;
        queryDesc.Usage = ERHIBufferUsageFlag::Structured | ERHIBufferUsageFlag::ShaderResource | ERHIBufferUsageFlag::UnorderedAccess;
        auto queries = builder.CreateBuffer("Reflections.Queries", queryDesc);
        RenderGraphBufferUAVDesc queryUAV;
        queryUAV.Buffer = queries;
        queryUAV.Stride = queryDesc.Stride;
        queryUAV.NumElements = count;

        auto* trace = builder.AllocateParameter<ReflectionTraceParameters>();
        trace->View = {input.ViewProjection, input.InverseViewProjection, input.CameraPosition, size, input.GBuffer};
        trace->ScreenHit = CreateOutputUAV(builder, screenHit);
        trace->TraceQueries = builder.CreateBufferUAV("Reflections.QueriesUAV", queryUAV);
        AddCompute<ReflectionTraceCS>(builder, "ReflectionScreenTrace", trace, size);

        const auto hits = AddGITracePasses(builder, material, queries, count, input.HardwareScene);
        const bool bUseHardwareLighting = input.HardwareScene && input.HardwareLights;
        const auto hitLighting = AddSurfaceCacheLightingLookupPass(builder, material, lighting, hits, count, bUseHardwareLighting);

        auto* resolve = builder.AllocateParameter<ReflectionResolveParameters>();
        resolve->View = trace->View;
        resolve->Environment = input.Environment;
        if (input.WorldCache)
        {
            resolve->WorldCache = *input.WorldCache;
        }
        if (bUseHardwareLighting)
        {
            resolve->Punctual = *input.HardwareLights;
        }
        resolve->ReflectionView = static_cast<uint32_t>(input.View);
        resolve->bHasEnvironment = input.bHasEnvironment ? 1u : 0u;
        resolve->DeferredColor = input.DeferredColor;
        resolve->ScreenHit = screenHit;
        resolve->TraceQueries = ReadBuffer(builder, queries, count);
        resolve->GeometryHits = ReadBuffer(builder, hits, count);
        resolve->HitLighting = ReadBuffer(builder, hitLighting, count);
        resolve->ReflectionOutput = CreateOutputUAV(builder, input.Output);
        ReflectionResolveCS::PermutationDomain domain;
        domain.SetFieldValue<ReflectionResolveCS::WorldCacheDimension>(input.WorldCache != nullptr);
        domain.SetFieldValue<ReflectionResolveCS::HardwareLightsDimension>(bUseHardwareLighting);
        AddCompute<ReflectionResolveCS>(builder, "ReflectionResolve", resolve, size, domain.GetPermutationId());
    }
}
