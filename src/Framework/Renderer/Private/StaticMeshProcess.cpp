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
    Engine::MaterialRenderProxy* MaterialProxy;
    RenderCore::RenderBuffer* IndexBuffer;
    RHI::ERHIFrontFace FrontFace;
    bool operator==(const MeshBatchKey& Other) const
    {
        return VertexFactory == Other.VertexFactory &&
            MaterialProxy == Other.MaterialProxy &&
            IndexBuffer == Other.IndexBuffer &&
            FrontFace == Other.FrontFace;
    }
};

struct MeshElementKey {
    // DrawRange
    uint32_t FirstIndex;
    uint32_t NumIndices;
    int32_t BaseVertexIndex;
    bool operator==(const MeshElementKey& Other) const
    {
        return FirstIndex == Other.FirstIndex &&
            NumIndices == Other.NumIndices &&
            BaseVertexIndex == Other.BaseVertexIndex;
    }
};


namespace std
{

    template<>
    struct hash<MeshBatchKey>
    {
        size_t operator()(const MeshBatchKey& Key) const noexcept
        {
            size_t Hash = 0;
            HashCombine(Hash, Key.VertexFactory);
            HashCombine(Hash, Key.MaterialProxy);
            HashCombine(Hash, Key.IndexBuffer);
            HashCombine(Hash, static_cast<uint32_t>(Key.FrontFace));
            return Hash;
        }
    };

    template<>
    struct hash<MeshElementKey>
    {
        size_t operator()(const MeshElementKey& Key) const noexcept
        {
            size_t Hash = 0;
            HashCombine(Hash, Key.FirstIndex);
            HashCombine(Hash, Key.NumIndices);
            HashCombine(Hash, Key.BaseVertexIndex);
            return Hash;
        }
    };

}
using namespace Engine;
namespace Renderer {

    

    std::unordered_map<MeshBatchKey,std::unordered_map<MeshElementKey,std::set<uint32_t>>>  GMeshBatchElementMap;
    std::unordered_map<size_t,Core::Mat4> GTransformMap;

    bool IsSameInstanceSet(
        const std::vector<uint32_t>& Lhs,
        const std::set<uint32_t>& Rhs)
    {
        if (Lhs.size() != Rhs.size())
        {
            return false;
        }

        auto Iter = Rhs.begin();

        for (uint32_t Id : Lhs)
        {
            if (Id != *Iter)
            {
                return false;
            }

            ++Iter;
        }

        return true;
    }
    void BuildMeshBatchFromMap(
        const std::unordered_map<
        MeshBatchKey,
        std::unordered_map<MeshElementKey, std::set<uint32_t>>>& MeshBatchElementMap,
        MeshBatchList& OutMeshBatchList)
    {
        OutMeshBatchList.clear();

        for (const auto& BatchPair : MeshBatchElementMap)
        {
            const MeshBatchKey& BatchKey = BatchPair.first;
            const auto& ElementMap = BatchPair.second;

            MeshBatch Batch;

            //----------------------------------------
            // Batch State
            //----------------------------------------

            Batch.VertexFactory = BatchKey.VertexFactory;
            Batch.MaterialProxy = BatchKey.MaterialProxy;
            Batch.IndexBuffer = BatchKey.IndexBuffer;
            Batch.FrontFace = BatchKey.FrontFace;

            //----------------------------------------
            // Build Elements
            //----------------------------------------

            bool bFirstElement = true;

            for (const auto& ElementPair : ElementMap)
            {
                const MeshElementKey& ElementKey = ElementPair.first;
                const std::set<uint32_t>& InstanceIds = ElementPair.second;

                MeshBatchElement& Element =
                    Batch.Elements.emplace_back();

                Element.FirstIndex = ElementKey.FirstIndex;
                Element.NumIndices = ElementKey.NumIndices;
                Element.BaseVertexIndex = ElementKey.BaseVertexIndex;

                if (bFirstElement)
                {
                    Batch.InstanceDataIds.assign(
                        InstanceIds.begin(),
                        InstanceIds.end());

                    bFirstElement = false;
					Batch.LocalToWorld = GTransformMap[*InstanceIds.begin()];
                }
                else
                {
                    assert(IsSameInstanceSet(
                        Batch.InstanceDataIds,
                        InstanceIds));
                }
            }

            OutMeshBatchList.emplace_back(std::move(Batch));
        }
    }

    void AllocateInstanceDataIds(Scene* scene, MeshBatchList& OutMeshBatchList) {
        auto hashInstanceAllocated = [](const std::vector<uint32_t>& ids)->auto {
            size_t hash = 0;
            HashCombine(hash, ids.size());
            for (auto& id : ids) {
                HashCombine(hash, id);
            }
            return hash;
            };
        std::unordered_map<size_t, uint32_t> InstanceIdMap;
        auto instanceDataBlock = scene->GetGPUResourceInfo().PrimitiveResourceInfo.localvertexfactoryinstanceInfo.InstanceBlock;
        for (auto& Batch : OutMeshBatchList) {
            auto iter = InstanceIdMap.find(hashInstanceAllocated(Batch.InstanceDataIds));
            if (iter == InstanceIdMap.end()) {
                auto offset = instanceDataBlock->AddInstanceIds(Batch.InstanceDataIds);
                Batch.StartInstance = offset;
                Batch.InstanceDataBufferAccessor = instanceDataBlock.get();
                Batch.InstanceDataBufferSRV = instanceDataBlock->GetInstanceDataSRV();
            }
            else {
                Batch.StartInstance = iter->second;
                Batch.InstanceDataBufferAccessor = instanceDataBlock.get();
                Batch.InstanceDataBufferSRV = instanceDataBlock->GetInstanceDataSRV();
            }
        }
    }


	void StaticMeshDrawBuild(Scene* scene, const Engine::SceneView& view, MeshBatchList& outDrawMeshList)
	{
        std::vector<Engine::StaticMeshProxy*> proxys;
		auto& StaticMeshProxyInstanceId = scene->GetGPUResourceInfo().PrimitiveResourceInfo.localvertexfactoryinstanceInfo.StaticMeshProxyToInstanceId;
        auto primitives = scene->GatherVisiblePrimitives(view);
        for (auto primitive : primitives) {
            if (primitive->IsA<Engine::StaticMeshProxy>()) {
                proxys.push_back(static_cast<Engine::StaticMeshProxy*>(primitive));
            }
        }
        GMeshBatchElementMap.clear();

        for (auto* Mesh : proxys)
        {
            if (!Mesh)
            {
                continue;
            }

            auto InstanceIter = StaticMeshProxyInstanceId.find(Mesh);
            if (InstanceIter == StaticMeshProxyInstanceId.end())
            {
                continue;
            }

            const uint32_t InstanceId = InstanceIter->second;

            auto StaticMesh = Mesh->GetStaticMeshComponent()->GetStaticMesh();
            auto RenderData = StaticMesh->GetRenderData().get();

            if (RenderData->GetLODCount() == 0)
            {
                continue;
            }

            // TODO: 根据距离选择LOD
            const uint32_t LODIndex = 0;

            const LODResource& LOD =
                RenderData->GetLODResource(LODIndex);

            if (!LOD.VertexFactory)
            {
                continue;
            }

            for (const SectionInfo& Section : LOD.Sections)
            {
                auto* Material =
                    Mesh->GetStaticMeshComponent()->GetMaterial(Section.MaterialIndex);

                if (!Material)
                {
                    continue;
                }

                //----------------------------------------
                // Batch Key
                //----------------------------------------

                MeshBatchKey BatchKey;
                BatchKey.VertexFactory = LOD.VertexFactory.get();
                BatchKey.MaterialProxy = Material->GetRenderProxy();
                BatchKey.IndexBuffer = LOD.IndexBuffer.Buffer.get();
                BatchKey.FrontFace = RHI::ERHIFrontFace::CounterClockwise;

                //----------------------------------------
                // Element Key
                //----------------------------------------

                MeshElementKey ElementKey;
                ElementKey.FirstIndex = Section.FirstIndex;
                ElementKey.NumIndices = Section.NumIndices;
                ElementKey.BaseVertexIndex = Section.BaseVertexIndex;

                //----------------------------------------
                // Add Instance
                //----------------------------------------

                GMeshBatchElementMap[BatchKey][ElementKey].insert(InstanceId);

				GTransformMap[InstanceId] = Mesh->GetLocalToWorld();
            }
        }
		BuildMeshBatchFromMap(GMeshBatchElementMap, outDrawMeshList);
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
            [=](RHI::RHICommandListBase& RHICmdList)
            {
                auto& cmd = static_cast<RHI::RHIComputeCommandList&>(RHICmdList);
                StaticMeshMaterialDefferedShadingCSParameters* materialParams = params;

                cmd.SetComputePipelineState(defferedDrawPipeline);

                //����pixel����
                SetShaderParameters(cmd, cshader, params);

                cmd.Dispatch(params->ScreenSize.X / 8, params->ScreenSize.Y / 8, 1);
            }
        );
    }



}