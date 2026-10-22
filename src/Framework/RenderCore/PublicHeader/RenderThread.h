#pragma once

#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <queue>
#include <mutex>
#include <condition_variable>
#include "RHICommandList.h"
#include "RHICommandContex.h"
namespace RenderCore {

// 渲染命令结构体，包含名称和执行lambda
struct RENDERCORE_API RenderCommand
{
    std::string Name;
    std::function<void()> Execute;

    RenderCommand(const std::string& inName, std::function<void()> inFunc)
        : Name(inName), Execute(std::move(inFunc)) {}
};

RENDERCORE_API bool StartRenderThread();

RENDERCORE_API bool StopRenderThread();

RENDERCORE_API void EnqueueRenderCommand(const RenderCommand& cmd);

RENDERCORE_API void EnqueueRenderCommand(const std::string& cmdName, const std::function<void()>& cmdFunc);

RENDERCORE_API void EnqueueRHICommand(const std::string& cmdName, const std::function<void()>& cmdFunc);

RENDERCORE_API void ExecuteSync(const RenderCommand& cmd);

RENDERCORE_API void ExecuteSync(const std::string& cmdName, const std::function<void()>& cmdFunc);


// 简化版渲染线程管理类
class RENDERCORE_API RenderThread
{
public:
    RenderThread();
    ~RenderThread();

    void Start();
    void Stop();

    // 提交渲染命令到渲染线程
    void EnqueueCommand(const RenderCommand& cmd);

    void ExecuteSync(const RenderCommand& cmd);

    bool IsRunning() const { return m_bRunning.load(); }


private:
    void ThreadFunc();

    std::thread m_WorkerThread;
    std::atomic<bool> m_bRunning{ false };
    std::queue<RenderCommand> m_CommandQueue;
    std::mutex m_QueueMutex;
    std::condition_variable m_QueueCV;
    std::condition_variable m_CmdFinishCV;
};



} // namespace RenderCore