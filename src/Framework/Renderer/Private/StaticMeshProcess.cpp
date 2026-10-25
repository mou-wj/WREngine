#include "StaticMeshProcess.h"
#include "Scene.h"
#include "StaticMeshComponent.h"
#include "StaticMeshResources.h"
#include "StaticMeshProxy.h"
#include "StaticMeshMaterialShader.h"
#include "HashHelper.hpp"
#include "RHIPipelineStateCache.h"
#include <unordered_map>
#include <set>
using namespace RenderCore;
using namespace RHI;

struct MeshBatchKey
{
    // PSO相关
    RenderCore::VertexFactory* VertexFactory;
    std::shared_ptr<const Engine::MaterialRenderProxy> MaterialProxy;
    RenderCore::RenderBuffer* IndexBuffer;
    RHI::ERHIFrontFace FrontFace;
    bool operator==(const MeshBatchKey& other) const
    {
        return VertexFactory == other.VertexFactory &&
            MaterialProxy == other.MaterialProxy &&
            IndexBuffer == other.IndexBuffer &&
            FrontFace == other.FrontFace;
    }
};

struct MeshElementKey {
    // DrawRange
    uint32_t FirstIndex;
    uint32_t NumIndices;
    int32_t BaseVertexIndex;
    bool operator==(const MeshElementKey& other) const
    {
        return FirstIndex == other.FirstIndex &&
            NumIndices == other.NumIndices &&
            BaseVertexIndex == other.BaseVertexIndex;
    }
};


namespace std
{

    template<>
    struct hash<MeshBatchKey>
    {
        size_t operator()(const MeshBatchKey& key) const noexcept
        {
            size_t hash = 0;
            HashCombine(hash, key.VertexFactory);
            HashCombine(hash, key.MaterialProxy);
            HashCombine(hash, key.IndexBuffer);
            HashCombine(hash, static_cast<uint32_t>(key.FrontFace));
            return hash;
        }
    };

    template<>
    struct hash<MeshElementKey>
    {
        size_t operator()(const MeshElementKey& key) const noexcept
        {
            size_t hash = 0;
            HashCombine(hash, key.FirstIndex);
            HashCombine(hash, key.NumIndices);
            HashCombine(hash, key.BaseVertexIndex);
            return hash;
        }
    };

}
using namespace Engine;
namespace Renderer {

    

    std::unordered_map<MeshBatchKey,std::unordered_map<MeshElementKey,std::set<uint32_t>>>  GMeshBatchElementMap;
    std::unordered_map<size_t,Core::Mat4> GTransformMap;

    bool IsSameInstanceSet(
        const std::vector<uint32_t>& lhs,
        const std::set<uint32_t>& rhs)
    {
        if (lhs.size() != rhs.size())
        {
            return false;
        }

        auto iter = rhs.begin();

        for (uint32_t id : lhs)
        {
            if (id != *iter)
            {
                return false;
            }

            ++iter;
        }

        return true;
    }
    void BuildMeshBatchFromMap(
        const std::unordered_map<
        MeshBatchKey,
        std::unordered_map<MeshElementKey, std::set<uint32_t>>>& meshBatchElementMap,
        MeshBatchList& outMeshBatchList)
    {
        outMeshBatchList.clear();

        for (const auto& batchPair : meshBatchElementMap)
        {
            const MeshBatchKey& batchKey = batchPair.first;
            const auto& elementMap = batchPair.second;

            MeshBatch batch;

            //----------------------------------------
            // Batch State
            //----------------------------------------

            batch.VertexFactory = batchKey.VertexFactory;
            batch.MaterialProxy = batchKey.MaterialProxy;
            batch.IndexBuffer = batchKey.IndexBuffer;
            batch.FrontFace = batchKey.FrontFace;

            //----------------------------------------
            // Build Elements
            //----------------------------------------

            bool bFirstElement = true;

            for (const auto& elementPair : elementMap)
            {
                const MeshElementKey& elementKey = elementPair.first;
                const std::set<uint32_t>& instanceIds = elementPair.second;

                MeshBatchElement& element =
                    batch.Elements.emplace_back();

                element.FirstIndex = elementKey.FirstIndex;
                element.NumIndices = elementKey.NumIndices;
                element.BaseVertexIndex = elementKey.BaseVertexIndex;

                if (bFirstElement)
                {
                    batch.InstanceDataIds.assign(
                        instanceIds.begin(),
                        instanceIds.end());

                    bFirstElement = false;
					batch.LocalToWorld = GTransformMap[*instanceIds.begin()];
                }
                else
                {
                    assert(IsSameInstanceSet(
                        batch.InstanceDataIds,
                        instanceIds));
                }
            }

            outMeshBatchList.emplace_back(std::move(batch));
        }
    }

    void AllocateInstanceDataIds(Scene* scene, MeshBatchList& outMeshBatchList) {
        auto hashInstanceAllocated = [](const std::vector<uint32_t>& ids)->auto {
            size_t hash = 0;
            HashCombine(hash, ids.size());
            for (auto& id : ids) {
                HashCombine(hash, id);
            }
            return hash;
            };
        std::unordered_map<size_t, uint32_t> instanceIdMap;
        auto instanceDataBlock = scene->GetGPUResourceInfo().PrimitiveResourceInfo.LocalVertexFactoryInstanceInfo.InstanceBlock;
        for (auto& batch : outMeshBatchList) {
            auto iter = instanceIdMap.find(hashInstanceAllocated(batch.InstanceDataIds));
            if (iter == instanceIdMap.end()) {
                auto offset = instanceDataBlock->AddInstanceIds(batch.InstanceDataIds);
                batch.StartInstance = offset;
                batch.InstanceDataBufferAccessor = instanceDataBlock.get();
                batch.InstanceDataBufferSRV = instanceDataBlock->GetInstanceDataSRV();
            }
            else {
                batch.StartInstance = iter->second;
                batch.InstanceDataBufferAccessor = instanceDataBlock.get();
                batch.InstanceDataBufferSRV = instanceDataBlock->GetInstanceDataSRV();
            }
        }
    }


	void StaticMeshDrawBuild(Scene* scene, const Engine::SceneView& view, MeshBatchList& outDrawMeshList)
	{
        std::vector<Engine::StaticMeshProxy*> proxys;
		auto& staticMeshProxyInstanceId = scene->GetGPUResourceInfo().PrimitiveResourceInfo.LocalVertexFactoryInstanceInfo.StaticMeshProxyToInstanceId;
        auto primitives = scene->GatherVisiblePrimitives(view);
        for (auto primitive : primitives) {
            if (primitive->IsA<Engine::StaticMeshProxy>()) {
                proxys.push_back(static_cast<Engine::StaticMeshProxy*>(primitive));
            }
        }
        GMeshBatchElementMap.clear();

        for (auto* mesh : proxys)
        {
            if (!mesh)
            {
                continue;
            }

            auto instanceIter = staticMeshProxyInstanceId.find(mesh);
            if (instanceIter == staticMeshProxyInstanceId.end())
            {
                continue;
            }

            const uint32_t instanceId = instanceIter->second;

            auto staticMesh = mesh->GetStaticMeshComponent()->GetStaticMesh();
            auto renderData = staticMesh->GetRenderData().get();

            if (renderData->GetLODCount() == 0)
            {
                continue;
            }

            // TODO: 根据距离选择LOD
            const uint32_t lodIndex = 0;

            const LODResource& lod =
                renderData->GetLODResource(lodIndex);

            if (!lod.VertexFactory)
            {
                continue;
            }

            for (const SectionInfo& section : lod.Sections)
            {
                auto* material =
                    mesh->GetStaticMeshComponent()->GetMaterial(section.MaterialIndex);

                if (!material)
                {
                    continue;
                }

                //----------------------------------------
                // Batch Key
                //----------------------------------------

                MeshBatchKey batchKey;
                batchKey.VertexFactory = lod.VertexFactory.get();
                batchKey.MaterialProxy = material->GetRenderProxyShared();
                batchKey.IndexBuffer = lod.IndexBuffer.Buffer.get();
                batchKey.FrontFace = RHI::ERHIFrontFace::CounterClockwise;

                //----------------------------------------
                // Element Key
                //----------------------------------------

                MeshElementKey elementKey;
                elementKey.FirstIndex = section.FirstIndex;
                elementKey.NumIndices = section.NumIndices;
                elementKey.BaseVertexIndex = section.BaseVertexIndex;

                //----------------------------------------
                // Add Instance
                //----------------------------------------

                GMeshBatchElementMap[batchKey][elementKey].insert(instanceId);

				GTransformMap[instanceId] = mesh->GetLocalToWorld();
            }
        }
		BuildMeshBatchFromMap(GMeshBatchElementMap, outDrawMeshList);
        GMeshBatchElementMap.clear(); // Batches now retain the snapshots; do not keep the last frame globally.
		AllocateInstanceDataIds(scene, outDrawMeshList);
	}

    void AddStaticMeshDefferedShadingPass(RenderCore::RenderGraphBuilder& builder, StaticMeshMaterialDefferedShadingCSParameters* params, bool isIBLMode)
    {
        RHIComputePipelineStateDesc computePipelineDesc;
        auto defferedShaderType = ShaderType::GetRegisterMap()[ShaderType::EShaderTypeFlag::Material]["StaticMeshMaterialDefferedShadingCS"];
        MaterialShaderKey key;
        key.ShaderType = static_cast<MaterialShaderType*>(defferedShaderType); ;
        key.PermutationId = 0;
        std::string passName = "StaticMeshDefferedDrawPass";
        if (isIBLMode) {
            key.PermutationId = 1;
            passName = "StaticMeshDefferedDrawIBLPass";
        }
        key.MaterialParameter.ShadingModel = EShadingModel::Lit;
        auto cshader = GMaterialShaderMap.GetShader(key);
        computePipelineDesc.ComputeShader = dynamic_cast<RHI::RHIComputeShader*>(cshader->GetRHIShader());
        auto defferedDrawPipeline = RHIPipelineStateCache::GetOrCreateComputePipelineState(computePipelineDesc);
        builder.AddPass<StaticMeshMaterialDefferedShadingCSParameters>(
            passName,
            StaticMeshMaterialDefferedShadingCSParameters::GetMetaData(),
            params,
            EPassFlag::Compute,
            [=](RHI::RHICommandListBase& rhiCmdList)
            {
                auto& cmd = static_cast<RHI::RHIComputeCommandList&>(rhiCmdList);
                StaticMeshMaterialDefferedShadingCSParameters* materialParams = params;

                cmd.SetComputePipelineState(defferedDrawPipeline);

                // Bind shader parameters.
                SetShaderParameters(cmd, cshader, params);

                cmd.Dispatch(params->ScreenSize.X / 8, params->ScreenSize.Y / 8, 1);
            }
        );
    }



}