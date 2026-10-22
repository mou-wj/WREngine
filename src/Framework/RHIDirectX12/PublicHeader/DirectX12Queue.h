#pragma once

#include "RHICommandContex.h"
#include <atomic>
#include <memory>
#include <mutex>

#if defined(_WIN32)
#include <d3d12.h>
#include <wrl/client.h>
#include <windows.h>
#endif

namespace RHIDirectX12
{
    class DirectX12Device;

    class DirectX12SyncPoint final : public RHI::RHISyncPoint
    {
    public:
        DirectX12SyncPoint(RHI::EQueueType inType, DirectX12Device* inDevice = nullptr,
                           ID3D12CommandQueue* inQueue = nullptr);
        ~DirectX12SyncPoint() override;
        ID3D12Fence* GetNativeFence() const
        {
            return m_Fence.Get();
        }

        uint64_t GetCurrentValue() override;
        void Wait(uint64_t value, uint64_t timeoutNS = UINT64_MAX) override;
        uint64_t Signal();

    private:
        std::atomic<uint64_t> m_TimelineValue{0};
        std::mutex m_WaitMutex;
        DirectX12Device* m_Device = nullptr;

#if defined(_WIN32)
        ID3D12CommandQueue* m_Queue = nullptr;
        Microsoft::WRL::ComPtr<ID3D12Fence> m_Fence;
        HANDLE m_FenceEvent = nullptr;
#endif
    };

    class DirectX12Queue : public RHI::RHIQueue
    {
    public:
        DirectX12Queue(RHI::EQueueType type, DirectX12Device* inDevice, ID3D12CommandQueue* inQueue);
        ~DirectX12Queue() override;

        RHI::EQueueType GetType() const override;
        RHI::RHIContextBase* AcquireCommandContext() override;
        RHI::RHIContextBase* ReleaseCommandContext(RHI::RHIContextBase* context) override;
        uint64_t ExecuteContext(RHI::RHIContextBase* context) override;
        uint64_t ExecuteContext(const std::vector<RHI::RHIContextBase*>& cmds,
                                const std::vector<RHI::RHIWaitInfo>& waitInfos) override;
        void WaitValue(uint64_t fenceValue) override;
        void WaitIdle() override;
        uint64_t GetCurrentTimelineValue() override;
        RHI::RHISyncPoint* GetSyncPoint() override;

        DirectX12Device* GetDevice() const
        {
            return m_Device;
        }
#if defined(_WIN32)
        ID3D12CommandQueue* GetNativeQueue() const
        {
            return m_NativeQueue;
        }
#endif

    private:
        DirectX12Device* m_Device = nullptr;

#if defined(_WIN32)
        ID3D12CommandQueue* m_NativeQueue = nullptr;
#endif

        RHI::EQueueType m_QueueType = RHI::EQueueType::Graphics;
        DirectX12SyncPoint m_SyncPoint;
        std::vector<std::unique_ptr<class DirectX12CommandContext>> m_Contexts;
        std::mutex m_QueueMutex;
    };

    class DirectX12PresentExecutor : public RHI::RHIPresentExecutor
    {
    public:
        explicit DirectX12PresentExecutor(DirectX12Queue* queue = nullptr);
        ~DirectX12PresentExecutor() override = default;

        void Present(RHI::RHISwapchain* swapchain, const RHI::RHIWaitInfo& waitInfo) override;

    private:
        DirectX12Queue* m_Queue = nullptr;
        uint64_t m_PresentCount = 0;
    };
} // namespace RHIDirectX12
