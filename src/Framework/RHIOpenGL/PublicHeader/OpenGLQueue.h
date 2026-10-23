#pragma once

#include "RHICommandContex.h"
#include "OpenGLResource.h"
#include "Singleton.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_set>
#include <vector>

namespace RHIOpenGL
{
    class OpenGLFramebuffer;
    class OpenGLPlatformContext;
    class OpenGLSyncPoint : public RHI::RHISyncPoint
    {
	public:
		OpenGLSyncPoint(bool isSwapchainFence = false, RHI::EQueueType queueType = RHI::EQueueType::Graphics);
		~OpenGLSyncPoint() override;
        uint64_t GetCurrentValue() override;
        // worker 每完成一条 entry 推进进度并唤醒等待方；只前进不后退（取 max）。
        void SetCurrentValue(uint64_t value);
        // CPU 阻塞等待直到 CurrentValue >= Value。真正会等，而不是"等待即完成"。
        void Wait(uint64_t value, uint64_t timeoutNs = UINT64_MAX) override;

        bool IsSwapchainFence() const { return m_bIsSwapchainFence; }
    private:
		bool m_bIsSwapchainFence = false;
        std::atomic<uint64_t> m_CurrentValue{ 0 };
        mutable std::mutex m_ValueMutex;
        std::condition_variable m_ValueCond;
    };

    class OpenGLSwapchain : public RHI::RHISwapchain
    {
    public:
        OpenGLSwapchain(void* inWindowHandle, uint32_t width, uint32_t height, RHI::ERHIFormat format);
        ~OpenGLSwapchain() override;

        RHISwapchainSlot AcquireNextSlot() override;
        void Resize(uint32_t width, uint32_t height) override;

        uint32_t GetWidth() const { return m_Width; }
        uint32_t GetHeight() const { return m_Height; }

		void Present(uint32_t backBufferIndex);
        uint32_t GetAcquiredBackBufferIndex() const { return m_AcquiredBackBufferIndex; }

    private:
        
        void* m_WindowHandle = nullptr;
        uint32_t m_Width = 0;
        uint32_t m_Height = 0;
        RHI::ERHIFormat m_Format = RHI::ERHIFormat::Unknown;
		uint32_t m_CurrentBackBufferIndex = 0;
        uint32_t m_AcquiredBackBufferIndex = 0;
        std::vector<OpenGLSyncPoint*> m_BackBufferFences;
		std::vector<std::shared_ptr<OpenGLTexture>> m_BackBufferTextures;
		OpenGLPlatformContext* m_PlatformContext = nullptr;
		OpenGLFramebuffer* m_Framebuffer = nullptr;
    };
    using OpenGLSwapchainSP = std::shared_ptr<OpenGLSwapchain>;

    struct QueueExecuteEntry {
        std::vector<RHI::RHIContextBase*> Contexts;
        std::vector<RHI::RHIWaitInfo> WaitInfos;
        uint64_t FenceValue = 0;
        std::function<void()> ExecuteFunction;
    };
    using ExecuteQueue = std::queue<QueueExecuteEntry>;

    class OpenGLQueue : public RHI::RHIQueue
    {
    public:
        explicit OpenGLQueue(RHI::EQueueType type = RHI::EQueueType::Graphics);
        ~OpenGLQueue() override;

        RHI::EQueueType GetType() const override;
        RHI::RHIContextBase* AcquireCommandContext() override;
        RHI::RHIContextBase* ReleaseCommandContext(RHI::RHIContextBase* context) override;
        uint64_t ExecuteContext(RHI::RHIContextBase* context) override;
        uint64_t ExecuteContext(const std::vector<RHI::RHIContextBase*>& cmds, const std::vector<RHI::RHIWaitInfo>& waitInfos) override;
        void WaitValue(uint64_t fenceValue) override;
        void WaitIdle() override;
        uint64_t GetCurrentTimelineValue() override;
        RHI::RHISyncPoint* GetSyncPoint() override;
        ExecuteQueue& GetExecuteQueue();
        std::mutex& GetExecuteQueueMutex();
        bool HasPendingExecute() const;

        // 分配下一个单调唯一 fence 值。不再从 CurrentValue+1 推导——那样在
        // worker 积压时会让多条 entry 拿到同一个值，WaitValue 语义失真。
        uint64_t AllocateNextFence();
    private:
        ExecuteQueue m_ExecuteQueueIns;
        mutable std::mutex m_ExecuteQueueMutex;
        mutable std::mutex m_ContextPoolMutex;
        std::vector<RHI::RHIContextBase*> m_FreeContexts;
        std::vector<RHI::RHIContextBase*> m_PendingContexts;
        // 已提交给 worker、尚未执行完的 ctx。主线程对这些 ctx 的
        // ReleaseCommandContext 是无害 no-op，由 worker 执行完后归还。
        std::unordered_set<RHI::RHIContextBase*> m_InFlightContexts;
        // 本队列最新已分配的 fence（含 Present entry）。WaitIdle 等它。
        std::atomic<uint64_t> m_LastSubmittedFence{ 0 };
        RHI::EQueueType m_QueueType = RHI::EQueueType::Graphics;
        OpenGLSyncPoint* m_SyncPoint = nullptr;

        // worker 专用：entry 执行完毕后把 ctx 从在飞集合移除并归还池。
        void FinishContextExecution(RHI::RHIContextBase* context);
        friend class OpenGLQueueManager;
    };

    class OpenGLQueueManager : public Singleton<OpenGLQueueManager>, public RHI::RHIPresentExecutor {
		friend class Singleton<OpenGLQueueManager>;
    public:
		OpenGLQueue* GetQueue(RHI::EQueueType type) {
			if (type == RHI::EQueueType::Graphics) {
                if (!m_GraphicsQueue) {
                    m_GraphicsQueue = new OpenGLQueue(RHI::EQueueType::Graphics);
                }
                return m_GraphicsQueue;
			}
			else if (type == RHI::EQueueType::Compute) {
				if (!m_ComputeQueue) {
					m_ComputeQueue = new OpenGLQueue(RHI::EQueueType::Compute);
				}
				return m_ComputeQueue;
			}
            if (!m_GraphicsQueue) {
                m_GraphicsQueue = new OpenGLQueue(RHI::EQueueType::Graphics);
            }
            return m_GraphicsQueue;
		}
        void Destroy() {
            StopWorker();
			if (m_GraphicsQueue) {
				delete m_GraphicsQueue;
				m_GraphicsQueue = nullptr;
			}
			if (m_ComputeQueue) {
				delete m_ComputeQueue;
				m_ComputeQueue = nullptr;
			}
		}
        void Present(RHI::RHISwapchain* swapchain, const RHI::RHIWaitInfo& waitInfo) override;
        // 把任意任务（通常包装 GL 调用）投递给 worker 线程异步执行。
        // 注意：任务队列与命令队列（ExecuteQueue）是两条独立 FIFO，
        // 两者之间的相对顺序不保证；需要严格顺序时用 EnqueueTaskAndWait。
        void EnqueueTask(std::function<void()> task);
        // 投递任务并阻塞等待 worker 执行完成（在 worker 上跑完才返回）。
        // 主线程需要立刻拿到 GL 调用结果（句柄创建/查询/读回）时用这个。
        void EnqueueTaskAndWait(std::function<void()> task);
        bool HasPendingTasks() const;
        // 当前调用是否发生在 worker 线程上。转发 GL 调用的接口在 worker
        // 内部被调用时会选择内联执行，避免 EnqueueTaskAndWait 自等死锁。
        bool IsWorkerThread() const;
        void StartWorker();
        void StopWorker();
    private:
        void Init();
		OpenGLQueue* m_GraphicsQueue = nullptr;
        OpenGLQueue* m_ComputeQueue = nullptr;
        std::thread m_WorkerThread;
        std::thread::id m_WorkerThreadId{};
        std::mutex m_WorkerMutex;
        std::condition_variable m_WorkerCond;
        bool m_bStopWorker = false;
        bool m_bInit = false;

        // 通用任务队列：主线程投递、worker 按序执行。
        std::queue<std::function<void()>> m_TaskQueue;
        mutable std::mutex m_TaskQueueMutex;
        // worker 循环里每次先排空任务队列再执行命令队列；返回本轮是否执行过任务。
        bool DrainTasks();

		OpenGLQueueManager();
		~OpenGLQueueManager();

        void WorkerLoop();
        bool TryExecuteQueue(OpenGLQueue* queue);
    };

    // =========================================================================
    // GL 任务转发快捷接口（自由函数）
    // -----------------------------------------------------------------------------
    // 主线程不允许直接调用 GL：所有 GL 调用通过这两个函数转发给 worker 线程。
    //   EnqueueTask(task)        —— 异步：投递即返回，适合析构/无返回值依赖的场景
    //   EnqueueTaskAndWait(task) —— 同步：等 worker 执行完才返回，适合需要立刻
    //                               读句柄/结果的场景（资源构造、查询、读回）
    // 内部转投 OpenGLQueueManager；worker 线程内调用会自动内联执行（防自等死锁），
    // 所以同一份调用代码在两个线程上都是安全的。
    // =========================================================================
    void EnqueueTask(std::function<void()> task);
    void EnqueueTaskAndWait(std::function<void()> task);
}
