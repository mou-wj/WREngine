#include "PrimitiveSceneProxy.h"
#include <cstring>

namespace Engine {

    PrimitiveSceneProxy::PrimitiveSceneProxy()
        : _primitiveId(0)
        , _bVisible(true)
        , _bCastShadow(true)
        , _bOpaque(true)
        , _renderFlags(0)
    {

    }

    PrimitiveSceneProxy::~PrimitiveSceneProxy() {
        // 注意：像 VertexBuffer、IndexBuffer、PrimitiveUniformBuffer
        // 它们的生命周期都遵循 RenderResource 系统的延迟释放（如你实现的 ReleaseRHIResource）。
        // Proxy 内部仅作为只读方持有指针，不需要、也不能在这里执行 delete。
    }

} // namespace Engine
