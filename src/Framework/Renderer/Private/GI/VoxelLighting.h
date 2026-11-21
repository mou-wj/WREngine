#pragma once

#include "WorldRadianceCache.h"

namespace Renderer
{
    BEGIN_SHADER_PARAMETER_STRUCT(VoxelLightingParameters)
        SHADER_PARAMETER(Core::Int3, VoxelOrigin)
        SHADER_PARAMETER(uint32_t, VoxelGridSize)
        SHADER_PARAMETER(float, VoxelSpacing)
        SHADER_PARAMETER(uint32_t, bVoxelRefreshedAll)
        SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float4>, VoxelRadiance)
    END_SHADER_PARAMETER_STRUCT(VoxelLightingParameters)

    // One volume per viewer. Its world cell keys follow the coarse level of the source cache.
    class RENDERER_API VoxelLighting
    {
    public:
        VoxelLightingParameters Update(RenderCore::RenderGraphBuilder& builder, const WorldRadianceCacheParameters& source);
        void Invalidate() { m_bRefreshAll = true; }
        static void AddDebugPass(RenderCore::RenderGraphBuilder& builder, const WorldRadianceCacheParameters& source,
            const VoxelLightingParameters& voxels, RenderCore::RenderGraphTextureRef output, uint32_t slice);

    private:
        RenderCore::RenderTextureSP m_Volume;
        bool m_bRefreshAll = true;
    };
}
