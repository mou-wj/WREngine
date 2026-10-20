#pragma once

#include "RHICommandContex.h"
#include "DirectX12Resource.h"
#include "DirectX12PipelineState.h"
#include "RHITransition.h"
#include <functional>
#include <optional>

#if defined(_WIN32)
#include <d3d12.h>
#include <wrl/client.h>
#endif

namespace RHIDirectX12
{
    class DirectX12Queue;
    struct DirectX12TransitionData
    {
        std::vector<RHI::RHITransitionInfo> Infos;
    };

    class DirectX12CommandContext : public virtual RHI::RHIContextBase
    {
    public:
        explicit DirectX12CommandContext(DirectX12Queue* inQueue = nullptr);
        ~DirectX12CommandContext() override;

        void Begin() override;
        void End() override;
        void BeginTransitions(std::vector<const RHI::RHITransition*> transitions) override;
        void EndTransitions(std::vector<const RHI::RHITransition*> transitions) override;
        void CopyTexture(RHI::RHITexture* src, RHI::RHITexture* dst, const RHI::RHICopyTextureDesc& copyDesc) override;
        void BlitTexture(RHI::RHITexture* src, RHI::RHITexture* dst, const RHI::RHIBlitTextureDesc& blitDesc) override;

        DirectX12Queue* GetQueue() const
        {
            return Queue;
        }
        void TransitionResource(RHI::RHIViewableResource* resource, D3D12_RESOURCE_STATES state,
                                uint32_t subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
        void RetainResource(Microsoft::WRL::ComPtr<ID3D12Resource> resource)
        {
            RetainedResources.push_back(std::move(resource));
        }
        void TrackResource(RHI::RHIViewableResource* resource);
        void RequireAccess(RHI::RHIViewableResource* resource, RHI::ERHIResourceAccess access,
                           uint32_t subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
        void Complete();
        void OnComplete(std::function<void()> callback)
        {
            CompletionCallbacks.push_back(std::move(callback));
        }
        uint64_t LastSubmission = 0;
        bool Reusable = false;
        std::vector<DirectX12ResourceState*> UsedResources;
        std::vector<RHI::RHIWaitInfo> ResourceWaits;

#if defined(_WIN32)
        ID3D12GraphicsCommandList* GetNativeCommandList() const
        {
            return CommandList.Get();
        }
#endif

    protected:
        DirectX12Queue* Queue = nullptr;

#if defined(_WIN32)
        Microsoft::WRL::ComPtr<ID3D12CommandAllocator> CommandAllocator;
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> CommandList;
        bool bRecording = false;
        std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> RetainedResources;
        std::vector<std::shared_ptr<std::atomic_uint32_t>> ResourceUseTokens;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> ResourceHeap, SamplerHeap;
        uint32_t ResourceDescriptorCursor = 0, SamplerDescriptorCursor = 0;
        std::vector<DirectX12RootBinding> ComputeBindings, GraphicsBindings;
        RHI::RHIGraphicsPipelineState* CurrentGraphicsPipeline = nullptr;
        RHI::RHIComputePipelineState* CurrentComputePipeline = nullptr;
        std::unordered_map<const DirectX12ShaderBase*, RHI::RHIBatchedShaderParameters> GraphicsParameterCache;
        using ReadOnlyStorageMap = std::unordered_map<const DirectX12ShaderBase*, std::vector<DirectX12Buffer*>>;
        ReadOnlyStorageMap ComputeReadOnlyStorage, GraphicsReadOnlyStorage;
        struct NativeStorageState
        {
            DirectX12Buffer* Buffer;
            D3D12_RESOURCE_STATES State;
        };
        std::vector<NativeStorageState> BeginReadOnlyStorageAccess(bool compute);
        void EndReadOnlyStorageAccess(const std::vector<NativeStorageState>& states);
        std::unordered_map<uint32_t, D3D12_VERTEX_BUFFER_VIEW> VertexBuffers;
        std::optional<D3D12_VIEWPORT> CurrentViewport;
        std::optional<D3D12_RECT> CurrentScissor;
        Microsoft::WRL::ComPtr<ID3D12RootSignature> BlitRoot;
        std::unordered_map<uint32_t, Microsoft::WRL::ComPtr<ID3D12PipelineState>> BlitPipelines;
        void BindParameters(const DirectX12ShaderBase* shader, const RHI::RHIBatchedShaderParameters& parameters,
                            const std::vector<DirectX12RootBinding>& bindings, bool compute);
        D3D12_GPU_DESCRIPTOR_HANDLE CopyDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE source, bool sampler = false);
        struct DiagnosticReadback
        {
            Microsoft::WRL::ComPtr<ID3D12Resource> Resource;
            std::string Name;
        };
        std::vector<DiagnosticReadback> DiagnosticReadbacks;
        std::vector<std::function<void()>> CompletionCallbacks;
#endif
    };

    class DirectX12ComputeContext : public DirectX12CommandContext, public RHI::RHIComputeContex
    {
    public:
        explicit DirectX12ComputeContext(DirectX12Queue* inQueue = nullptr);
        ~DirectX12ComputeContext() override = default;

        void SetComputePipelineState(RHI::RHIComputePipelineState* pipelineState) override;
        void SetBatchedShaderParameters(RHI::RHIComputeShader* shader,
                                        const RHI::RHIBatchedShaderParameters& parameter) override;
        void Dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) override;
    };

    class DirectX12GraphicContext : public DirectX12CommandContext, public RHI::RHIGraphicContex
    {
    public:
        explicit DirectX12GraphicContext(DirectX12Queue* inQueue = nullptr);
        ~DirectX12GraphicContext() override = default;

        void SetBatchedShaderParameters(RHI::RHIGraphicShader* shader,
                                        const RHI::RHIBatchedShaderParameters& parameter) override;
        void SetStreamSource(uint32_t streamIndex, RHI::RHIBuffer* vertexBuffer, uint32_t offset) override;
        void SetGraphicPipelineState(RHI::RHIGraphicsPipelineState* pipelineState) override;
        void SetViewport(float x, float y, float w, float h, float minDepth, float maxDepth) override;
        void SetScissor(int32_t x, int32_t y, uint32_t w, uint32_t h) override;
        void Draw(uint32_t vertexCount, uint32_t instanceCount = 1, uint32_t firstVertex = 0,
                  uint32_t firstInstance = 0) override;
        void DrawIndexed(RHI::RHIBuffer* indexBuffer, uint32_t indexCount, uint32_t instanceCount = 1,
                         uint32_t firstIndex = 0, int32_t vertexOffset = 0, uint32_t firstInstance = 0) override;
        void BeginRenderPass(const RHI::RHIRenderPassInfo& renderPassInfo) override;
        void EndRenderPass() override;

        void SetBatchedShaderParameters(RHI::RHIRayTracingShader* shader,
                                        const RHI::RHIBatchedShaderParameters& parameter) override;
        void SetRayTracingPipelineState(RHI::RHIRayTracingPipelineState* pipelineState) override;
        void BuildAccelerationStructure(RHI::RHIRayTracingAccelerationStructure* accelerationStructure) override;
        void UpdateAccelerationStructure(RHI::RHIRayTracingAccelerationStructure* accelerationStructure) override;
        void TraceRays(uint32_t width, uint32_t height, uint32_t depth = 1) override;
    };
} // namespace RHIDirectX12
