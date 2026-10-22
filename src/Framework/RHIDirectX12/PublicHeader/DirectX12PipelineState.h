#pragma once

#include "RHIResource.h"
#include <memory>
#include "DirectX12Resource.h"

#if defined(_WIN32)
#include <d3d12.h>
#include <wrl/client.h>
#endif

namespace RHIDirectX12
{
    class DirectX12Device;
    struct DirectX12RootBinding
    {
        const DirectX12ShaderBase* Shader = nullptr;
        DirectX12ShaderBinding Binding;
        uint32_t RootIndex = 0;
    };

    class DirectX12RayTracingPipelineState : public RHI::RHIRayTracingPipelineState
    {
    public:
        DirectX12RayTracingPipelineState(DirectX12Device* device, const RHI::RHIRayTracingPipelineStateDesc& desc);
        Microsoft::WRL::ComPtr<ID3D12RootSignature> RootSignature;
        Microsoft::WRL::ComPtr<ID3D12StateObject> StateObject;
        Microsoft::WRL::ComPtr<ID3D12Resource> ShaderTable;
        std::vector<DirectX12RootBinding> Bindings;
        D3D12_DISPATCH_RAYS_DESC Dispatch{};
    };

    class DirectX12GraphicsPipelineState : public RHI::RHIGraphicsPipelineState
    {
    public:
        DirectX12GraphicsPipelineState(DirectX12Device* inDevice, const RHI::RHIGraphicsPipelineStateDesc& desc);
        ~DirectX12GraphicsPipelineState() override = default;

        DirectX12Device* GetDevice() const
        {
            return m_Device;
        }

#if defined(_WIN32)
        ID3D12RootSignature* GetRootSignature() const
        {
            return m_RootSignature.Get();
        }
        ID3D12PipelineState* GetPipelineState() const
        {
            return m_PipelineState.Get();
        }
        const std::vector<DirectX12RootBinding>& GetBindings() const
        {
            return m_Bindings;
        }
#endif

    private:
        DirectX12Device* m_Device = nullptr;

#if defined(_WIN32)
        Microsoft::WRL::ComPtr<ID3D12RootSignature> m_RootSignature;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> m_PipelineState;
        std::vector<DirectX12RootBinding> m_Bindings;
#endif
    };
    using DirectX12GraphicsPipelineStateSP = std::shared_ptr<DirectX12GraphicsPipelineState>;

    class DirectX12ComputePipelineState : public RHI::RHIComputePipelineState
    {
    public:
        DirectX12ComputePipelineState(DirectX12Device* inDevice, const RHI::RHIComputePipelineStateDesc& desc);
        ~DirectX12ComputePipelineState() override = default;

        DirectX12Device* GetDevice() const
        {
            return m_Device;
        }

#if defined(_WIN32)
        ID3D12RootSignature* GetRootSignature() const
        {
            return m_RootSignature.Get();
        }
        ID3D12PipelineState* GetPipelineState() const
        {
            return m_PipelineState.Get();
        }
        const std::vector<DirectX12RootBinding>& GetBindings() const
        {
            return m_Bindings;
        }
#endif

    private:
        DirectX12Device* m_Device = nullptr;

#if defined(_WIN32)
        Microsoft::WRL::ComPtr<ID3D12RootSignature> m_RootSignature;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> m_PipelineState;
        std::vector<DirectX12RootBinding> m_Bindings;
#endif
    };
    using DirectX12ComputePipelineStateSP = std::shared_ptr<DirectX12ComputePipelineState>;
} // namespace RHIDirectX12
