// Scene.h
#pragma once
#include "SceneInterface.h"
#include "RenderResource.h"
#include "Flags.h"
#include "ShadowMapAllocator.h"
#include "BoxSphereBounds.h"
#include "LocalVertexFactory.h"
#include "GlobalDistanceField.h"
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <vector>
#include <mutex>
#include <algorithm>
#include <cstdint>
namespace Engine
{
    class PrimitiveComponent;
    class PrimitiveSceneProxy;
    class StaticMeshProxy;

    class LightComponent;
    class LightSceneProxy;

    class SceneView;
}
namespace Renderer {
    static constexpr float SceneDistanceFieldBlockWorldSize = 10.0f;
    static constexpr uint32_t SceneStaticDistanceFieldTextureSize = 128;
    static constexpr uint32_t SceneDynamicDistanceFieldTextureSize = 128;
    static constexpr uint32_t SceneStaticDistanceFieldTextureCount = 30;
    static constexpr uint32_t SceneDynamicDistanceFieldTextureCount = 15;
    static constexpr uint32_t SceneDistanceFieldInvalidTextureId = 0xFFFFFFFFu;

    //====================================================
    // Primitive Scene Info
    //====================================================

    class PrimitiveSceneInfo
    {
    public:

        explicit PrimitiveSceneInfo(
            std::unique_ptr<
            Engine::PrimitiveSceneProxy>
            inProxy);

        Engine::PrimitiveSceneProxy*
            GetProxy() const
        {
            return m_Proxy.get();
        }

    public:

        bool bVisible = true;
        bool bCastShadow = true;
        bool bDynamic = true;

        uint32_t VisibilityFrame = 0;

    private:

        std::unique_ptr<
            Engine::PrimitiveSceneProxy>
            m_Proxy;
    };

    //====================================================
    // Light Scene Info
    //====================================================

    class LightSceneInfo
    {
    public:

        explicit LightSceneInfo(
            std::unique_ptr<
            Engine::LightSceneProxy>
            inProxy);

        Engine::LightSceneProxy*
            GetProxy() const
        {
            return m_Proxy.get();
        }

    public:

        bool bVisible = true;

    private:

        std::unique_ptr<
            Engine::LightSceneProxy>
            m_Proxy;
    };

    struct LightShadowInfo
    {
        ShadowAllocation Allocation;

        std::vector<Core::Float4x4> ShadowMatrices;

        bool bDirty = true;
    };

    //====================================================
    // Scene Commands
    //====================================================

    enum class ESceneCommandType
    {
        AddPrimitive,
        RemovePrimitive,

        AddLight,
        RemoveLight
    };

    struct SceneCommand
    {
        ESceneCommandType Type;

        union
        {
            Engine::PrimitiveComponent*
                PrimitiveComponent;

            Engine::LightComponent*
                LightComponent;
        };

        
        std::unique_ptr<Engine::PrimitiveSceneProxy>
            PrimitiveProxy;

        
        std::unique_ptr<Engine::LightSceneProxy>
            LightProxy;
    };


    enum class ESceneGPUResourceDirty : uint32_t
    {
        None = 0,

        DirectionalLight = 1 << 0,
        PointLight = 1 << 1,
        SpotLight = 1 << 2,
        RectLight = 1 << 3,
        SkyLight = 1 << 4,
        Primitive = 1 << 5,

        GlobalDistanceField = 1 << 6,
        AllLight =
        DirectionalLight |
        PointLight |
        SpotLight |
        RectLight |
        SkyLight,

        All = 0xFFFFFFFF
    };
    ENUM_CLASS_FLAGS(ESceneGPUResourceDirty, ESceneGPUResourceDirtys);

    struct SceneGPULightResourceInfo {
        uint32_t PointLightCount = 0;
        uint32_t SpotLightCount = 0;
        uint32_t DirectionalLightCount = 0;
        uint32_t RectLightCount = 0;
        RenderCore::RenderBufferSP PointLightBuffer = nullptr;
        RenderCore::RenderBufferSP SpotLightBuffer = nullptr;
        RenderCore::RenderBufferSP DirectionalLightBuffer = nullptr;
        RenderCore::RenderBufferSP RectLightBuffer = nullptr;
        RenderCore::RenderTexture* IBLDiffuseTexture = nullptr;
        RenderCore::RenderTexture* IBLSpecularTexture = nullptr;
    };
    struct SceneShadowResourceInfo
    {
        std::unordered_map<Engine::LightSceneProxy*, LightShadowInfo> LightShadowInfos;
        RenderCore::RenderBufferSP LightShadowInfoBuffer = nullptr;
        RenderCore::RenderBufferSP AtlasAccessInfoBuffer = nullptr;
        RenderCore::RenderBufferSP DirectionalLightShadowViewInfoBuffer = nullptr;
        RenderCore::RenderBufferSP SplitBuffer = nullptr;   
    };
    struct SceneGPUPrimitiveResourceInfo
    {
        struct LocalVertexFactoryInstanceInfo
        {
            Engine::LocalVertexFactoryInstanceBlockRef InstanceBlock = nullptr;
            std::unordered_map<Engine::StaticMeshProxy*, uint32_t> StaticMeshProxyToInstanceId;
        };

        uint32_t PrimitiveCount = 0;
        RenderCore::RenderBufferSP PrimitiveBoundsBuffer = nullptr;
        RenderCore::RenderBufferSP VisibilityFlagsBuffer = nullptr;
        LocalVertexFactoryInstanceInfo LocalVertexFactoryInstanceInfo;
    };


    struct SceneDistanceFieldResourceInfo
    {
        GlobalDistanceField StaticDistanceField;//静态对象的全局距离场，更新频率低
    };

    struct SceneGPUResourceInfo {
        SceneGPULightResourceInfo LightResourceInfo;
        SceneShadowResourceInfo ShadowResourceInfo;
        SceneGPUPrimitiveResourceInfo PrimitiveResourceInfo;
        SceneDistanceFieldResourceInfo DistanceFieldResourceInfo;
        ESceneGPUResourceDirtys DirtyFlags = ESceneGPUResourceDirty::None;
    };

    class SceneAccelerationStructure
    {
    public:
        void Build(const std::unordered_map<Engine::PrimitiveComponent*, std::unique_ptr<PrimitiveSceneInfo>>& primitiveInfos);
        void Query(const Engine::SceneView& view, std::vector<Engine::PrimitiveSceneProxy*>& outPrimitives) const;
        void Clear();
        bool IsValid() const { return !m_Nodes.empty(); }

    private:
        struct Node
        {
            Core::BoxSphereBounds Bounds;
            int Start = 0;
            int Count = 0;
            int LeftChild = -1;
            int RightChild = -1;
            bool bLeaf = false;
        };

        std::vector<Node> m_Nodes;
        std::vector<Engine::PrimitiveSceneProxy*> m_Primitives;

        int BuildNode(int start, int count);
        int Partition(int start, int count, int axis);
    };

    /*
    ===============================================================================
        Scene implementation of the Engine scene interface.
    ===============================================================================
    */
    class RENDERER_API Scene : public Engine::SceneInterface {
    public:
        Scene();
        ~Scene() override;

        // -------------------------------------------------------------------------
        // Game-thread scene registration interface.
        // -------------------------------------------------------------------------
        void AddPrimitive(Engine::PrimitiveComponent* component) override;
        void RemovePrimitive(Engine::PrimitiveComponent* component) override;
        void AddLight(Engine::LightComponent* component) override;
        void RemoveLight(Engine::LightComponent* component) override;
        // -------------------------------------------------------------------------
        // Render-thread scene update and query interface.
        // -------------------------------------------------------------------------
        void FlushPendingUpdates() override;
        void NotifyComponentChanged(Engine::SceneComponent* component) override;
        bool UpdateGlobalDistanceFieldIfNeeded() override;


        std::vector<Engine::PrimitiveSceneProxy*> GatherVisiblePrimitives(const Engine::SceneView& view) const ;

        void ForEachPrimitive(std::function<void(Engine::PrimitiveSceneProxy*)> visitor);

        void ForEachLight(std::function<void(Engine::LightSceneProxy*)> visitor);

        const SceneGPUResourceInfo& GetGPUResourceInfo() const;
        ShadowMapAllocator& GetShadowMapAllocator();
        const Core::BoxSphereBounds& GetSceneBounds() const;
        LightShadowInfo& GetLightShadowInfo(Engine::LightSceneProxy* light);

    private:
        std::vector<Engine::PrimitiveSceneProxy*> GatherVisiblePrimitivesCPU(const Engine::SceneView& view) const;
        std::vector<Engine::PrimitiveSceneProxy*> GatherVisiblePrimitivesGPU(const Engine::SceneView& view) const;
        friend class SceneRenderer;
        void UpdateGPUResourceIfNeeded();
        void UpdatePrimitiveGPUResource();
        void EnsureDistanceFieldResources();

        //=========================================
        // Render Thread Storage
        //=========================================

        std::unordered_map<
            Engine::PrimitiveComponent*,
            std::unique_ptr<
            PrimitiveSceneInfo>>
            m_PrimitiveInfos;

        std::vector<
            Engine::PrimitiveSceneProxy*>
            m_PrimitiveGPUProxyOrder;

        SceneAccelerationStructure m_AccelerationStructure;

        std::unordered_map<
            Engine::LightComponent*,
            std::unique_ptr<
            LightSceneInfo>>
            m_LightInfos;

        std::unordered_map<
            Engine::LightSceneProxy*,
            uint32_t>
            m_LightIndexs;

        //=========================================
        // GT -> RT command queue
        //=========================================

        std::vector<
            SceneCommand>
            m_PendingCommands;

        std::mutex
            m_PendingCommandMutex;
        SceneGPUResourceInfo m_GPUResourceInfo;
        ShadowMapAllocator m_ShadowMapAllocator;
        Core::BoxSphereBounds m_SceneBounds;


    };
} // namespace Renderer