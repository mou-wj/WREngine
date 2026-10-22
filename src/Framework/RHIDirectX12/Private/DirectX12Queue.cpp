#include "DirectX12Queue.h"
#include "DirectX12Context.h"
#include "DirectX12Device.h"
#include "DirectX12RHIUtils.h"
#include "RHIApi.h"
#include <fstream>
#include <filesystem>
#include <algorithm>

namespace RHIDirectX12
{
    DirectX12SyncPoint::DirectX12SyncPoint(RHI::EQueueType type, DirectX12Device* device, ID3D12CommandQueue* queue)
        : Device(device), Queue(queue)
    {
        _type = type;
        CheckDX12(Device->GetNativeDevice()->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&Fence)),
                  "Create queue fence");
        FenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (!FenceEvent)
            throw std::runtime_error("Create fence event failed");
    }
    DirectX12SyncPoint::~DirectX12SyncPoint()
    {
        if (FenceEvent)
            CloseHandle(FenceEvent);
    }
    uint64_t DirectX12SyncPoint::GetCurrentValue()
    {
        const auto value = Fence->GetCompletedValue();
        if (value == UINT64_MAX)
            CheckDX12(Device->GetNativeDevice()->GetDeviceRemovedReason(), "D3D12 device removed");
        return value;
    }
    void DirectX12SyncPoint::Wait(uint64_t value, uint64_t timeoutNS)
    {
        if (!value || GetCurrentValue() >= value)
            return;
        std::lock_guard<std::mutex> lock(WaitMutex);
        CheckDX12(Fence->SetEventOnCompletion(value, FenceEvent), "SetEventOnCompletion");
        DWORD timeout = timeoutNS == UINT64_MAX
                            ? INFINITE
                            : static_cast<DWORD>(std::min<uint64_t>((timeoutNS + 999999) / 1000000, MAXDWORD - 1));
        const auto result = WaitForSingleObject(FenceEvent, timeout);
        if (result != WAIT_OBJECT_0)
            throw std::runtime_error(result == WAIT_TIMEOUT ? "D3D12 fence wait timed out" : "D3D12 fence wait failed");
        GetCurrentValue();
    }
    uint64_t DirectX12SyncPoint::Signal()
    {
        const auto value = TimelineValue.fetch_add(1) + 1;
        CheckDX12(Queue->Signal(Fence.Get(), value), "Signal queue fence");
        return value;
    }
    DirectX12Queue::DirectX12Queue(RHI::EQueueType type, DirectX12Device* device, ID3D12CommandQueue* queue)
        : Device(device), NativeQueue(queue), QueueType(type), SyncPoint(type, device, queue)
    {
    }
    DirectX12Queue::~DirectX12Queue()
    {
        try
        {
            WaitIdle();
        }
        catch (...)
        {
        }
        Contexts.clear();
    }
    RHI::EQueueType DirectX12Queue::GetType() const
    {
        return QueueType;
    }
    RHI::RHIContextBase* DirectX12Queue::AcquireCommandContext()
    {
        const auto completed = SyncPoint.GetCurrentValue();
        for (const auto& context : Contexts)
            if (context->Reusable && context->LastSubmission <= completed)
            {
                context->Complete();
                context->Reusable = false;
                return context.get();
            }
        std::unique_ptr<DirectX12CommandContext> context;
        if (QueueType == RHI::EQueueType::Compute)
            context = std::make_unique<DirectX12ComputeContext>(this);
        else
            context = std::make_unique<DirectX12GraphicContext>(this);
        auto* result = context.get();
        Contexts.push_back(std::move(context));
        return result;
    }
    RHI::RHIContextBase* DirectX12Queue::ReleaseCommandContext(RHI::RHIContextBase* context)
    {
        if (auto* dx = dynamic_cast<DirectX12CommandContext*>(context))
        {
            if (dx->GetQueue() != this)
                throw std::runtime_error("Releasing context to the wrong queue");
            if (dx->LastSubmission)
                WaitValue(dx->LastSubmission);
            dx->Reusable = true;
        }
        return nullptr;
    }
    uint64_t DirectX12Queue::ExecuteContext(RHI::RHIContextBase* context)
    {
        return ExecuteContext({context}, {});
    }
    uint64_t DirectX12Queue::ExecuteContext(const std::vector<RHI::RHIContextBase*>& contexts,
                                            const std::vector<RHI::RHIWaitInfo>& waits)
    {
        std::lock_guard<std::mutex> lock(QueueMutex);
        auto wait = [&](const RHI::RHIWaitInfo& info) {
            if (!info.SyncPoint || !info.Value)
                return;
            if (auto* point = dynamic_cast<DirectX12SyncPoint*>(info.SyncPoint))
                CheckDX12(NativeQueue->Wait(point->GetNativeFence(), info.Value), "GPU queue wait");
            else
                info.SyncPoint->Wait(info.Value);
        };
        for (const auto& info : waits)
            wait(info);
        std::vector<ID3D12CommandList*> lists;
        for (auto* context : contexts)
        {
            auto* dx = dynamic_cast<DirectX12CommandContext*>(context);
            if (!dx || dx->GetQueue() != this)
                throw std::runtime_error("Submitting an incompatible DirectX12 context");
            dx->End();
            for (const auto& info : dx->ResourceWaits)
                wait(info);
            lists.push_back(dx->GetNativeCommandList());
        }
        if (!lists.empty())
            NativeQueue->ExecuteCommandLists(static_cast<UINT>(lists.size()), lists.data());
        const auto value = SyncPoint.Signal();
        for (auto* context : contexts)
        {
            auto* dx = dynamic_cast<DirectX12CommandContext*>(context);
            dx->LastSubmission = value;
            dx->Reusable = true;
            for (auto* resource : dx->UsedResources)
            {
                resource->LastQueue = this;
                resource->LastFence = value;
                // Keep the last explicitly recorded state. After buffer decay D3D12
                // permits that promotable state as StateBefore; this also preserves
                // the GPU validation layer's explicit transition history.
            }
        }
        Device->CheckDebugMessages();
        return value;
    }
    void DirectX12Queue::WaitValue(uint64_t value)
    {
        SyncPoint.Wait(value);
        for (const auto& context : Contexts)
            if (context->LastSubmission && context->LastSubmission <= value)
                context->Complete();
        Device->CheckDebugMessages();
    }
    void DirectX12Queue::WaitIdle()
    {
        WaitValue(SyncPoint.Signal());
    }
    uint64_t DirectX12Queue::GetCurrentTimelineValue()
    {
        return SyncPoint.GetCurrentValue();
    }
    RHI::RHISyncPoint* DirectX12Queue::GetSyncPoint()
    {
        return &SyncPoint;
    }
    DirectX12PresentExecutor::DirectX12PresentExecutor(DirectX12Queue* queue) : Queue(queue)
    {
    }
    void DirectX12PresentExecutor::Present(RHI::RHISwapchain* swapchain, const RHI::RHIWaitInfo& wait)
    {
        if (wait.SyncPoint)
            wait.SyncPoint->Wait(wait.Value);
        auto* dx = dynamic_cast<DirectX12Swapchain*>(swapchain);
        if (!dx)
            throw std::runtime_error("Invalid DirectX12 swapchain");
        // Optional native readback captures validate the actual rendered GPU image.
        static uint32_t captureIndex = 0;
        if (DX12Diagnostics() && captureIndex < 4)
        {
            auto* texture = dx->GetCurrentTexture();
            auto* context = dynamic_cast<DirectX12CommandContext*>(Queue->AcquireCommandContext());
            RHI::RHIGraphicCommandList commandList(dynamic_cast<RHI::RHIGraphicContex*>(context));
            commandList.SetImmediate(true);
            commandList.Begin();
            const auto* data = static_cast<const uint8_t*>(RHI::GRHIApi->MapReadTexture(commandList, texture, {}));
            commandList.End();
            Queue->WaitValue(Queue->ExecuteContext(context));
            const auto folder = DX12DiagnosticDirectory();
            std::filesystem::create_directories(folder);
            const auto path = std::filesystem::path(folder) / ("frame-" + std::to_string(captureIndex++) + ".ppm");
            std::ofstream image(path, std::ios::binary);
            image << "P6\n" << dx->GetWidth() << ' ' << dx->GetHeight() << "\n255\n";
            const bool bgra = texture->GetNativeFormat() == DXGI_FORMAT_B8G8R8A8_UNORM;
            for (UINT y = 0; y < dx->GetHeight(); ++y)
                for (UINT x = 0; x < dx->GetWidth(); ++x)
                {
                    const auto* pixel = data + y * dx->GetWidth() * 4 + 4 * x;
                    const char rgb[] = {char(pixel[bgra ? 2 : 0]), char(pixel[1]), char(pixel[bgra ? 0 : 2])};
                    image.write(rgb, 3);
                }
            LOG_INFO("DirectX12 GPU image captured: %s", path.string().c_str());
            RHI::GRHIApi->Unmap(const_cast<uint8_t*>(data));
            Queue->ReleaseCommandContext(context);
        }
        Queue->GetDevice()->CheckDebugMessages();
        dx->Present();
        ++PresentCount;
        if (DX12Diagnostics() && PresentCount % 120 == 0)
            LOG_INFO("DirectX12 presented %llu frames", PresentCount);
    }
} // namespace RHIDirectX12
