#pragma once

#include "RHICommandContex.h"
#include "OpenGLResource.h"
#include "Singleton.hpp"
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
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
        void SetCurrentValue(uint64_t Value);
        void Wait(uint64_t Value, uint64_t TimeoutNS = UINT64_MAX) override;

        bool IsSwapchainFence() const { return bIsSwapchainFence; }
    private:
		bool bIsSwapchainFence = false;
        uint64_t CurrentValue = 0;
    };

    class OpenGLSwapchain : public RHI::RHISwapchain
    {
    public:
        OpenGLSwapchain(void* inWindowHandle, uint32_t width, uint32_t height, RHI::ERHIFormat format);
        ~OpenGLSwapchain() override;

        RHISwapchainSlot AcquireNextSlot() override;
        void Resize(uint32_t width, uint32_t height) override;

        uint32_t GetWidth() const { return Width; }
        uint32_t GetHeight() const { return Height; }

		void Present();

    private:
        
        void* WindowHandle = nullptr;
        uint32_t Width = 0;
        uint32_t Height = 0;
        RHI::ERHIFormat Format = RHI::ERHIFormat::Unknown;
		uint32_t CurrentBackBufferIndex = 0;
        std::vector<OpenGLSyncPoint*> BackBufferFences;
		std::vector<std::shared_ptr<OpenGLTexture>> BackBufferTextures;
		OpenGLPlatformContext* PlatformContext = nullptr;
		OpenGLFramebuffer* Framebuffer = nullptr;
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
    private:
        ExecuteQueue ExecuteQueueIns;
        mutable std::mutex ExecuteQueueMutex;
        mutable std::mutex ContextPoolMutex;
        std::vector<RHI::RHIContextBase*> FreeContexts;
        std::vector<RHI::RHIContextBase*> PendingContexts;
        RHI::EQueueType QueueType = RHI::EQueueType::Graphics;
        OpenGLSyncPoint* SyncPoint = nullptr;
    };

    class OpenGLQueueManager : public Singleton<OpenGLQueueManager>, public RHI::RHIPresentExecutor {
		friend class Singleton<OpenGLQueueManager>;
    public:
		OpenGLQueue* GetQueue(RHI::EQueueType type) {
			if (type == RHI::EQueueType::Graphics) {
                if (!GraphicsQueue) {
                    GraphicsQueue = new OpenGLQueue(RHI::EQueueType::Graphics);
                }
                return GraphicsQueue;
			}
			else if (type == RHI::EQueueType::Compute) {
				if (!ComputeQueue) {
					ComputeQueue = new OpenGLQueue(RHI::EQueueType::Compute);
				}
				return ComputeQueue;
			}
            if (!GraphicsQueue) {
                GraphicsQueue = new OpenGLQueue(RHI::EQueueType::Graphics);
            }
            return GraphicsQueue;
		}
        void Destroy() {
            StopWorker();
			if (GraphicsQueue) {
				delete GraphicsQueue;
				GraphicsQueue = nullptr;
			}
			if (ComputeQueue) {
				delete ComputeQueue;
				ComputeQueue = nullptr;
			}
		}
        void Present(RHI::RHISwapchain* swapchain, const RHI::RHIWaitInfo& waitInfo) override;
        void EnqueueTask(std::function<void()> task);
        void StartWorker();
        void StopWorker();
    private:
        void Init();
		OpenGLQueue* GraphicsQueue = nullptr;
        OpenGLQueue* ComputeQueue = nullptr;
        std::thread WorkerThread;
        std::mutex WorkerMutex;
        std::condition_variable WorkerCond;
        bool bStopWorker = false;
        bool bInit = false;

		OpenGLQueueManager();
		~OpenGLQueueManager();

        void WorkerLoop();
        bool TryExecuteQueue(OpenGLQueue* queue);
    };
}
