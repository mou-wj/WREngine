#include "OpenGLQueue.h"
#include "OpenGLContext.h"
#include "glad/gl.h"
#include "OpenGLPlatformSurport.h"
#include "OpenGLRenderTarget.h"
#include <algorithm>

namespace RHIOpenGL
{
    OpenGLSyncPoint::OpenGLSyncPoint(bool isSwapchainFence, RHI::EQueueType queueType)
        : bIsSwapchainFence(isSwapchainFence)
    {
        _type = queueType;
    }

    OpenGLSyncPoint::~OpenGLSyncPoint() = default;

    uint64_t OpenGLSyncPoint::GetCurrentValue() {
        return CurrentValue.load(std::memory_order_acquire);
    }

    void OpenGLSyncPoint::SetCurrentValue(uint64_t Value)
    {
        // 只前进不后退：worker 按 FIFO 完成时天然单调，这里再兜一层 max，
        // 防止 swapchain fence 等特殊路径被误回拨。
        uint64_t current = CurrentValue.load(std::memory_order_relaxed);
        if (Value <= current)
        {
            return;
        }

        {
            std::lock_guard<std::mutex> lock(ValueMutex);
            current = CurrentValue.load(std::memory_order_relaxed);
            if (Value > current)
            {
                CurrentValue.store(Value, std::memory_order_release);
            }
        }
        ValueCond.notify_all();
    }

    void OpenGLSyncPoint::Wait(uint64_t Value, uint64_t TimeoutNS)
    {
        // 已达成直接返回，不加锁（原子读）。
        if (Value <= CurrentValue.load(std::memory_order_acquire))
        {
            return;
        }

        std::unique_lock<std::mutex> lock(ValueMutex);
        if (TimeoutNS == UINT64_MAX)
        {
            ValueCond.wait(lock, [&]() { return Value <= CurrentValue.load(std::memory_order_acquire); });
            return;
        }

        // 限时等待：以稳定时钟换算截止时间（旧实现的"循环次数比纳秒"是伪超时）。
        const auto deadline = std::chrono::steady_clock::now()
            + std::chrono::nanoseconds(static_cast<std::chrono::nanoseconds::rep>(TimeoutNS));
        ValueCond.wait_until(lock, deadline,
            [&]() { return Value <= CurrentValue.load(std::memory_order_acquire); });
    }


    OpenGLSwapchain::OpenGLSwapchain(void* inWindowHandle, uint32_t width, uint32_t height, RHI::ERHIFormat format)
        : WindowHandle(inWindowHandle)
        , Width(width)
        , Height(height)
        , Format(format)
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
        BackBufferTextures.resize(3);
        BackBufferFences.resize(3);
        for (int i = 0; i < 3; i++)
        {
            BackBufferTextures[i] = std::make_shared<OpenGLTexture>(desc);
			BackBufferFences[i] = new OpenGLSyncPoint(true);
        }
		PlatformContext = CreateOpenGLPlatformContext(WindowHandle,desc.Format);
        EnqueueTaskAndWait([this]()
            {
                PlatformContext->Initialize();
            });
        
        Framebuffer = new OpenGLFramebuffer();
        Framebuffer->Create();
    }

    OpenGLSwapchain::~OpenGLSwapchain()
    {
        BackBufferTextures.clear();
		if (PlatformContext)
		{
			delete PlatformContext;
			PlatformContext = nullptr;
		}
        if (Framebuffer) {
            delete Framebuffer;
        }
    }

    RHI::RHISwapchain::RHISwapchainSlot OpenGLSwapchain::AcquireNextSlot()
    {
        RHISwapchainSlot slot{};
		slot.ReadySync = BackBufferFences[CurrentBackBufferIndex];
        slot.Texture = BackBufferTextures[CurrentBackBufferIndex].get();
        return slot;
    }

    void OpenGLSwapchain::Resize(uint32_t width, uint32_t height)
    {
        Width = width;
        Height = height;

        for (auto& texture : BackBufferTextures) {
            RHI::RHITextureDesc desc = texture->GetDesc();
            desc.Width = width;
            desc.Height = height;
            desc.Depth = 1;
            texture = std::make_shared<OpenGLTexture>(desc);
        }
    }
    void OpenGLSwapchain::Present() {
        
        if (CurrentBackBufferIndex > BackBufferTextures.size()) {
            return;
        }
        auto& texture = BackBufferTextures[CurrentBackBufferIndex];

		bool makeCurrent = PlatformContext->MakeCurrent();
        
		Framebuffer->Bind();

        Framebuffer->AttachColorTexture(texture->GetHandle(),0, GL_TEXTURE_2D);

		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glClearColor(0.2f, 0.3f, 0.4f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
		glBlitFramebuffer(0, 0, Width, Height, 0, 0, Width, Height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
		PlatformContext->SwapBuffers();
        
		CurrentBackBufferIndex = (CurrentBackBufferIndex + 1) % BackBufferTextures.size();
		PlatformContext->DoneCurrent();
        
    }
    OpenGLQueue::OpenGLQueue(RHI::EQueueType type)
        : QueueType(type)
    {
        SyncPoint = new OpenGLSyncPoint(false, type);
    }

    OpenGLQueue::~OpenGLQueue()
    {
        std::lock_guard<std::mutex> lock(ContextPoolMutex);
        for (auto* context : PendingContexts)
        {
            delete context;
        }
        for (auto* context : FreeContexts)
        {
            delete context;
        }
        for (auto* context : InFlightContexts)
        {
            delete context;
        }
        PendingContexts.clear();
        FreeContexts.clear();
        InFlightContexts.clear();

        delete SyncPoint;
        SyncPoint = nullptr;
    }

    RHI::EQueueType OpenGLQueue::GetType() const
    {
        return QueueType;
    }

    RHI::RHIContextBase* OpenGLQueue::AcquireCommandContext()
    {
        std::lock_guard<std::mutex> lock(ContextPoolMutex);
        if (!FreeContexts.empty())
        {
            RHI::RHIContextBase* context = FreeContexts.back();
            FreeContexts.pop_back();
            return context;
        }

        if (QueueType == RHI::EQueueType::Compute)
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

        std::lock_guard<std::mutex> lock(ContextPoolMutex);

        // 在飞守卫：worker 还没执行完该 ctx 时，所有权在 worker 手里。
        // 主线程提前 Release（Execute→WaitIdle→Release 模式里的时序误差）
        // 直接返回即可——worker 会在 FinishContextExecution 里归还，
        // 不会重复入池，也不会在执行期间被 Acquire 拿回去重录。
        if (InFlightContexts.find(context) != InFlightContexts.end())
        {
            return nullptr;
        }

        auto pendingIt = std::find(PendingContexts.begin(), PendingContexts.end(), context);
        if (pendingIt != PendingContexts.end())
        {
            PendingContexts.erase(pendingIt);
        }

        auto freeIt = std::find(FreeContexts.begin(), FreeContexts.end(), context);
        if (freeIt == FreeContexts.end())
        {
            FreeContexts.push_back(context);
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
            std::lock_guard<std::mutex> lock(ContextPoolMutex);
            PendingContexts.insert(PendingContexts.end(), cmds.begin(), cmds.end());
            InFlightContexts.insert(cmds.begin(), cmds.end());
        }

        QueueExecuteEntry entry;
        entry.Contexts = cmds;
        entry.WaitInfos = waitInfos;
        entry.FenceValue = AllocateNextFence();

        {
            std::lock_guard<std::mutex> lock(ExecuteQueueMutex);
            ExecuteQueueIns.push(entry);
        }

        return entry.FenceValue;
    }

    void OpenGLQueue::WaitValue(uint64_t fenceValue)
    {
        if (SyncPoint)
        {
            SyncPoint->Wait(fenceValue);
        }
    }

    void OpenGLQueue::WaitIdle()
    {
        if (!SyncPoint)
        {
            return;
        }

        // FIFO + 单调唯一 fence：等待最新已分配的 fence，即代表
        // 所有已提交 entry（含 Present）都已在 worker 上执行完毕。
        SyncPoint->Wait(LastSubmittedFence.load(std::memory_order_acquire));
    }

    uint64_t OpenGLQueue::AllocateNextFence()
    {
        return LastSubmittedFence.fetch_add(1, std::memory_order_acq_rel) + 1;
    }

    uint64_t OpenGLQueue::GetCurrentTimelineValue()
    {
        return SyncPoint ? SyncPoint->GetCurrentValue() : 0;
    }

    RHI::RHISyncPoint* OpenGLQueue::GetSyncPoint()
    {
        return SyncPoint;
    }

    ExecuteQueue& OpenGLQueue::GetExecuteQueue()
    {
        return ExecuteQueueIns;
    }

    void OpenGLQueue::FinishContextExecution(RHI::RHIContextBase* context)
    {
        if (!context)
        {
            return;
        }

        std::lock_guard<std::mutex> lock(ContextPoolMutex);
        InFlightContexts.erase(context);

        auto pendingIt = std::find(PendingContexts.begin(), PendingContexts.end(), context);
        if (pendingIt != PendingContexts.end())
        {
            PendingContexts.erase(pendingIt);
        }

        auto freeIt = std::find(FreeContexts.begin(), FreeContexts.end(), context);
        if (freeIt == FreeContexts.end())
        {
            FreeContexts.push_back(context);
        }
    }

    std::mutex& OpenGLQueue::GetExecuteQueueMutex()
    {
        return ExecuteQueueMutex;
    }

    bool OpenGLQueue::HasPendingExecute() const
    {
        std::lock_guard<std::mutex> lock(ExecuteQueueMutex);
        return !ExecuteQueueIns.empty();
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
        if (WorkerThread.joinable())
        {
            return;
        }

        bStopWorker = false;
        WorkerThread = std::thread(&OpenGLQueueManager::WorkerLoop, this);
        WorkerThreadId = WorkerThread.get_id();
    }

    void OpenGLQueueManager::StopWorker()
    {
        bStopWorker = true;
        WorkerCond.notify_all();
        if (WorkerThread.joinable())
        {
            WorkerThread.join();
        }
        WorkerThreadId = std::thread::id{};
    }

    bool OpenGLQueueManager::IsWorkerThread() const
    {
        return WorkerThread.joinable() && std::this_thread::get_id() == WorkerThreadId;
    }

    void OpenGLQueueManager::Init() {
        // worker 接管默认（bootstrap）上下文，作为其常驻 current context
        bInit = InitializeWorkerPlatformContext();
    }
    void OpenGLQueueManager::WorkerLoop()
    {
        while (!bStopWorker)
        {
            if (!bInit) {
                Init();
            }

            bool executed = false;
            CheckError();
            // 先排空通用任务队列（主线程投递的 GL 任务），再执行命令队列
            executed |= DrainTasks();
            executed |= TryExecuteQueue(GraphicsQueue);
            executed |= TryExecuteQueue(ComputeQueue);

            if (executed)
            {
                continue;
            }

            std::unique_lock<std::mutex> lock(WorkerMutex);
            WorkerCond.wait_for(lock, std::chrono::milliseconds(1));
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
        if (IsWorkerThread() || !WorkerThread.joinable())
        {
            task();
            return;
        }

        {
            std::lock_guard<std::mutex> lock(TaskQueueMutex);
            TaskQueue.push(std::move(task));
        }
        WorkerCond.notify_all();
    }

    void OpenGLQueueManager::EnqueueTaskAndWait(std::function<void()> task)
    {
        if (!task)
        {
            return;
        }

        // 同上：worker 线程内调用必须内联，否则自己等自己，永久死锁。
        if (IsWorkerThread() || !WorkerThread.joinable())
        {
            task();
            return;
        }

        auto promise = std::make_shared<std::promise<void>>();
        std::future<void> future = promise->get_future();
        {
            std::lock_guard<std::mutex> lock(TaskQueueMutex);
            TaskQueue.push([capturedTask = std::move(task), promise]()
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
        WorkerCond.notify_all();
        future.wait();
    }

    bool OpenGLQueueManager::HasPendingTasks() const
    {
        std::lock_guard<std::mutex> lock(TaskQueueMutex);
        return !TaskQueue.empty();
    }

    bool OpenGLQueueManager::DrainTasks()
    {
        bool executed = false;
        while (true)
        {
            std::function<void()> task;
            {
                std::lock_guard<std::mutex> lock(TaskQueueMutex);
                if (TaskQueue.empty())
                {
                    break;
                }
                task = std::move(TaskQueue.front());
                TaskQueue.pop();
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
