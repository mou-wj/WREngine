#include "RenderThread.h"
#include "RHIApi.h"
#include <future>
#include "ThreadInfo.h"
using namespace Core;
namespace RenderCore {

RenderThread::RenderThread() : m_bRunning(false) {}

RenderThread::~RenderThread() {
    Stop();
}

void RenderThread::Start() {
    if (m_bRunning.load()) return;
    m_bRunning.store(true);
    m_WorkerThread = std::thread(&RenderThread::ThreadFunc, this);
}

void RenderThread::Stop() {
    if (!m_bRunning.load()) return;
    {
        std::lock_guard<std::mutex> lock(m_QueueMutex);
        m_bRunning.store(false);
        m_QueueCV.notify_all();
    }
    if (m_WorkerThread.joinable()) {
        m_WorkerThread.join();
    }
}

void RenderThread::EnqueueCommand(const RenderCommand& cmd) {
    {
        std::lock_guard<std::mutex> lock(m_QueueMutex);
        m_CommandQueue.push(cmd);
    }
    m_QueueCV.notify_one();
}

void RenderThread::ExecuteSync(const RenderCommand& cmd) {
    // Create a promise and its completion future.
    std::promise<void> promise;
    std::future<void> future = promise.get_future();

    // Wrap the command to signal completion after execution.
    RenderCommand wrappedCmd = cmd;
    auto originalExecute = cmd.Execute;

    wrappedCmd.Execute = [originalExecute, &promise]() {
        if (originalExecute) {
            originalExecute();
        }
        // Notify the waiting thread after rendering finishes.
        promise.set_value();
        };

    // Enqueue the wrapped command.
    EnqueueCommand(wrappedCmd);

    // Block the caller until the render command signals completion.
    future.wait();
}

void RenderThread::ThreadFunc() {
    InitRenderThreadId();
    while (m_bRunning.load() || !m_CommandQueue.empty()) {
        RenderCommand cmd("", nullptr);
        {
            std::unique_lock<std::mutex> lock(m_QueueMutex);
            m_QueueCV.wait(lock, [this] { return !m_CommandQueue.empty() || !m_bRunning.load(); });
            if (!m_CommandQueue.empty()) {
                cmd = std::move(m_CommandQueue.front());
                m_CommandQueue.pop();
            } else {
                continue;
            }
        }
        if (cmd.Execute) {
            cmd.Execute();
        }
        m_CmdFinishCV.notify_one();
    }
}

RenderThread Instance;
RenderThread RHIInstance;

bool StartRenderThread() 
{
    Instance.Start();
    RHIInstance.Start();
	return true;
}

bool StopRenderThread() 
{
    RHIInstance.Stop();
	Instance.Stop();
	return true;
}

void EnqueueRenderCommand(const RenderCommand& cmd)
{
    Instance.EnqueueCommand(cmd);
}

void EnqueueRenderCommand(const std::string& cmdName, const std::function<void()>& cmdFunc)
{
	RenderCommand namedCmd(cmdName, cmdFunc);
	EnqueueRenderCommand(namedCmd);
}

RENDERCORE_API void EnqueueRHICommand(const std::string& cmdName, const std::function<void()>& cmdFunc)
{
    RenderCommand namedCmd(cmdName, cmdFunc);
    RHIInstance.EnqueueCommand(namedCmd);
}

RENDERCORE_API void ExecuteSync(const RenderCommand& cmd)
{
    Instance.ExecuteSync(cmd);
}

RENDERCORE_API void ExecuteSync(const std::string& cmdName, const std::function<void()>& cmdFunc)
{
    RenderCommand namedCmd(cmdName, cmdFunc);
    ExecuteSync(namedCmd);
}



} // namespace RenderCore