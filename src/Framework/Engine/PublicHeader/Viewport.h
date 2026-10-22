#pragma once
#include "RenderResource.h"
#include "EngineExport.h"
namespace RenderCore {
	class RenderTexture;
}
namespace Engine {
    class ViewportClient;
    class RenderTarget{
	public:
        virtual RenderCore::RenderTexture* GetRenderTarget() = 0;
    };

    // Engine/Viewport.h
    class ENGINE_API Viewport : public RenderTarget
    {
    public:
        Viewport(ViewportClient* inClient);
        virtual ~Viewport() = default;


        // 渲染入口（由 SceneViewport 实现）
        virtual void Draw() = 0;
        virtual int GetWidth() const = 0;
        virtual int GetHeight() const = 0;
    protected:
        ViewportClient* _client = nullptr;
        
    };
}