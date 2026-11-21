#pragma once

#include "../SceneShaderParameters.h"
#include "SurfaceCacheDirectLighting.h"
#include <array>

namespace Renderer
{
    struct VoxelLightingParameters;
    BEGIN_SHADER_PARAMETER_STRUCT(WorldRadianceProbeRecord)
        SHADER_PARAMETER(Core::Float4, PositionSpacing)
        SHADER_PARAMETER(Core::UInt4, Status)
    END_SHADER_PARAMETER_STRUCT(WorldRadianceProbeRecord)

    BEGIN_SHADER_PARAMETER_STRUCT(WorldRadianceCacheParameters)
        SHADER_PARAMETER(Core::Int3, LevelOrigin0)
        SHADER_PARAMETER(Core::Int3, LevelOrigin1)
        SHADER_PARAMETER(Core::Float2, CellSize)
        SHADER_PARAMETER(uint32_t, GridSize)
        SHADER_PARAMETER(uint32_t, DirectionSize)
        SHADER_PARAMETER(float, TraceDistance)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, WorldRadianceAtlas)
        SHADER_PARAMETER_RDG_STRUCTURED_BUFFER(WorldRadianceProbeRecord, WorldProbes)
    END_SHADER_PARAMETER_STRUCT(WorldRadianceCacheParameters)

    BEGIN_SHADER_PARAMETER_STRUCT(WorldRadianceCandidate)
        SHADER_PARAMETER(Core::Float4, RadianceLevel)
    END_SHADER_PARAMETER_STRUCT(WorldRadianceCandidate)

    struct WorldRadianceCacheRayOutput
    {
        RenderCore::RenderGraphBufferRef Queries = nullptr;
        RenderCore::RenderGraphBufferRef Candidates = nullptr;
    };

    class RENDERER_API WorldRadianceCache
    {
    public:
        WorldRadianceCache() = default;
        WorldRadianceCache(const WorldRadianceCache&) = delete;
        WorldRadianceCache& operator=(const WorldRadianceCache&) = delete;

        void Invalidate();
        uint32_t GetUpdatedProbeCount() const { return m_UpdatedProbeCount; }
        WorldRadianceCacheParameters Update(RenderCore::RenderGraphBuilder& builder, const Core::Float3& camera,
            uint32_t budget, const SurfaceCacheMaterialParameters& material, const SurfaceCacheLightingParameters& lighting,
            const SceneEvnIBLLightParameters& environment, bool bHasEnvironment);
        static void AddDebugPass(RenderCore::RenderGraphBuilder& builder, const WorldRadianceCacheParameters& cache,
            RenderCore::RenderGraphTextureRef output);
        static WorldRadianceCacheRayOutput AddRayLookupPass(RenderCore::RenderGraphBuilder& builder,
            const WorldRadianceCacheParameters& cache, RenderCore::RenderGraphBufferRef queries, uint32_t queryCount,
            const VoxelLightingParameters* voxels = nullptr);

    private:
        static constexpr uint32_t GridSize = 6;
        static constexpr uint32_t LevelCount = 2;
        static constexpr uint32_t SlotsPerLevel = GridSize * GridSize * GridSize;
        static constexpr uint32_t SlotCount = LevelCount * SlotsPerLevel;

        struct Slot
        {
            Core::Int3 Cell{};
            uint64_t UpdatedFrame = 0;
        };

        RenderCore::RenderTextureSP m_Atlas;
        std::array<Slot, SlotCount> m_Slots{};
        uint64_t m_Frame = 0;
        uint32_t m_UpdatedProbeCount = 0;
    };
}
