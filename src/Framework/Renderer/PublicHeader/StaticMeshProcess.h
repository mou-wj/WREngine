#pragma once
#include "SceneView.h"
#include "RenderGraphBuilder.h"
#include "StaticMesh.h"
#include "RenderResource.h"
#include "StaticMeshProxy.h"
namespace Engine {
    class InstanceIdBufferDefferedAccessor;
}

namespace Renderer {
    class Scene;

    /*
===============================================================================
    MeshBatchElement (完全基于指针的原子绘制单元)
===============================================================================
*/
    struct MeshBatchElement {

        // 几何绘制区间控制
        uint32_t FirstIndex = 0;
        uint32_t NumIndices = 0;
        int32_t BaseVertexIndex = 0;
        
        
    };

    using MeshBatchElementList = std::vector<MeshBatchElement>;

    /*
    ===============================================================================
        MeshBatch (状态一致的批次集合)
    ===============================================================================
    */
    struct MeshBatch {
        // 内嵌的元素数组
        std::vector<MeshBatchElement> Elements;

        // 核心渲染状态（一个批次内必须严格一致，触发同一个 PSO）
        RenderCore::VertexFactory* VertexFactory = nullptr;
        Engine::MaterialRenderProxy * MaterialProxy = nullptr;

        // 几何数据源指针挪到了 Batch 状态层，完美适配你的 RenderCore 资源
        RenderCore::RenderBuffer* IndexBuffer = nullptr;

        // 管线状态标志位
        RHI::ERHIFrontFace FrontFace = RHI::ERHIFrontFace::CounterClockwise;
        uint32_t StartInstance = 0;
        std::vector<uint32_t> InstanceDataIds;
		RHI::RHIShaderResourceView* InstanceDataBufferSRV = nullptr;
        Engine::InstanceIdBufferDefferedAccessor* InstanceDataBufferAccessor = nullptr;
        Core::Mat4 LocalToWorld;

        MeshBatch()
        {

        }
    };

    using MeshBatchList = std::vector<MeshBatch>;

    RENDERER_API void StaticMeshDrawBuild(Scene* scene,const Engine::SceneView& view, MeshBatchList& outDrawMeshList);

    struct StaticMeshMaterialDefferedShadingCSParameters;
    RENDERER_API void AddStaticMeshDefferedShadingPass(RenderCore::RenderGraphBuilder& builder, StaticMeshMaterialDefferedShadingCSParameters* params,bool isIBLMode);
}