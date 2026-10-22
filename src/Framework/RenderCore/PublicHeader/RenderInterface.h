#pragma once

#include <string>
#include <vector>
#include "Module.h"
#include "SceneView.h"

namespace RenderCore {
class RenderTexture;
}

namespace Engine {
class PrimitiveComponent;
}

namespace RenderCore {
class RENDERCORE_API RenderInterface : public Core::Module
{
public:
    virtual ~RenderInterface() = default;
    virtual void BeginRender(Engine::SceneViewFamily* sceneViewCollection) = 0;
    virtual Engine::SceneInterface* AllocateScene() = 0;
    virtual void PreComputeIBL(RenderTexture* inHdrTexture,RenderTexture* outDiffuseIbl,RenderTexture* outSpecularIbl) = 0;
    
private:


};


}