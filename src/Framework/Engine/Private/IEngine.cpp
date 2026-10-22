#include "IEngine.h"
#include <iostream>
#include "Module.h"
#include "RenderThread.h"
namespace Engine {


    IEngine::IEngine() {

    }
    IEngine::~IEngine() {

    }

    void IEngine::Init()
    {
        // 注册渲染/物理模块
    }

    void IEngine::Tick(float deltaTime)
    {
        // 额外游戏逻辑
    }

    void IEngine::Shutdown()
    {

    }




}