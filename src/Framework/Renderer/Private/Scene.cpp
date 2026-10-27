#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "Scene.h"
#include "SceneView.h"
#include "PrimitiveSceneProxy.h"
#include "PrimitiveComponent.h"
#include "LightComponent.h"
#include "LightSceneProxy.h"
#include "SceneShaderParameters.h"
#include "FrustumCullPass.h"
#include "RenderResource.h"
#include "RHIPipelineStateCache.h"
#include "ShaderParameter.h"
#include "Shader.h"
#include "RHIApi.h"
#include "StaticMeshProxy.h"
#include "LocalVertexFactory.h"
#include "StaticMeshComponent.h"
#include "DistanceFieldMgr.h"
#include <cstring>
#include <algorithm>
#include <cmath>
#include <limits>

using namespace Engine;


namespace Renderer {


    //====================================================
    // PrimitiveSceneInfo
    //====================================================

    PrimitiveSceneInfo::
        PrimitiveSceneInfo(
            std::unique_ptr<
            Engine::PrimitiveSceneProxy>
            inProxy)
        :
        m_Proxy(std::move(inProxy))
    {
    }

    //====================================================
    // LightSceneInfo
    //====================================================

    LightSceneInfo::
        LightSceneInfo(
            std::unique_ptr<
            Engine::LightSceneProxy>
            inProxy)
        :
        m_Proxy(std::move(inProxy))
    {
    }

    //====================================================
    // SceneAccelerationStructure

    void SceneAccelerationStructure::Clear()
    {
        m_Nodes.clear();
        m_Primitives.clear();
    }

    void SceneAccelerationStructure::Build(const std::unordered_map<Engine::PrimitiveComponent*, std::unique_ptr<PrimitiveSceneInfo>>& primitiveInfos)
    {
        m_Primitives.clear();
        m_Nodes.clear();

        m_Primitives.reserve(primitiveInfos.size());
        for (auto& pair : primitiveInfos)
        {
            if (!pair.second) continue;
            if (!pair.second->bVisible) continue;
            auto* proxy = pair.second->GetProxy();
            if (proxy)
            {
                m_Primitives.push_back(proxy);
            }
        }

        if (m_Primitives.empty())
        {
            return;
        }

        m_Nodes.reserve(m_Primitives.size() * 2);
        BuildNode(0, static_cast<int>(m_Primitives.size()));
    }

    int SceneAccelerationStructure::Partition(int start, int count, int axis)
    {
        auto compare = [axis](Engine::PrimitiveSceneProxy* a, Engine::PrimitiveSceneProxy* b) {
            const Core::AABB& aBox = a->GetBounds().Box;
            const Core::AABB& bBox = b->GetBounds().Box;
            float aCenter = (axis == 0 ? (aBox.Min.X + aBox.Max.X) : (axis == 1 ? (aBox.Min.Y + aBox.Max.Y) : (aBox.Min.Z + aBox.Max.Z)));
            float bCenter = (axis == 0 ? (bBox.Min.X + bBox.Max.X) : (axis == 1 ? (bBox.Min.Y + bBox.Max.Y) : (bBox.Min.Z + bBox.Max.Z)));
            return aCenter < bCenter;
        };

        int mid = start + count / 2;
        std::nth_element(m_Primitives.begin() + start, m_Primitives.begin() + mid, m_Primitives.begin() + start + count, compare);
        return mid;
    }

    int SceneAccelerationStructure::BuildNode(int start, int count)
    {
        int nodeIndex = static_cast<int>(m_Nodes.size());
        m_Nodes.emplace_back();
        Node& current = m_Nodes.back();
        current.Start = start;
        current.Count = count;
        current.LeftChild = -1;
        current.RightChild = -1;
        current.bLeaf = false;
        current.Bounds.SetEmpty();

        for (int index = start; index < start + count; ++index)
        {
            current.Bounds.Merge(m_Primitives[index]->GetBounds());
        }

        if (count <= 1)
        {
            current.bLeaf = true;
            return nodeIndex;
        }

        Core::Float3 extent = current.Bounds.Box.GetExtent();
        int axis = 0;
        if (extent.Y > extent.X) axis = 1;
        if (extent.Z > ((axis == 0) ? extent.X : extent.Y)) axis = 2;

        int mid = Partition(start, count, axis);
        if (mid <= start || mid >= start + count)
        {
            current.bLeaf = true;
            return nodeIndex;
        }

        current.LeftChild = BuildNode(start, mid - start);
        current.RightChild = BuildNode(mid, start + count - mid);
        return nodeIndex;
    }

    void SceneAccelerationStructure::Query(const Engine::SceneView& view, std::vector<Engine::PrimitiveSceneProxy*>& outPrimitives) const
    {
        if (m_Nodes.empty())
        {
            return;
        }

        auto queryNode = [&](auto&& self, int nodeIndex) -> void {
            const Node& current = m_Nodes[nodeIndex];
            if (!view.IsBoxVisible(current.Bounds.Box))
            {
                return;
            }

            if (current.bLeaf)
            {
                for (int index = current.Start; index < current.Start + current.Count; ++index)
                {
                    outPrimitives.push_back(m_Primitives[index]);
                }
                return;
            }

            if (current.LeftChild != -1)
            {
                self(self, current.LeftChild);
            }
            if (current.RightChild != -1)
            {
                self(self, current.RightChild);
            }
        };

        queryNode(queryNode, 0);
    }


    //====================================================
    // Scene
    //====================================================

    Scene::Scene()
    {
		m_ShadowMapAllocator.Initialize(
			ShadowAllocatorDesc{
				{ 4096, 4096 } // SpotShadowAtlas
			});
         
            EnsureDistanceFieldResources();
    }

    Scene::~Scene()
    {
    }


    void Scene::AddPrimitive(
        Engine::PrimitiveComponent*
        component)
    {
        if (!component)
        {
            return;
        }
        if (m_PrimitiveInfos.find(component) != m_PrimitiveInfos.end()) {
            return;
        }
        component->SetSceneOwner(this);
        auto proxy =
            std::unique_ptr<Engine::PrimitiveSceneProxy>(component
            ->CreateSceneProxy());

        if (!proxy)
        {
            return;
        }

        SceneCommand command;

        command.Type =
            ESceneCommandType
            ::AddPrimitive;

        command.PrimitiveComponent =
            component;

        command.PrimitiveProxy =
            std::move(proxy);

        std::scoped_lock lock(
            m_PendingCommandMutex);

        m_PendingCommands.push_back(
            std::move(command));
    }

    void Scene::RemovePrimitive(
        Engine::PrimitiveComponent*
        component)
    {
        if (m_PrimitiveInfos.find(component) == m_PrimitiveInfos.end()) {
            return;
        }
        component->SetSceneOwner(nullptr);
        SceneCommand command;

        command.Type =
            ESceneCommandType
            ::RemovePrimitive;

        command.PrimitiveComponent =
            component;

        std::scoped_lock lock(
            m_PendingCommandMutex);

        m_PendingCommands.push_back(
            std::move(command));
    }

    void Scene::AddLight(
        Engine::LightComponent*
        component)
    {
        if (!component)
        {
            return;
        }
        if (m_LightInfos.find(component) != m_LightInfos.end()) {
            return;
        }
        component->SetSceneOwner(this);
        auto proxy =
            std::unique_ptr<Engine::LightSceneProxy>(component
            ->CreateSceneProxy());

        if (!proxy)
        {
            return;
        }

        SceneCommand command;

        command.Type =
            ESceneCommandType
            ::AddLight;

        command.LightComponent =
            component;

        command.LightProxy =
            std::move(proxy);

        std::scoped_lock lock(
            m_PendingCommandMutex);

        m_PendingCommands.push_back(
            std::move(command));
    }

    void Scene::RemoveLight(
        Engine::LightComponent*
        component)
    {
        if (m_LightInfos.find(component) == m_LightInfos.end()) {
            return;
        }
        component->SetSceneOwner(nullptr);
        SceneCommand command;

        command.Type =
            ESceneCommandType
            ::RemoveLight;

        command.LightComponent =
            component;

        std::scoped_lock lock(
            m_PendingCommandMutex);

        m_PendingCommands.push_back(
            std::move(command));
    }

    void Scene::FlushPendingUpdates()
    {
        std::vector<
            SceneCommand>
            commands;

        {
            std::scoped_lock lock(
                m_PendingCommandMutex);

            commands.swap(
                m_PendingCommands);
        }
        Engine::LightSceneProxy* lightProxy = nullptr;
        auto markLightDiry = [this](Engine::LightSceneProxy* lightProxy) {
            if (!lightProxy) return;
            auto lightType = lightProxy->GetLightType();
            switch (lightType)
            {
                case Engine::ELightType::Directional:
                    m_GPUResourceInfo.DirtyFlags |= ESceneGPUResourceDirty::DirectionalLight;
                    break;
                case Engine::ELightType::Point:
                    m_GPUResourceInfo.DirtyFlags |= ESceneGPUResourceDirty::PointLight;
                    break;
                case Engine::ELightType::Spot:
                    m_GPUResourceInfo.DirtyFlags |= ESceneGPUResourceDirty::SpotLight;
                case Engine::ELightType::Sky:
                    m_GPUResourceInfo.DirtyFlags |= ESceneGPUResourceDirty::SkyLight;
                    break;
                default:
                    break;
            }


         };
        for (auto& command
            : commands)
        {
            switch (command.Type)
            {
                case ESceneCommandType::
                AddPrimitive:
                {
                    m_PrimitiveInfos[
                        command
                            .PrimitiveComponent]
                        =
                        std::make_unique<
                        PrimitiveSceneInfo>(
                            std::move(
                                command
                                .PrimitiveProxy));

                    PrimitiveSceneInfo* addedInfo = m_PrimitiveInfos[command.PrimitiveComponent].get();
                    m_GPUResourceInfo.DirtyFlags |=
                        ESceneGPUResourceDirty
                        ::Primitive;
                    break;
                }

                case ESceneCommandType::
                RemovePrimitive:
                {
                    auto existingIt = m_PrimitiveInfos.find(command.PrimitiveComponent);

                    m_PrimitiveInfos.erase(
                        command
                        .PrimitiveComponent);

                    m_GPUResourceInfo.DirtyFlags |=
                        ESceneGPUResourceDirty
                        ::Primitive;

                    break;
                }

                case ESceneCommandType::
                AddLight:
                {
                    markLightDiry(command.LightProxy.get());
                    m_LightInfos[
                        command
                            .LightComponent]
                        =
                        std::make_unique<
                        LightSceneInfo>(
                            std::move(
                                command
                                .LightProxy));
                    
                    break;
                }

                case ESceneCommandType::
                RemoveLight:
                {
                    markLightDiry(command.LightProxy.get());
                    m_LightInfos.erase(
                        command
                        .LightComponent);
                    
                    break;
                }

                default:
                    break;
            }
        }
        //update scene bounds
        m_SceneBounds.SetEmpty();
        for (auto& primitive : m_PrimitiveInfos) {
            m_SceneBounds.Merge(primitive.second->GetProxy()->GetBounds());
        }
        m_AccelerationStructure.Build(m_PrimitiveInfos);
		//build GPU resource info
        UpdateGPUResourceIfNeeded();


    }

    void Scene::NotifyComponentChanged(SceneComponent* component)
    {
        if (!component) return;
        if (component->IsA<Engine::LightComponent>())
        {
            auto* light = static_cast<Engine::LightComponent*>(component);
            switch (light->GetLightType())
            {
            case ELightType::Directional:
                m_GPUResourceInfo.DirtyFlags |=
                    ESceneGPUResourceDirty
                    ::DirectionalLight;
                break;

            case ELightType::Point:
                m_GPUResourceInfo.DirtyFlags |=
                    ESceneGPUResourceDirty
                    ::PointLight;
                break;

            case ELightType::Spot:
                m_GPUResourceInfo.DirtyFlags |=
                    ESceneGPUResourceDirty
                    ::SpotLight;
                break;
            case ELightType::Sky:
                m_GPUResourceInfo.DirtyFlags |=
                    ESceneGPUResourceDirty
                    ::SkyLight;
                break;
            }
        }

        if (component->IsA<Engine::PrimitiveComponent>())
        {
            // Replacing the keyed proxy at FlushPendingUpdates refreshes its cached
            // transform/bounds; setting the GPU dirty bit alone leaves the old proxy.
            auto* primitive = static_cast<Engine::PrimitiveComponent*>(component);
            SceneCommand command;
            command.Type = ESceneCommandType::AddPrimitive;
            command.PrimitiveComponent = primitive;
            command.PrimitiveProxy.reset(primitive->CreateSceneProxy());
            if (command.PrimitiveProxy)
            {
                std::scoped_lock lock(m_PendingCommandMutex);
                m_PendingCommands.push_back(std::move(command));
            }
            m_GPUResourceInfo.DirtyFlags |=
                ESceneGPUResourceDirty
                ::Primitive;
        }
    }

    bool Scene::UpdateGlobalDistanceFieldIfNeeded()
    {
        static bool firstInit = false;
        if ( EnumHasAnyFlags(m_GPUResourceInfo.DirtyFlags, ESceneGPUResourceDirty::GlobalDistanceField) || firstInit == false) {
            firstInit = true;
            //收集静态网格信息，更新距离场
            DistanceFieldMergePassInput input;
            Core::AABB affectBox;

            for (auto& primitive : m_PrimitiveInfos) {
				if(primitive.second->GetProxy()->IsA<StaticMeshProxy>()){
                    
                    StaticMeshProxy* staticMeshProxyIns = primitive.second->GetProxy()->Cast<StaticMeshProxy>();
                    auto renderData = staticMeshProxyIns->GetStaticMeshComponent()->GetStaticMesh()->GetRenderData();
                    auto allocation = renderData->LODResources[0].DistanceFieldData.Allocation;
                    DistanceFieldSourceParameters source;
                    source.LocalToWorld = staticMeshProxyIns->GetLocalToWorld();
                    source.WorldToLocal = staticMeshProxyIns->GetWorldToLocal();
                    Core::UInt3 offset(allocation.X, allocation.Y, allocation.Z);
                    Core::UInt3 size(allocation.SizeX, allocation.SizeY, allocation.SizeZ);
                    source.Offset = offset;
                    source.InputResolution = size;
                    auto localBound = renderData->Bounds;
                    auto bound = staticMeshProxyIns->GetBounds();
                    Core::Float3 boundMin,boundMax;
                    boundMin = localBound.Box.Min;
                    boundMax = localBound.Box.Max;
                    source.BoundsMax = boundMax;
                    source.BoundsMin = boundMin;
                    affectBox.Merge(bound.Box);
                    input.SourceParams.emplace_back(source);
                }
            }
            if (input.SourceParams.empty()) {
                return false;
            }
            GlobalDistanceFieldBlockClipMap clipMap;
            auto suc = m_GPUResourceInfo.DistanceFieldResourceInfo.StaticDistanceField.Allocate(affectBox, clipMap);
            //收集需要更新的globaldistancefield 区域id
            std::vector<uint32_t> affectBlockIDs;
            m_GPUResourceInfo.DistanceFieldResourceInfo.StaticDistanceField.GetBlocksInsideBounds(affectBox, affectBlockIDs);
            for (auto& id : affectBlockIDs) {
                const auto block = m_GPUResourceInfo.DistanceFieldResourceInfo.StaticDistanceField.GetBlock(id);
				if (block) {
					DistanceFieldOutParameters out;
					out.Offset = Core::Int3(block->Allocation.X, block->Allocation.Y, block->Allocation.Z);
					out.BoundsMin = block->Bounds.Min;
					out.BoundsMax = block->Bounds.Max;
					input.OutputParams.emplace_back(out);
				}
            }
            if (input.OutputParams.empty()) {
                return false;
            }

            input.InputSDFTexture = Engine::GDistanceFieldMgr.GetAtlas().GetAtlasTexture();
            input.OutputSDFTexture = m_GPUResourceInfo.DistanceFieldResourceInfo.StaticDistanceField.GetAtlas().GetAtlasTexture();
            ExecuteDistanceFieldMergePass(input);

        }
        return false;
    }

    void Scene::ForEachPrimitive(
        std::function<void(
            Engine::PrimitiveSceneProxy*)>
        visitor)
    {
        for (auto& pair
            : m_PrimitiveInfos)
        {
            auto* info =
                pair.second.get();

            if (!info->bVisible)
            {
                continue;
            }

            visitor(
                info->GetProxy());
        }
    }

    void Scene::ForEachLight(
        std::function<void(
            Engine::LightSceneProxy*)>
        visitor)
    {
        for (auto& pair
            : m_LightInfos)
        {
            auto* info =
                pair.second.get();

            if (!info->bVisible)
            {
                continue;
            }

            visitor(
                info->GetProxy());
        }
    }

    const SceneGPUResourceInfo& Scene::GetGPUResourceInfo() const
    {
        return m_GPUResourceInfo;
    }

    ShadowMapAllocator& Scene::GetShadowMapAllocator()
    {
        return m_ShadowMapAllocator;
    }

    const Core::BoxSphereBounds& Scene::GetSceneBounds() const
    {
        return m_SceneBounds;
    }

    LightShadowInfo& Scene::GetLightShadowInfo(Engine::LightSceneProxy* light)
    {
        return m_GPUResourceInfo.ShadowResourceInfo.LightShadowInfos[light];
    }

    std::vector<Engine::PrimitiveSceneProxy*> Scene::GatherVisiblePrimitivesCPU(const Engine::SceneView& view) const
    {
        std::vector<Engine::PrimitiveSceneProxy*> result;
        result.reserve(m_PrimitiveInfos.size());

        if (m_AccelerationStructure.IsValid())
        {
            m_AccelerationStructure.Query(view, result);
            return result;
        }

        for (const auto& pair : m_PrimitiveInfos)
        {
            const PrimitiveSceneInfo* info = pair.second.get();
            if (!info || !info->bVisible)
            {
                continue;
            }
            Engine::PrimitiveSceneProxy* proxy = info->GetProxy();
            if (!proxy)
            {
                continue;
            }
            if (view.IsBoxVisible(proxy->GetBounds().Box))
            {
                result.push_back(proxy);
            }
        }
        return result;
    }

    std::vector<Engine::PrimitiveSceneProxy*> Scene::GatherVisiblePrimitivesGPU(const Engine::SceneView& view) const
    {
        std::vector<Engine::PrimitiveSceneProxy*> result;
        const auto& primitiveRes = m_GPUResourceInfo.PrimitiveResourceInfo;
        result.reserve(primitiveRes.PrimitiveCount);

        if (primitiveRes.PrimitiveCount == 0)
        {
            return result;
        }

        if (!primitiveRes.PrimitiveBoundsBuffer ||
            !primitiveRes.VisibilityFlagsBuffer ||
            m_PrimitiveGPUProxyOrder.size() < primitiveRes.PrimitiveCount)
        {
            return GatherVisiblePrimitivesCPU(view);
        }

        FrustumCullPassInput passInput;
        passInput.ViewProjection = view.ViewProjectionMatrix;
        passInput.PrimitiveCount = primitiveRes.PrimitiveCount;
        passInput.PrimitiveBoundsBuffer = primitiveRes.PrimitiveBoundsBuffer.get();
        passInput.VisibilityFlagsBuffer = primitiveRes.VisibilityFlagsBuffer.get();

        std::vector<uint32_t> visibilityFlags;
        if (!ExecuteFrustumCullPass(passInput, visibilityFlags) ||
            visibilityFlags.size() < primitiveRes.PrimitiveCount)
        {
            return GatherVisiblePrimitivesCPU(view);
        }

        for (uint32_t index = 0; index < primitiveRes.PrimitiveCount; ++index)
        {
            if (visibilityFlags[index] == 0)
            {
                continue;
            }

            auto* proxy = m_PrimitiveGPUProxyOrder[index];
            if (proxy)
            {
                result.push_back(proxy);
            }
        }

        return result;
    }

    std::vector<Engine::PrimitiveSceneProxy*> Scene::GatherVisiblePrimitives(const Engine::SceneView& view) const
    {
        return GatherVisiblePrimitivesCPU(view);
    }

    void Scene::UpdatePrimitiveGPUResource()
    {
        auto& primitiveRes = m_GPUResourceInfo.PrimitiveResourceInfo;
        auto& localVfInfo = primitiveRes.LocalVertexFactoryInstanceInfo;

        m_PrimitiveGPUProxyOrder.clear();
        m_PrimitiveGPUProxyOrder.reserve(m_PrimitiveInfos.size());

        std::vector<AABBParameters> boundsData;
        boundsData.reserve(m_PrimitiveInfos.size());

        for (const auto& pair : m_PrimitiveInfos)
        {
            const PrimitiveSceneInfo* info = pair.second.get();
            if (!info || !info->bVisible)
            {
                continue;
            }

            Engine::PrimitiveSceneProxy* proxy = info->GetProxy();
            if (!proxy)
            {
                continue;
            }

            const Core::AABB& bounds = proxy->GetBounds().Box;
            AABBParameters data;
            data.Min = bounds.Min;
            data.Max = bounds.Max;

            m_PrimitiveGPUProxyOrder.push_back(proxy);
            boundsData.emplace_back(data);
        }

        primitiveRes.PrimitiveCount = static_cast<uint32_t>(boundsData.size());



        RHI::RHIBufferDesc boundsDesc;
        boundsDesc.Size = std::max<uint64_t>(1ull, static_cast<uint64_t>(boundsData.size() * sizeof(AABBParameters)));
        boundsDesc.Stride = sizeof(AABBParameters);
        boundsDesc.Usage =
            RHI::ERHIBufferUsageFlag::Structured |
            RHI::ERHIBufferUsageFlag::ShaderResource |
            RHI::ERHIBufferUsageFlag::TransferDst;
        boundsDesc.InitialQueueType = RHI::EQueueType::Compute;
        primitiveRes.PrimitiveBoundsBuffer = std::make_shared<RenderCore::RenderBuffer>(boundsDesc);
        primitiveRes.PrimitiveBoundsBuffer->InitRHIResource();
        if (!boundsData.empty())
        {
            primitiveRes.PrimitiveBoundsBuffer->UploadData(
                boundsData.data(),
                static_cast<uint32_t>(boundsData.size() * sizeof(AABBParameters)));
        }

        RHI::RHIBufferDesc visibilityDesc;
        visibilityDesc.Size = std::max<uint64_t>(1ull, static_cast<uint64_t>(primitiveRes.PrimitiveCount * sizeof(uint32_t)));
        visibilityDesc.Stride = sizeof(uint32_t);
        visibilityDesc.Usage =
            RHI::ERHIBufferUsageFlag::Structured |
            RHI::ERHIBufferUsageFlag::UnorderedAccess |
            RHI::ERHIBufferUsageFlag::TransferSrc |
            RHI::ERHIBufferUsageFlag::TransferDst;
        visibilityDesc.InitialQueueType = RHI::EQueueType::Compute;
        primitiveRes.VisibilityFlagsBuffer = std::make_shared<RenderCore::RenderBuffer>(visibilityDesc);
        primitiveRes.VisibilityFlagsBuffer->InitRHIResource();

        std::vector<uint32_t> zeroVisibility(primitiveRes.PrimitiveCount, 0u);
        const uint32_t uploadSize = primitiveRes.PrimitiveCount > 0 ?
            primitiveRes.PrimitiveCount * sizeof(uint32_t) :
            sizeof(uint32_t);
        if (!zeroVisibility.empty()) {
            primitiveRes.VisibilityFlagsBuffer->UploadData(static_cast<const void*>(zeroVisibility.data()),
                uploadSize);
        }


        std::vector<Engine::LocalVertexFactoryInstanceSubParameters> subParameters;
        subParameters.reserve(m_PrimitiveGPUProxyOrder.size());
        localVfInfo.StaticMeshProxyToInstanceId.clear();

        for (Engine::PrimitiveSceneProxy* proxy : m_PrimitiveGPUProxyOrder)
        {
            if (!proxy)
            {
                continue;
            }

            Engine::StaticMeshProxy* staticMeshProxy = proxy->Cast<Engine::StaticMeshProxy>();
            if (!staticMeshProxy)
            {
                continue;
            }

            Engine::LocalVertexFactoryInstanceSubParameters param;
            param.LocalToWorld = staticMeshProxy->GetLocalToWorld();

            const uint32_t instanceId = static_cast<uint32_t>(subParameters.size());
            subParameters.push_back(param);
            localVfInfo.StaticMeshProxyToInstanceId.emplace(staticMeshProxy, instanceId);
        }

        auto& instanceManager = Engine::LocalVertexFactoryInstanceManager::Get();
        if (localVfInfo.InstanceBlock)
        {
            instanceManager.RemoveInstanceBlock(localVfInfo.InstanceBlock);
            localVfInfo.InstanceBlock.reset();
        }

        if (!subParameters.empty())
        {
            localVfInfo.InstanceBlock = instanceManager.CreateInstanceBlock("ScenePrimitiveLocalVFInstances");
            localVfInfo.InstanceBlock->AddSubParameters(subParameters);
        }

        instanceManager.UpdateGPUResources();
    }

    void Scene::EnsureDistanceFieldResources()
    {
        auto& dfRes = m_GPUResourceInfo.DistanceFieldResourceInfo;
        dfRes.StaticDistanceField.Initialize();
    }

    

    void Scene::UpdateGPUResourceIfNeeded()
    {
        auto dirty = m_GPUResourceInfo.DirtyFlags;
        uint32_t lightIndex = 0;
        if (dirty ==
            ESceneGPUResourceDirty
            ::None)
        {
            return;
        }

        auto& lightRes =
            m_GPUResourceInfo
            .LightResourceInfo;

        //------------------------------------------------
        // Directional
        //------------------------------------------------

        if (EnumHasAnyFlags(
            dirty,
            ESceneGPUResourceDirty
            ::DirectionalLight))
        {
            std::vector<
                DirectionalLightData>
                gpuData;

            for (auto& pair :
                m_LightInfos)
            {
                auto* proxy =
                    pair.second
                    ->GetProxy();

                if (!proxy)
                    continue;

                if (proxy
                    ->GetLightType()
                    != ELightType
                    ::Directional)
                {
                    continue;
                }

                DirectionalLightData
                    data;
                data.Common.Color = proxy->GetColor();
                data.Common.Intensity = proxy->GetIntensity();
                data.Common.LightId = lightIndex;
                m_LightIndexs[proxy] = lightIndex;
                lightIndex++;
                auto dirProxy = dynamic_cast<DirectionalLightSceneProxy*>(proxy);
                data.Direction = dirProxy ? dirProxy->GetDirection() : Core::Float3(0.0f, -1.0f, 0.0f);
                gpuData.emplace_back(
                    data);
                
            }

            lightRes.DirectionalLightCount = static_cast<uint32_t>(gpuData.size());
            RHI::RHIBufferDesc desc;
            desc.Size = gpuData.size() * sizeof(DirectionalLightData);
            if (desc.Size == 0) {
                desc.Size = 1;
            }
            desc.Usage = RHI::ERHIBufferUsageFlag::ShaderResource | RHI::ERHIBufferUsageFlag::TransferDst;
            lightRes.DirectionalLightBuffer = std::make_shared<RenderCore::RenderBuffer>(desc);
            lightRes.DirectionalLightBuffer->InitRHIResource();
            lightRes.DirectionalLightBuffer->UploadData(gpuData.data(), desc.Size);
        }

        //------------------------------------------------
        // Point
        //------------------------------------------------

        if (EnumHasAnyFlags(
            dirty,
            ESceneGPUResourceDirty
            ::PointLight))
        {
            std::vector<
                PointLightData>
                gpuData;

            for (auto& pair :
                m_LightInfos)
            {
                auto* proxy =
                    pair.second
                    ->GetProxy();

                if (!proxy)
                    continue;

                if (proxy
                    ->GetLightType()
                    != ELightType
                    ::Point)
                {
                    continue;
                }

                PointLightData
                    data;
                data.Common.Color = proxy->GetColor();
                data.Common.Intensity = proxy->GetIntensity();
                data.Position = proxy->GetPosition();
                data.Common.LightId = lightIndex;
                m_LightIndexs[proxy] = lightIndex;
                lightIndex++;
                auto pointLightProxy = dynamic_cast<PointLightSceneProxy*>(proxy);
                data.Radius = pointLightProxy->GetAttenuationRadius();

                gpuData.emplace_back(
                    data);
            }

            lightRes.PointLightCount =
                static_cast<uint32_t>(
                    gpuData.size());
            RHI::RHIBufferDesc desc;
            desc.Size = gpuData.size() * sizeof(PointLightData);
            if (desc.Size == 0) {
                desc.Size = 1;
            }
            desc.Usage = RHI::ERHIBufferUsageFlag::ShaderResource | RHI::ERHIBufferUsageFlag::TransferDst;
            lightRes.PointLightBuffer = std::make_shared<RenderCore::RenderBuffer>(desc);
            lightRes.PointLightBuffer->InitRHIResource();
            lightRes.PointLightBuffer->UploadData(gpuData.data(), desc.Size);
        }

        //------------------------------------------------
        // Spot
        //------------------------------------------------

        if (EnumHasAnyFlags(
            dirty,
            ESceneGPUResourceDirty
            ::SpotLight))
        {
            std::vector<
                SpotLightData>
                gpuData;

            for (auto& pair :
                m_LightInfos)
            {
                auto* proxy =
                    pair.second
                    ->GetProxy();

                if (!proxy)
                    continue;

                if (proxy
                    ->GetLightType()
                    != ELightType
                    ::Spot)
                {
                    continue;
                }

                SpotLightData
                    data;
                data.Common.Color = proxy->GetColor();
                data.Common.Intensity = proxy->GetIntensity();
                data.Common.LightId = lightIndex;
                m_LightIndexs[proxy] = lightIndex;
                lightIndex++;
                data.Position = proxy->GetPosition();
                auto spotLightProxy = dynamic_cast<SpotLightSceneProxy*>(proxy);
                data.Radius = spotLightProxy->GetAttenuationRadius();
                data.Direction = spotLightProxy->GetDirection();
                data.InnerConeCos = spotLightProxy->GetInnerConeAngle();
                data.OuterConeCos = spotLightProxy->GetOuterConeAngle();

                gpuData.emplace_back(
                    data);
            }

            lightRes.SpotLightCount =
                static_cast<uint32_t>(
                    gpuData.size());
            RHI::RHIBufferDesc desc;
            desc.Size = gpuData.size() * sizeof(SpotLightData);
            if (desc.Size == 0) {
                desc.Size = 1;
            }
            desc.Usage = RHI::ERHIBufferUsageFlag::ShaderResource | RHI::ERHIBufferUsageFlag::TransferDst;
            lightRes.SpotLightBuffer = std::make_shared<RenderCore::RenderBuffer>(desc);
            lightRes.SpotLightBuffer->InitRHIResource();
            lightRes.SpotLightBuffer->UploadData(gpuData.data(), desc.Size);
        }

        if (EnumHasAnyFlags(
            dirty,
            ESceneGPUResourceDirty
            ::Primitive))
        {
            UpdatePrimitiveGPUResource();
        }
        lightRes.IBLSpecularTexture = nullptr;
        lightRes.IBLSpecularTexture = nullptr;
        if (EnumHasAnyFlags(
            dirty,
            ESceneGPUResourceDirty
            ::SkyLight)) {
            for (auto& pair :
                m_LightInfos)
            {
                auto* proxy =
                    pair.second
                    ->GetProxy();

                if (!proxy)
                    continue;

                if (proxy
                    ->GetLightType()
                    != ELightType
                    ::Sky)
                {
                    continue;
                }
                auto skyLightProxy = dynamic_cast<SkyLightSceneProxy*>(proxy);
                lightRes.IBLSpecularTexture = skyLightProxy->GetSpecularPrefilter();
                lightRes.IBLDiffuseTexture = skyLightProxy->GetDiffuseIrradiance();
            }

        }
        
    }

    

} // namespace Renderer
