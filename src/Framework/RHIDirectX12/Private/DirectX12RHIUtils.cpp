#include "DirectX12Device.h"
#include "DirectX12RHIUtils.h"

namespace RHIDirectX12
{
    RHI::ERHIFormat ToStorageImageFormat(RHI::ERHIFormat preferred)
    {
        // Internal UAV images use RGBA8; presentation keeps the requested BGRA8 format.
        return preferred == RHI::ERHIFormat::B8G8R8A8_UNorm ? RHI::ERHIFormat::R8G8B8A8_UNorm : preferred;
    }
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS ToRayTracingBuildFlags(bool update, bool compaction)
    {
        auto flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
        if (update) flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;
        if (compaction) flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_COMPACTION;
        return flags;
    }
    Microsoft::WRL::ComPtr<ID3D12Resource> CreateRayTracingBuffer(DirectX12Device* device, uint64_t size,
        D3D12_RESOURCE_STATES state, bool upload)
    {
        if (!size) throw std::runtime_error("Zero-sized ray tracing buffer");
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = upload ? D3D12_HEAP_TYPE_UPLOAD : D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = (size + 255) & ~uint64_t(255);
        desc.Height = desc.DepthOrArraySize = desc.MipLevels = 1;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        desc.Flags = upload ? D3D12_RESOURCE_FLAG_NONE : D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
        CheckDX12(device->GetNativeDevice()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
            upload ? D3D12_RESOURCE_STATE_GENERIC_READ : state, nullptr, IID_PPV_ARGS(&buffer)), "Create DXR buffer");
        return buffer;
    }

    D3D12_UNORDERED_ACCESS_VIEW_DESC ToReadOnlyStorageUAV(const RHI::RHIBufferSRVCreateInfo& view,
                                                          D3D_SHADER_INPUT_TYPE type, uint64_t bufferSize)
    {
        const bool raw = type == D3D_SIT_UAV_RWBYTEADDRESS;
        if ((!raw && type != D3D_SIT_UAV_RWSTRUCTURED) || (!raw && !view.Stride) || (raw && view.Stride))
            throw std::runtime_error("Read-only storage UAV requires a structured or raw buffer view");
        const uint64_t stride = raw ? 4 : view.Stride;
        if (view.Offset % stride || !view.NumElements || view.NumElements > UINT_MAX || view.Offset > bufferSize ||
            view.NumElements > (bufferSize - view.Offset) / stride)
            throw std::out_of_range("Read-only storage UAV buffer range");
        D3D12_UNORDERED_ACCESS_VIEW_DESC desc{};
        desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        desc.Format = raw ? DXGI_FORMAT_R32_TYPELESS : DXGI_FORMAT_UNKNOWN;
        desc.Buffer.FirstElement = view.Offset / stride;
        desc.Buffer.NumElements = static_cast<UINT>(view.NumElements);
        desc.Buffer.StructureByteStride = raw ? 0 : view.Stride;
        desc.Buffer.Flags = raw ? D3D12_BUFFER_UAV_FLAG_RAW : D3D12_BUFFER_UAV_FLAG_NONE;
        return desc;
    }
    std::vector<uint32_t> TextureViewSubresources(const RHI::RHITextureDesc& desc, uint32_t mip, uint32_t mipCount,
                                                  uint32_t slice, uint32_t sliceCount)
    {
        const uint32_t arrays = desc.Type == RHI::ERHITextureType::Texture3D ? 1 : desc.ArraySize;
        if (mip >= desc.MipLevels || !mipCount || mipCount > desc.MipLevels - mip || slice >= arrays || !sliceCount ||
            sliceCount > arrays - slice)
            throw std::out_of_range("Texture view subresource range");
        std::vector<uint32_t> result;
        for (uint32_t s = slice; s < slice + sliceCount; ++s)
            for (uint32_t m = mip; m < mip + mipCount; ++m)
                result.push_back(m + s * desc.MipLevels); // SRV/UAV currently select plane 0.
        return result;
    }
    D3D12_RESOURCE_STATES ToResourceState(RHI::ERHIResourceAccess access, RHI::RHIViewableResource* resource)
    {
        using A = RHI::ERHIResourceAccess;
        const auto bits = static_cast<uint32_t>(access);
        if (!bits)
            return D3D12_RESOURCE_STATE_COMMON;
        const uint32_t known = static_cast<uint32_t>(A::CPURead) | static_cast<uint32_t>(A::Present) |
                               static_cast<uint32_t>(A::IndirectArgs) | static_cast<uint32_t>(A::VertexOrIndexBuffer) |
                               static_cast<uint32_t>(A::SRV) | static_cast<uint32_t>(A::TransferSrc) |
                               static_cast<uint32_t>(A::ResolveSrc) | static_cast<uint32_t>(A::DSVRead) |
                               static_cast<uint32_t>(A::UAV) | static_cast<uint32_t>(A::RenderTargetView) |
                               static_cast<uint32_t>(A::TransferDest) | static_cast<uint32_t>(A::ResolveDst) |
                               static_cast<uint32_t>(A::DSVWrite);
        if (bits & ~known)
            throw std::runtime_error("Unsupported DirectX12 RHI resource access bits");
        const uint32_t writes = bits & static_cast<uint32_t>(A::WritableMask);
        if ((writes && (bits != writes || (writes & (writes - 1)))) ||
            (EnumHasAnyFlags(access, A::Present) && access != A::Present))
            throw std::runtime_error("D3D12 write/Present access cannot be combined with other accesses");
        D3D12_RESOURCE_STATES result = D3D12_RESOURCE_STATE_COMMON;
        const auto has = [&](A value) { return EnumHasAnyFlags(access, value); };
        auto* buffer = dynamic_cast<RHI::RHIBuffer*>(resource);
        auto* texture = dynamic_cast<RHI::RHITexture*>(resource);
        if ((has(A::VertexOrIndexBuffer) || has(A::IndirectArgs)) && !buffer)
            throw std::runtime_error("Buffer access requested for a texture");
        if ((has(A::RenderTargetView) || has(A::DSVRead) || has(A::DSVWrite) || has(A::Present) || has(A::ResolveSrc) ||
             has(A::ResolveDst)) &&
            !texture)
            throw std::runtime_error("Texture access requested for a buffer");
        if (has(A::UAV))
        {
            if (auto* dxTexture = dynamic_cast<DirectX12Texture*>(texture)) dxTexture->EnsureStorageTarget();
            if ((buffer && !EnumHasAnyFlags(buffer->GetDesc().Usage, RHI::ERHIBufferUsageFlag::UnorderedAccess)) ||
                (texture && !EnumHasAnyFlags(texture->GetDesc().Usage, RHI::ERHITextureCreateFlag::UAV) &&
                 !EnumHasAnyFlags(texture->GetDesc().Usage, RHI::ERHITextureCreateFlag::Presentable)))
                throw std::runtime_error("UAV access requires a resource created for unordered access");
            result |= D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        }
        if (has(A::CPURead) || has(A::TransferSrc))
            result |= D3D12_RESOURCE_STATE_COPY_SOURCE;
        if (has(A::TransferDest))
            result |= D3D12_RESOURCE_STATE_COPY_DEST;
        if (has(A::IndirectArgs))
            result |= D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
        if (has(A::RenderTargetView))
            result |= D3D12_RESOURCE_STATE_RENDER_TARGET;
        if (has(A::DSVRead))
            result |= D3D12_RESOURCE_STATE_DEPTH_READ;
        if (has(A::DSVWrite))
            result |= D3D12_RESOURCE_STATE_DEPTH_WRITE;
        if (has(A::ResolveSrc))
            result |= D3D12_RESOURCE_STATE_RESOLVE_SOURCE;
        if (has(A::ResolveDst))
            result |= D3D12_RESOURCE_STATE_RESOLVE_DEST;
        if (has(A::VertexOrIndexBuffer))
        {
            if (!EnumHasAnyFlags(buffer->GetDesc().Usage,
                                 RHI::ERHIBufferUsageFlag::Index | RHI::ERHIBufferUsageFlag::Vertex))
                throw std::runtime_error("Vertex/index access requires matching buffer usage");
            if (EnumHasAnyFlags(buffer->GetDesc().Usage, RHI::ERHIBufferUsageFlag::Index))
                result |= D3D12_RESOURCE_STATE_INDEX_BUFFER;
            if (EnumHasAnyFlags(buffer->GetDesc().Usage, RHI::ERHIBufferUsageFlag::Vertex))
                result |= D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        }
        if (has(A::SRV))
            result |= buffer && EnumHasAnyFlags(buffer->GetDesc().Usage, RHI::ERHIBufferUsageFlag::Constant)
                          ? D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER
                          : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        return result;
    }

    D3D12_TEXTURE_ADDRESS_MODE ToAddressMode(RHI::ERHIAddressMode mode)
    {
        switch (mode)
        {
        case RHI::ERHIAddressMode::Repeat:
            return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        case RHI::ERHIAddressMode::ClampToEdge:
            return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        case RHI::ERHIAddressMode::MirrorRepeat:
            return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
        case RHI::ERHIAddressMode::MirrorClampToEdge:
            return D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE;
        default:
            throw std::runtime_error("Unsupported DirectX12 sampler address mode");
        }
    }
    D3D12_FILTER ToFilter(RHI::ERHIFilter filter, bool anisotropic, bool comparison)
    {
        if (filter != RHI::ERHIFilter::Nearest && filter != RHI::ERHIFilter::Linear)
            throw std::runtime_error("Unsupported DirectX12 sampler filter");
        const auto native = anisotropic                          ? D3D12_FILTER_ANISOTROPIC
                            : filter == RHI::ERHIFilter::Nearest ? D3D12_FILTER_MIN_MAG_MIP_POINT
                                                                 : D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        return static_cast<D3D12_FILTER>(native | (comparison ? D3D12_FILTER_COMPARISON_MIN_MAG_MIP_POINT : 0));
    }
    D3D12_INPUT_CLASSIFICATION ToInputClassification(RHI::ERHIInputRate rate)
    {
        switch (rate)
        {
        case RHI::ERHIInputRate::PerVertex:
            return D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
        case RHI::ERHIInputRate::PerInstance:
            return D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA;
        default:
            throw std::runtime_error("Unsupported DirectX12 vertex input rate");
        }
    }
    BOOL ToFrontCounterClockwise(RHI::ERHIFrontFace face)
    {
        switch (face)
        {
        case RHI::ERHIFrontFace::CounterClockwise:
            return TRUE;
        case RHI::ERHIFrontFace::Clockwise:
            return FALSE;
        default:
            throw std::runtime_error("Unsupported DirectX12 front face");
        }
    }
    DXGI_FORMAT ToDxgiFormat(RHI::ERHIFormat format)
    {
        switch (format)
        {
        case RHI::ERHIFormat::R8_UNorm:
            return DXGI_FORMAT_R8_UNORM;
        case RHI::ERHIFormat::R8G8B8A8_UNorm:
            return DXGI_FORMAT_R8G8B8A8_UNORM;
        case RHI::ERHIFormat::R8G8B8A8_SRGB:
            return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        case RHI::ERHIFormat::B8G8R8A8_UNorm:
            return DXGI_FORMAT_B8G8R8A8_UNORM;
        case RHI::ERHIFormat::B8G8R8A8_SRGB:
            return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        case RHI::ERHIFormat::R16G16_Float:
            return DXGI_FORMAT_R16G16_FLOAT;
        case RHI::ERHIFormat::R16G16B16A16_Float:
            return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case RHI::ERHIFormat::R32_Float:
            return DXGI_FORMAT_R32_FLOAT;
        case RHI::ERHIFormat::R32G32_Float:
            return DXGI_FORMAT_R32G32_FLOAT;
        case RHI::ERHIFormat::R32G32B32_Float:
            return DXGI_FORMAT_R32G32B32_FLOAT;
        case RHI::ERHIFormat::R32G32B32A32_Float:
            return DXGI_FORMAT_R32G32B32A32_FLOAT;
        case RHI::ERHIFormat::D24_UNorm_S8_UInt:
            return DXGI_FORMAT_D24_UNORM_S8_UINT;
        case RHI::ERHIFormat::D32_Float:
            return DXGI_FORMAT_D32_FLOAT;
        case RHI::ERHIFormat::R16_UInt:
            return DXGI_FORMAT_R16_UINT;
        case RHI::ERHIFormat::R32_UInt:
            return DXGI_FORMAT_R32_UINT;
        case RHI::ERHIFormat::Unknown:
            return DXGI_FORMAT_UNKNOWN;
        default:
            throw std::runtime_error("RHI format has no supported DXGI representation");
        }
    }

    D3D12_RESOURCE_DIMENSION ToResourceDimension(RHI::ERHITextureType type)
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
            return D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        default:
            throw std::runtime_error("Unsupported DirectX12 texture type");
        }
    }

    D3D12_RESOURCE_FLAGS ToTextureFlags(RHI::ERHITextureCreateFlags flags)
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

        if (EnumHasAnyFlags(flags, RHI::ERHITextureCreateFlag::DepthStencil) &&
            !EnumHasAnyFlags(flags, RHI::ERHITextureCreateFlag::ShaderResource))
        {
            resourceFlags |= D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
        }

        return resourceFlags;
    }

    D3D12_RESOURCE_STATES ToTextureInitialState(RHI::ERHITextureCreateFlags flags)
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

    D3D12_RESOURCE_FLAGS ToBufferFlags(RHI::ERHIBufferUsageFlags usage, bool uploadHeap)
    {
        if (uploadHeap)
        {
            if (EnumHasAnyFlags(usage, RHI::ERHIBufferUsageFlag::UnorderedAccess))
                throw std::runtime_error("D3D12 UPLOAD buffers cannot provide unordered access");
            return D3D12_RESOURCE_FLAG_NONE;
        }
        D3D12_RESOURCE_FLAGS resourceFlags = D3D12_RESOURCE_FLAG_NONE;
        // Reading an HLSL RW buffer can require a native UAV even with logical
        // SRV access. Native allocation capability does not grant RHI write access.
        if (EnumHasAnyFlags(usage,
                            RHI::ERHIBufferUsageFlag::UnorderedAccess | RHI::ERHIBufferUsageFlag::ShaderResource))
        {
            resourceFlags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        }
        return resourceFlags;
    }

    DXGI_FORMAT ToDxgiSrvFormat(RHI::ERHIFormat format, DXGI_FORMAT fallback)
    {
        const DXGI_FORMAT translated = ToDxgiFormat(format);
        return translated == DXGI_FORMAT_UNKNOWN ? fallback : translated;
    }

    D3D12_SRV_DIMENSION ToSrvDimension(RHI::ERHITextureType textureType, RHI::ERHITextureViewType viewType)
    {
        const RHI::ERHITextureViewType resolved =
            viewType == RHI::ERHITextureViewType::Derived
                ? (textureType == RHI::ERHITextureType::Texture1D        ? RHI::ERHITextureViewType::TextureView1D
                   : textureType == RHI::ERHITextureType::Texture3D      ? RHI::ERHITextureViewType::TextureView3D
                   : textureType == RHI::ERHITextureType::TextureCube    ? RHI::ERHITextureViewType::TextureViewCube
                   : textureType == RHI::ERHITextureType::Texture2DArray ? RHI::ERHITextureViewType::TextureView2DArray
                   : textureType == RHI::ERHITextureType::TextureCubeArray
                       ? RHI::ERHITextureViewType::TextureViewCubeArray
                       : RHI::ERHITextureViewType::TextureView2D)
                : viewType;

        switch (resolved)
        {
        case RHI::ERHITextureViewType::TextureView1D:
            return D3D12_SRV_DIMENSION_TEXTURE1D;
        case RHI::ERHITextureViewType::TextureView3D:
            return D3D12_SRV_DIMENSION_TEXTURE3D;
        case RHI::ERHITextureViewType::TextureViewCube:
            return D3D12_SRV_DIMENSION_TEXTURECUBE;
        case RHI::ERHITextureViewType::TextureView2DArray:
            return D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        case RHI::ERHITextureViewType::TextureViewCubeArray:
            return D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
        case RHI::ERHITextureViewType::TextureView2D:
            return D3D12_SRV_DIMENSION_TEXTURE2D;
        default:
            throw std::runtime_error("Unsupported DirectX12 SRV dimension");
        }
    }

    D3D12_UAV_DIMENSION ToUavDimension(RHI::ERHITextureType textureType, RHI::ERHITextureViewType viewType)
    {
        const RHI::ERHITextureViewType resolved =
            viewType == RHI::ERHITextureViewType::Derived
                ? (textureType == RHI::ERHITextureType::Texture1D   ? RHI::ERHITextureViewType::TextureView1D
                   : textureType == RHI::ERHITextureType::Texture3D ? RHI::ERHITextureViewType::TextureView3D
                   : textureType == RHI::ERHITextureType::Texture2DArray ||
                           textureType == RHI::ERHITextureType::TextureCube ||
                           textureType == RHI::ERHITextureType::TextureCubeArray
                       ? RHI::ERHITextureViewType::TextureView2DArray
                       : RHI::ERHITextureViewType::TextureView2D)
                : viewType;

        switch (resolved)
        {
        case RHI::ERHITextureViewType::TextureView1D:
            return D3D12_UAV_DIMENSION_TEXTURE1D;
        case RHI::ERHITextureViewType::TextureView3D:
            return D3D12_UAV_DIMENSION_TEXTURE3D;
        case RHI::ERHITextureViewType::TextureView2DArray:
            return D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
        case RHI::ERHITextureViewType::TextureView2D:
            return D3D12_UAV_DIMENSION_TEXTURE2D;
        default:
            throw std::runtime_error("Unsupported DirectX12 UAV dimension");
        }
    }

    D3D12_FILL_MODE ToFillMode(RHI::ERHIPolygonMode mode)
    {
        switch (mode)
        {
        case RHI::ERHIPolygonMode::Line:
            return D3D12_FILL_MODE_WIREFRAME;
        case RHI::ERHIPolygonMode::Point:
            throw std::runtime_error("D3D12 does not support point polygon mode");
        case RHI::ERHIPolygonMode::Fill:
            return D3D12_FILL_MODE_SOLID;
        default:
            throw std::runtime_error("Unsupported DirectX12 polygon mode");
        }
    }

    D3D12_CULL_MODE ToCullMode(RHI::ERHICullMode mode)
    {
        switch (mode)
        {
        case RHI::ERHICullMode::Front:
            return D3D12_CULL_MODE_FRONT;
        case RHI::ERHICullMode::Back:
            return D3D12_CULL_MODE_BACK;
        case RHI::ERHICullMode::FrontAndBack:
            throw std::runtime_error("D3D12 does not support front-and-back culling");
        case RHI::ERHICullMode::None:
            return D3D12_CULL_MODE_NONE;
        default:
            throw std::runtime_error("Unsupported DirectX12 cull mode");
        }
    }

    D3D12_COMPARISON_FUNC ToComparisonFunc(RHI::ERHICompareOp op)
    {
        switch (op)
        {
        case RHI::ERHICompareOp::Never:
            return D3D12_COMPARISON_FUNC_NEVER;
        case RHI::ERHICompareOp::Less:
            return D3D12_COMPARISON_FUNC_LESS;
        case RHI::ERHICompareOp::Equal:
            return D3D12_COMPARISON_FUNC_EQUAL;
        case RHI::ERHICompareOp::LessOrEqual:
            return D3D12_COMPARISON_FUNC_LESS_EQUAL;
        case RHI::ERHICompareOp::Greater:
            return D3D12_COMPARISON_FUNC_GREATER;
        case RHI::ERHICompareOp::NotEqual:
            return D3D12_COMPARISON_FUNC_NOT_EQUAL;
        case RHI::ERHICompareOp::GreaterOrEqual:
            return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
        case RHI::ERHICompareOp::Always:
            return D3D12_COMPARISON_FUNC_ALWAYS;
        default:
            throw std::runtime_error("Unsupported DirectX12 comparison function");
        }
    }

    D3D12_PRIMITIVE_TOPOLOGY_TYPE ToPrimitiveTopologyType(RHI::EPrimitiveTopology topology)
    {
        switch (topology)
        {
        case RHI::EPrimitiveTopology::PointList:
            return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
        case RHI::EPrimitiveTopology::LineList:
        case RHI::EPrimitiveTopology::LineStrip:
        case RHI::EPrimitiveTopology::LineListWithAdjacency:
        case RHI::EPrimitiveTopology::LineStripWithAdjacency:
            return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
        case RHI::EPrimitiveTopology::PatchList_1:
        case RHI::EPrimitiveTopology::PatchList_2:
        case RHI::EPrimitiveTopology::PatchList_3:
        case RHI::EPrimitiveTopology::PatchList_4:
            return D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
        case RHI::EPrimitiveTopology::TriangleFan:
            throw std::runtime_error("D3D12 does not support triangle fans");
        case RHI::EPrimitiveTopology::TriangleList:
        case RHI::EPrimitiveTopology::TriangleStrip:
        case RHI::EPrimitiveTopology::TriangleListWithAdjacency:
        case RHI::EPrimitiveTopology::TriangleStripWithAdjacency:
            return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        default:
            throw std::runtime_error("Unsupported DirectX12 primitive topology");
        }
    }

    D3D_PRIMITIVE_TOPOLOGY ToPrimitiveTopology(RHI::EPrimitiveTopology topology)
    {
        switch (topology)
        {
        case RHI::EPrimitiveTopology::PointList:
            return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
        case RHI::EPrimitiveTopology::LineList:
            return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
        case RHI::EPrimitiveTopology::LineStrip:
            return D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
        case RHI::EPrimitiveTopology::TriangleStrip:
            return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
        case RHI::EPrimitiveTopology::LineListWithAdjacency:
            return D3D_PRIMITIVE_TOPOLOGY_LINELIST_ADJ;
        case RHI::EPrimitiveTopology::LineStripWithAdjacency:
            return D3D_PRIMITIVE_TOPOLOGY_LINESTRIP_ADJ;
        case RHI::EPrimitiveTopology::TriangleListWithAdjacency:
            return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST_ADJ;
        case RHI::EPrimitiveTopology::TriangleStripWithAdjacency:
            return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP_ADJ;
        case RHI::EPrimitiveTopology::PatchList_1:
            return D3D_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST;
        case RHI::EPrimitiveTopology::PatchList_2:
            return D3D_PRIMITIVE_TOPOLOGY_2_CONTROL_POINT_PATCHLIST;
        case RHI::EPrimitiveTopology::PatchList_3:
            return D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST;
        case RHI::EPrimitiveTopology::PatchList_4:
            return D3D_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST;
        case RHI::EPrimitiveTopology::TriangleFan:
            throw std::runtime_error("D3D12 does not support triangle fans");
        case RHI::EPrimitiveTopology::TriangleList:
            return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        default:
            throw std::runtime_error("Unsupported DirectX12 primitive topology");
        }
    }
} // namespace RHIDirectX12
