#pragma once

#include "SurfaceCacheMaterial.h"
#include "../ShadowMapAllocator.h"

namespace Renderer
{
    enum class ESurfaceCacheLightType : uint32_t { Directional, Point, Spot };

    struct SurfaceCacheLight
    {
        ESurfaceCacheLightType Type = ESurfaceCacheLightType::Directional;
        Core::Float3 Color = {1, 1, 1};
        float Intensity = 1;
        Core::Float3 Position = {0, 0, 0};
        float Radius = 10;
        Core::Float3 Direction = {0, -1, 0};
        float InnerConeAngle = 0.35f, OuterConeAngle = 0.7f; // Radians, half angles.
        bool bEnabled = true, bCastShadow = true;
        bool operator==(const SurfaceCacheLight&) const = default;
    };

    BEGIN_SHADER_PARAMETER_STRUCT(SurfaceCacheLightingParameters)
        SHADER_PARAMETER(uint32_t, RadiosityTileSize)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DirectRadiance)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, IndirectRadiance)
    END_SHADER_PARAMETER_STRUCT(SurfaceCacheLightingParameters)

    BEGIN_SHADER_PARAMETER_STRUCT(SurfaceCacheLightingQueryResult)
        // RGB = outgoing Lambertian diffuse radiance; A = valid (black can be valid).
        SHADER_PARAMETER(Core::Float4, Radiance)
        SHADER_PARAMETER(Core::Float4, PositionDistance)
        SHADER_PARAMETER(Core::UInt4, Status)
    END_SHADER_PARAMETER_STRUCT(SurfaceCacheLightingQueryResult)

    class RENDERER_API SurfaceCacheDirectLighting
    {
    public:
        SurfaceCacheDirectLighting() = default;
        SurfaceCacheDirectLighting(const SurfaceCacheDirectLighting&) = delete;
        SurfaceCacheDirectLighting& operator=(const SurfaceCacheDirectLighting&) = delete;
        // Shadow slices use the existing shadow allocator. Point lights consume six slices.
        void Initialize(const SurfaceCacheMaterial& material, uint32_t shadowResolution = 128, uint32_t maxShadowSlices = 64);
        void Release(); // Caller retires all GPU users, as for the material cache.
        void Invalidate() { m_bValid = false; }
        bool IsValidFor(const SurfaceCacheMaterial& material) const
        {
            return m_bValid && m_Radiance && m_Material == &material && m_MaterialRevision == material.GetRevision();
        }
        SurfaceCacheLightingParameters AddUpdatePasses(RenderCore::RenderGraphBuilder& builder,
            const SurfaceCacheMaterial& material, const std::vector<SurfaceCacheLight>& lights);
        SurfaceCacheLightingParameters Register(RenderCore::RenderGraphBuilder& builder, const SurfaceCacheMaterial& material) const;
        RenderCore::RenderTexture* GetAtlas() const { return m_Radiance.get(); }
        static std::vector<SurfaceCacheLight> GatherSceneLights(Scene& scene);
    private:
        RenderCore::RenderTextureSP m_Radiance;
        std::unique_ptr<ShadowMapAllocator> m_ShadowAllocator;
        std::vector<ShadowAllocationSlice> m_ShadowSlots;
        const SurfaceCacheMaterial* m_Material = nullptr; // Explicit caller-owned snapshot; must outlive this cache.
        uint64_t m_MaterialRevision = 0;
        uint32_t m_ShadowSize = 0;
        bool m_bValid = false;
    };

    // Consume exact texel coordinates from AddSurfaceCacheQueryPass (direct or SDF hit).
    RENDERER_API RenderCore::RenderGraphBufferRef AddSurfaceCacheLightingLookupPass(RenderCore::RenderGraphBuilder& builder,
        const SurfaceCacheMaterialParameters& material, const SurfaceCacheLightingParameters& lighting,
        RenderCore::RenderGraphBufferRef materialResults, uint32_t queryCount);
    // Query results = null displays the atlas; otherwise displays 32x32 swatches of queried radiance.
    RENDERER_API void AddSurfaceCacheLightingDebugPass(RenderCore::RenderGraphBuilder& builder,
        const SurfaceCacheLightingParameters& lighting, RenderCore::RenderGraphTextureRef output,
        RenderCore::RenderGraphBufferRef queryResults = nullptr, uint32_t queryCount = 0, float exposure = 1);
}
