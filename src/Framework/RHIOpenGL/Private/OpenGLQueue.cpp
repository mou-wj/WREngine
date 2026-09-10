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
        Type = queueType;
    }

    OpenGLSyncPoint::~OpenGLSyncPoint() = default;

    uint64_t OpenGLSyncPoint::GetCurrentValue() {
        return CurrentValue;
    }

    void OpenGLSyncPoint::SetCurrentValue(uint64_t Value)
    {
        CurrentValue = Value;
    }

    void OpenGLSyncPoint::Wait(uint64_t Value, uint64_t TimeoutNS)
    {
        (void)TimeoutNS;
        if (Value <= CurrentValue)
        {
            return;
        }
        uint32_t WaitCount = 0;
        while (true && WaitCount < TimeoutNS) {
            if (Value <= CurrentValue) {
                break;
            }
        }
        CurrentValue = Value;
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
        PlatformContext->Initialize();
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
        CheckError();
        Framebuffer->AttachColorTexture(texture->GetHandle(),0, GL_TEXTURE_2D);
        CheckError();
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
        PendingContexts.clear();
        FreeContexts.clear();

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
        }

        QueueExecuteEntry entry;
        entry.Contexts = cmds;
        entry.WaitInfos = waitInfos;
        entry.FenceValue = GetCurrentTimelineValue() + 1;

        {
            std::lock_guard<std::mutex> lock(ExecuteQueueMutex);
            ExecuteQueueIns.push(entry);
        }

        return entry.FenceValue;
    }

    void OpenGLQueue::WaitValue(uint64_t fenceValue)
    {
        if (fenceValue <= GetCurrentTimelineValue())
        {
            return;
        }

        if (SyncPoint)
        {
            SyncPoint->SetCurrentValue(fenceValue);
        }
    }

    void OpenGLQueue::WaitIdle()
    {
        if (SyncPoint)
        {
            SyncPoint->SetCurrentValue(GetCurrentTimelineValue());
        }
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
        entry.FenceValue = queue->GetCurrentTimelineValue() + 1;
        entry.ExecuteFunction = [swapchain]()
        {
            auto* glSwapchain = dynamic_cast<OpenGLSwapchain*>(swapchain);
            if (glSwapchain)
            {
                glSwapchain->Present();
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                glViewport(0, 0, glSwapchain->GetWidth(), glSwapchain->GetHeight());
                MakeBackCurrent(true);
                bool HasCurrent = HasCurrentContext();
                CheckError();
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
    }

    void OpenGLQueueManager::StopWorker()
    {
        bStopWorker = true;
        WorkerCond.notify_all();
        if (WorkerThread.joinable())
        {
            WorkerThread.join();
        }
    }
    void OpenGLQueueManager::Init() {
        bInit = InitializeBackPlatformSurport();
    }
    void OpenGLQueueManager::WorkerLoop()
    {
        while (!bStopWorker)
        {
            if (!bInit) {
                Init();
            }

            bool executed = false;

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
                        }
                    }
                }
            }
        }

        for (auto* context : entry.Contexts)
        {
            if (queue)
            {
                queue->ReleaseCommandContext(context);
            }
        }

        if (queue->GetSyncPoint())
        {
            dynamic_cast<OpenGLSyncPoint*>(queue->GetSyncPoint())->SetCurrentValue(entry.FenceValue);
        }

        return true;
    }
}
