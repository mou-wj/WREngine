#include "DirectX12Resource.h"
#include "DirectX12Device.h"
#include "ShaderCompiledDataPacker.h"
#include <algorithm>
#include <string>

#if defined(_WIN32)
#include <cassert>
#endif

namespace
{
    static std::vector<char> UnpackDXIL(const std::vector<char>& packedCode)
    {
        RenderCore::HLSLCompiledBinaryResultPacker packer;
        packer.Depack(packedCode);
        if (!packer.DepackedData.DXILCode.empty())
        {
            return packer.DepackedData.DXILCode;
        }
        return packedCode;
    }

#if defined(_WIN32)
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

    static D3D12_RESOURCE_DIMENSION ToResourceDimension(RHI::ERHITextureType type)
    {
        switch (type)
        {
        case RHI::ERHITextureType::Texture1D:
            return D3D12_RESOURCE_DIMENSION_TEXTURE1D;
        case RHI::ERHITextureType::Texture3D:
            return D3D12_RESOURCE_DIMENSION_TEXTURE3D;
        case RHI::ERHITextureType::Texture2D:
        case RHI::ERHITextureType::Texture2DArray:
        case RHI::ERHITextureType::TextureCube:
        case RHI::ERHITextureType::TextureCubeArray:
        default:
            return D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        }
    }

    static D3D12_RESOURCE_FLAGS ToTextureFlags(RHI::ERHITextureCreateFlags flags)
    {
        D3D12_RESOURCE_FLAGS resourceFlags = D3D12_RESOURCE_FLAG_NONE;

        if (EnumHasAnyFlags(flags, RHI::ERHITextureCreateFlag::RenderTarget) ||
            EnumHasAnyFlags(flags, RHI::ERHITextureCreateFlag::Presentable))
        {
            resourceFlags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        }
        if (EnumHasAnyFlags(flags, RHI::ERHITextureCreateFlag::DepthStencil))
        {
            resourceFlags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        }
        if (EnumHasAnyFlags(flags, RHI::ERHITextureCreateFlag::UAV))
        {
            resourceFlags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        }

        if (!EnumHasAnyFlags(flags, RHI::ERHITextureCreateFlag::ShaderResource) &&
            !EnumHasAnyFlags(flags, RHI::ERHITextureCreateFlag::UAV))
        {
            resourceFlags |= D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
        }

        return resourceFlags;
    }

    static D3D12_RESOURCE_STATES ToTextureInitialState(RHI::ERHITextureCreateFlags flags)
    {
        if (EnumHasAnyFlags(flags, RHI::ERHITextureCreateFlag::DepthStencil))
        {
            return D3D12_RESOURCE_STATE_DEPTH_WRITE;
        }
        if (EnumHasAnyFlags(flags, RHI::ERHITextureCreateFlag::RenderTarget) ||
            EnumHasAnyFlags(flags, RHI::ERHITextureCreateFlag::Presentable))
        {
            return D3D12_RESOURCE_STATE_RENDER_TARGET;
        }
        if (EnumHasAnyFlags(flags, RHI::ERHITextureCreateFlag::TransferDest))
        {
            return D3D12_RESOURCE_STATE_COPY_DEST;
        }
        if (EnumHasAnyFlags(flags, RHI::ERHITextureCreateFlag::TransferSrc))
        {
            return D3D12_RESOURCE_STATE_COPY_SOURCE;
        }
        return D3D12_RESOURCE_STATE_COMMON;
    }

    static D3D12_RESOURCE_FLAGS ToBufferFlags(RHI::ERHIBufferUsageFlags usage)
    {
        D3D12_RESOURCE_FLAGS resourceFlags = D3D12_RESOURCE_FLAG_NONE;
        if (EnumHasAnyFlags(usage, RHI::ERHIBufferUsageFlag::UnorderedAccess))
        {
            resourceFlags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        }
        return resourceFlags;
    }

    static D3D12_RESOURCE_STATES ToBufferInitialState(RHI::ERHIBufferUsageFlags usage)
    {
        if (EnumHasAnyFlags(usage, RHI::ERHIBufferUsageFlag::TransferDst))
        {
            return D3D12_RESOURCE_STATE_COPY_DEST;
        }
        if (EnumHasAnyFlags(usage, RHI::ERHIBufferUsageFlag::TransferSrc) ||
            EnumHasAnyFlags(usage, RHI::ERHIBufferUsageFlag::Staging))
        {
            return D3D12_RESOURCE_STATE_COPY_SOURCE;
        }
        if (EnumHasAnyFlags(usage, RHI::ERHIBufferUsageFlag::Index))
        {
            return D3D12_RESOURCE_STATE_INDEX_BUFFER;
        }
        if (EnumHasAnyFlags(usage, RHI::ERHIBufferUsageFlag::Vertex) ||
            EnumHasAnyFlags(usage, RHI::ERHIBufferUsageFlag::Constant))
        {
            return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        }
        if (EnumHasAnyFlags(usage, RHI::ERHIBufferUsageFlag::Indirect))
        {
            return D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
        }
        if (EnumHasAnyFlags(usage, RHI::ERHIBufferUsageFlag::UnorderedAccess))
        {
            return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        }
        return D3D12_RESOURCE_STATE_COMMON;
    }

    static std::wstring ToWide(const char* text)
    {
        if (!text || text[0] == '\0')
        {
            return std::wstring();
        }

        const int required = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
        if (required <= 0)
        {
            return std::wstring();
        }

        std::wstring result;
        result.resize(static_cast<size_t>(required));
        MultiByteToWideChar(CP_UTF8, 0, text, -1, result.data(), required);
        if (!result.empty() && result.back() == L'\0')
        {
            result.pop_back();
        }
        return result;
    }

    static DXGI_FORMAT ToDxgiSrvFormat(RHI::ERHIFormat format, DXGI_FORMAT fallback)
    {
        const DXGI_FORMAT translated = ToDxgiFormat(format);
        return translated == DXGI_FORMAT_UNKNOWN ? fallback : translated;
    }

    static D3D12_SRV_DIMENSION ToSrvDimension(RHI::ERHITextureType textureType, RHI::ERHITextureViewType viewType)
    {
        const RHI::ERHITextureViewType resolved = viewType == RHI::ERHITextureViewType::Derived
            ? (textureType == RHI::ERHITextureType::Texture1D ? RHI::ERHITextureViewType::TextureView1D
                : textureType == RHI::ERHITextureType::Texture3D ? RHI::ERHITextureViewType::TextureView3D
                : textureType == RHI::ERHITextureType::TextureCube ? RHI::ERHITextureViewType::TextureViewCube
                : textureType == RHI::ERHITextureType::Texture2DArray ? RHI::ERHITextureViewType::TextureView2DArray
                : textureType == RHI::ERHITextureType::TextureCubeArray ? RHI::ERHITextureViewType::TextureViewCubeArray
                : RHI::ERHITextureViewType::TextureView2D)
            : viewType;

        switch (resolved)
        {
        case RHI::ERHITextureViewType::TextureView1D: return D3D12_SRV_DIMENSION_TEXTURE1D;
        case RHI::ERHITextureViewType::TextureView3D: return D3D12_SRV_DIMENSION_TEXTURE3D;
        case RHI::ERHITextureViewType::TextureViewCube: return D3D12_SRV_DIMENSION_TEXTURECUBE;
        case RHI::ERHITextureViewType::TextureView2DArray: return D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        case RHI::ERHITextureViewType::TextureViewCubeArray: return D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
        case RHI::ERHITextureViewType::TextureView2D:
        default:
            return D3D12_SRV_DIMENSION_TEXTURE2D;
        }
    }

    static D3D12_UAV_DIMENSION ToUavDimension(RHI::ERHITextureType textureType, RHI::ERHITextureViewType viewType)
    {
        const RHI::ERHITextureViewType resolved = viewType == RHI::ERHITextureViewType::Derived
            ? (textureType == RHI::ERHITextureType::Texture1D ? RHI::ERHITextureViewType::TextureView1D
                : textureType == RHI::ERHITextureType::Texture3D ? RHI::ERHITextureViewType::TextureView3D
                : textureType == RHI::ERHITextureType::Texture2DArray ? RHI::ERHITextureViewType::TextureView2DArray
                : RHI::ERHITextureViewType::TextureView2D)
            : viewType;

        switch (resolved)
        {
        case RHI::ERHITextureViewType::TextureView1D: return D3D12_UAV_DIMENSION_TEXTURE1D;
        case RHI::ERHITextureViewType::TextureView3D: return D3D12_UAV_DIMENSION_TEXTURE3D;
        case RHI::ERHITextureViewType::TextureView2DArray: return D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
        case RHI::ERHITextureViewType::TextureView2D:
        default:
            return D3D12_UAV_DIMENSION_TEXTURE2D;
        }
    }
#endif
}

namespace RHIDirectX12
{
    DirectX12Texture::DirectX12Texture(DirectX12Device* inDevice, const RHI::RHITextureDesc& desc)
        : RHI::RHITexture(desc)
        , Device(inDevice)
    {
#if defined(_WIN32)
        if (!Device || !Device->GetNativeDevice())
        {
            return;
        }

        NativeFormat = ToDxgiFormat(desc.Format);
        const bool isDepth = EnumHasAnyFlags(desc.Usage, RHI::ERHITextureCreateFlag::DepthStencil);

        D3D12_RESOURCE_DESC resourceDesc{};
        resourceDesc.Dimension = ToResourceDimension(desc.Type);
        resourceDesc.Alignment = 0;
        resourceDesc.Width = std::max<uint32_t>(1u, desc.Width);
        resourceDesc.Height = std::max<uint32_t>(1u, desc.Height);
        resourceDesc.DepthOrArraySize = static_cast<UINT16>(
            desc.Type == RHI::ERHITextureType::Texture3D
                ? std::max<uint32_t>(1u, desc.Depth)
                : std::max<uint32_t>(1u, desc.ArraySize));
        resourceDesc.MipLevels = static_cast<UINT16>(std::max<uint32_t>(1u, desc.MipLevels));
        resourceDesc.Format = NativeFormat;
        resourceDesc.SampleDesc.Count = std::max<uint32_t>(1u, desc.SampleCount);
        resourceDesc.SampleDesc.Quality = desc.SampleQuality;
        resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        resourceDesc.Flags = ToTextureFlags(desc.Usage);

        D3D12_HEAP_PROPERTIES heapProperties{};
        heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
        heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heapProperties.CreationNodeMask = 1;
        heapProperties.VisibleNodeMask = 1;

        D3D12_CLEAR_VALUE clearValue{};
        D3D12_CLEAR_VALUE* clearValuePtr = nullptr;
        if (isDepth)
        {
            clearValue.Format = NativeFormat;
            clearValue.DepthStencil.Depth = 1.0f;
            clearValue.DepthStencil.Stencil = 0;
            clearValuePtr = &clearValue;
        }
        else if (EnumHasAnyFlags(desc.Usage, RHI::ERHITextureCreateFlag::RenderTarget) ||
                 EnumHasAnyFlags(desc.Usage, RHI::ERHITextureCreateFlag::Presentable))
        {
            clearValue.Format = NativeFormat;
            clearValue.Color[0] = 0.0f;
            clearValue.Color[1] = 0.0f;
            clearValue.Color[2] = 0.0f;
            clearValue.Color[3] = 1.0f;
            clearValuePtr = &clearValue;
        }

        const bool created = Device->CreateCommittedResource(
            heapProperties,
            D3D12_HEAP_FLAG_NONE,
            resourceDesc,
            ToTextureInitialState(desc.Usage),
            clearValuePtr,
            Resource);
        if (created && desc.DebugName)
        {
            std::wstring debugName = ToWide(desc.DebugName);
            if (!debugName.empty())
            {
                Resource->SetName(debugName.c_str());
            }
        }
#endif
    }

    DirectX12Texture::~DirectX12Texture()
    {
#if defined(_WIN32)
        Resource.Reset();
#endif
    }

    DirectX12Buffer::DirectX12Buffer(DirectX12Device* inDevice, const RHI::RHIBufferDesc& desc)
        : RHI::RHIBuffer(desc)
        , Device(inDevice)
    {
#if defined(_WIN32)
        if (!Device || !Device->GetNativeDevice())
        {
            return;
        }

        D3D12_RESOURCE_DESC resourceDesc{};
        resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        resourceDesc.Alignment = 0;
        resourceDesc.Width = std::max<uint64_t>(1ull, desc.Size);
        resourceDesc.Height = 1;
        resourceDesc.DepthOrArraySize = 1;
        resourceDesc.MipLevels = 1;
        resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
        resourceDesc.SampleDesc.Count = 1;
        resourceDesc.SampleDesc.Quality = 0;
        resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        resourceDesc.Flags = ToBufferFlags(desc.Usage);

        D3D12_HEAP_PROPERTIES heapProperties{};
        heapProperties.CreationNodeMask = 1;
        heapProperties.VisibleNodeMask = 1;
        if (desc.bCPUAccessible || EnumHasAnyFlags(desc.Usage, RHI::ERHIBufferUsageFlag::Staging))
        {
            heapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
            heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
            heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        }
        else
        {
            heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
            heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
            heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        }

        D3D12_RESOURCE_STATES initialState = heapProperties.Type == D3D12_HEAP_TYPE_UPLOAD
            ? D3D12_RESOURCE_STATE_GENERIC_READ
            : ToBufferInitialState(desc.Usage);

        const bool created = Device->CreateCommittedResource(
            heapProperties,
            D3D12_HEAP_FLAG_NONE,
            resourceDesc,
            initialState,
            nullptr,
            Resource);
        if (created && desc.DebugName)
        {
            std::wstring debugName = ToWide(desc.DebugName);
            if (!debugName.empty())
            {
                Resource->SetName(debugName.c_str());
            }
        }
#endif
    }

    DirectX12Buffer::~DirectX12Buffer()
    {
#if defined(_WIN32)
        Resource.Reset();
#endif
    }

    DirectX12ShaderResourceView::DirectX12ShaderResourceView(RHI::RHIViewableResource* resource, const RHI::RHITexSRVCreateInfo& createInfo)
        : RHI::RHIShaderResourceView(resource)
    {
#if defined(_WIN32)
        auto* texture = dynamic_cast<DirectX12Texture*>(resource);
        if (!texture || !texture->GetDevice() || !texture->GetDevice()->GetNativeDevice() || !texture->GetNativeResource())
        {
            return;
        }

        if (!texture->GetDevice()->AllocateCbvSrvUavDescriptor(CpuHandle))
        {
            return;
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = ToDxgiSrvFormat(createInfo.Format, texture->GetNativeFormat());
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.ViewDimension = ToSrvDimension(texture->GetDesc().Type, createInfo.ViewType);

        switch (srvDesc.ViewDimension)
        {
        case D3D12_SRV_DIMENSION_TEXTURE1D:
            srvDesc.Texture1D.MostDetailedMip = createInfo.FirstMipSlice;
            srvDesc.Texture1D.MipLevels = createInfo.MipCount;
            srvDesc.Texture1D.ResourceMinLODClamp = 0.0f;
            break;
        case D3D12_SRV_DIMENSION_TEXTURE2D:
            srvDesc.Texture2D.MostDetailedMip = createInfo.FirstMipSlice;
            srvDesc.Texture2D.MipLevels = createInfo.MipCount;
            srvDesc.Texture2D.PlaneSlice = 0;
            srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
            break;
        case D3D12_SRV_DIMENSION_TEXTURE2DARRAY:
            srvDesc.Texture2DArray.MostDetailedMip = createInfo.FirstMipSlice;
            srvDesc.Texture2DArray.MipLevels = createInfo.MipCount;
            srvDesc.Texture2DArray.FirstArraySlice = createInfo.FirstArraySlice;
            srvDesc.Texture2DArray.ArraySize = createInfo.ArraySize;
            srvDesc.Texture2DArray.PlaneSlice = 0;
            srvDesc.Texture2DArray.ResourceMinLODClamp = 0.0f;
            break;
        case D3D12_SRV_DIMENSION_TEXTURE3D:
            srvDesc.Texture3D.MostDetailedMip = createInfo.FirstMipSlice;
            srvDesc.Texture3D.MipLevels = createInfo.MipCount;
            srvDesc.Texture3D.ResourceMinLODClamp = 0.0f;
            break;
        case D3D12_SRV_DIMENSION_TEXTURECUBE:
            srvDesc.TextureCube.MostDetailedMip = createInfo.FirstMipSlice;
            srvDesc.TextureCube.MipLevels = createInfo.MipCount;
            srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;
            break;
        case D3D12_SRV_DIMENSION_TEXTURECUBEARRAY:
            srvDesc.TextureCubeArray.MostDetailedMip = createInfo.FirstMipSlice;
            srvDesc.TextureCubeArray.MipLevels = createInfo.MipCount;
            srvDesc.TextureCubeArray.First2DArrayFace = createInfo.FirstArraySlice;
            srvDesc.TextureCubeArray.NumCubes = std::max(1u, createInfo.ArraySize / 6);
            srvDesc.TextureCubeArray.ResourceMinLODClamp = 0.0f;
            break;
        default:
            return;
        }

        texture->GetDevice()->GetNativeDevice()->CreateShaderResourceView(texture->GetNativeResource(), &srvDesc, CpuHandle);
        bValid = true;
#else
        (void)createInfo;
#endif
    }

    DirectX12ShaderResourceView::DirectX12ShaderResourceView(RHI::RHIViewableResource* resource, const RHI::RHIBufferSRVCreateInfo& createInfo)
        : RHI::RHIShaderResourceView(resource)
    {
#if defined(_WIN32)
        auto* buffer = dynamic_cast<DirectX12Buffer*>(resource);
        if (!buffer || !buffer->GetDevice() || !buffer->GetDevice()->GetNativeDevice() || !buffer->GetNativeResource())
        {
            return;
        }

        if (!buffer->GetDevice()->AllocateCbvSrvUavDescriptor(CpuHandle))
        {
            return;
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = ToDxgiSrvFormat(createInfo.Format, DXGI_FORMAT_UNKNOWN);
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Buffer.FirstElement = createInfo.Stride == 0 ? 0 : (createInfo.Offset / createInfo.Stride);
        srvDesc.Buffer.NumElements = static_cast<UINT>(createInfo.NumElements);
        srvDesc.Buffer.StructureByteStride = createInfo.Stride;
        srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

        buffer->GetDevice()->GetNativeDevice()->CreateShaderResourceView(buffer->GetNativeResource(), &srvDesc, CpuHandle);
        bValid = true;
#else
        (void)createInfo;
#endif
    }

    DirectX12UnorderedAccessView::DirectX12UnorderedAccessView(RHI::RHIViewableResource* resource, const RHI::RHITexUAVCreateInfo& createInfo)
        : RHI::RHIUnorderedAccessView(resource)
    {
#if defined(_WIN32)
        auto* texture = dynamic_cast<DirectX12Texture*>(resource);
        if (!texture || !texture->GetDevice() || !texture->GetDevice()->GetNativeDevice() || !texture->GetNativeResource())
        {
            return;
        }

        if (!texture->GetDevice()->AllocateCbvSrvUavDescriptor(CpuHandle))
        {
            return;
        }

        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
        uavDesc.Format = ToDxgiSrvFormat(createInfo.Format, texture->GetNativeFormat());
        uavDesc.ViewDimension = ToUavDimension(texture->GetDesc().Type, createInfo.ViewType);
        switch (uavDesc.ViewDimension)
        {
        case D3D12_UAV_DIMENSION_TEXTURE1D:
            uavDesc.Texture1D.MipSlice = createInfo.FirstMipSlice;
            break;
        case D3D12_UAV_DIMENSION_TEXTURE2D:
            uavDesc.Texture2D.MipSlice = createInfo.FirstMipSlice;
            uavDesc.Texture2D.PlaneSlice = 0;
            break;
        case D3D12_UAV_DIMENSION_TEXTURE2DARRAY:
            uavDesc.Texture2DArray.MipSlice = createInfo.FirstMipSlice;
            uavDesc.Texture2DArray.FirstArraySlice = createInfo.FirstArraySlice;
            uavDesc.Texture2DArray.ArraySize = createInfo.ArraySize;
            uavDesc.Texture2DArray.PlaneSlice = 0;
            break;
        case D3D12_UAV_DIMENSION_TEXTURE3D:
            uavDesc.Texture3D.MipSlice = createInfo.FirstMipSlice;
            uavDesc.Texture3D.FirstWSlice = 0;
            uavDesc.Texture3D.WSize = static_cast<UINT>(-1);
            break;
        default:
            return;
        }

        texture->GetDevice()->GetNativeDevice()->CreateUnorderedAccessView(texture->GetNativeResource(), nullptr, &uavDesc, CpuHandle);
        bValid = true;
#else
        (void)createInfo;
#endif
    }

    DirectX12UnorderedAccessView::DirectX12UnorderedAccessView(RHI::RHIViewableResource* resource, const RHI::RHIBufferUAVCreateInfo& createInfo)
        : RHI::RHIUnorderedAccessView(resource)
    {
#if defined(_WIN32)
        auto* buffer = dynamic_cast<DirectX12Buffer*>(resource);
        if (!buffer || !buffer->GetDevice() || !buffer->GetDevice()->GetNativeDevice() || !buffer->GetNativeResource())
        {
            return;
        }

        if (!buffer->GetDevice()->AllocateCbvSrvUavDescriptor(CpuHandle))
        {
            return;
        }

        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
        uavDesc.Format = ToDxgiSrvFormat(createInfo.Format, DXGI_FORMAT_UNKNOWN);
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.FirstElement = createInfo.Stride == 0 ? 0 : (createInfo.Offset / createInfo.Stride);
        uavDesc.Buffer.NumElements = static_cast<UINT>(createInfo.NumElements);
        uavDesc.Buffer.StructureByteStride = createInfo.Stride;
        uavDesc.Buffer.CounterOffsetInBytes = 0;
        uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

        buffer->GetDevice()->GetNativeDevice()->CreateUnorderedAccessView(buffer->GetNativeResource(), nullptr, &uavDesc, CpuHandle);
        bValid = true;
#else
        (void)createInfo;
#endif
    }

    DirectX12StagingBuffer::DirectX12StagingBuffer(DirectX12Device* inDevice, uint32_t size, D3D12_HEAP_TYPE heapType)
        : RHI::RHIStagingBuffer(size)
        , Device(inDevice)
        , Storage(size == 0 ? 1u : size, 0)
    {
#if defined(_WIN32)
        HeapType = heapType;
        if (!Device || !Device->GetNativeDevice())
        {
            return;
        }

        D3D12_HEAP_PROPERTIES heapProperties{};
        heapProperties.Type = heapType;
        heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heapProperties.CreationNodeMask = 1;
        heapProperties.VisibleNodeMask = 1;

        D3D12_RESOURCE_DESC resourceDesc{};
        resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        resourceDesc.Alignment = 0;
        resourceDesc.Width = std::max<uint32_t>(1u, size);
        resourceDesc.Height = 1;
        resourceDesc.DepthOrArraySize = 1;
        resourceDesc.MipLevels = 1;
        resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
        resourceDesc.SampleDesc.Count = 1;
        resourceDesc.SampleDesc.Quality = 0;
        resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

        const D3D12_RESOURCE_STATES initialState = heapType == D3D12_HEAP_TYPE_READBACK ? D3D12_RESOURCE_STATE_COPY_DEST : D3D12_RESOURCE_STATE_GENERIC_READ;
        if (Device->CreateCommittedResource(
                heapProperties,
                D3D12_HEAP_FLAG_NONE,
                resourceDesc,
                initialState,
                nullptr,
                Resource))
        {
            Resource->Map(0, nullptr, &MappedPtr);
        }
#endif
    }

    DirectX12StagingBuffer::~DirectX12StagingBuffer()
    {
#if defined(_WIN32)
        if (Resource && MappedPtr)
        {
            Resource->Unmap(0, nullptr);
            MappedPtr = nullptr;
        }
        Resource.Reset();
#endif
    }

    void* DirectX12StagingBuffer::Map(uint32_t offset, uint32_t numBytes)
    {
        if (offset > Storage.size())
        {
            return nullptr;
        }

        if (numBytes > Storage.size() - offset)
        {
            return nullptr;
        }

        bMapped = true;
#if defined(_WIN32)
        if (MappedPtr)
        {
            return reinterpret_cast<uint8_t*>(MappedPtr) + offset;
        }
#endif
        return Storage.data() + offset;
    }

    void DirectX12StagingBuffer::Unmap()
    {
        bMapped = false;
#if defined(_WIN32)
        if (Resource && MappedPtr)
        {
            Resource->Unmap(0, nullptr);
            MappedPtr = nullptr;
        }
#endif
    }

    DirectX12VertexShader::DirectX12VertexShader(const std::vector<char>& inByteCode)
        : ByteCode(UnpackDXIL(inByteCode))
    {
    }

    DirectX12FragmentShader::DirectX12FragmentShader(const std::vector<char>& inByteCode)
        : ByteCode(UnpackDXIL(inByteCode))
    {
    }

    DirectX12ComputeShader::DirectX12ComputeShader(const std::vector<char>& inByteCode)
        : ByteCode(UnpackDXIL(inByteCode))
    {
    }

    DirectX12GeometryShader::DirectX12GeometryShader(const std::vector<char>& inByteCode)
        : ByteCode(UnpackDXIL(inByteCode))
    {
    }

    DirectX12TessControlShader::DirectX12TessControlShader(const std::vector<char>& inByteCode)
        : ByteCode(UnpackDXIL(inByteCode))
    {
    }

    DirectX12TessEvalShader::DirectX12TessEvalShader(const std::vector<char>& inByteCode)
        : ByteCode(UnpackDXIL(inByteCode))
    {
    }

    DirectX12MeshShader::DirectX12MeshShader(const std::vector<char>& inByteCode)
        : ByteCode(UnpackDXIL(inByteCode))
    {
    }

    DirectX12TaskShader::DirectX12TaskShader(const std::vector<char>& inByteCode)
        : ByteCode(UnpackDXIL(inByteCode))
    {
    }

    DirectX12Swapchain::DirectX12Swapchain(DirectX12Device* inDevice, void* inWindowHandle, uint32_t width, uint32_t height, RHI::ERHIFormat format)
        : Device(inDevice)
        , WindowHandle(inWindowHandle)
        , Width(width)
        , Height(height)
        , Format(format)
    {
        RHI::RHITextureDesc desc{};
        desc.Width = width;
        desc.Height = height;
        desc.Depth = 1;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = format;
        desc.Type = RHI::ERHITextureType::Texture2D;
        desc.Usage = RHI::ERHITextureCreateFlag::RenderTarget | RHI::ERHITextureCreateFlag::Presentable;
        BackBufferTexture = std::make_shared<DirectX12Texture>(Device, desc);
    }

    RHI::RHISwapchain::RHISwapchainSlot DirectX12Swapchain::AcquireNextSlot()
    {
        RHISwapchainSlot slot{};
        slot.Texture = BackBufferTexture.get();
        slot.ReadySync = nullptr;
        return slot;
    }

    void DirectX12Swapchain::Resize(uint32_t width, uint32_t height)
    {
        Width = width;
        Height = height;

        RHI::RHITextureDesc desc{};
        desc.Width = width;
        desc.Height = height;
        desc.Depth = 1;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = Format;
        desc.Type = RHI::ERHITextureType::Texture2D;
        desc.Usage = RHI::ERHITextureCreateFlag::RenderTarget | RHI::ERHITextureCreateFlag::Presentable;
        BackBufferTexture = std::make_shared<DirectX12Texture>(Device, desc);
    }

    DirectX12Sampler::DirectX12Sampler(const RHI::RHISamplerDesc& desc)
        : RHI::RHISampler(desc)
    {
    }

    DirectX12VertexDescState::DirectX12VertexDescState(const RHI::RHIVertexDescStateDesc& desc)
        : RHI::RHIVertexDescState(desc)
    {
    }

    DirectX12RasterizerState::DirectX12RasterizerState(const RHI::RHIRasterizerStateDesc& desc)
        : RHI::RHIRasterizerState(desc)
    {
    }

    DirectX12ColorBlendState::DirectX12ColorBlendState(const RHI::RHIColorBlendStateDesc& desc)
        : RHI::RHIColorBlendState(desc)
    {
    }

    DirectX12DepthStencilState::DirectX12DepthStencilState(const RHI::RHIDepthStencilStateDesc& desc)
        : RHI::RHIDepthStencilState(desc)
    {
    }
}
