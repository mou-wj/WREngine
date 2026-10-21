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

        std::weak_ptr<void> GetLifetimeToken() const { return LifetimeToken; }
        bool Init();
        void Shutdown();

        bool IsValid() const
        {
            return bValid;
        }

#if defined(_WIN32)
        ID3D12Device* GetNativeDevice() const
        {
            return NativeDevice.Get();
        }
        bool SupportsRayTracing() const { return RayTracingSupported; }
        IDXGIAdapter1* GetAdapter() const
        {
            return Adapter.Get();
        }
        ID3D12CommandQueue* GetGraphicsCommandQueue() const
        {
            return GraphicsCommandQueue.Get();
        }
        ID3D12CommandQueue* GetComputeCommandQueue() const
        {
            return ComputeCommandQueue.Get();
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
            return CbvSrvUavDescriptorSize;
        }
        uint32_t GetRenderTargetDescriptorSize() const
        {
            return RtvDescriptorSize;
        }
        uint32_t GetDepthStencilDescriptorSize() const
        {
            return DsvDescriptorSize;
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
        std::shared_ptr<void> LifetimeToken = std::make_shared<int>(0);
        bool bValid = false;
        bool RayTracingSupported = false;

#if defined(_WIN32)
        Microsoft::WRL::ComPtr<IDXGIFactory6> Factory;
        Microsoft::WRL::ComPtr<IDXGIAdapter1> Adapter;
        Microsoft::WRL::ComPtr<ID3D12Device> NativeDevice;
        Microsoft::WRL::ComPtr<ID3D12CommandQueue> GraphicsCommandQueue;
        Microsoft::WRL::ComPtr<ID3D12CommandQueue> ComputeCommandQueue;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> CbvSrvUavHeap;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> RtvHeap;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> DsvHeap;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> SamplerHeap;
        uint32_t SamplerDescriptorAllocated = 0;
        std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> FreeDescriptors[4];
        uint32_t CbvSrvUavDescriptorSize = 0;
        uint32_t CbvSrvUavDescriptorCapacity = 0;
        uint32_t CbvSrvUavDescriptorAllocated = 0;
        uint32_t RtvDescriptorSize = 0;
        uint32_t RtvDescriptorCapacity = 0;
        uint32_t RtvDescriptorAllocated = 0;
        uint32_t DsvDescriptorSize = 0;
        uint32_t DsvDescriptorCapacity = 0;
        uint32_t DsvDescriptorAllocated = 0;
        std::mutex CbvSrvUavMutex;
        std::mutex RtvMutex;
        std::mutex DsvMutex;
#endif
    };
} // namespace RHIDirectX12
