#include "DirectX12RHIApi.h"
#include "DirectX12Device.h"
#include "DirectX12PipelineState.h"
#include "DirectX12Queue.h"
#include "DirectX12Resource.h"
#include "RHITransition.h"
#include "DirectX12Context.h"
#include <cstddef>
static DXGI_FORMAT ToDxgiFormat(RHI::ERHIFormat format)
{
    switch (format)
    {
    case RHI::ERHIFormat::R8_UNorm: return DXGI_FORMAT_R8_UNORM;
    case RHI::ERHIFormat::R8G8B8A8_UNorm: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case RHI::ERHIFormat::R8G8B8A8_SRGB: return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    case RHI::ERHIFormat::B8G8R8A8_UNorm: return DXGI_FORMAT_B8G8R8A8_UNORM;
    case RHI::ERHIFormat::B8G8R8A8_SRGB: return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    case RHI::ERHIFormat::R16G16_Float: return DXGI_FORMAT_R16G16_FLOAT;
    case RHI::ERHIFormat::R16G16B16A16_Float: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case RHI::ERHIFormat::R32_Float: return DXGI_FORMAT_R32_FLOAT;
    case RHI::ERHIFormat::R32G32_Float: return DXGI_FORMAT_R32G32_FLOAT;
    case RHI::ERHIFormat::R32G32B32_Float: return DXGI_FORMAT_R32G32B32_FLOAT;
    case RHI::ERHIFormat::R32G32B32A32_Float: return DXGI_FORMAT_R32G32B32A32_FLOAT;
    case RHI::ERHIFormat::D24_UNorm_S8_UInt: return DXGI_FORMAT_D24_UNORM_S8_UINT;
    case RHI::ERHIFormat::D32_Float: return DXGI_FORMAT_D32_FLOAT;
    case RHI::ERHIFormat::R16_UInt: return DXGI_FORMAT_R16_UINT;
    case RHI::ERHIFormat::R32_UInt: return DXGI_FORMAT_R32_UINT;
    default: return DXGI_FORMAT_UNKNOWN;
    }
}
namespace RHIDirectX12
{
    DirectX12RHIApi::DirectX12RHIApi() = default;

    DirectX12RHIApi::~DirectX12RHIApi()
    {
        Shutdown();
    }

    bool DirectX12RHIApi::Init()
    {
        if (bInitialized)
        {
            return true;
        }

        RHI::GShaderPlatform = RHI::ERHIShaderPlatform::D3D12;
        PlatformInfo.DepthRange = RHI::EDepthRange::ZeroToOne;
        PlatformInfo.EnableRayTracing = false;

        const uint32_t headerSize = static_cast<uint32_t>(sizeof(RHI::RHITransition));
        const uint32_t alignment = static_cast<uint32_t>(alignof(std::max_align_t));
        const uint32_t privateDataSize = 1;

        RHI::G_RHITransition_PrivateDataOffset = (headerSize + alignment - 1) & ~(alignment - 1);
        RHI::G_RHITransition_TotalSize = RHI::G_RHITransition_PrivateDataOffset + privateDataSize;

        Device = std::make_unique<DirectX12Device>();
        if (!Device->Init())
        {
            Device.reset();
            return false;
        }

#if defined(_WIN32)
        GraphicsQueue = std::make_unique<DirectX12Queue>(RHI::EQueueType::Graphics, Device.get(), Device->GetGraphicsCommandQueue());
        ComputeQueue = std::make_unique<DirectX12Queue>(RHI::EQueueType::Compute, Device.get(), Device->GetComputeCommandQueue());
#else
        GraphicsQueue = nullptr;
        ComputeQueue = nullptr;
#endif

        if (!GraphicsQueue)
        {
            Device->Shutdown();
            Device.reset();
            return false;
        }

        if (!ComputeQueue)
        {
            ComputeQueue = std::make_unique<DirectX12Queue>(RHI::EQueueType::Compute, Device.get(), Device->GetGraphicsCommandQueue());
        }

        PresentExecutor = std::make_unique<DirectX12PresentExecutor>(GraphicsQueue.get());

        bInitialized = true;
        return true;
    }

    void DirectX12RHIApi::Shutdown()
    {
        PresentExecutor.reset();
        ComputeQueue.reset();
        GraphicsQueue.reset();
        if (Device)
        {
            Device->Shutdown();
            Device.reset();
        }
        bInitialized = false;
    }

    const RHI::RHIPlatformInfo& DirectX12RHIApi::GetPlatformInfo() const
    {
        return PlatformInfo;
    }

    RHI::RHITextureSP DirectX12RHIApi::CreateTexture(const RHI::RHITextureDesc& desc)
    {
        return std::make_shared<DirectX12Texture>(Device.get(), desc);
    }

    RHI::RHIBufferSP DirectX12RHIApi::CreateBuffer(const RHI::RHIBufferDesc& desc)
    {
        return std::make_shared<DirectX12Buffer>(Device.get(), desc);
    }

    void DirectX12RHIApi::UpdateTexture(RHI::RHICommandListBase& cmdList, RHI::RHITexture* texture, const void* data, const RHI::RHIUpdateTextureRegion& size)
    {
        if (!texture || !data || !Device || !Device->GetNativeDevice())
        {
            return;
        }

        auto* dxTexture = dynamic_cast<DirectX12Texture*>(texture);
        if (!dxTexture)
        {
            return;
        }

        auto* ctx = dynamic_cast<DirectX12CommandContext*>(cmdList.GetContext());
        if (!ctx || !ctx->GetNativeCommandList())
        {
            return;
        }

        const auto& desc = dxTexture->GetDesc();
        const auto formatInfo = RHI::GFormatInfoMap.find(desc.Format);
        if (formatInfo == RHI::GFormatInfoMap.end() || formatInfo->second.BytesPerPixel == 0)
        {
            return;
        }

        const uint32_t bytesPerPixel = formatInfo->second.BytesPerPixel;
        const uint32_t rowPitch = std::max<uint32_t>(1u, ((size.width * bytesPerPixel + 255u) / 256u) * 256u);
        const uint32_t totalBytes = rowPitch * size.height * std::max<uint32_t>(1u, size.depth);
        auto staging = std::make_shared<DirectX12StagingBuffer>(Device.get(), totalBytes, D3D12_HEAP_TYPE_UPLOAD);
        if (!staging || !staging->GetNativeResource())
        {
            return;
        }

        void* mapped = staging->Map(0, totalBytes);
        if (!mapped)
        {
            return;
        }

        std::memcpy(mapped, data, totalBytes);
        staging->Unmap();

        D3D12_TEXTURE_COPY_LOCATION srcLocation{};
        srcLocation.pResource = staging->GetNativeResource();
        srcLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        srcLocation.PlacedFootprint.Offset = 0;
        srcLocation.PlacedFootprint.Footprint.Format = ToDxgiFormat(desc.Format);
        srcLocation.PlacedFootprint.Footprint.Width = size.width;
        srcLocation.PlacedFootprint.Footprint.Height = size.height;
        srcLocation.PlacedFootprint.Footprint.Depth = size.depth;
        srcLocation.PlacedFootprint.Footprint.RowPitch = rowPitch;

        D3D12_TEXTURE_COPY_LOCATION dstLocation{};
        dstLocation.pResource = dxTexture->GetNativeResource();
        dstLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dstLocation.SubresourceIndex = size.mipLevel + size.arraySlice;

        D3D12_BOX sourceBox{};
        sourceBox.left = 0;
        sourceBox.top = 0;
        sourceBox.front = 0;
        sourceBox.right = static_cast<LONG>(size.width);
        sourceBox.bottom = static_cast<LONG>(size.height);
        sourceBox.back = static_cast<LONG>(size.depth);

        D3D12_RESOURCE_BARRIER barrierBefore{};
        barrierBefore.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrierBefore.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrierBefore.Transition.pResource = dxTexture->GetNativeResource();
        barrierBefore.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
        barrierBefore.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        barrierBefore.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        ctx->GetNativeCommandList()->ResourceBarrier(1, &barrierBefore);

        ctx->GetNativeCommandList()->CopyTextureRegion(&dstLocation,
            size.xOffset, size.yOffset, size.zOffset,
            &srcLocation, &sourceBox);

        D3D12_RESOURCE_BARRIER barrierAfter{};
        barrierAfter.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrierAfter.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrierAfter.Transition.pResource = dxTexture->GetNativeResource();
        barrierAfter.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barrierAfter.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
        barrierAfter.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        ctx->GetNativeCommandList()->ResourceBarrier(1, &barrierAfter);
    }

    void DirectX12RHIApi::UpdateBuffer(RHI::RHICommandListBase& cmdList, RHI::RHIBuffer* buffer, const void* data, const RHI::RHIUpdateBufferRegion& region)
    {
        if (!buffer || !data || !Device || !Device->GetNativeDevice())
        {
            return;
        }

        auto* dxBuffer = dynamic_cast<DirectX12Buffer*>(buffer);
        if (!dxBuffer)
        {
            return;
        }

        auto* ctx = dynamic_cast<DirectX12CommandContext*>(cmdList.GetContext());
        if (!ctx || !ctx->GetNativeCommandList())
        {
            return;
        }

        const uint64_t byteSize = region.size == 0 ? static_cast<uint64_t>(buffer->GetDesc().Size) : static_cast<uint64_t>(region.size);
        auto staging = std::make_shared<DirectX12StagingBuffer>(Device.get(), static_cast<uint32_t>(byteSize), D3D12_HEAP_TYPE_UPLOAD);
        if (!staging || !staging->GetNativeResource())
        {
            return;
        }

        void* mapped = staging->Map(0, static_cast<uint32_t>(byteSize));
        if (!mapped)
        {
            return;
        }

        std::memcpy(mapped, data, static_cast<size_t>(byteSize));
        staging->Unmap();

        D3D12_RESOURCE_BARRIER barrierBefore{};
        barrierBefore.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrierBefore.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrierBefore.Transition.pResource = dxBuffer->GetNativeResource();
        barrierBefore.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
        barrierBefore.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        barrierBefore.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        ctx->GetNativeCommandList()->ResourceBarrier(1, &barrierBefore);

        ctx->GetNativeCommandList()->CopyBufferRegion(dxBuffer->GetNativeResource(), region.offset, staging->GetNativeResource(), 0, byteSize);

        D3D12_RESOURCE_BARRIER barrierAfter{};
        barrierAfter.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrierAfter.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrierAfter.Transition.pResource = dxBuffer->GetNativeResource();
        barrierAfter.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barrierAfter.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
        barrierAfter.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        ctx->GetNativeCommandList()->ResourceBarrier(1, &barrierAfter);
    }

    void* DirectX12RHIApi::MapReadTexture(RHI::RHICommandListBase& cmdList, RHI::RHITexture* texture, const RHI::RHIReadTextureInfo& info)
    {
        if (!texture || !Device || !Device->GetNativeDevice())
        {
            return nullptr;
        }

        auto* dxTexture = dynamic_cast<DirectX12Texture*>(texture);
        if (!dxTexture)
        {
            return nullptr;
        }

        auto* ctx = dynamic_cast<DirectX12CommandContext*>(cmdList.GetContext());
        if (!ctx || !ctx->GetNativeCommandList())
        {
            return nullptr;
        }

        const auto& desc = dxTexture->GetDesc();
        const auto formatInfo = RHI::GFormatInfoMap.find(desc.Format);
        if (formatInfo == RHI::GFormatInfoMap.end() || formatInfo->second.BytesPerPixel == 0)
        {
            return nullptr;
        }

        const auto mipSize = dxTexture->GetMipSize(info.MipLevel);
        const uint32_t width = std::max(1u, static_cast<uint32_t>(mipSize.x));
        const uint32_t height = std::max(1u, static_cast<uint32_t>(mipSize.y));
        const uint32_t depth = std::max(1u, static_cast<uint32_t>(mipSize.z));
        const uint32_t rowPitch = std::max<uint32_t>(1u, ((width * formatInfo->second.BytesPerPixel + 255u) / 256u) * 256u);
        const uint32_t totalBytes = rowPitch * height * depth;

        auto staging = std::make_shared<DirectX12StagingBuffer>(Device.get(), totalBytes, D3D12_HEAP_TYPE_READBACK);
        if (!staging || !staging->GetNativeResource())
        {
            return nullptr;
        }

        D3D12_RESOURCE_BARRIER barrierBefore{};
        barrierBefore.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrierBefore.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrierBefore.Transition.pResource = dxTexture->GetNativeResource();
        barrierBefore.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
        barrierBefore.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        barrierBefore.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        ctx->GetNativeCommandList()->ResourceBarrier(1, &barrierBefore);

        D3D12_TEXTURE_COPY_LOCATION srcLocation{};
        srcLocation.pResource = dxTexture->GetNativeResource();
        srcLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        srcLocation.SubresourceIndex = info.MipLevel + info.ArraySlice;

        D3D12_TEXTURE_COPY_LOCATION dstLocation{};
        dstLocation.pResource = staging->GetNativeResource();
        dstLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dstLocation.PlacedFootprint.Offset = 0;
        dstLocation.PlacedFootprint.Footprint.Format = ToDxgiFormat(desc.Format);
        dstLocation.PlacedFootprint.Footprint.Width = width;
        dstLocation.PlacedFootprint.Footprint.Height = height;
        dstLocation.PlacedFootprint.Footprint.Depth = depth;
        dstLocation.PlacedFootprint.Footprint.RowPitch = rowPitch;

        D3D12_BOX copyBox{};
        copyBox.left = 0;
        copyBox.top = 0;
        copyBox.front = 0;
        copyBox.right = static_cast<LONG>(width);
        copyBox.bottom = static_cast<LONG>(height);
        copyBox.back = static_cast<LONG>(depth);
        ctx->GetNativeCommandList()->CopyTextureRegion(&dstLocation, 0, 0, 0, &srcLocation, &copyBox);

        D3D12_RESOURCE_BARRIER barrierAfter{};
        barrierAfter.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrierAfter.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrierAfter.Transition.pResource = dxTexture->GetNativeResource();
        barrierAfter.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        barrierAfter.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
        barrierAfter.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        ctx->GetNativeCommandList()->ResourceBarrier(1, &barrierAfter);

        void* mapped = staging->Map(0, totalBytes);
        if (!mapped)
        {
            return nullptr;
        }

        MappedStagingBuffers[mapped] = staging;
        return mapped;
    }

    void* DirectX12RHIApi::MapReadBuffer(RHI::RHICommandListBase& cmdList, RHI::RHIBuffer* buffer, const RHI::RHIReadBufferInfo& info)
    {
        if (!buffer || !Device || !Device->GetNativeDevice())
        {
            return nullptr;
        }

        auto* dxBuffer = dynamic_cast<DirectX12Buffer*>(buffer);
        if (!dxBuffer)
        {
            return nullptr;
        }

        auto* ctx = dynamic_cast<DirectX12CommandContext*>(cmdList.GetContext());
        if (!ctx || !ctx->GetNativeCommandList())
        {
            return nullptr;
        }

        const uint64_t offset = info.offset;
        const uint64_t size = info.size == 0 ? buffer->GetDesc().Size - offset : info.size;
        if (size == 0)
        {
            return nullptr;
        }

        auto staging = std::make_shared<DirectX12StagingBuffer>(Device.get(), static_cast<uint32_t>(size), D3D12_HEAP_TYPE_READBACK);
        if (!staging || !staging->GetNativeResource())
        {
            return nullptr;
        }

        D3D12_RESOURCE_BARRIER barrierBefore{};
        barrierBefore.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrierBefore.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrierBefore.Transition.pResource = dxBuffer->GetNativeResource();
        barrierBefore.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
        barrierBefore.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        barrierBefore.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        ctx->GetNativeCommandList()->ResourceBarrier(1, &barrierBefore);

        ctx->GetNativeCommandList()->CopyBufferRegion(staging->GetNativeResource(), 0, dxBuffer->GetNativeResource(), offset, size);

        D3D12_RESOURCE_BARRIER barrierAfter{};
        barrierAfter.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrierAfter.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrierAfter.Transition.pResource = dxBuffer->GetNativeResource();
        barrierAfter.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        barrierAfter.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
        barrierAfter.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        ctx->GetNativeCommandList()->ResourceBarrier(1, &barrierAfter);

        void* mapped = staging->Map(0, static_cast<uint32_t>(size));
        if (!mapped)
        {
            return nullptr;
        }

        MappedStagingBuffers[mapped] = staging;
        return mapped;
    }

    void DirectX12RHIApi::Unmap(void* mappedData)
    {
        if (!mappedData)
        {
            return;
        }

        auto it = MappedStagingBuffers.find(mappedData);
        if (it == MappedStagingBuffers.end())
        {
            return;
        }

        it->second->Unmap();
        MappedStagingBuffers.erase(it);
    }

    RHI::RHIShaderResourceViewSP DirectX12RHIApi::CreateTextureShaderResourceView(
        RHI::RHITexture* Texture, const RHI::RHITexSRVCreateInfo& Desc)
    {
        auto* viewable = dynamic_cast<RHI::RHIViewableResource*>(Texture);
        if (!viewable)
        {
            return nullptr;
        }
        return std::make_shared<DirectX12ShaderResourceView>(viewable, Desc);
    }

    RHI::RHIUnorderedAccessViewSP DirectX12RHIApi::CreateTextureUnorderedAccessView(
        RHI::RHITexture* Texture, const RHI::RHITexUAVCreateInfo& Desc)
    {
        auto* viewable = dynamic_cast<RHI::RHIViewableResource*>(Texture);
        if (!viewable)
        {
            return nullptr;
        }
        return std::make_shared<DirectX12UnorderedAccessView>(viewable, Desc);
    }

    RHI::RHIShaderResourceViewSP DirectX12RHIApi::CreateBufferShaderResourceView(
        RHI::RHIBuffer* Buffer, const RHI::RHIBufferSRVCreateInfo& Desc)
    {
        auto* viewable = dynamic_cast<RHI::RHIViewableResource*>(Buffer);
        if (!viewable)
        {
            return nullptr;
        }
        return std::make_shared<DirectX12ShaderResourceView>(viewable, Desc);
    }

    RHI::RHIUnorderedAccessViewSP DirectX12RHIApi::CreateBufferUnorderedAccessView(
        RHI::RHIBuffer* Buffer, const RHI::RHIBufferUAVCreateInfo& Desc)
    {
        auto* viewable = dynamic_cast<RHI::RHIViewableResource*>(Buffer);
        if (!viewable)
        {
            return nullptr;
        }
        return std::make_shared<DirectX12UnorderedAccessView>(viewable, Desc);
    }

    RHI::RHIRayTracingGeometrySP DirectX12RHIApi::CreateRayTracingGeometry(const RHI::RHIRayTracingGeometryDesc& desc)
    {
        (void)desc;
        return nullptr;
    }

    RHI::RHIRayTracingInstanceSP DirectX12RHIApi::CreateRayTracingInstance(const RHI::RHIRayTracingInstancesDesc& desc)
    {
        (void)desc;
        return nullptr;
    }

    RHI::RHIStagingBufferSP DirectX12RHIApi::CreateStagingBuffer(uint32_t size)
    {
        return std::make_shared<DirectX12StagingBuffer>(Device.get(), size);
    }

    RHI::RHIGraphicsPipelineStateSP DirectX12RHIApi::CreateGraphicsPipelineState(const RHI::RHIGraphicsPipelineStateDesc& desc)
    {
        return std::make_shared<DirectX12GraphicsPipelineState>(Device.get(), desc);
    }

    RHI::RHIComputePipelineStateSP DirectX12RHIApi::CreateComputePipelineState(const RHI::RHIComputePipelineStateDesc& desc)
    {
        return std::make_shared<DirectX12ComputePipelineState>(Device.get(), desc);
    }

    RHI::RHIRayTracingPipelineStateSP DirectX12RHIApi::CreateRayTracingsPipelineState(const RHI::RHIRayTracingPipelineStateDesc& desc)
    {
        (void)desc;
        return nullptr;
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
        (void)shaderSourceCode;
        return nullptr;
    }

    RHI::RHICloseHitShaderSP DirectX12RHIApi::CreateCloseHitShader(const std::vector<char>& shaderSourceCode)
    {
        (void)shaderSourceCode;
        return nullptr;
    }

    RHI::RHIMissShaderSP DirectX12RHIApi::CreateMissShader(const std::vector<char>& shaderSourceCode)
    {
        (void)shaderSourceCode;
        return nullptr;
    }

    RHI::RHIAnyHitShaderSP DirectX12RHIApi::CreateAnyHitShader(const std::vector<char>& shaderSourceCode)
    {
        (void)shaderSourceCode;
        return nullptr;
    }

    RHI::RHIIntersectionShaderSP DirectX12RHIApi::CreateIntersectionShader(const std::vector<char>& shaderSourceCode)
    {
        (void)shaderSourceCode;
        return nullptr;
    }

    RHI::RHICallableShaderSP DirectX12RHIApi::CreateCallableShader(const std::vector<char>& shaderSourceCode)
    {
        (void)shaderSourceCode;
        return nullptr;
    }

    RHI::RHISwapchainSP DirectX12RHIApi::CreateSwapchain(void* inWindowHandle, uint32_t w, uint32_t h, RHI::ERHIFormat format)
    {
        return std::make_shared<DirectX12Swapchain>(Device.get(), inWindowHandle, w, h, format);
    }

    RHI::RHISamplerSP DirectX12RHIApi::CreateSampler(const RHI::RHISamplerDesc& desc)
    {
        return std::make_shared<DirectX12Sampler>(desc);
    }

    RHI::RHIQueue* DirectX12RHIApi::GetQueue(RHI::EQueueType Type)
    {
        switch (Type)
        {
        case RHI::EQueueType::Compute:
            return ComputeQueue.get();
        case RHI::EQueueType::Graphics:
        default:
            return GraphicsQueue.get();
        }
    }

    RHI::RHIPresentExecutor* DirectX12RHIApi::GetPresentExecutor()
    {
        return PresentExecutor.get();
    }

    void DirectX12RHIApi::RHICreateTransition(RHI::RHITransition* Transition, const RHI::RHITransitionCreateInfo& CreateInfo)
    {
        (void)Transition;
        (void)CreateInfo;
    }

    void DirectX12RHIApi::RHIReleaseTransition(RHI::RHITransition* Transition)
    {
        (void)Transition;
    }

    RHI::RHITransientResourceManagerSP DirectX12RHIApi::CreateTransientResourceManager()
    {
        return nullptr;
    }

    DirectX12RHIModule::DirectX12RHIModule() = default;
    DirectX12RHIModule::~DirectX12RHIModule() = default;

    void DirectX12RHIModule::StartupModule()
    {
        if (bLoaded)
        {
            return;
        }

        RHI::GRHIApi = CreateRHIApi();
        if (!RHI::GRHIApi)
        {
            bLoaded = false;
            return;
        }

        bLoaded = RHI::GRHIApi->Init();
        if (!bLoaded)
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
        bLoaded = false;
    }

    bool DirectX12RHIModule::IsLoaded() const
    {
        return bLoaded;
    }

    RHI::RHIApi* DirectX12RHIModule::CreateRHIApi()
    {
        return new DirectX12RHIApi();
    }

    IMPLEMENT_SIMPLE_MODULE(DirectX12RHIModule, "RHIDirectX12");
}
