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
        if (m_bValid)
        {
            return true;
        }

        if (!m_LifetimeToken) m_LifetimeToken = std::make_shared<int>(0);

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

        if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&m_Factory))))
        {
            return false;
        }

        // Prefer a DXR-capable adapter; retain a graphics fallback for other RHI work.
        Microsoft::WRL::ComPtr<IDXGIAdapter1> candidate;
        for (UINT index = 0; m_Factory->EnumAdapterByGpuPreference(index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                 IID_PPV_ARGS(&candidate)) != DXGI_ERROR_NOT_FOUND; ++index)
        {
            DXGI_ADAPTER_DESC1 desc{};
            candidate->GetDesc1(&desc);
            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
            Microsoft::WRL::ComPtr<ID3D12Device> probe;
            if (FAILED(D3D12CreateDevice(candidate.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&probe)))) continue;
            D3D12_FEATURE_DATA_D3D12_OPTIONS5 options{};
            const bool dxr = SUCCEEDED(probe->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options, sizeof(options)))
                && options.RaytracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED;
            if (!m_NativeDevice || dxr)
            {
                m_NativeDevice = probe;
                m_Adapter = candidate;
                m_bRayTracingSupported = dxr;
            }
            if (dxr) break;
        }

        if (!m_NativeDevice)
        {
            return false;
        }

        D3D12_COMMAND_QUEUE_DESC graphicsQueueDesc{};
        graphicsQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        graphicsQueueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
        graphicsQueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        if (FAILED(m_NativeDevice->CreateCommandQueue(&graphicsQueueDesc, IID_PPV_ARGS(&m_GraphicsCommandQueue))))
        {
            Shutdown();
            return false;
        }

        // RHI compute contexts may blit directly into a swapchain. DXGI requires
        // every back-buffer write on the swapchain's own queue. Share the native
        // DIRECT queue while keeping separate logical contexts and fence timelines.
        m_ComputeCommandQueue = m_GraphicsCommandQueue;

        D3D12_DESCRIPTOR_HEAP_DESC cbvSrvUavHeapDesc{};
        cbvSrvUavHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        cbvSrvUavHeapDesc.NumDescriptors = 8192;
        cbvSrvUavHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        cbvSrvUavHeapDesc.NodeMask = 0;
        if (FAILED(m_NativeDevice->CreateDescriptorHeap(&cbvSrvUavHeapDesc, IID_PPV_ARGS(&m_CbvSrvUavHeap))))
        {
            Shutdown();
            return false;
        }

        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
        rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDesc.NumDescriptors = 2048;
        rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        rtvHeapDesc.NodeMask = 0;
        if (FAILED(m_NativeDevice->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_RtvHeap))))
        {
            Shutdown();
            return false;
        }

        D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc{};
        dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        dsvHeapDesc.NumDescriptors = 2048;
        dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        dsvHeapDesc.NodeMask = 0;
        if (FAILED(m_NativeDevice->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_DsvHeap))))
        {
            Shutdown();
            return false;
        }

        m_CbvSrvUavDescriptorSize =
            m_NativeDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        m_CbvSrvUavDescriptorCapacity = cbvSrvUavHeapDesc.NumDescriptors;
        m_CbvSrvUavDescriptorAllocated = 0;
        m_RtvDescriptorSize = m_NativeDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        m_RtvDescriptorCapacity = rtvHeapDesc.NumDescriptors;
        m_RtvDescriptorAllocated = 0;
        m_DsvDescriptorSize = m_NativeDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
        m_DsvDescriptorCapacity = dsvHeapDesc.NumDescriptors;
        m_DsvDescriptorAllocated = 0;

        D3D12_DESCRIPTOR_HEAP_DESC samplerHeapDesc{};
        samplerHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
        samplerHeapDesc.NumDescriptors = 2048;
        CheckDX12(m_NativeDevice->CreateDescriptorHeap(&samplerHeapDesc, IID_PPV_ARGS(&m_SamplerHeap)),
                  "Create sampler heap");
        DXGI_ADAPTER_DESC1 adapterDesc{};
        m_Adapter->GetDesc1(&adapterDesc);
        LOG_INFO("DirectX12 initialized (adapter vendor 0x%04X, device 0x%04X)", adapterDesc.VendorId,
                 adapterDesc.DeviceId);
        m_bValid = true;
        return true;
#endif
    }

    void DirectX12Device::Shutdown()
    {
        m_LifetimeToken.reset();
#if defined(_WIN32)
        m_CbvSrvUavDescriptorAllocated = 0;
        m_CbvSrvUavDescriptorCapacity = 0;
        m_CbvSrvUavDescriptorSize = 0;
        m_RtvDescriptorAllocated = 0;
        m_RtvDescriptorCapacity = 0;
        m_RtvDescriptorSize = 0;
        m_DsvDescriptorAllocated = 0;
        m_DsvDescriptorCapacity = 0;
        m_DsvDescriptorSize = 0;
        m_SamplerHeap.Reset();
        m_SamplerDescriptorAllocated = 0;
        for (auto& descriptors : m_FreeDescriptors)
            descriptors.clear();
        m_DsvHeap.Reset();
        m_RtvHeap.Reset();
        m_CbvSrvUavHeap.Reset();
        m_ComputeCommandQueue.Reset();
        m_GraphicsCommandQueue.Reset();
        m_NativeDevice.Reset();
        m_Adapter.Reset();
        m_Factory.Reset();
#endif
        m_bValid = false;
    }

#if defined(_WIN32)
    bool DirectX12Device::CreateCommittedResource(const D3D12_HEAP_PROPERTIES& heapProperties,
                                                  D3D12_HEAP_FLAGS heapFlags, const D3D12_RESOURCE_DESC& resourceDesc,
                                                  D3D12_RESOURCE_STATES initialState,
                                                  const D3D12_CLEAR_VALUE* clearValue,
                                                  Microsoft::WRL::ComPtr<ID3D12Resource>& outResource) const
    {
        if (!m_NativeDevice)
        {
            return false;
        }

        outResource.Reset();
        CheckDX12(m_NativeDevice->CreateCommittedResource(&heapProperties, heapFlags, &resourceDesc, initialState,
                                                        clearValue, IID_PPV_ARGS(&outResource)),
                  "CreateCommittedResource");
        return true;
    }

    bool DirectX12Device::AllocateCbvSrvUavDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE& outHandle, uint32_t count)
    {
        std::lock_guard<std::mutex> lock(m_CbvSrvUavMutex);
        auto& free = m_FreeDescriptors[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV];
        if (count == 1 && !free.empty())
        {
            outHandle = free.back();
            free.pop_back();
            return true;
        }

        if (!m_CbvSrvUavHeap || count == 0)
        {
            return false;
        }

        if (m_CbvSrvUavDescriptorAllocated + count > m_CbvSrvUavDescriptorCapacity)
        {
            return false;
        }

        outHandle = m_CbvSrvUavHeap->GetCPUDescriptorHandleForHeapStart();
        outHandle.ptr +=
            static_cast<SIZE_T>(m_CbvSrvUavDescriptorAllocated) * static_cast<SIZE_T>(m_CbvSrvUavDescriptorSize);
        m_CbvSrvUavDescriptorAllocated += count;
        return true;
    }

    bool DirectX12Device::AllocateRenderTargetDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE& outHandle, uint32_t count)
    {
        std::lock_guard<std::mutex> lock(m_RtvMutex);
        auto& free = m_FreeDescriptors[D3D12_DESCRIPTOR_HEAP_TYPE_RTV];
        if (count == 1 && !free.empty())
        {
            outHandle = free.back();
            free.pop_back();
            return true;
        }

        if (!m_RtvHeap || count == 0)
        {
            return false;
        }

        if (m_RtvDescriptorAllocated + count > m_RtvDescriptorCapacity)
        {
            return false;
        }

        outHandle = m_RtvHeap->GetCPUDescriptorHandleForHeapStart();
        outHandle.ptr += static_cast<SIZE_T>(m_RtvDescriptorAllocated) * static_cast<SIZE_T>(m_RtvDescriptorSize);
        m_RtvDescriptorAllocated += count;
        return true;
    }

    bool DirectX12Device::AllocateDepthStencilDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE& outHandle, uint32_t count)
    {
        std::lock_guard<std::mutex> lock(m_DsvMutex);
        auto& free = m_FreeDescriptors[D3D12_DESCRIPTOR_HEAP_TYPE_DSV];
        if (count == 1 && !free.empty())
        {
            outHandle = free.back();
            free.pop_back();
            return true;
        }

        if (!m_DsvHeap || count == 0)
        {
            return false;
        }

        if (m_DsvDescriptorAllocated + count > m_DsvDescriptorCapacity)
        {
            return false;
        }

        outHandle = m_DsvHeap->GetCPUDescriptorHandleForHeapStart();
        outHandle.ptr += static_cast<SIZE_T>(m_DsvDescriptorAllocated) * static_cast<SIZE_T>(m_DsvDescriptorSize);
        m_DsvDescriptorAllocated += count;
        return true;
    }
    bool DirectX12Device::AllocateSamplerDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE& outHandle)
    {
        std::lock_guard<std::mutex> lock(m_CbvSrvUavMutex);
        auto& free = m_FreeDescriptors[D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER];
        if (!free.empty())
        {
            outHandle = free.back();
            free.pop_back();
            return true;
        }
        if (!m_SamplerHeap || m_SamplerDescriptorAllocated >= 2048)
            return false;
        outHandle = m_SamplerHeap->GetCPUDescriptorHandleForHeapStart();
        outHandle.ptr += SIZE_T(m_SamplerDescriptorAllocated++) *
                         m_NativeDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
        return true;
    }
    void DirectX12Device::FreeDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE type, D3D12_CPU_DESCRIPTOR_HANDLE handle)
    {
        if (!handle.ptr || !m_NativeDevice)
            return;
        std::mutex& mutex = type == D3D12_DESCRIPTOR_HEAP_TYPE_RTV   ? m_RtvMutex
                            : type == D3D12_DESCRIPTOR_HEAP_TYPE_DSV ? m_DsvMutex
                                                                     : m_CbvSrvUavMutex;
        std::lock_guard<std::mutex> lock(mutex);
        m_FreeDescriptors[type].push_back(handle);
    }
    void DirectX12Device::CheckDebugMessages()
    {
        Microsoft::WRL::ComPtr<ID3D12InfoQueue> messages;
        if (!m_NativeDevice || FAILED(m_NativeDevice.As(&messages)))
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
