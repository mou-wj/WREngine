// SceneInterface.h
// Interface-only scene abstraction for Renderer-side access.
#pragma once
#include "EngineExport.h"
#include "Math.hpp"
#include <cstdint>
#include <functional>
#include <vector>

namespace Core { struct Ray; }

namespace Engine {
    // Forward declarations for scene components and render-side snapshots.
    class PrimitiveComponent;
    class PrimitiveSceneProxy; // Render-side primitive snapshot.
    class LightComponent;
    class SceneComponent;
    class SceneView; // View data passed to rendering.

    using FrameIndex = uint64_t;

    enum class EFeatureLevel : uint8_t {
        Low = 0,
        Medium = 1,
        High = 2
    };

    struct SceneSetting {
        bool bDrawGrid = false;
        bool bDrawLightHints = false;
        // Borrowed light to visualize; nullptr draws no light hint. Remove the
        // light from its scene before destroying it so this reference is cleared.
        const LightComponent* LightHintTarget = nullptr;
    };

    enum class EScenePickFilter : uint8_t { Meshes, Lights, All };

    struct ScenePickResult {
        SceneComponent* Component = nullptr; // Borrowed until scene/component changes.
        Core::Float3 WorldPosition;
        float Distance = 0; // World distance from ray origin.
        int32_t SectionIndex = -1; // Geometry metadata stays -1 for lights.
        int32_t MaterialIndex = -1;
        int32_t TriangleIndex = -1; // Within SectionIndex of rendered LOD0.
    };

    /*
    ===============================================================================
        SceneInterface
        Shared scene interface implemented by Renderer.
    ===============================================================================
    */
    class ENGINE_API SceneInterface {
    public:
        virtual ~SceneInterface() = default;

        // Configure before BeginRender on the scene's control thread. Renderer
        // snapshots these values while building the graph, not on GPU execution.
        SceneSetting& GetSceneSetting() { return m_SceneSetting; }
        const SceneSetting& GetSceneSetting() const { return m_SceneSetting; }

        // Read registered proxies, serially with scene/asset mutations. Mesh
        // bounds follow Flush; existing light notifications update proxies directly.
        // Normalizes the direction; maxDistance is in world units. Miss resets outResult.
        // Mesh-only default preserves geometry/material queries. All additionally
        // tests local-light range surfaces and the directional-light hint arrow.
        virtual bool Raycast(const Core::Ray& ray, float maxDistance, ScenePickResult& outResult,
            EScenePickFilter filter = EScenePickFilter::Meshes) const = 0;

        // -------------------------------------------------------------------------
        // Component registration; implementations queue pending scene updates.
        // -------------------------------------------------------------------------
        virtual void AddPrimitive(PrimitiveComponent* component) = 0;
        virtual void RemovePrimitive(PrimitiveComponent* component) = 0;
        virtual void AddLight(LightComponent* component) = 0;
        virtual void RemoveLight(LightComponent* component) = 0;
        virtual void FlushPendingUpdates() = 0;
        virtual void NotifyComponentChanged(SceneComponent* component) = 0;
        // Called externally to process pending global distance field block updates.
        virtual bool UpdateGlobalDistanceFieldIfNeeded() = 0;

    private:
        SceneSetting m_SceneSetting;
    };
} // namespace Engine
