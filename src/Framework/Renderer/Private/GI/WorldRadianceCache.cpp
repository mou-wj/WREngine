#include "WorldRadianceCache.h"

#include "GlobalShader.h"
#include "RHIPipelineStateCache.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Renderer
{
    using namespace RenderCore;
    using namespace RHI;

    BEGIN_SHADER_PARAMETER_STRUCT(WorldRadianceUpdateRecord)
        SHADER_PARAMETER(Core::Float4, Position)
        SHADER_PARAMETER(Core::UInt4, Target)
    END_SHADER_PARAMETER_STRUCT(WorldRadianceUpdateRecord)

    BEGIN_SHADER_PARAMETER_STRUCT(WorldRadianceUploadParameters)
        RDG_BUFFER_ACCESS(Buffer, ERHIResourceAccess::TransferDest)
    END_SHADER_PARAMETER_STRUCT(WorldRadianceUploadParameters)

    BEGIN_SHADER_PARAMETER_STRUCT(WorldRadianceRaysParameters)
        SHADER_PARAMETER(uint32_t, UpdateCount)
        SHADER_PARAMETER(uint32_t, DirectionSize)
        SHADER_PARAMETER(float, TraceDistance)
        SHADER_PARAMETER_RDG_STRUCTURED_BUFFER(WorldRadianceUpdateRecord, Updates)
        SHADER_PARAMETER_RDG_RWSTRUCTURED_BUFFER(SurfaceCacheQuery, WorldQueries)
    END_SHADER_PARAMETER_STRUCT(WorldRadianceRaysParameters)

    BEGIN_SHADER_PARAMETER_STRUCT(WorldRadianceStoreParameters)
        SHADER_PARAMETER_STRUCT_REFERENCE(SceneEvnIBLLightParameters, Environment)
        SHADER_PARAMETER(uint32_t, UpdateCount)
        SHADER_PARAMETER(uint32_t, DirectionSize)
        SHADER_PARAMETER(float, TraceDistance)
        SHADER_PARAMETER(uint32_t, bHasEnvironment)
        SHADER_PARAMETER_RDG_STRUCTURED_BUFFER(WorldRadianceUpdateRecord, Updates)
        SHADER_PARAMETER_RDG_STRUCTURED_BUFFER(SurfaceCacheQuery, WorldQueries)
        SHADER_PARAMETER_RDG_STRUCTURED_BUFFER(SurfaceCacheQueryResult, WorldHits)
        SHADER_PARAMETER_RDG_STRUCTURED_BUFFER(SurfaceCacheLightingQueryResult, WorldLighting)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, WorldOutput)
    END_SHADER_PARAMETER_STRUCT(WorldRadianceStoreParameters)

    BEGIN_SHADER_PARAMETER_STRUCT(WorldRadianceRayLookupParameters)
        SHADER_PARAMETER_STRUCT_REFERENCE(WorldRadianceCacheParameters, Cache)
        SHADER_PARAMETER(uint32_t, QueryCount)
        SHADER_PARAMETER_RDG_STRUCTURED_BUFFER(SurfaceCacheQuery, ScreenQueries)
        SHADER_PARAMETER_RDG_RWSTRUCTURED_BUFFER(SurfaceCacheQuery, TraceQueries)
        SHADER_PARAMETER_RDG_RWSTRUCTURED_BUFFER(WorldRadianceCandidate, CacheCandidates)
    END_SHADER_PARAMETER_STRUCT(WorldRadianceRayLookupParameters)

    BEGIN_SHADER_PARAMETER_STRUCT(WorldRadianceDebugParameters)
        SHADER_PARAMETER_STRUCT_REFERENCE(WorldRadianceCacheParameters, Cache)
        SHADER_PARAMETER(Core::UInt2, OutputSize)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, DebugOutput)
    END_SHADER_PARAMETER_STRUCT(WorldRadianceDebugParameters)

    class WorldRadianceRaysCS : public GlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER_TYPE(WorldRadianceRaysCS)
        static bool ShouldCompilePermutation(const ShaderPermutationParameters&) { return true; }
        static void ModifyShaderCompilerEnvironment(const ShaderPermutationParameters&, ShaderCompilerEnvironment&) {}
        static const ShaderParametersMetadata* GetShaderParameterMetadata() { return WorldRadianceRaysParameters::GetMetaData(); }
    };

    class WorldRadianceStoreCS : public GlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER_TYPE(WorldRadianceStoreCS)
        static bool ShouldCompilePermutation(const ShaderPermutationParameters&) { return true; }
        static void ModifyShaderCompilerEnvironment(const ShaderPermutationParameters&, ShaderCompilerEnvironment&) {}
        static const ShaderParametersMetadata* GetShaderParameterMetadata() { return WorldRadianceStoreParameters::GetMetaData(); }
    };

    class WorldRadianceRayLookupCS : public GlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER_TYPE(WorldRadianceRayLookupCS)
        static bool ShouldCompilePermutation(const ShaderPermutationParameters&) { return true; }
        static void ModifyShaderCompilerEnvironment(const ShaderPermutationParameters&, ShaderCompilerEnvironment&) {}
        static const ShaderParametersMetadata* GetShaderParameterMetadata() { return WorldRadianceRayLookupParameters::GetMetaData(); }
    };

    class WorldRadianceDebugCS : public GlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER_TYPE(WorldRadianceDebugCS)
        static bool ShouldCompilePermutation(const ShaderPermutationParameters&) { return true; }
        static void ModifyShaderCompilerEnvironment(const ShaderPermutationParameters&, ShaderCompilerEnvironment&) {}
        static const ShaderParametersMetadata* GetShaderParameterMetadata() { return WorldRadianceDebugParameters::GetMetaData(); }
    };

    IMPLEMENT_GLOBAL_SHADER_TYPE(WorldRadianceRaysCS, "WorldRadianceRaysCS", "/GI/WorldRadianceRays.sf", "MainCS", ERHIShaderFrequency::Compute);
    IMPLEMENT_GLOBAL_SHADER_TYPE(WorldRadianceStoreCS, "WorldRadianceStoreCS", "/GI/WorldRadianceStore.sf", "MainCS", ERHIShaderFrequency::Compute);
    IMPLEMENT_GLOBAL_SHADER_TYPE(WorldRadianceRayLookupCS, "WorldRadianceRayLookupCS", "/GI/WorldRadianceRayLookup.sf", "MainCS", ERHIShaderFrequency::Compute);
    IMPLEMENT_GLOBAL_SHADER_TYPE(WorldRadianceDebugCS, "WorldRadianceDebugCS", "/GI/WorldRadianceDebug.sf", "MainCS", ERHIShaderFrequency::Compute);

    namespace
    {
        constexpr uint32_t DirectionSize = 6;
        constexpr float TraceDistance = 100.0f;
        constexpr float CellSizes[] = {2.0f, 5.0f};
        constexpr uint64_t RefreshPeriod = 32;

        template<class Shader, class Parameters>
        void AddCompute(RenderGraphBuilder& builder, const char* name, Parameters* params, uint32_t x, uint32_t y = 1)
        {
            auto shader = GShaderMap.GetShader<Shader>(0);
            if (!shader)
            {
                throw std::runtime_error(std::string("World Radiance Cache: missing shader for ") + name);
            }
            RHIComputePipelineStateDesc desc;
            desc.ComputeShader = static_cast<RHIComputeShader*>(shader->GetRHIShader());
            auto* pipeline = RHIPipelineStateCache::GetOrCreateComputePipelineState(desc);
            builder.AddPass(name, Parameters::GetMetaData(), params, EPassFlag::Compute, [=](RHICommandListBase& list)
            {
                auto& cmd = static_cast<RHIComputeCommandList&>(list);
                cmd.SetComputePipelineState(pipeline);
                SetShaderParameters(cmd, shader, params);
                cmd.Dispatch(x, y, 1);
            });
        }

        RenderGraphBufferRef CreateBuffer(RenderGraphBuilder& builder, const char* name,
            uint32_t stride, uint32_t count, ERHIBufferUsageFlags usage)
        {
            RenderGraphBufferDesc desc;
            desc.Stride = stride;
            desc.Size = uint64_t(stride) * count;
            desc.Usage = ERHIBufferUsageFlag::Structured | ERHIBufferUsageFlag::ShaderResource | usage;
            return builder.CreateBuffer(name, desc);
        }

        RenderGraphBufferSRVRef ReadBuffer(RenderGraphBuilder& builder, RenderGraphBufferRef buffer)
        {
            RenderGraphBufferSRVDesc desc;
            desc.Buffer = buffer;
            desc.Stride = buffer->GetDesc().Stride;
            desc.NumElements = buffer->GetDesc().Size / desc.Stride;
            return builder.CreateBufferSRV(buffer->GetName() + ".WorldCacheSRV", desc);
        }

        RenderGraphBufferUAVRef WriteBuffer(RenderGraphBuilder& builder, RenderGraphBufferRef buffer)
        {
            RenderGraphBufferUAVDesc desc;
            desc.Buffer = buffer;
            desc.Stride = buffer->GetDesc().Stride;
            desc.NumElements = buffer->GetDesc().Size / desc.Stride;
            return builder.CreateBufferUAV(buffer->GetName() + ".WorldCacheUAV", desc);
        }

        RenderGraphTextureUAVRef WriteTexture(RenderGraphBuilder& builder, RenderGraphTextureRef texture)
        {
            RenderGraphTextureUAVDesc desc;
            desc.Texture = texture;
            desc.Format = texture->GetDesc().Format;
            return builder.CreateTextureUAV(texture->GetName() + ".WorldCacheUAV", desc);
        }

        template<class T>
        RenderGraphBufferSRVRef Upload(RenderGraphBuilder& builder, const std::vector<T>& data, const char* name)
        {
            auto* snapshot = builder.AllocateParameter<std::vector<T>>();
            *snapshot = data;
            auto buffer = CreateBuffer(builder, name, sizeof(T), uint32_t(data.size()), ERHIBufferUsageFlag::TransferDst);
            auto* params = builder.AllocateParameter<WorldRadianceUploadParameters>();
            params->Buffer = buffer;
            builder.AddPass(name, WorldRadianceUploadParameters::GetMetaData(), params, EPassFlag::Graphic,
                [=](RHICommandListBase& cmd)
            {
                RHIUpdateBufferRegion region;
                region.Size = buffer->GetDesc().Size;
                GRHIApi->UpdateBuffer(cmd, buffer->GetRHIBuffer(), snapshot->data(), region);
            });
            return ReadBuffer(builder, buffer);
        }
    }

    void WorldRadianceCache::Invalidate()
    {
        m_Slots.fill({});
        m_UpdatedProbeCount = 0;
    }

    WorldRadianceCacheParameters WorldRadianceCache::Update(RenderGraphBuilder& builder, const Core::Float3& camera,
        uint32_t budget, const SurfaceCacheMaterialParameters& material, const SurfaceCacheLightingParameters& lighting,
        const SceneEvnIBLLightParameters& environment, bool bHasEnvironment)
    {
        if (!std::isfinite(camera.X) || !std::isfinite(camera.Y) || !std::isfinite(camera.Z) ||
            std::max({std::abs(camera.X), std::abs(camera.Y), std::abs(camera.Z)}) > 1e8f)
        {
            throw std::invalid_argument("World Radiance Cache: camera outside supported grid coordinates");
        }
        if (!m_Atlas)
        {
            RHITextureDesc desc;
            desc.Width = DirectionSize * DirectionSize;
            desc.Height = SlotCount;
            desc.Format = ERHIFormat::R32G32B32A32_Float;
            desc.Usage = ERHITextureCreateFlag::UAV | ERHITextureCreateFlag::ShaderResource | ERHITextureCreateFlag::TransferSrc;
            m_Atlas = std::make_shared<RenderTexture>(desc);
            m_Atlas->InitRHIResource();
        }

        ++m_Frame;
        WorldRadianceCacheParameters cache{};
        cache.GridSize = GridSize;
        cache.DirectionSize = DirectionSize;
        cache.CellSize = {CellSizes[0], CellSizes[1]};
        cache.TraceDistance = TraceDistance;
        cache.WorldRadianceAtlas = builder.RegisterExternalTexture("WorldRadianceCache.Atlas", m_Atlas.get());
        const auto wrap = [](int32_t value)
        {
            constexpr auto size = int32_t(GridSize);
            return uint32_t((value % size + size) % size);
        };

        struct PendingProbe
        {
            uint32_t Index;
            uint64_t LastUpdate;
            float DistanceSquared;
        };
        std::vector<PendingProbe> pending;
        std::vector<WorldRadianceProbeRecord> records(SlotCount);
        for (uint32_t level = 0; level < LevelCount; ++level)
        {
            const float spacing = CellSizes[level];
            const int32_t half = GridSize / 2;
            const Core::Int3 origin{int32_t(std::floor(camera.X / spacing)) - half,
                int32_t(std::floor(camera.Y / spacing)) - half, int32_t(std::floor(camera.Z / spacing)) - half};
            auto& levelOrigin = level == 0 ? cache.LevelOrigin0 : cache.LevelOrigin1;
            levelOrigin = origin;

            for (int32_t z = 0; z < int32_t(GridSize); ++z)
            {
                for (int32_t y = 0; y < int32_t(GridSize); ++y)
                {
                    for (int32_t x = 0; x < int32_t(GridSize); ++x)
                    {
                        const Core::Int3 cell{origin.X + x, origin.Y + y, origin.Z + z};
                        const auto index = level * SlotsPerLevel + (wrap(cell.Z) * GridSize + wrap(cell.Y)) * GridSize + wrap(cell.X);
                        auto& slot = m_Slots[index];
                        if (!(cell == slot.Cell))
                        {
                            slot = {cell, 0};
                        }
                        const Core::Float3 position{cell.X * spacing, cell.Y * spacing, cell.Z * spacing};
                        auto& record = records[index];
                        record.PositionSpacing = {position.X, position.Y, position.Z, spacing};
                        const auto age = m_Frame - slot.UpdatedFrame;
                        record.Status = {slot.UpdatedFrame != 0 ? 1u : 0u, 0,
                            uint32_t(std::min<uint64_t>(age, UINT32_MAX)), level};
                        if (slot.UpdatedFrame == 0 || age >= RefreshPeriod)
                        {
                            pending.push_back({index, slot.UpdatedFrame, (position - camera).LengthSquared()});
                        }
                    }
                }
            }
        }

        const auto updateCount = std::min<uint32_t>(budget, uint32_t(pending.size()));
        m_UpdatedProbeCount = updateCount;
        if (updateCount > 0)
        {
            std::partial_sort(pending.begin(), pending.begin() + updateCount, pending.end(),
                [](const PendingProbe& lhs, const PendingProbe& rhs)
            {
                if (lhs.LastUpdate != rhs.LastUpdate)
                {
                    return lhs.LastUpdate < rhs.LastUpdate;
                }
                if (lhs.DistanceSquared != rhs.DistanceSquared)
                {
                    return lhs.DistanceSquared < rhs.DistanceSquared;
                }
                return lhs.Index < rhs.Index;
            });
            std::vector<WorldRadianceUpdateRecord> updates(updateCount);
            for (uint32_t i = 0; i < updateCount; ++i)
            {
                const auto index = pending[i].Index;
                auto& record = records[index];
                updates[i].Position = record.PositionSpacing;
                updates[i].Target = {index, 0, 0, 0};
                record.Status.X = record.Status.Y = 1;
                record.Status.Z = 0;
            }

            const auto queryCount = updateCount * DirectionSize * DirectionSize;
            auto queries = CreateBuffer(builder, "WorldRadianceCache.Queries", sizeof(SurfaceCacheQuery), queryCount,
                ERHIBufferUsageFlag::UnorderedAccess);
            auto* rays = builder.AllocateParameter<WorldRadianceRaysParameters>();
            rays->UpdateCount = updateCount;
            rays->DirectionSize = DirectionSize;
            rays->TraceDistance = TraceDistance;
            rays->Updates = Upload(builder, updates, "WorldRadianceCache.Updates");
            rays->WorldQueries = WriteBuffer(builder, queries);
            AddCompute<WorldRadianceRaysCS>(builder, "WorldRadianceCacheRays", rays, updateCount);

            const auto hits = AddSurfaceCacheQueryPass(builder, material, queries, queryCount);
            const auto outgoing = AddSurfaceCacheLightingLookupPass(builder, material, lighting, hits, queryCount);
            auto* store = builder.AllocateParameter<WorldRadianceStoreParameters>();
            store->Environment = environment;
            store->UpdateCount = updateCount;
            store->DirectionSize = DirectionSize;
            store->TraceDistance = TraceDistance;
            store->bHasEnvironment = bHasEnvironment ? 1u : 0u;
            store->Updates = rays->Updates;
            store->WorldQueries = ReadBuffer(builder, queries);
            store->WorldHits = ReadBuffer(builder, hits);
            store->WorldLighting = ReadBuffer(builder, outgoing);
            store->WorldOutput = WriteTexture(builder, cache.WorldRadianceAtlas);
            AddCompute<WorldRadianceStoreCS>(builder, "WorldRadianceCacheStore", store, updateCount);
            for (uint32_t i = 0; i < updateCount; ++i)
            {
                m_Slots[pending[i].Index].UpdatedFrame = m_Frame;
            }
        }
        cache.WorldProbes = Upload(builder, records, "WorldRadianceCache.Probes");
        return cache;
    }

    void WorldRadianceCache::AddDebugPass(RenderGraphBuilder& builder, const WorldRadianceCacheParameters& cache,
        RenderGraphTextureRef output)
    {
        auto* params = builder.AllocateParameter<WorldRadianceDebugParameters>();
        params->Cache = cache;
        params->OutputSize = {output->GetDesc().Width, output->GetDesc().Height};
        params->DebugOutput = WriteTexture(builder, output);
        AddCompute<WorldRadianceDebugCS>(builder, "WorldRadianceCacheDebug", params,
            (params->OutputSize.X + 7) / 8, (params->OutputSize.Y + 7) / 8);
    }

    WorldRadianceCacheRayOutput WorldRadianceCache::AddRayLookupPass(RenderGraphBuilder& builder,
        const WorldRadianceCacheParameters& cache, RenderGraphBufferRef queries, uint32_t queryCount)
    {
        WorldRadianceCacheRayOutput output;
        output.Queries = CreateBuffer(builder, "ScreenProbe.TraceQueries", sizeof(SurfaceCacheQuery), queryCount,
            ERHIBufferUsageFlag::UnorderedAccess);
        output.Candidates = CreateBuffer(builder, "ScreenProbe.WorldRadianceCandidates",
            sizeof(WorldRadianceCandidate), queryCount,
            ERHIBufferUsageFlag::UnorderedAccess | ERHIBufferUsageFlag::TransferSrc);

        auto* params = builder.AllocateParameter<WorldRadianceRayLookupParameters>();
        params->Cache = cache;
        params->QueryCount = queryCount;
        params->ScreenQueries = ReadBuffer(builder, queries);
        params->TraceQueries = WriteBuffer(builder, output.Queries);
        params->CacheCandidates = WriteBuffer(builder, output.Candidates);
        AddCompute<WorldRadianceRayLookupCS>(builder, "WorldRadianceCacheRayLookup", params, (queryCount + 63) / 64);
        return output;
    }
}
