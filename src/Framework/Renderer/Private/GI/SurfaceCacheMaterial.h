#pragma once

#include "RenderGraphBuilder.h"
#include "StaticMeshResources.h"
#include "Material.h"
#include <vector>

namespace Renderer
{
    class Scene;

    struct SurfaceCacheMeshInstance
    {
        uint32_t InstanceID = UINT32_MAX;
        Core::Mat4 LocalToWorld = Core::Mat4::Identity();
        std::shared_ptr<Engine::StaticMeshRenderData> RenderData;
        std::vector<std::shared_ptr<const Engine::MaterialRenderProxy>> Materials;
    };

    // World-aligned orthographic capture. Each tile belongs to exactly one instance.
    BEGIN_SHADER_PARAMETER_STRUCT(SurfaceCard)
        SHADER_PARAMETER(Core::Float4, Center)
        SHADER_PARAMETER(Core::Float4, AxisU)
        SHADER_PARAMETER(Core::Float4, AxisV)
        SHADER_PARAMETER(Core::Float4, AxisN)
        SHADER_PARAMETER(Core::Float4, Extent)
        SHADER_PARAMETER(Core::UInt4, Tile)
        SHADER_PARAMETER(Core::UInt4, Owner)
    END_SHADER_PARAMETER_STRUCT(SurfaceCard)

    BEGIN_SHADER_PARAMETER_STRUCT(SurfaceCacheInstance)
        SHADER_PARAMETER(Core::Mat4, WorldToLocal)
        SHADER_PARAMETER(Core::Mat4, NormalToWorld)
        SHADER_PARAMETER(Core::Float4, BoundsMin)
        SHADER_PARAMETER(Core::Float4, BoundsMax)
        SHADER_PARAMETER(Core::Float4, SDFMin)
        SHADER_PARAMETER(Core::Float4, SDFMax)
        SHADER_PARAMETER(Core::Float4, SDFAllocation)
        SHADER_PARAMETER(Core::Float4, SDFResolution)
        SHADER_PARAMETER(Core::UInt4, Info)
    END_SHADER_PARAMETER_STRUCT(SurfaceCacheInstance)

    BEGIN_SHADER_PARAMETER_STRUCT(SurfaceCacheMaterialParameters)
        SHADER_PARAMETER(uint32_t, CardCount)
        SHADER_PARAMETER(uint32_t, InstanceCount)
        SHADER_PARAMETER(Core::UInt2, AtlasSize)
        SHADER_PARAMETER(float, ScreenYSign)
        SHADER_PARAMETER_RDG_STRUCTURED_BUFFER(SurfaceCard, Cards)
        SHADER_PARAMETER_RDG_STRUCTURED_BUFFER(SurfaceCacheInstance, Instances)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, BaseColorMetallic)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, NormalRoughness)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, PositionCoverage)
    END_SHADER_PARAMETER_STRUCT(SurfaceCacheMaterialParameters)

    enum class ESurfaceCardAtlasView : uint32_t
    {
        BaseColor,
        WorldNormal,
        Coverage,
        Roughness,
        Metallic
    };

    // Mode 0: known world hit (Direction is the geometric world normal).
    // Mode 1: world ray. UINT32_MAX traces all instances; otherwise restrict to InstanceID.
    BEGIN_SHADER_PARAMETER_STRUCT(SurfaceCacheQuery)
        SHADER_PARAMETER(Core::Float4, OriginDistance)
        SHADER_PARAMETER(Core::Float4, Direction)
        SHADER_PARAMETER(Core::UInt4, Control)
    END_SHADER_PARAMETER_STRUCT(SurfaceCacheQuery)

    BEGIN_SHADER_PARAMETER_STRUCT(SurfaceCacheQueryResult)
        SHADER_PARAMETER(Core::Float4, BaseColorMetallic)
        SHADER_PARAMETER(Core::Float4, NormalRoughness)
        SHADER_PARAMETER(Core::Float4, PositionDistance)
        // InstanceID, CardID, flags (1=SDF/direct hit, 2=cached material, 4=iteration limit), iterations.
        SHADER_PARAMETER(Core::UInt4, Status)
        // Exact material texel chosen by lookup; avoids a second, potentially different Card search.
        SHADER_PARAMETER(Core::UInt4, SampleCoordinate)
    END_SHADER_PARAMETER_STRUCT(SurfaceCacheQueryResult)

    class RENDERER_API SurfaceCacheMaterial
    {
    public:
        SurfaceCacheMaterial() = default;
        SurfaceCacheMaterial(const SurfaceCacheMaterial&) = delete;
        SurfaceCacheMaterial& operator=(const SurfaceCacheMaterial&) = delete;
        // Explicit snapshot; call again after transform/material/geometry changes, after retiring GPU users.
        void Initialize(const std::vector<SurfaceCacheMeshInstance>& instances, uint32_t cardResolution = 64);
        void Release(); // Caller retires graphs and all last-access fences before release/destruction.
        SurfaceCacheMaterialParameters AddCapturePasses(RenderCore::RenderGraphBuilder& builder);
        SurfaceCacheMaterialParameters Register(RenderCore::RenderGraphBuilder& builder) const;
        const std::vector<SurfaceCard>& GetCards() const { return m_Cards; }
        const std::vector<SurfaceCacheMeshInstance>& GetInstances() const { return m_Instances; }
        uint64_t GetRevision() const { return m_Revision; }
        RenderCore::RenderTexture* GetBaseColorAtlas() const { return m_BaseColor.get(); }
        RenderCore::RenderTexture* GetNormalAtlas() const { return m_Normal.get(); }
        RenderCore::RenderTexture* GetPositionAtlas() const { return m_Position.get(); }
        static std::vector<SurfaceCacheMeshInstance> GatherSceneInstances(Scene& scene);
    private:
        std::vector<SurfaceCacheMeshInstance> m_Instances;
        std::vector<SurfaceCard> m_Cards;
        std::vector<SurfaceCacheInstance> m_InstanceData;
        RenderCore::RenderTextureSP m_BaseColor, m_Normal, m_Position, m_Depth;
        uint32_t m_Size = 0;
        bool m_bCaptureScheduled = false;
        uint64_t m_Revision = 0;
    };

    RENDERER_API RenderCore::RenderGraphBufferRef AddSurfaceCacheQueryPass(RenderCore::RenderGraphBuilder& builder,
        const SurfaceCacheMaterialParameters& cache, const std::vector<SurfaceCacheQuery>& queries,
        uint32_t maxSteps = 256, float hitTolerance = 0.002f);

    // Consume queries generated by an earlier RDG pass, such as screen-probe ray placement.
    RENDERER_API RenderCore::RenderGraphBufferRef AddSurfaceCacheQueryPass(RenderCore::RenderGraphBuilder& builder,
        const SurfaceCacheMaterialParameters& cache, RenderCore::RenderGraphBufferRef queries, uint32_t queryCount,
        uint32_t maxSteps = 256, float hitTolerance = 0.002f);

    // Modes: 0 albedo, 1 normal, 2 coverage/Card tiles, 3 roughness, 4 metallic.
    RENDERER_API void AddSurfaceCacheDebugPass(RenderCore::RenderGraphBuilder& builder,
        const SurfaceCacheMaterialParameters& cache, RenderCore::RenderGraphTextureRef output, uint32_t mode = 0,
        uint32_t selectedCardIndex = UINT32_MAX);

    RENDERER_API void AddSurfaceCacheCardDebugPass(RenderCore::RenderGraphBuilder& builder,
        const SurfaceCacheMaterialParameters& cache, uint32_t cardIndex, ESurfaceCardAtlasView view,
        RenderCore::RenderGraphTextureRef output);

    // 32x32 pixels per query: modes 0 albedo, 1 normal, 2 Card ID; magenta = hit without coverage.
    RENDERER_API void AddSurfaceCacheQueryDebugPass(RenderCore::RenderGraphBuilder& builder,
        RenderCore::RenderGraphBufferRef results, uint32_t queryCount, RenderCore::RenderGraphTextureRef output, uint32_t mode = 0);
}
