#include "OpenGLQueue.h"
#include "OpenGLContext.h"
#include "glad/gl.h"
#include "OpenGLPlatformSurport.h"
#include "OpenGLRenderTarget.h"
#include <algorithm>

namespace RHIOpenGL
{
    OpenGLSyncPoint::OpenGLSyncPoint(bool isSwapchainFence, RHI::EQueueType queueType)
        : m_bIsSwapchainFence(isSwapchainFence)
    {
        _type = queueType;
    }

    OpenGLSyncPoint::~OpenGLSyncPoint() = default;

    uint64_t OpenGLSyncPoint::GetCurrentValue() {
        return m_CurrentValue.load(std::memory_order_acquire);
    }

    void OpenGLSyncPoint::SetCurrentValue(uint64_t value)
    {
        // 只前进不后退：worker 按 FIFO 完成时天然单调，这里再兜一层 max，
        // 防止 swapchain fence 等特殊路径被误回拨。
        uint64_t current = m_CurrentValue.load(std::memory_order_relaxed);
        if (value <= current)
        {
            return;
        }

        {
            std::lock_guard<std::mutex> lock(m_ValueMutex);
            current = m_CurrentValue.load(std::memory_order_relaxed);
            if (value > current)
            {
                m_CurrentValue.store(value, std::memory_order_release);
            }
        }
        m_ValueCond.notify_all();
    }

    void OpenGLSyncPoint::Wait(uint64_t value, uint64_t timeoutNs)
    {
        // 已达成直接返回，不加锁（原子读）。
        if (value <= m_CurrentValue.load(std::memory_order_acquire))
        {
            return;
        }

        std::unique_lock<std::mutex> lock(m_ValueMutex);
        if (timeoutNs == UINT64_MAX)
        {
            m_ValueCond.wait(lock, [&]() { return value <= m_CurrentValue.load(std::memory_order_acquire); });
            return;
        }

        // 限时等待：以稳定时钟换算截止时间（旧实现的"循环次数比纳秒"是伪超时）。
        const auto deadline = std::chrono::steady_clock::now()
            + std::chrono::nanoseconds(static_cast<std::chrono::nanoseconds::rep>(timeoutNs));
        m_ValueCond.wait_until(lock, deadline,
            [&]() { return value <= m_CurrentValue.load(std::memory_order_acquire); });
    }


    OpenGLSwapchain::OpenGLSwapchain(void* inWindowHandle, uint32_t width, uint32_t height, RHI::ERHIFormat format)
        : m_WindowHandle(inWindowHandle)
        , m_Width(width)
        , m_Height(height)
        , m_Format(format)
    {
        RHI::RHITextureDesc desc{};
        desc.Width = width;
        desc.Height = height;
        desc.Depth = 1;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = format;
        desc.Type = RHI::ERHITextureType::Texture2D;
        desc.Usage = RHI::ERHITextureCreateFlag::RenderTarget | RHI::ERHITextureCreateFlag::Presentable;
        m_BackBufferTextures.resize(3);
        m_BackBufferFences.resize(3);
        for (int i = 0; i < 3; i++)
        {
            m_BackBufferTextures[i] = std::make_shared<OpenGLTexture>(desc);
			m_BackBufferFences[i] = new OpenGLSyncPoint(true);
        }
		m_PlatformContext = CreateOpenGLPlatformContext(m_WindowHandle,desc.Format);
        EnqueueTaskAndWait([this]()
            {
                m_PlatformContext->Initialize();
            });
        
        m_Framebuffer = new OpenGLFramebuffer();
        m_Framebuffer->Create();
    }

    OpenGLSwapchain::~OpenGLSwapchain()
    {
        m_BackBufferTextures.clear();
		if (m_PlatformContext)
		{
			delete m_PlatformContext;
			m_PlatformContext = nullptr;
		}
        if (m_Framebuffer) {
            delete m_Framebuffer;
        }
    }

    RHI::RHISwapchain::RHISwapchainSlot OpenGLSwapchain::AcquireNextSlot()
    {
        RHISwapchainSlot slot{};
		slot.ReadySync = m_BackBufferFences[m_CurrentBackBufferIndex];
        slot.Texture = m_BackBufferTextures[m_CurrentBackBufferIndex].get();
        return slot;
    }

    void OpenGLSwapchain::Resize(uint32_t width, uint32_t height)
    {
        m_Width = width;
        m_Height = height;

        for (auto& texture : m_BackBufferTextures) {
            RHI::RHITextureDesc desc = texture->GetDesc();
            desc.Width = width;
            desc.Height = height;
            desc.Depth = 1;
            texture = std::make_shared<OpenGLTexture>(desc);
        }
    }
    void OpenGLSwapchain::Present() {
        
        if (m_CurrentBackBufferIndex > m_BackBufferTextures.size()) {
            return;
        }
        auto& texture = m_BackBufferTextures[m_CurrentBackBufferIndex];

		bool makeCurrent = m_PlatformContext->MakeCurrent();
        
		m_Framebuffer->Bind();

        m_Framebuffer->AttachColorTexture(texture->GetHandle(),0, GL_TEXTURE_2D);

		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glClearColor(0.2f, 0.3f, 0.4f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
		glBlitFramebuffer(0, 0, m_Width, m_Height, 0, 0, m_Width, m_Height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
		m_PlatformContext->SwapBuffers();
        
		m_CurrentBackBufferIndex = (m_CurrentBackBufferIndex + 1) % m_BackBufferTextures.size();
		m_PlatformContext->DoneCurrent();
        
    }
    OpenGLQueue::OpenGLQueue(RHI::EQueueType type)
        : m_QueueType(type)
    {
        m_SyncPoint = new OpenGLSyncPoint(false, type);
    }

    OpenGLQueue::~OpenGLQueue()
    {
        std::lock_guard<std::mutex> lock(m_ContextPoolMutex);
        for (auto* context : m_PendingContexts)
        {
            delete context;
        }
        for (auto* context : m_FreeContexts)
        {
            delete context;
        }
        for (auto* context : m_InFlightContexts)
        {
            delete context;
        }
        m_PendingContexts.clear();
        m_FreeContexts.clear();
        m_InFlightContexts.clear();

        delete m_SyncPoint;
        m_SyncPoint = nullptr;
    }

    RHI::EQueueType OpenGLQueue::GetType() const
    {
        return m_QueueType;
    }

    RHI::RHIContextBase* OpenGLQueue::AcquireCommandContext()
    {
        std::lock_guard<std::mutex> lock(m_ContextPoolMutex);
        if (!m_FreeContexts.empty())
        {
            RHI::RHIContextBase* context = m_FreeContexts.back();
            m_FreeContexts.pop_back();
            return context;
        }

        if (m_QueueType == RHI::EQueueType::Compute)
        {
            return new OpenGLComputeContext(this);
        }

        return new OpenGLGraphicContext(this);
    }

    RHI::RHIContextBase* OpenGLQueue::ReleaseCommandContext(RHI::RHIContextBase* context)
    {
        if (!context)
        {
            return nullptr;
        }

        std::lock_guard<std::mutex> lock(m_ContextPoolMutex);

        // 在飞守卫：worker 还没执行完该 ctx 时，所有权在 worker 手里。
        // 主线程提前 Release（Execute→WaitIdle→Release 模式里的时序误差）
        // 直接返回即可——worker 会在 FinishContextExecution 里归还，
        // 不会重复入池，也不会在执行期间被 Acquire 拿回去重录。
        if (m_InFlightContexts.find(context) != m_InFlightContexts.end())
        {
            return nullptr;
        }

        auto pendingIt = std::find(m_PendingContexts.begin(), m_PendingContexts.end(), context);
        if (pendingIt != m_PendingContexts.end())
        {
            m_PendingContexts.erase(pendingIt);
        }

        auto freeIt = std::find(m_FreeContexts.begin(), m_FreeContexts.end(), context);
        if (freeIt == m_FreeContexts.end())
        {
            m_FreeContexts.push_back(context);
        }

        return nullptr;
    }

    uint64_t OpenGLQueue::ExecuteContext(RHI::RHIContextBase* context)
    {
        return ExecuteContext(context ? std::vector<RHI::RHIContextBase*>{ context } : std::vector<RHI::RHIContextBase*>{}, {});
    }

    uint64_t OpenGLQueue::ExecuteContext(const std::vector<RHI::RHIContextBase*>& cmds, const std::vector<RHI::RHIWaitInfo>& waitInfos)
    {
        if (cmds.empty())
        {
            return GetCurrentTimelineValue();
        }

        {
            std::lock_guard<std::mutex> lock(m_ContextPoolMutex);
            m_PendingContexts.insert(m_PendingContexts.end(), cmds.begin(), cmds.end());
            m_InFlightContexts.insert(cmds.begin(), cmds.end());
        }

        QueueExecuteEntry entry;
        entry.Contexts = cmds;
        entry.WaitInfos = waitInfos;
        entry.FenceValue = AllocateNextFence();

        {
            std::lock_guard<std::mutex> lock(m_ExecuteQueueMutex);
            m_ExecuteQueueIns.push(entry);
        }

        return entry.FenceValue;
    }

    void OpenGLQueue::WaitValue(uint64_t fenceValue)
    {
        if (m_SyncPoint)
        {
            m_SyncPoint->Wait(fenceValue);
        }
    }

    void OpenGLQueue::WaitIdle()
    {
        if (!m_SyncPoint)
        {
            return;
        }

        // FIFO + 单调唯一 fence：等待最新已分配的 fence，即代表
        // 所有已提交 entry（含 Present）都已在 worker 上执行完毕。
        m_SyncPoint->Wait(m_LastSubmittedFence.load(std::memory_order_acquire));
    }

    uint64_t OpenGLQueue::AllocateNextFence()
    {
        return m_LastSubmittedFence.fetch_add(1, std::memory_order_acq_rel) + 1;
    }

    uint64_t OpenGLQueue::GetCurrentTimelineValue()
    {
        return m_SyncPoint ? m_SyncPoint->GetCurrentValue() : 0;
    }

    RHI::RHISyncPoint* OpenGLQueue::GetSyncPoint()
    {
        return m_SyncPoint;
    }

    ExecuteQueue& OpenGLQueue::GetExecuteQueue()
    {
        return m_ExecuteQueueIns;
    }

    void OpenGLQueue::FinishContextExecution(RHI::RHIContextBase* context)
    {
        if (!context)
        {
            return;
        }

        std::lock_guard<std::mutex> lock(m_ContextPoolMutex);
        m_InFlightContexts.erase(context);

        auto pendingIt = std::find(m_PendingContexts.begin(), m_PendingContexts.end(), context);
        if (pendingIt != m_PendingContexts.end())
        {
            m_PendingContexts.erase(pendingIt);
        }

        auto freeIt = std::find(m_FreeContexts.begin(), m_FreeContexts.end(), context);
        if (freeIt == m_FreeContexts.end())
        {
            m_FreeContexts.push_back(context);
        }
    }

    std::mutex& OpenGLQueue::GetExecuteQueueMutex()
    {
        return m_ExecuteQueueMutex;
    }

    bool OpenGLQueue::HasPendingExecute() const
    {
        std::lock_guard<std::mutex> lock(m_ExecuteQueueMutex);
        return !m_ExecuteQueueIns.empty();
    }

    void OpenGLQueueManager::Present(RHI::RHISwapchain* swapchain, const RHI::RHIWaitInfo& waitInfo)
    {
        if (!swapchain)
        {
            return;
        }

        OpenGLQueue* queue = GetQueue(RHI::EQueueType::Graphics);
        QueueExecuteEntry entry;
        entry.WaitInfos = { waitInfo };
        entry.FenceValue = queue->AllocateNextFence();
        entry.ExecuteFunction = [swapchain]()
        {
            auto* glSwapchain = dynamic_cast<OpenGLSwapchain*>(swapchain);
            if (glSwapchain)
            {
                
                glSwapchain->Present();
                
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                glViewport(0, 0, glSwapchain->GetWidth(), glSwapchain->GetHeight());
				
                // Present 内部会切到 swapchain 上下文并 DoneCurrent 收尾，
                // worker 需要恢复自己的常驻默认上下文，供后续命令使用。
                MakeDefaultContextCurrent();
            }
        };

        {
            std::lock_guard<std::mutex> lock(queue->GetExecuteQueueMutex());
            queue->GetExecuteQueue().push(entry);
        }
    }

    OpenGLQueueManager::OpenGLQueueManager()
    {

    }

    OpenGLQueueManager::~OpenGLQueueManager()
    {

    }

    void OpenGLQueueManager::StartWorker()
    {
        if (m_WorkerThread.joinable())
        {
            return;
        }

        m_bStopWorker = false;
        m_WorkerThread = std::thread(&OpenGLQueueManager::WorkerLoop, this);
        m_WorkerThreadId = m_WorkerThread.get_id();
    }

    void OpenGLQueueManager::StopWorker()
    {
        m_bStopWorker = true;
        m_WorkerCond.notify_all();
        if (m_WorkerThread.joinable())
        {
            m_WorkerThread.join();
        }
        m_WorkerThreadId = std::thread::id{};
    }

    bool OpenGLQueueManager::IsWorkerThread() const
    {
        return m_WorkerThread.joinable() && std::this_thread::get_id() == m_WorkerThreadId;
    }

    void OpenGLQueueManager::Init() {
        // worker 接管默认（bootstrap）上下文，作为其常驻 current context
        m_bInit = InitializeWorkerPlatformContext();
    }
    void OpenGLQueueManager::WorkerLoop()
    {
        while (!m_bStopWorker)
        {
            if (!m_bInit) {
                Init();
            }

            bool executed = false;
            CheckError();
            // 先排空通用任务队列（主线程投递的 GL 任务），再执行命令队列
            executed |= DrainTasks();
            executed |= TryExecuteQueue(m_GraphicsQueue);
            executed |= TryExecuteQueue(m_ComputeQueue);

            if (executed)
            {
                continue;
            }

            std::unique_lock<std::mutex> lock(m_WorkerMutex);
            m_WorkerCond.wait_for(lock, std::chrono::milliseconds(1));
        }
    }

    void OpenGLQueueManager::EnqueueTask(std::function<void()> task)
    {
        if (!task)
        {
            return;
        }

        // worker 自己提交任务（命令 lambda 内部的转发调用）时直接内联执行，
        // 否则会绕一圈排队、甚至与 EnqueueTaskAndWait 组合成自等死锁；
        // worker 未启动（初始化前/关闭后）也只能就地执行。
        if (IsWorkerThread() || !m_WorkerThread.joinable())
        {
            task();
            return;
        }

        {
            std::lock_guard<std::mutex> lock(m_TaskQueueMutex);
            m_TaskQueue.push(std::move(task));
        }
        m_WorkerCond.notify_all();
    }

    void OpenGLQueueManager::EnqueueTaskAndWait(std::function<void()> task)
    {
        if (!task)
        {
            return;
        }

        // 同上：worker 线程内调用必须内联，否则自己等自己，永久死锁。
        if (IsWorkerThread() || !m_WorkerThread.joinable())
        {
            task();
            return;
        }

        auto promise = std::make_shared<std::promise<void>>();
        std::future<void> future = promise->get_future();
        {
            std::lock_guard<std::mutex> lock(m_TaskQueueMutex);
            m_TaskQueue.push([capturedTask = std::move(task), promise]()
            {
                try
                {
                    capturedTask();
                    promise->set_value();
                }
                catch (...)
                {
                    // 把异常转递给等待方，避免 future 永远不就绪造成挂死
                    promise->set_exception(std::current_exception());
                }
            });
        }
        m_WorkerCond.notify_all();
        future.wait();
    }

    bool OpenGLQueueManager::HasPendingTasks() const
    {
        std::lock_guard<std::mutex> lock(m_TaskQueueMutex);
        return !m_TaskQueue.empty();
    }

    bool OpenGLQueueManager::DrainTasks()
    {
        bool executed = false;
        while (true)
        {
            std::function<void()> task;
            {
                std::lock_guard<std::mutex> lock(m_TaskQueueMutex);
                if (m_TaskQueue.empty())
                {
                    break;
                }
                task = std::move(m_TaskQueue.front());
                m_TaskQueue.pop();
            }

            if (task)
            {
                task();
                executed = true;
            }
        }
        return executed;
    }

    bool OpenGLQueueManager::TryExecuteQueue(OpenGLQueue* queue)
    {
        if (!queue)
        {
            return false;
        }
		
        QueueExecuteEntry entry{};
        {
            std::lock_guard<std::mutex> lock(queue->GetExecuteQueueMutex());
            if (queue->GetExecuteQueue().empty())
            {
                return false;
            }

            entry = queue->GetExecuteQueue().front();
        }

        bool canExecute = true;
        bool waitsSameQueue = false;
        for (const auto& waitInfo : entry.WaitInfos)
        {
            const uint64_t dependencyValue = waitInfo.SyncPoint ? waitInfo.SyncPoint->GetCurrentValue() : 0;
            const bool isSameQueueDependency = waitInfo.SyncPoint ? waitInfo.SyncPoint->GetQueueType() == queue->GetType() : waitInfo.QueueType == queue->GetType();
            if (isSameQueueDependency)
            {
                waitsSameQueue = true;
            }

            if (waitInfo.SyncPoint)
            {
                const uint64_t requiredValue = waitInfo.Value;
                if (dependencyValue < requiredValue)
                {
                    canExecute = false;
                }
            }
            else if (waitInfo.QueueType != queue->GetType())
            {
                RHI::RHISyncPoint* otherSync = OpenGLQueueManager::GetInstance().GetQueue(waitInfo.QueueType)->GetSyncPoint();
                if (otherSync && otherSync->GetCurrentValue() < waitInfo.Value)
                {
                    canExecute = false;
                }
            }
            else if (waitInfo.Value > queue->GetCurrentTimelineValue())
            {
                canExecute = false;
            }
        }

        if (!canExecute)
        {
            if (waitsSameQueue)
            {
                // 同队列等待后续命令，说明存在循环依赖，直接报告错误。
                if (queue->GetType() == RHI::EQueueType::Graphics)
                {
                    std::fprintf(stderr, "OpenGLQueueManager: dependency deadlock detected while waiting for a later command in the same graphics queue.\n");
                }
                else
                {
                    std::fprintf(stderr, "OpenGLQueueManager: dependency deadlock detected while waiting for a later command in the same compute queue.\n");
                }
            }
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(queue->GetExecuteQueueMutex());
            if (queue->GetExecuteQueue().empty())
            {
                return false;
            }
            queue->GetExecuteQueue().pop();
        }

        if (entry.ExecuteFunction)
        {
            entry.ExecuteFunction();
        }
        else
        {
            for (auto* context : entry.Contexts)
            {
                if (!context)
                {
                    continue;
                }

                auto* glContext = dynamic_cast<OpenGLCommandContext*>(context);
                if (glContext)
                {
                    for (const auto& command : glContext->GetNativeCommands())
                    {
                        if (command)
                        {
                            command();
                            //CheckError();
                        }
                    }
                }
            }
        }

        for (auto* context : entry.Contexts)
        {
            if (queue)
            {
                queue->FinishContextExecution(context);
            }
        }

        if (queue->GetSyncPoint())
        {
            dynamic_cast<OpenGLSyncPoint*>(queue->GetSyncPoint())->SetCurrentValue(entry.FenceValue);
        }

        return true;
    }

    // ---- 自由函数快捷接口：转发到 OpenGLQueueManager 对应成员 ----
    // 注意：与成员函数同名，实现里必须用 OpenGLQueueManager:: 限定，
    // 否则会被解析成对自身的（无限递归）调用。
    void EnqueueTask(std::function<void()> task)
    {
        OpenGLQueueManager::GetInstance().EnqueueTask(std::move(task));
    }

    void EnqueueTaskAndWait(std::function<void()> task)
    {
        OpenGLQueueManager::GetInstance().EnqueueTaskAndWait(std::move(task));
    }
}
