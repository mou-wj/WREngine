// SceneInterface.h
// Interface-only scene abstraction for Renderer-side access.
#pragma once
#include "EngineExport.h"
#include <cstdint>
#include <functional>
#include <vector>

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
