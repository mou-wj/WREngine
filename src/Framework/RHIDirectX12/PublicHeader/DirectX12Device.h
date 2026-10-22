#pragma once
#include <memory>

#if defined(_WIN32)
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <mutex>
#include <vector>
#endif

namespace RHIDirectX12
{
    class DirectX12Device
    {
    public:
        DirectX12Device() = default;
        ~DirectX12Device();

        std::weak_ptr<void> GetLifetimeToken() const { return m_LifetimeToken; }
        bool Init();
        void Shutdown();

        bool IsValid() const
        {
            return m_bValid;
        }

#if defined(_WIN32)
        ID3D12Device* GetNativeDevice() const
        {
            return m_NativeDevice.Get();
        }
        bool SupportsRayTracing() const { return m_bRayTracingSupported; }
        IDXGIAdapter1* GetAdapter() const
        {
            return m_Adapter.Get();
        }
        ID3D12CommandQueue* GetGraphicsCommandQueue() const
        {
            return m_GraphicsCommandQueue.Get();
        }
        ID3D12CommandQueue* GetComputeCommandQueue() const
        {
            return m_ComputeCommandQueue.Get();
        }

        bool CreateCommittedResource(const D3D12_HEAP_PROPERTIES& heapProperties, D3D12_HEAP_FLAGS heapFlags,
                                     const D3D12_RESOURCE_DESC& resourceDesc, D3D12_RESOURCE_STATES initialState,
                                     const D3D12_CLEAR_VALUE* clearValue,
                                     Microsoft::WRL::ComPtr<ID3D12Resource>& outResource) const;

        bool AllocateCbvSrvUavDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE& outHandle, uint32_t count = 1);
        bool AllocateRenderTargetDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE& outHandle, uint32_t count = 1);
        bool AllocateDepthStencilDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE& outHandle, uint32_t count = 1);
        uint32_t GetCbvSrvUavDescriptorSize() const
        {
            return m_CbvSrvUavDescriptorSize;
        }
        uint32_t GetRenderTargetDescriptorSize() const
        {
            return m_RtvDescriptorSize;
        }
        uint32_t GetDepthStencilDescriptorSize() const
        {
            return m_DsvDescriptorSize;
        }
        bool AllocateSamplerDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE& outHandle);
        void FreeDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE type, D3D12_CPU_DESCRIPTOR_HANDLE handle);
        void CheckDebugMessages();
#else
        void* GetNativeDevice() const
        {
            return nullptr;
        }
        void* GetAdapter() const
        {
            return nullptr;
        }
        void* GetGraphicsCommandQueue() const
        {
            return nullptr;
        }
        void* GetComputeCommandQueue() const
        {
            return nullptr;
        }
#endif

    private:
        std::shared_ptr<void> m_LifetimeToken = std::make_shared<int>(0);
        bool m_bValid = false;
        bool m_bRayTracingSupported = false;

#if defined(_WIN32)
        Microsoft::WRL::ComPtr<IDXGIFactory6> m_Factory;
        Microsoft::WRL::ComPtr<IDXGIAdapter1> m_Adapter;
        Microsoft::WRL::ComPtr<ID3D12Device> m_NativeDevice;
        Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_GraphicsCommandQueue;
        Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_ComputeCommandQueue;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_CbvSrvUavHeap;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_RtvHeap;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_DsvHeap;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_SamplerHeap;
        uint32_t m_SamplerDescriptorAllocated = 0;
        std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> m_FreeDescriptors[4];
        uint32_t m_CbvSrvUavDescriptorSize = 0;
        uint32_t m_CbvSrvUavDescriptorCapacity = 0;
        uint32_t m_CbvSrvUavDescriptorAllocated = 0;
        uint32_t m_RtvDescriptorSize = 0;
        uint32_t m_RtvDescriptorCapacity = 0;
        uint32_t m_RtvDescriptorAllocated = 0;
        uint32_t m_DsvDescriptorSize = 0;
        uint32_t m_DsvDescriptorCapacity = 0;
        uint32_t m_DsvDescriptorAllocated = 0;
        std::mutex m_CbvSrvUavMutex;
        std::mutex m_RtvMutex;
        std::mutex m_DsvMutex;
#endif
    };
} // namespace RHIDirectX12
