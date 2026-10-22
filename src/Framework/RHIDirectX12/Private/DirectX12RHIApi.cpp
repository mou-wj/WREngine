#include "DirectX12RHIApi.h"
#include "DirectX12Device.h"
#include "DirectX12PipelineState.h"
#include "DirectX12Queue.h"
#include "DirectX12Resource.h"
#include "RHITransition.h"
#include "DirectX12Context.h"
#include "DirectX12RHIUtils.h"
#include "DirectX12TransientResource.h"
#include <cstddef>

namespace RHIDirectX12
{
    namespace
    {
        // Preserve the command-list's immediate/deferred execution contract.
        class DirectX12LambdaCommand final : public RHI::RHICommandBase
        {
            std::function<void(RHI::RHICommandListBase&)> m_Function;

        public:
            explicit DirectX12LambdaCommand(std::function<void(RHI::RHICommandListBase&)> function)
                : m_Function(std::move(function))
            {
            }
            void Execute(RHI::RHICommandListBase& list) override
            {
                m_Function(list);
            }
        };
    } // namespace

    DirectX12RHIApi::DirectX12RHIApi() = default;

    DirectX12RHIApi::~DirectX12RHIApi()
    {
        Shutdown();
    }

    bool DirectX12RHIApi::Init()
    {
        if (m_bInitialized)
        {
            return true;
        }

        RHI::GShaderPlatform = RHI::ERHIShaderPlatform::D3D12;
        m_PlatformInfo.DepthRange = RHI::EDepthRange::ZeroToOne;
        m_PlatformInfo.bEnableRayTracing = false;

        const uint32_t headerSize = static_cast<uint32_t>(sizeof(RHI::RHITransition));
        const uint32_t alignment = static_cast<uint32_t>(alignof(std::max_align_t));
        const uint32_t privateDataSize = sizeof(DirectX12TransitionData);

        RHI::G_RHITransition_PrivateDataOffset = (headerSize + alignment - 1) & ~(alignment - 1);
        RHI::G_RHITransition_TotalSize = RHI::G_RHITransition_PrivateDataOffset + privateDataSize;

        m_Device = std::make_unique<DirectX12Device>();
        if (!m_Device->Init())
        {
            m_Device.reset();
            return false;
        }

#if defined(_WIN32)
        m_PlatformInfo.bEnableRayTracing = m_Device->SupportsRayTracing();

        m_GraphicsQueue = std::make_unique<DirectX12Queue>(RHI::EQueueType::Graphics, m_Device.get(),
                                                         m_Device->GetGraphicsCommandQueue());
        m_ComputeQueue =
            std::make_unique<DirectX12Queue>(RHI::EQueueType::Compute, m_Device.get(), m_Device->GetComputeCommandQueue());
#else
        GraphicsQueue = nullptr;
        ComputeQueue = nullptr;
#endif

        if (!m_GraphicsQueue)
        {
            m_Device->Shutdown();
            m_Device.reset();
            return false;
        }

        if (!m_ComputeQueue)
        {
            m_ComputeQueue = std::make_unique<DirectX12Queue>(RHI::EQueueType::Compute, m_Device.get(),
                                                            m_Device->GetGraphicsCommandQueue());
        }

        m_PresentExecutor = std::make_unique<DirectX12PresentExecutor>(m_GraphicsQueue.get());

        m_bInitialized = true;
        return true;
    }

    void DirectX12RHIApi::Shutdown()
    {
        if (m_ComputeQueue)
            m_ComputeQueue->WaitIdle();
        if (m_GraphicsQueue)
            m_GraphicsQueue->WaitIdle();
        m_MappedStagingBuffers.clear();
        m_PackedReadbacks.clear();
        m_PresentExecutor.reset();
        m_ComputeQueue.reset();
        m_GraphicsQueue.reset();
        if (m_Device)
        {
            m_Device->Shutdown();
            m_Device.reset();
        }
        m_bInitialized = false;
    }

    const RHI::RHIPlatformInfo& DirectX12RHIApi::GetPlatformInfo() const
    {
        return m_PlatformInfo;
    }

    RHI::RHITextureSP DirectX12RHIApi::CreateTexture(const RHI::RHITextureDesc& desc)
    {
        return std::make_shared<DirectX12Texture>(m_Device.get(), desc);
    }

    RHI::RHIBufferSP DirectX12RHIApi::CreateBuffer(const RHI::RHIBufferDesc& desc)
    {
        return std::make_shared<DirectX12Buffer>(m_Device.get(), desc);
    }

    void DirectX12RHIApi::UpdateTexture(RHI::RHICommandListBase& cmdList, RHI::RHITexture* texture, const void* data,
                                        const RHI::RHIUpdateTextureRegion& region)
    {
        auto* dx = dynamic_cast<DirectX12Texture*>(texture);
        auto* context = dynamic_cast<DirectX12CommandContext*>(cmdList.GetContext());
        if (!dx || !context || !data || !region.Width || !region.Height || !region.Depth)
            throw std::runtime_error("Invalid texture upload");
        if (region.NumMipLevels != 1 || region.NumArraySlices != 1)
            throw std::runtime_error("UpdateTexture accepts one subresource per call");
        const UINT pixelSize = RHI::GFormatInfoMap.at(texture->GetDesc().Format).BytesPerPixel;
        const UINT srcPitch = region.Width * pixelSize, rowPitch = (srcPitch + 255) & ~255u;
        const UINT bytes = rowPitch * region.Height * region.Depth;
        auto upload = std::make_shared<DirectX12StagingBuffer>(m_Device.get(), bytes);
        auto* mapped = static_cast<uint8_t*>(upload->Map(0, bytes));
        for (UINT z = 0; z < region.Depth; ++z)
            for (UINT y = 0; y < region.Height; ++y)
                std::memcpy(mapped + (z * region.Height + y) * rowPitch,
                            static_cast<const uint8_t*>(data) + (z * region.Height + y) * srcPitch, srcPitch);
        cmdList.AddCommand<DirectX12LambdaCommand>(
            [dx, texture, upload, region, rowPitch](RHI::RHICommandListBase& list) {
                auto* context = dynamic_cast<DirectX12CommandContext*>(list.GetContext());
                D3D12_TEXTURE_COPY_LOCATION from{}, to{};
                from.pResource = upload->GetNativeResource();
                from.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                from.PlacedFootprint.Footprint = {ToDxgiFormat(texture->GetDesc().Format), region.Width, region.Height,
                                                  region.Depth, rowPitch};
                to.pResource = dx->GetNativeResource();
                to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                to.SubresourceIndex = region.MipLevel + region.ArraySlice * texture->GetDesc().MipLevels;
                if (!dx->DeclaredAccess.empty())
                    context->RequireAccess(texture, RHI::ERHIResourceAccess::TransferDest, to.SubresourceIndex);
                context->TransitionResource(texture, D3D12_RESOURCE_STATE_COPY_DEST, to.SubresourceIndex);
                context->GetNativeCommandList()->CopyTextureRegion(&to, region.XOffset, region.YOffset, region.ZOffset,
                                                                   &from, nullptr);
                context->RetainResource(upload->GetNativeResource());
            });
    }
    void DirectX12RHIApi::UpdateBuffer(RHI::RHICommandListBase& cmdList, RHI::RHIBuffer* buffer, const void* data,
                                       const RHI::RHIUpdateBufferRegion& region)
    {
        auto* dx = dynamic_cast<DirectX12Buffer*>(buffer);
        auto* context = dynamic_cast<DirectX12CommandContext*>(cmdList.GetContext());
        if (!dx || !context || !data || region.Offset > buffer->GetDesc().Size)
            throw std::runtime_error("Invalid buffer upload");
        const UINT64 bytes = region.Size ? region.Size : buffer->GetDesc().Size - region.Offset;
        if (!bytes || bytes > buffer->GetDesc().Size - region.Offset || bytes > UINT_MAX)
            throw std::out_of_range("Buffer upload region");
        if (dx->bUploadHeap)
        {
            // A deferred list must snapshot the caller's bytes before returning.
            auto copied = std::make_shared<std::vector<uint8_t>>(static_cast<size_t>(bytes));
            std::memcpy(copied->data(), data, copied->size());
            cmdList.AddCommand<DirectX12LambdaCommand>([dx, copied, region](RHI::RHICommandListBase& list) {
                auto* context = dynamic_cast<DirectX12CommandContext*>(list.GetContext());
                if (!dx->DeclaredAccess.empty())
                    context->RequireAccess(dx, RHI::ERHIResourceAccess::TransferDest);
                context->TrackResource(dx);
                if (dx->LastQueue && dx->LastFence)
                    dx->LastQueue->WaitValue(dx->LastFence);
                void* mapped = nullptr;
                D3D12_RANGE read{0, 0};
                CheckDX12(dx->GetNativeResource()->Map(0, &read, &mapped), "Map CPU-visible buffer");
                std::memcpy(static_cast<uint8_t*>(mapped) + region.Offset, copied->data(), copied->size());
                D3D12_RANGE written{region.Offset, region.Offset + copied->size()};
                dx->GetNativeResource()->Unmap(0, &written);
            });
            return;
        }
        auto upload = std::make_shared<DirectX12StagingBuffer>(m_Device.get(), static_cast<UINT>(bytes));
        std::memcpy(upload->Map(0, static_cast<UINT>(bytes)), data, static_cast<size_t>(bytes));
        cmdList.AddCommand<DirectX12LambdaCommand>([dx, buffer, upload, region, bytes](RHI::RHICommandListBase& list) {
            auto* context = dynamic_cast<DirectX12CommandContext*>(list.GetContext());
            if (!dx->DeclaredAccess.empty())
                context->RequireAccess(buffer, RHI::ERHIResourceAccess::TransferDest);
            context->TransitionResource(buffer, D3D12_RESOURCE_STATE_COPY_DEST);
            context->GetNativeCommandList()->CopyBufferRegion(dx->GetNativeResource(), region.Offset,
                                                              upload->GetNativeResource(), 0, bytes);
            context->RetainResource(upload->GetNativeResource());
        });
    }
    void* DirectX12RHIApi::MapReadTexture(RHI::RHICommandListBase& cmdList, RHI::RHITexture* texture,
                                          const RHI::RHIReadTextureInfo& info)
    {
        auto* dx = dynamic_cast<DirectX12Texture*>(texture);
        auto* context = dynamic_cast<DirectX12CommandContext*>(cmdList.GetContext());
        if (!dx || !context || !info.ArrayCount)
            throw std::runtime_error("Invalid texture readback");
        const auto& desc = texture->GetDesc();
        if (info.MipLevel >= desc.MipLevels || info.ArraySlice + info.ArrayCount > desc.ArraySize)
            throw std::out_of_range("Texture readback subresource");
        const UINT width = std::max(1u, desc.Width >> info.MipLevel),
                   height = std::max(1u, desc.Height >> info.MipLevel),
                   depth = std::max(1u, desc.Depth >> info.MipLevel);
        const UINT pitch = width * RHI::GFormatInfoMap.at(desc.Format).BytesPerPixel;
        const UINT gpuPitch = (pitch + 255) & ~255u;
        const UINT64 gpuSlice = (UINT64(gpuPitch) * height * depth + 511) & ~511ull;
        if (gpuSlice * info.ArrayCount > UINT_MAX)
            throw std::out_of_range("Texture readback is too large");
        auto readback = std::make_shared<DirectX12StagingBuffer>(
            m_Device.get(), static_cast<UINT>(gpuSlice * info.ArrayCount), D3D12_HEAP_TYPE_READBACK);
        auto packed = std::make_shared<std::vector<uint8_t>>(size_t(pitch) * height * depth * info.ArrayCount);
        cmdList.AddCommand<DirectX12LambdaCommand>([dx, texture, readback, packed, info, width, height, depth, pitch,
                                                    gpuPitch, gpuSlice](RHI::RHICommandListBase& list) {
            auto* context = dynamic_cast<DirectX12CommandContext*>(list.GetContext());
            const auto& desc = texture->GetDesc();
            for (UINT layer = 0; layer < info.ArrayCount; ++layer)
            {
                const UINT subresource = info.MipLevel + (info.ArraySlice + layer) * desc.MipLevels;
                const auto oldState = dx->States.at(subresource);
                context->TransitionResource(texture, D3D12_RESOURCE_STATE_COPY_SOURCE, subresource);
                D3D12_TEXTURE_COPY_LOCATION src{}, dst{};
                src.pResource = dx->GetNativeResource();
                src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                src.SubresourceIndex = subresource;
                dst.pResource = readback->GetNativeResource();
                dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                dst.PlacedFootprint.Offset = gpuSlice * layer;
                dst.PlacedFootprint.Footprint = {dx->GetNativeFormat(), width, height, depth, gpuPitch};
                context->GetNativeCommandList()->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
                context->TransitionResource(texture, oldState, subresource);
            }
            context->RetainResource(readback->GetNativeResource());
            context->OnComplete([readback, packed, pitch, gpuPitch, gpuSlice, height, depth, count = info.ArrayCount] {
                const auto* src = static_cast<const uint8_t*>(readback->Map(0, static_cast<UINT>(gpuSlice * count)));
                for (UINT layer = 0; layer < count; ++layer)
                    for (UINT row = 0; row < height * depth; ++row)
                        std::memcpy(packed->data() + (size_t(layer) * height * depth + row) * pitch,
                                    src + layer * gpuSlice + size_t(row) * gpuPitch, pitch);
            });
        });
        void* result = packed->data();
        m_MappedStagingBuffers[result] = readback;
        m_PackedReadbacks[result] = packed;
        return result;
    }
    void* DirectX12RHIApi::MapReadBuffer(RHI::RHICommandListBase& cmdList, RHI::RHIBuffer* buffer,
                                         const RHI::RHIReadBufferInfo& info)
    {
        auto* dx = dynamic_cast<DirectX12Buffer*>(buffer);
        auto* context = dynamic_cast<DirectX12CommandContext*>(cmdList.GetContext());
        if (!dx || !context || info.Offset > buffer->GetDesc().Size)
            throw std::runtime_error("Invalid buffer readback");
        const auto size = info.Size ? info.Size : buffer->GetDesc().Size - info.Offset;
        if (!size || size > UINT_MAX || size > buffer->GetDesc().Size - info.Offset)
            throw std::out_of_range("Buffer readback region");
        auto readback =
            std::make_shared<DirectX12StagingBuffer>(m_Device.get(), static_cast<UINT>(size), D3D12_HEAP_TYPE_READBACK);
        cmdList.AddCommand<DirectX12LambdaCommand>([dx, buffer, readback, info, size](RHI::RHICommandListBase& list) {
            auto* context = dynamic_cast<DirectX12CommandContext*>(list.GetContext());
            const auto oldState = dx->States[0];
            context->TransitionResource(buffer, D3D12_RESOURCE_STATE_COPY_SOURCE);
            context->GetNativeCommandList()->CopyBufferRegion(readback->GetNativeResource(), 0, dx->GetNativeResource(),
                                                              info.Offset, size);
            context->TransitionResource(buffer, oldState);
            context->RetainResource(readback->GetNativeResource());
        });
        void* result = readback->Map(0, static_cast<UINT>(size));
        m_MappedStagingBuffers[result] = readback;
        return result;
    }

    void DirectX12RHIApi::Unmap(void* mappedData)
    {
        if (!mappedData)
        {
            return;
        }

        auto it = m_MappedStagingBuffers.find(mappedData);
        if (it == m_MappedStagingBuffers.end())
        {
            return;
        }

        it->second->Unmap();
        m_MappedStagingBuffers.erase(it);
        m_PackedReadbacks.erase(mappedData);
    }

    RHI::RHIShaderResourceViewSP DirectX12RHIApi::CreateTextureShaderResourceView(RHI::RHITexture* texture,
                                                                                  const RHI::RHITexSRVCreateInfo& desc)
    {
        auto* viewable = dynamic_cast<RHI::RHIViewableResource*>(texture);
        if (!viewable)
        {
            return nullptr;
        }
        return std::make_shared<DirectX12ShaderResourceView>(viewable, desc);
    }

    RHI::RHIUnorderedAccessViewSP DirectX12RHIApi::CreateTextureUnorderedAccessView(
        RHI::RHITexture* texture, const RHI::RHITexUAVCreateInfo& desc)
    {
        auto* viewable = dynamic_cast<RHI::RHIViewableResource*>(texture);
        if (!viewable)
        {
            return nullptr;
        }
        return std::make_shared<DirectX12UnorderedAccessView>(viewable, desc);
    }

    RHI::RHIShaderResourceViewSP DirectX12RHIApi::CreateBufferShaderResourceView(
        RHI::RHIBuffer* buffer, const RHI::RHIBufferSRVCreateInfo& desc)
    {
        auto* viewable = dynamic_cast<RHI::RHIViewableResource*>(buffer);
        if (!viewable)
        {
            return nullptr;
        }
        return std::make_shared<DirectX12ShaderResourceView>(viewable, desc);
    }

    RHI::RHIUnorderedAccessViewSP DirectX12RHIApi::CreateBufferUnorderedAccessView(
        RHI::RHIBuffer* buffer, const RHI::RHIBufferUAVCreateInfo& desc)
    {
        auto* viewable = dynamic_cast<RHI::RHIViewableResource*>(buffer);
        if (!viewable)
        {
            return nullptr;
        }
        return std::make_shared<DirectX12UnorderedAccessView>(viewable, desc);
    }

    RHI::RHIRayTracingGeometrySP DirectX12RHIApi::CreateRayTracingGeometry(const RHI::RHIRayTracingGeometryDesc& desc)
    {
        return std::make_shared<DirectX12RayTracingGeometry>(m_Device.get(), desc);
    }

    RHI::RHIRayTracingInstanceSP DirectX12RHIApi::CreateRayTracingInstance(const RHI::RHIRayTracingInstancesDesc& desc)
    {
        return std::make_shared<DirectX12RayTracingInstance>(m_Device.get(), desc);
    }

    RHI::RHIStagingBufferSP DirectX12RHIApi::CreateStagingBuffer(uint32_t size)
    {
        return std::make_shared<DirectX12StagingBuffer>(m_Device.get(), size);
    }

    RHI::RHIGraphicsPipelineStateSP DirectX12RHIApi::CreateGraphicsPipelineState(
        const RHI::RHIGraphicsPipelineStateDesc& desc)
    {
        return std::make_shared<DirectX12GraphicsPipelineState>(m_Device.get(), desc);
    }

    RHI::RHIComputePipelineStateSP DirectX12RHIApi::CreateComputePipelineState(
        const RHI::RHIComputePipelineStateDesc& desc)
    {
        return std::make_shared<DirectX12ComputePipelineState>(m_Device.get(), desc);
    }

    RHI::RHIRayTracingPipelineStateSP DirectX12RHIApi::CreateRayTracingsPipelineState(
        const RHI::RHIRayTracingPipelineStateDesc& desc)
    {
        return std::make_shared<DirectX12RayTracingPipelineState>(m_Device.get(), desc);
    }

    RHI::RHIVertexDescStateSP DirectX12RHIApi::CreateVertexDescState(const RHI::RHIVertexDescStateDesc& desc)
    {
        return std::make_shared<DirectX12VertexDescState>(desc);
    }

    RHI::RHIRasterizerStateSP DirectX12RHIApi::CreateRasterizerState(const RHI::RHIRasterizerStateDesc& desc)
    {
        return std::make_shared<DirectX12RasterizerState>(desc);
    }

    RHI::RHIColorBlendStateSP DirectX12RHIApi::CreateColorBlendState(const RHI::RHIColorBlendStateDesc& desc)
    {
        return std::make_shared<DirectX12ColorBlendState>(desc);
    }

    RHI::RHIDepthStencilStateSP DirectX12RHIApi::CreateDepthStencilState(const RHI::RHIDepthStencilStateDesc& desc)
    {
        return std::make_shared<DirectX12DepthStencilState>(desc);
    }

    RHI::RHIVertexShaderSP DirectX12RHIApi::CreateVertexShader(const std::vector<char>& shaderSourceCode)
    {
        return std::make_shared<DirectX12VertexShader>(shaderSourceCode);
    }

    RHI::RHIFragmentShaderSP DirectX12RHIApi::CreateFragmentShader(const std::vector<char>& shaderSourceCode)
    {
        return std::make_shared<DirectX12FragmentShader>(shaderSourceCode);
    }

    RHI::RHIComputeShaderSP DirectX12RHIApi::CreateComputeShader(const std::vector<char>& shaderSourceCode)
    {
        return std::make_shared<DirectX12ComputeShader>(shaderSourceCode);
    }

    RHI::RHIGeometryShaderSP DirectX12RHIApi::CreateGeometryShader(const std::vector<char>& shaderSourceCode)
    {
        return std::make_shared<DirectX12GeometryShader>(shaderSourceCode);
    }

    RHI::RHITessControlShaderSP DirectX12RHIApi::CreateTessControlShader(const std::vector<char>& shaderSourceCode)
    {
        return std::make_shared<DirectX12TessControlShader>(shaderSourceCode);
    }

    RHI::RHITessEvalShaderSP DirectX12RHIApi::CreateTessEvalShader(const std::vector<char>& shaderSourceCode)
    {
        return std::make_shared<DirectX12TessEvalShader>(shaderSourceCode);
    }

    RHI::RHIMeshShaderSP DirectX12RHIApi::CreateMeshShader(const std::vector<char>& shaderSourceCode)
    {
        return std::make_shared<DirectX12MeshShader>(shaderSourceCode);
    }

    RHI::RHITaskShaderSP DirectX12RHIApi::CreateTaskShader(const std::vector<char>& shaderSourceCode)
    {
        return std::make_shared<DirectX12TaskShader>(shaderSourceCode);
    }

    RHI::RHIRayGenShaderSP DirectX12RHIApi::CreateRayGenShader(const std::vector<char>& shaderSourceCode)
    {
        return std::make_shared<DirectX12RayTracingShader<RHI::RHIRayGenShader>>(shaderSourceCode);
    }

    RHI::RHICloseHitShaderSP DirectX12RHIApi::CreateCloseHitShader(const std::vector<char>& shaderSourceCode)
    {
        return std::make_shared<DirectX12RayTracingShader<RHI::RHICloseHitShader>>(shaderSourceCode);
    }

    RHI::RHIMissShaderSP DirectX12RHIApi::CreateMissShader(const std::vector<char>& shaderSourceCode)
    {
        return std::make_shared<DirectX12RayTracingShader<RHI::RHIMissShader>>(shaderSourceCode);
    }

    RHI::RHIAnyHitShaderSP DirectX12RHIApi::CreateAnyHitShader(const std::vector<char>& shaderSourceCode)
    {
        return std::make_shared<DirectX12RayTracingShader<RHI::RHIAnyHitShader>>(shaderSourceCode);
    }

    RHI::RHIIntersectionShaderSP DirectX12RHIApi::CreateIntersectionShader(const std::vector<char>& shaderSourceCode)
    {
        return std::make_shared<DirectX12RayTracingShader<RHI::RHIIntersectionShader>>(shaderSourceCode);
    }

    RHI::RHICallableShaderSP DirectX12RHIApi::CreateCallableShader(const std::vector<char>& shaderSourceCode)
    {
        return std::make_shared<DirectX12RayTracingShader<RHI::RHICallableShader>>(shaderSourceCode);
    }

    RHI::RHISwapchainSP DirectX12RHIApi::CreateSwapchain(void* inWindowHandle, uint32_t w, uint32_t h,
                                                         RHI::ERHIFormat format)
    {
        return std::make_shared<DirectX12Swapchain>(m_Device.get(), m_GraphicsQueue.get(), m_ComputeQueue.get(),
                                                    inWindowHandle, w, h, format);
    }

    RHI::RHISamplerSP DirectX12RHIApi::CreateSampler(const RHI::RHISamplerDesc& desc)
    {
        return std::make_shared<DirectX12Sampler>(m_Device.get(), desc);
    }

    RHI::RHIQueue* DirectX12RHIApi::GetQueue(RHI::EQueueType type)
    {
        switch (type)
        {
        case RHI::EQueueType::Compute:
            return m_ComputeQueue.get();
        case RHI::EQueueType::Graphics:
        default:
            return m_GraphicsQueue.get();
        }
    }

    RHI::RHIPresentExecutor* DirectX12RHIApi::GetPresentExecutor()
    {
        return m_PresentExecutor.get();
    }

    void DirectX12RHIApi::RHICreateTransition(RHI::RHITransition* transition,
                                              const RHI::RHITransitionCreateInfo& createInfo)
    {
        if (!transition)
            throw std::invalid_argument("Null RHI transition");
        if (createInfo.Flags != RHI::ERHITransitionCreateFlags::None &&
            createInfo.Flags != RHI::ERHITransitionCreateFlags::NoSplit)
            throw std::invalid_argument("Unsupported transition creation flags");
        for (const auto& info : createInfo.TransitionInfos)
        {
            if (info.Type == RHI::RHITransitionInfo::EType::BVH && !m_PlatformInfo.bEnableRayTracing)
                throw std::runtime_error("The selected DirectX12 adapter does not support DXR");
            if (info.Flags != RHI::EResourceTransitionFlags::None)
                throw std::runtime_error("DirectX12 transition discard/compression flags are not implemented");
        }
        new (transition->GetPrivateData<DirectX12TransitionData>()) DirectX12TransitionData{createInfo.TransitionInfos};
    }

    void DirectX12RHIApi::RHIReleaseTransition(RHI::RHITransition* transition)
    {
        if (transition)
            transition->GetPrivateData<DirectX12TransitionData>()->~DirectX12TransitionData();
    }

    RHI::RHITransientResourceManagerSP DirectX12RHIApi::CreateTransientResourceManager()
    {
        return std::make_shared<DirectX12TransientResourceManager>(m_Device.get());
    }

    DirectX12RHIModule::DirectX12RHIModule() = default;
    DirectX12RHIModule::~DirectX12RHIModule() = default;

    void DirectX12RHIModule::StartupModule()
    {
        if (m_bLoaded)
        {
            return;
        }

        RHI::GRHIApi = CreateRHIApi();
        if (!RHI::GRHIApi)
        {
            m_bLoaded = false;
            return;
        }

        m_bLoaded = RHI::GRHIApi->Init();
        if (!m_bLoaded)
        {
            delete RHI::GRHIApi;
            RHI::GRHIApi = nullptr;
        }
    }

    void DirectX12RHIModule::ShutdownModule()
    {
        if (RHI::GRHIApi)
        {
            RHI::GRHIApi->Shutdown();
            delete RHI::GRHIApi;
            RHI::GRHIApi = nullptr;
        }
        m_bLoaded = false;
    }

    bool DirectX12RHIModule::IsLoaded() const
    {
        return m_bLoaded;
    }

    RHI::RHIApi* DirectX12RHIModule::CreateRHIApi()
    {
        return new DirectX12RHIApi();
    }

    IMPLEMENT_SIMPLE_MODULE(DirectX12RHIModule, "RHIDirectX12");
} // namespace RHIDirectX12
