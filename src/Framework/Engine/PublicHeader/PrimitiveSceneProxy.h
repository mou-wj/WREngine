// PrimitiveSceneProxy.h
#pragma once
#include "EngineExport.h"
#include <cstdint>
#include <vector>
#include "BoxSphereBounds.h"
#include "Math.hpp"
#include "TypeIDCast.h"

// 前置声明底层及核心层的渲染资源
namespace RenderCore {
    class RenderBuffer;
    class VertexFactory;
}
namespace RHI {
    class RHIBuffer;
}

namespace Engine {
    struct SceneView;
    class MaterialRenderProxy;



    /*
    ===============================================================================
        PrimitiveSceneProxy (基类)
    ===============================================================================
    */
    class ENGINE_API PrimitiveSceneProxy {
    public:
        PrimitiveSceneProxy();
        virtual ~PrimitiveSceneProxy();

        int32_t GetPrimitiveId() const { return _primitiveId; }
        const Core::BoxSphereBounds& GetBounds() const { return _proxyBounds; }
        const Core::Mat4& GetLocalToWorld() const { return _localToWorld; }
        const Core::Mat4& GetWorldToLocal() const { return _worldToLocal; }

        virtual bool IsVisible() const { return _bVisible; }
        virtual bool CastsShadow() const { return _bCastShadow; }
        virtual bool IsOpaque() const { return _bOpaque; }

        virtual bool HasStaticGeometry() const = 0;
        virtual bool IsDynamic() const = 0;
        DECLARE_TYPE_ID_BASE_TYPE(PrimitiveSceneProxy)
    protected:
        int32_t _primitiveId;
        Core::BoxSphereBounds _proxyBounds;
        Core::Mat4 _localToWorld;
		Core::Mat4 _worldToLocal;

        // 常量数据资源
        void* _primitiveUniformBufferData = nullptr;

        uint32_t _bVisible : 1;
        uint32_t _bCastShadow : 1;
        uint32_t _bOpaque : 1;
        uint32_t _renderFlags : 29;
    };
} // namespace Engine