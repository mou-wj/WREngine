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

    class DirectX12GraphicsPipelineState : public RHI::RHIGraphicsPipelineState
    {
    public:
        DirectX12GraphicsPipelineState(DirectX12Device* inDevice, const RHI::RHIGraphicsPipelineStateDesc& desc);
        ~DirectX12GraphicsPipelineState() override = default;

        DirectX12Device* GetDevice() const
        {
            return Device;
        }

#if defined(_WIN32)
        ID3D12RootSignature* GetRootSignature() const
        {
            return RootSignature.Get();
        }
        ID3D12PipelineState* GetPipelineState() const
        {
            return PipelineState.Get();
        }
        const std::vector<DirectX12RootBinding>& GetBindings() const
        {
            return Bindings;
        }
#endif

    private:
        DirectX12Device* Device = nullptr;

#if defined(_WIN32)
        Microsoft::WRL::ComPtr<ID3D12RootSignature> RootSignature;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> PipelineState;
        std::vector<DirectX12RootBinding> Bindings;
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
            return Device;
        }

#if defined(_WIN32)
        ID3D12RootSignature* GetRootSignature() const
        {
            return RootSignature.Get();
        }
        ID3D12PipelineState* GetPipelineState() const
        {
            return PipelineState.Get();
        }
        const std::vector<DirectX12RootBinding>& GetBindings() const
        {
            return Bindings;
        }
#endif

    private:
        DirectX12Device* Device = nullptr;

#if defined(_WIN32)
        Microsoft::WRL::ComPtr<ID3D12RootSignature> RootSignature;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> PipelineState;
        std::vector<DirectX12RootBinding> Bindings;
#endif
    };
    using DirectX12ComputePipelineStateSP = std::shared_ptr<DirectX12ComputePipelineState>;
} // namespace RHIDirectX12
