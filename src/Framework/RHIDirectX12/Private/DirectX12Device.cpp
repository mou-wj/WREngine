#include "DirectX12Device.h"
#include "DirectX12RHIUtils.h"

#if defined(_WIN32)
#include <d3d12sdklayers.h>
#include <iostream>
#endif

namespace RHIDirectX12
{
    DirectX12Device::~DirectX12Device()
    {
        Shutdown();
    }

    bool DirectX12Device::Init()
    {
        if (bValid)
        {
            return true;
        }

#if !defined(_WIN32)
        return false;
#else
#if defined(DEBUG_INFO)
        {
            Microsoft::WRL::ComPtr<ID3D12Debug> debugController;
            if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
            {
                debugController->EnableDebugLayer();
                if (GetEnvironmentVariableA("RHI_DX12_GPU_VALIDATION", nullptr, 0))
                {
                    Microsoft::WRL::ComPtr<ID3D12Debug1> validation;
                    if (SUCCEEDED(debugController.As(&validation)))
                        validation->SetEnableGPUBasedValidation(TRUE);
                }
            }
        }
#endif

        if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&Factory))))
        {
            return false;
        }

        Microsoft::WRL::ComPtr<IDXGIAdapter1> candidate;
        for (UINT adapterIndex = 0; Factory->EnumAdapters1(adapterIndex, &candidate) != DXGI_ERROR_NOT_FOUND;
             ++adapterIndex)
        {
            DXGI_ADAPTER_DESC1 desc{};
            candidate->GetDesc1(&desc);
            if ((desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0)
            {
                continue;
            }

            if (SUCCEEDED(D3D12CreateDevice(candidate.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&NativeDevice))))
            {
                Adapter = candidate;
                break;
            }
        }

        if (!NativeDevice)
        {
            return false;
        }

        D3D12_COMMAND_QUEUE_DESC graphicsQueueDesc{};
        graphicsQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        graphicsQueueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
        graphicsQueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        if (FAILED(NativeDevice->CreateCommandQueue(&graphicsQueueDesc, IID_PPV_ARGS(&GraphicsCommandQueue))))
        {
            Shutdown();
            return false;
        }

        // RHI compute contexts may blit directly into a swapchain. DXGI requires
        // every back-buffer write on the swapchain's own queue. Share the native
        // DIRECT queue while keeping separate logical contexts and fence timelines.
        ComputeCommandQueue = GraphicsCommandQueue;

        D3D12_DESCRIPTOR_HEAP_DESC cbvSrvUavHeapDesc{};
        cbvSrvUavHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        cbvSrvUavHeapDesc.NumDescriptors = 8192;
        cbvSrvUavHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        cbvSrvUavHeapDesc.NodeMask = 0;
        if (FAILED(NativeDevice->CreateDescriptorHeap(&cbvSrvUavHeapDesc, IID_PPV_ARGS(&CbvSrvUavHeap))))
        {
            Shutdown();
            return false;
        }

        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
        rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDesc.NumDescriptors = 2048;
        rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        rtvHeapDesc.NodeMask = 0;
        if (FAILED(NativeDevice->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&RtvHeap))))
        {
            Shutdown();
            return false;
        }

        D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc{};
        dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        dsvHeapDesc.NumDescriptors = 2048;
        dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        dsvHeapDesc.NodeMask = 0;
        if (FAILED(NativeDevice->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&DsvHeap))))
        {
            Shutdown();
            return false;
        }

        CbvSrvUavDescriptorSize =
            NativeDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        CbvSrvUavDescriptorCapacity = cbvSrvUavHeapDesc.NumDescriptors;
        CbvSrvUavDescriptorAllocated = 0;
        RtvDescriptorSize = NativeDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        RtvDescriptorCapacity = rtvHeapDesc.NumDescriptors;
        RtvDescriptorAllocated = 0;
        DsvDescriptorSize = NativeDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
        DsvDescriptorCapacity = dsvHeapDesc.NumDescriptors;
        DsvDescriptorAllocated = 0;

        D3D12_DESCRIPTOR_HEAP_DESC samplerHeapDesc{};
        samplerHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
        samplerHeapDesc.NumDescriptors = 2048;
        CheckDX12(NativeDevice->CreateDescriptorHeap(&samplerHeapDesc, IID_PPV_ARGS(&SamplerHeap)),
                  "Create sampler heap");
        DXGI_ADAPTER_DESC1 adapterDesc{};
        Adapter->GetDesc1(&adapterDesc);
        LOG_INFO("DirectX12 initialized (adapter vendor 0x%04X, device 0x%04X)", adapterDesc.VendorId,
                 adapterDesc.DeviceId);
        bValid = true;
        return true;
#endif
    }

    void DirectX12Device::Shutdown()
    {
#if defined(_WIN32)
        CbvSrvUavDescriptorAllocated = 0;
        CbvSrvUavDescriptorCapacity = 0;
        CbvSrvUavDescriptorSize = 0;
        RtvDescriptorAllocated = 0;
        RtvDescriptorCapacity = 0;
        RtvDescriptorSize = 0;
        DsvDescriptorAllocated = 0;
        DsvDescriptorCapacity = 0;
        DsvDescriptorSize = 0;
        SamplerHeap.Reset();
        SamplerDescriptorAllocated = 0;
        for (auto& descriptors : FreeDescriptors)
            descriptors.clear();
        DsvHeap.Reset();
        RtvHeap.Reset();
        CbvSrvUavHeap.Reset();
        ComputeCommandQueue.Reset();
        GraphicsCommandQueue.Reset();
        NativeDevice.Reset();
        Adapter.Reset();
        Factory.Reset();
#endif
        bValid = false;
    }

#if defined(_WIN32)
    bool DirectX12Device::CreateCommittedResource(const D3D12_HEAP_PROPERTIES& heapProperties,
                                                  D3D12_HEAP_FLAGS heapFlags, const D3D12_RESOURCE_DESC& resourceDesc,
                                                  D3D12_RESOURCE_STATES initialState,
                                                  const D3D12_CLEAR_VALUE* clearValue,
                                                  Microsoft::WRL::ComPtr<ID3D12Resource>& outResource) const
    {
        if (!NativeDevice)
        {
            return false;
        }

        outResource.Reset();
        CheckDX12(NativeDevice->CreateCommittedResource(&heapProperties, heapFlags, &resourceDesc, initialState,
                                                        clearValue, IID_PPV_ARGS(&outResource)),
                  "CreateCommittedResource");
        return true;
    }

    bool DirectX12Device::AllocateCbvSrvUavDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE& outHandle, uint32_t count)
    {
        std::lock_guard<std::mutex> lock(CbvSrvUavMutex);
        auto& free = FreeDescriptors[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV];
        if (count == 1 && !free.empty())
        {
            outHandle = free.back();
            free.pop_back();
            return true;
        }

        if (!CbvSrvUavHeap || count == 0)
        {
            return false;
        }

        if (CbvSrvUavDescriptorAllocated + count > CbvSrvUavDescriptorCapacity)
        {
            return false;
        }

        outHandle = CbvSrvUavHeap->GetCPUDescriptorHandleForHeapStart();
        outHandle.ptr +=
            static_cast<SIZE_T>(CbvSrvUavDescriptorAllocated) * static_cast<SIZE_T>(CbvSrvUavDescriptorSize);
        CbvSrvUavDescriptorAllocated += count;
        return true;
    }

    bool DirectX12Device::AllocateRenderTargetDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE& outHandle, uint32_t count)
    {
        std::lock_guard<std::mutex> lock(RtvMutex);
        auto& free = FreeDescriptors[D3D12_DESCRIPTOR_HEAP_TYPE_RTV];
        if (count == 1 && !free.empty())
        {
            outHandle = free.back();
            free.pop_back();
            return true;
        }

        if (!RtvHeap || count == 0)
        {
            return false;
        }

        if (RtvDescriptorAllocated + count > RtvDescriptorCapacity)
        {
            return false;
        }

        outHandle = RtvHeap->GetCPUDescriptorHandleForHeapStart();
        outHandle.ptr += static_cast<SIZE_T>(RtvDescriptorAllocated) * static_cast<SIZE_T>(RtvDescriptorSize);
        RtvDescriptorAllocated += count;
        return true;
    }

    bool DirectX12Device::AllocateDepthStencilDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE& outHandle, uint32_t count)
    {
        std::lock_guard<std::mutex> lock(DsvMutex);
        auto& free = FreeDescriptors[D3D12_DESCRIPTOR_HEAP_TYPE_DSV];
        if (count == 1 && !free.empty())
        {
            outHandle = free.back();
            free.pop_back();
            return true;
        }

        if (!DsvHeap || count == 0)
        {
            return false;
        }

        if (DsvDescriptorAllocated + count > DsvDescriptorCapacity)
        {
            return false;
        }

        outHandle = DsvHeap->GetCPUDescriptorHandleForHeapStart();
        outHandle.ptr += static_cast<SIZE_T>(DsvDescriptorAllocated) * static_cast<SIZE_T>(DsvDescriptorSize);
        DsvDescriptorAllocated += count;
        return true;
    }
    bool DirectX12Device::AllocateSamplerDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE& outHandle)
    {
        std::lock_guard<std::mutex> lock(CbvSrvUavMutex);
        auto& free = FreeDescriptors[D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER];
        if (!free.empty())
        {
            outHandle = free.back();
            free.pop_back();
            return true;
        }
        if (!SamplerHeap || SamplerDescriptorAllocated >= 2048)
            return false;
        outHandle = SamplerHeap->GetCPUDescriptorHandleForHeapStart();
        outHandle.ptr += SIZE_T(SamplerDescriptorAllocated++) *
                         NativeDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
        return true;
    }
    void DirectX12Device::FreeDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE type, D3D12_CPU_DESCRIPTOR_HANDLE handle)
    {
        if (!handle.ptr || !NativeDevice)
            return;
        std::mutex& mutex = type == D3D12_DESCRIPTOR_HEAP_TYPE_RTV   ? RtvMutex
                            : type == D3D12_DESCRIPTOR_HEAP_TYPE_DSV ? DsvMutex
                                                                     : CbvSrvUavMutex;
        std::lock_guard<std::mutex> lock(mutex);
        FreeDescriptors[type].push_back(handle);
    }
    void DirectX12Device::CheckDebugMessages()
    {
        Microsoft::WRL::ComPtr<ID3D12InfoQueue> messages;
        if (!NativeDevice || FAILED(NativeDevice.As(&messages)))
            return;
        bool failed = false;
        for (UINT64 i = 0; i < messages->GetNumStoredMessages(); ++i)
        {
            SIZE_T size = 0;
            messages->GetMessage(i, nullptr, &size);
            std::vector<char> storage(size);
            auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
            if (SUCCEEDED(messages->GetMessage(i, message, &size)) &&
                message->Severity <= D3D12_MESSAGE_SEVERITY_WARNING)
            {
                if (message->Severity <= D3D12_MESSAGE_SEVERITY_ERROR)
                {
                    LOG_ERROR("D3D12 [%u]: %s", message->ID, message->pDescription);
                    failed = true;
                }
                else
                    LOG_WARN("D3D12 [%u]: %s", message->ID, message->pDescription);
            }
        }
        messages->ClearStoredMessages();
        if (failed)
            throw std::runtime_error("D3D12 debug layer reported an error (see log)");
    }
#endif
} // namespace RHIDirectX12
