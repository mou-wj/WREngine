#pragma once 
#include "Window.h"
#include "SlateRenderer.h"
namespace SlateCore {

    class SLATECORE_API ApplicationBase
    {
    public:
        virtual ~ApplicationBase() = default;

        // 生命周期
        virtual bool Initialize() = 0;
        virtual bool RequestExit() = 0;
        virtual void Shutdown() = 0;


        // 帧同步
        virtual void TickFrame() = 0;   // 可以把 BeginFrame + EndFrame 合并为一个 TickFrame

        // 全局访问
        static ApplicationBase* GetApplication();
        static void SetApplication(ApplicationBase* inApplication);
    protected:
		SlateRenderer* _renderer = nullptr;
    };
}