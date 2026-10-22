#include "DirectX12Resource.h"
#include "DirectX12Device.h"
#include "DirectX12Queue.h"
#include "DirectX12ShaderAccess.h"
#include "DirectX12RHIUtils.h"
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

#endif
} // namespace

namespace RHIDirectX12
{
    void DirectX12AccelerationStructure::Allocate(RHI::RayTracingAccelerationStructureSizeInfo& size)
    {
        if (!Device || !Device->SupportsRayTracing()) throw std::runtime_error("D3D12 adapter does not support DXR");
        Microsoft::WRL::ComPtr<ID3D12Device5> dxr;
        CheckDX12(Device->GetNativeDevice()->QueryInterface(IID_PPV_ARGS(&dxr)), "Query DXR device");
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info{};
        dxr->GetRaytracingAccelerationStructurePrebuildInfo(&Inputs, &info);
        size.ResultSize = info.ResultDataMaxSizeInBytes;
        size.BuildScratchSize = info.ScratchDataSizeInBytes;
        size.UpdateScratchSize = info.UpdateScratchDataSizeInBytes;
        Result = CreateRayTracingBuffer(Device, size.ResultSize, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
        Scratch = CreateRayTracingBuffer(Device, std::max(size.BuildScratchSize, size.UpdateScratchSize), D3D12_RESOURCE_STATE_COMMON);
        bBuilt = false;
        bScratchReady = false;
        Access = RHI::ERHIResourceAccess::Unknown;
    }
    DirectX12RayTracingGeometry::DirectX12RayTracingGeometry(DirectX12Device* device, const RHI::RHIRayTracingGeometryDesc& desc)
    { Device = device; SetGeometryDesc(desc); }
    void DirectX12RayTracingGeometry::SetGeometryDesc(const RHI::RHIRayTracingGeometryDesc& desc)
    {
        auto* vertex = dynamic_cast<DirectX12Buffer*>(desc.VertexBuffer);
        auto* index = dynamic_cast<DirectX12Buffer*>(desc.IndexBuffer);
        if (!vertex || !index || !desc.VertexCount || !desc.IndexCount || desc.IndexCount % 3 || desc.VertexStride < 12 ||
            (index->GetDesc().Stride != 2 && index->GetDesc().Stride != 4) ||
            uint64_t(desc.VertexCount) * desc.VertexStride > vertex->GetDesc().Size ||
            uint64_t(desc.IndexCount) * index->GetDesc().Stride > index->GetDesc().Size)
            throw std::runtime_error("Invalid DXR indexed triangle geometry");
        _geometryDesc = desc;
        Geometry = {};
        Geometry.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
        Geometry.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
        Geometry.Triangles.VertexBuffer = { vertex->GetNativeResource()->GetGPUVirtualAddress(), desc.VertexStride };
        Geometry.Triangles.VertexCount = desc.VertexCount;
        Geometry.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
        Geometry.Triangles.IndexBuffer = index->GetNativeResource()->GetGPUVirtualAddress();
        Geometry.Triangles.IndexCount = desc.IndexCount;
        Geometry.Triangles.IndexFormat = index->GetDesc().Stride == 4 ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT;
        Inputs = {};
        Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
        Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        Inputs.NumDescs = 1;
        Inputs.pGeometryDescs = &Geometry;
        Inputs.Flags = ToRayTracingBuildFlags(desc.bAllowUpdate, desc.bAllowCompaction);
        Allocate(_sizeInfo);
    }
    DirectX12RayTracingInstance::DirectX12RayTracingInstance(DirectX12Device* device, const RHI::RHIRayTracingInstancesDesc& desc)
    { Device = device; SetInstancesDesc(desc); }
    void DirectX12RayTracingInstance::SetInstancesDesc(const RHI::RHIRayTracingInstancesDesc& desc)
    {
        if (desc.Instances.empty()) throw std::runtime_error("DXR TLAS requires instances");
        _instances = desc;
        ReferencedResults.clear();
        std::vector<D3D12_RAYTRACING_INSTANCE_DESC> native(desc.Instances.size());
        for (size_t i = 0; i < native.size(); ++i)
        {
            const auto& instance = desc.Instances[i];
            auto* blas = dynamic_cast<DirectX12RayTracingGeometry*>(instance.Geometry);
            if (!blas || !blas->Result || instance.InstanceID > 0xffffff) throw std::runtime_error("Invalid DXR TLAS instance");
            auto& out = native[i];
            for (uint32_t row = 0; row < 3; ++row)
                for (uint32_t col = 0; col < 4; ++col) out.Transform[row][col] = instance.Transform(row,col);
            out.InstanceID = instance.InstanceID;
            out.InstanceMask = instance.Mask;
            out.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE;
            out.AccelerationStructure = blas->Result->GetGPUVirtualAddress();
            ReferencedResults.push_back(blas->Result);
        }
        InstanceUpload = CreateRayTracingBuffer(Device, native.size() * sizeof(native[0]), D3D12_RESOURCE_STATE_GENERIC_READ, true);
        void* mapped = nullptr;
        D3D12_RANGE empty{};
        CheckDX12(InstanceUpload->Map(0, &empty, &mapped), "Map DXR instances");
        memcpy(mapped, native.data(), native.size() * sizeof(native[0]));
        InstanceUpload->Unmap(0, nullptr);
        Inputs = {};
        Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
        Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        Inputs.NumDescs = static_cast<UINT>(native.size());
        Inputs.InstanceDescs = InstanceUpload->GetGPUVirtualAddress();
        Inputs.Flags = ToRayTracingBuildFlags(true, false);
        Allocate(_sizeInfo);
    }

    void DirectX12Texture::EnsureStorageTarget()
    {
        if (!EnumHasAnyFlags(_desc.Usage, RHI::ERHITextureCreateFlag::Presentable) || StorageTarget) return;
        // DXGI images remain native presentation targets; only UAV views use this internal image.
        RHI::RHITextureDesc storageDesc = _desc;
        storageDesc.Format = ToStorageImageFormat(_desc.Format);
        storageDesc.Usage = RHI::ERHITextureCreateFlag::UAV | RHI::ERHITextureCreateFlag::ShaderResource;
        StorageTarget = std::make_shared<DirectX12Texture>(m_Device, storageDesc);
    }

    DirectX12Texture::DirectX12Texture(DirectX12Device* inDevice, const RHI::RHITextureDesc& desc)
        : RHI::RHITexture(desc), m_Device(inDevice)
    {
        if (m_Device) m_DeviceLifetime = m_Device->GetLifetimeToken();
#if defined(_WIN32)
        if (!m_Device || !m_Device->GetNativeDevice())
        {
            return;
        }

        m_NativeFormat = ToDxgiFormat(desc.Format);
        const bool isDepth = EnumHasAnyFlags(desc.Usage, RHI::ERHITextureCreateFlag::DepthStencil);

        D3D12_RESOURCE_DESC resourceDesc{};
        resourceDesc.Dimension = ToResourceDimension(desc.Type);
        resourceDesc.Alignment = 0;
        resourceDesc.Width = std::max<uint32_t>(1u, desc.Width);
        resourceDesc.Height = std::max<uint32_t>(1u, desc.Height);
        resourceDesc.DepthOrArraySize =
            static_cast<UINT16>(desc.Type == RHI::ERHITextureType::Texture3D ? std::max<uint32_t>(1u, desc.Depth)
                                                                             : std::max<uint32_t>(1u, desc.ArraySize));
        resourceDesc.MipLevels = static_cast<UINT16>(std::max<uint32_t>(1u, desc.MipLevels));
        resourceDesc.Format = m_NativeFormat;
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
            clearValue.Format = m_NativeFormat;
            clearValue.DepthStencil.Depth = 1.0f;
            clearValue.DepthStencil.Stencil = 0;
            clearValuePtr = &clearValue;
        }
        else if (EnumHasAnyFlags(desc.Usage, RHI::ERHITextureCreateFlag::RenderTarget) ||
                 EnumHasAnyFlags(desc.Usage, RHI::ERHITextureCreateFlag::Presentable))
        {
            clearValue.Format = m_NativeFormat;
            clearValue.Color[0] = 0.0f;
            clearValue.Color[1] = 0.0f;
            clearValue.Color[2] = 0.0f;
            clearValue.Color[3] = 1.0f;
            clearValuePtr = &clearValue;
        }

        const bool created =
            m_Device->CreateCommittedResource(heapProperties, D3D12_HEAP_FLAG_NONE, resourceDesc,
                                            ToTextureInitialState(desc.Usage), clearValuePtr, m_Resource);
        States.assign(resourceDesc.MipLevels *
                          (desc.Type == RHI::ERHITextureType::Texture3D ? 1u : resourceDesc.DepthOrArraySize) *
                          (desc.Format == RHI::ERHIFormat::D24_UNorm_S8_UInt ? 2u : 1u),
                      ToTextureInitialState(desc.Usage));
        if (created && desc.DebugName)
        {
            std::wstring debugName = ToWide(desc.DebugName);
            if (!debugName.empty())
            {
                m_Resource->SetName(debugName.c_str());
            }
        }
#endif
    }

    DirectX12Texture::~DirectX12Texture()
    {
#if defined(_WIN32)
        if (!m_DeviceLifetime.expired() && m_Device && m_Device->IsValid())
        {
            for (const auto& view : m_RTVs)
                m_Device->FreeDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, view.second);
            for (const auto& view : m_DSVs)
                m_Device->FreeDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, view.second);
            m_Device->FreeDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, m_DefaultSRV);
        }
        m_Resource.Reset();
#endif
    }

    DirectX12Buffer::DirectX12Buffer(DirectX12Device* inDevice, const RHI::RHIBufferDesc& desc)
        : RHI::RHIBuffer(desc), m_Device(inDevice)
    {
#if defined(_WIN32)
        if (!m_Device || !m_Device->GetNativeDevice())
        {
            return;
        }

        D3D12_RESOURCE_DESC resourceDesc{};
        resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        resourceDesc.Alignment = 0;
        resourceDesc.Width = std::max<uint64_t>(1ull, desc.Size);
        if (EnumHasAnyFlags(desc.Usage, RHI::ERHIBufferUsageFlag::Constant))
            resourceDesc.Width = (resourceDesc.Width + 255) & ~255ull;
        resourceDesc.Height = 1;
        resourceDesc.DepthOrArraySize = 1;
        resourceDesc.MipLevels = 1;
        resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
        resourceDesc.SampleDesc.Count = 1;
        resourceDesc.SampleDesc.Quality = 0;
        resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        resourceDesc.Flags = ToBufferFlags(
            desc.Usage, desc.bCPUAccessible || EnumHasAnyFlags(desc.Usage, RHI::ERHIBufferUsageFlag::Staging));

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
                                                 : D3D12_RESOURCE_STATE_COMMON;

        const bool created = m_Device->CreateCommittedResource(heapProperties, D3D12_HEAP_FLAG_NONE, resourceDesc,
                                                             initialState, nullptr, m_Resource);
        bUploadHeap = heapProperties.Type == D3D12_HEAP_TYPE_UPLOAD;
        States.assign(1, initialState);
        if (created && desc.DebugName)
        {
            std::wstring debugName = ToWide(desc.DebugName);
            if (!debugName.empty())
            {
                m_Resource->SetName(debugName.c_str());
            }
        }
#endif
    }

    DirectX12Buffer::~DirectX12Buffer()
    {
#if defined(_WIN32)
        m_Resource.Reset();
#endif
    }

    DirectX12ShaderResourceView::DirectX12ShaderResourceView(RHI::RHIViewableResource* resource,
                                                             const RHI::RHITexSRVCreateInfo& createInfo)
        : RHI::RHIShaderResourceView(resource)
    {
#if defined(_WIN32)
        auto* texture = dynamic_cast<DirectX12Texture*>(resource);
        if (!texture || !texture->GetDevice() || !texture->GetDevice()->GetNativeDevice() ||
            !texture->GetNativeResource())
        {
            return;
        }

        Subresources = TextureViewSubresources(texture->GetDesc(), createInfo.FirstMipSlice, createInfo.MipCount,
                                               createInfo.FirstArraySlice, createInfo.ArraySize);
        if (!texture->GetDevice()->AllocateCbvSrvUavDescriptor(m_CpuHandle))
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

        texture->GetDevice()->GetNativeDevice()->CreateShaderResourceView(texture->GetNativeResource(), &srvDesc,
                                                                          m_CpuHandle);
        m_Device = texture->GetDevice();
        m_DeviceLifetime = m_Device->GetLifetimeToken();
        OwnerUseToken = GetResourceState(resource)->ContextUses;
        ++*OwnerUseToken;
        m_bValid = true;
#else
        (void)createInfo;
#endif
    }

    DirectX12ShaderResourceView::DirectX12ShaderResourceView(RHI::RHIViewableResource* resource,
                                                             const RHI::RHIBufferSRVCreateInfo& createInfo)
        : RHI::RHIShaderResourceView(resource)
    {
#if defined(_WIN32)
        auto* buffer = dynamic_cast<DirectX12Buffer*>(resource);
        if (!buffer || !buffer->GetDevice() || !buffer->GetDevice()->GetNativeDevice() || !buffer->GetNativeResource())
        {
            return;
        }

        if (!buffer->GetDevice()->AllocateCbvSrvUavDescriptor(m_CpuHandle))
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
        if (createInfo.Stride)
            srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        BufferDesc = createInfo;
        srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

        buffer->GetDevice()->GetNativeDevice()->CreateShaderResourceView(buffer->GetNativeResource(), &srvDesc,
                                                                         m_CpuHandle);
        m_Device = buffer->GetDevice();
        m_DeviceLifetime = m_Device->GetLifetimeToken();
        OwnerUseToken = GetResourceState(resource)->ContextUses;
        ++*OwnerUseToken;
        m_bValid = true;
#else
        (void)createInfo;
#endif
    }

    DirectX12UnorderedAccessView::DirectX12UnorderedAccessView(RHI::RHIViewableResource* resource,
                                                               const RHI::RHITexUAVCreateInfo& createInfo)
        : RHI::RHIUnorderedAccessView(resource)
    {
#if defined(_WIN32)
        auto* texture = dynamic_cast<DirectX12Texture*>(resource);
        if (!texture || !texture->GetDevice() || !texture->GetDevice()->GetNativeDevice() ||
            !texture->GetNativeResource())
        {
            return;
        }

        if (createInfo.MipCount != 1)
            throw std::invalid_argument("A D3D12 UAV covers exactly one mip");
        ToResourceState(RHI::ERHIResourceAccess::UAV, resource);
        Subresources = TextureViewSubresources(texture->GetDesc(), createInfo.FirstMipSlice, createInfo.MipCount,
                                               createInfo.FirstArraySlice, createInfo.ArraySize);
        if (!texture->GetDevice()->AllocateCbvSrvUavDescriptor(m_CpuHandle))
        {
            return;
        }

        auto* storageTexture = texture->StorageTarget ? texture->StorageTarget.get() : texture;
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
        uavDesc.Format = texture->StorageTarget ? storageTexture->GetNativeFormat() : ToDxgiSrvFormat(createInfo.Format, texture->GetNativeFormat());
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

        texture->GetDevice()->GetNativeDevice()->CreateUnorderedAccessView(storageTexture->GetNativeResource(), nullptr,
                                                                           &uavDesc, m_CpuHandle);
        m_Device = texture->GetDevice();
        m_DeviceLifetime = m_Device->GetLifetimeToken();
        OwnerUseToken = GetResourceState(resource)->ContextUses;
        ++*OwnerUseToken;
        m_bValid = true;
#else
        (void)createInfo;
#endif
    }

    DirectX12UnorderedAccessView::DirectX12UnorderedAccessView(RHI::RHIViewableResource* resource,
                                                               const RHI::RHIBufferUAVCreateInfo& createInfo)
        : RHI::RHIUnorderedAccessView(resource)
    {
#if defined(_WIN32)
        auto* buffer = dynamic_cast<DirectX12Buffer*>(resource);
        if (!buffer || !buffer->GetDevice() || !buffer->GetDevice()->GetNativeDevice() || !buffer->GetNativeResource())
        {
            return;
        }

        if (!buffer->GetDevice()->AllocateCbvSrvUavDescriptor(m_CpuHandle))
        {
            return;
        }

        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
        uavDesc.Format = ToDxgiSrvFormat(createInfo.Format, DXGI_FORMAT_UNKNOWN);
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.FirstElement = createInfo.Stride == 0 ? 0 : (createInfo.Offset / createInfo.Stride);
        uavDesc.Buffer.NumElements = static_cast<UINT>(createInfo.NumElements);
        uavDesc.Buffer.StructureByteStride = createInfo.Stride;
        if (createInfo.Stride)
            uavDesc.Format = DXGI_FORMAT_UNKNOWN;
        uavDesc.Buffer.CounterOffsetInBytes = 0;
        uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

        buffer->GetDevice()->GetNativeDevice()->CreateUnorderedAccessView(buffer->GetNativeResource(), nullptr,
                                                                          &uavDesc, m_CpuHandle);
        m_Device = buffer->GetDevice();
        m_DeviceLifetime = m_Device->GetLifetimeToken();
        OwnerUseToken = GetResourceState(resource)->ContextUses;
        ++*OwnerUseToken;
        m_bValid = true;
#else
        (void)createInfo;
#endif
    }

    DirectX12StagingBuffer::DirectX12StagingBuffer(DirectX12Device* inDevice, uint32_t size, D3D12_HEAP_TYPE heapType)
        : RHI::RHIStagingBuffer(size), m_Device(inDevice), m_Storage(size == 0 ? 1u : size, 0)
    {
#if defined(_WIN32)
        m_HeapType = heapType;
        if (!m_Device || !m_Device->GetNativeDevice())
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

        const D3D12_RESOURCE_STATES initialState =
            heapType == D3D12_HEAP_TYPE_READBACK ? D3D12_RESOURCE_STATE_COPY_DEST : D3D12_RESOURCE_STATE_GENERIC_READ;
        if (m_Device->CreateCommittedResource(heapProperties, D3D12_HEAP_FLAG_NONE, resourceDesc, initialState, nullptr,
                                            m_Resource))
        {
            D3D12_RANGE range{0, heapType == D3D12_HEAP_TYPE_READBACK ? size : 0};
            CheckDX12(m_Resource->Map(0, &range, &m_MappedPtr), "Map staging buffer");
        }
#endif
    }

    DirectX12StagingBuffer::~DirectX12StagingBuffer()
    {
#if defined(_WIN32)
        if (m_Resource && m_MappedPtr)
        {
            m_Resource->Unmap(0, nullptr);
            m_MappedPtr = nullptr;
        }
        m_Resource.Reset();
#endif
    }

    void* DirectX12StagingBuffer::Map(uint32_t offset, uint32_t numBytes)
    {
        if (offset > m_Storage.size())
        {
            return nullptr;
        }

        if (numBytes > m_Storage.size() - offset)
        {
            return nullptr;
        }

        m_bMapped = true;
#if defined(_WIN32)
        if (m_MappedPtr)
        {
            return reinterpret_cast<uint8_t*>(m_MappedPtr) + offset;
        }
#endif
        return m_Storage.data() + offset;
    }

    void DirectX12StagingBuffer::Unmap()
    {
        m_bMapped = false;
    }

    DirectX12ShaderBase::DirectX12ShaderBase(const std::vector<char>& code, bool library)
    {
        _byteCode = code.size() >= 4 && std::memcmp(code.data(), "DXBC", 4) == 0 ? code : UnpackDXIL(code);
        if (_byteCode.empty())
            throw std::runtime_error("Empty DXIL shader");
        Microsoft::WRL::ComPtr<IDxcUtils> utils;
        CheckDX12(DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils)), "Create DXC reflection utils");
        Microsoft::WRL::ComPtr<IDxcBlobEncoding> blob;
        CheckDX12(utils->CreateBlob(_byteCode.data(), static_cast<UINT32>(_byteCode.size()), DXC_CP_ACP, &blob),
                  "Create DXIL blob");
        Microsoft::WRL::ComPtr<IDxcContainerReflection> container;
        CheckDX12(DxcCreateInstance(CLSID_DxcContainerReflection, IID_PPV_ARGS(&container)), "Create DXIL reflection");
        CheckDX12(container->Load(blob.Get()), "Load DXIL container");
        UINT32 index;
        CheckDX12(container->FindFirstPartKind(DXC_PART_DXIL, &index), "Find DXIL part");
        if (library)
        {
            CheckDX12(container->GetPartReflection(index, IID_PPV_ARGS(&_libraryReflection)), "Reflect DXIL library");
            RenderCore::HLSLCompiledBinaryResultPacker packer;
            packer.Depack(code);
            const std::string entry = packer.DepackedData.HeaderData.EntryPoint;
            D3D12_LIBRARY_DESC desc{};
            CheckDX12(_libraryReflection->GetDesc(&desc), "Reflect DXIL library description");
            ID3D12FunctionReflection* function = nullptr;
            D3D12_FUNCTION_DESC functionDesc{};
            for (UINT i = 0; i < desc.FunctionCount; ++i)
            {
                auto* candidate = _libraryReflection->GetFunctionByIndex(i);
                D3D12_FUNCTION_DESC info{};
                CheckDX12(candidate->GetDesc(&info), "Reflect DXR function");
                const std::string name = info.Name;
                if (name == entry || name.find("?" + entry + "@") != std::string::npos)
                { function = candidate; functionDesc = info; ExportName = ToWide(entry.c_str()); break; }
            }
            if (!function) throw std::runtime_error("DXR library entry point not found: " + entry);
            for (UINT i = 0; i < functionDesc.BoundResources; ++i)
            {
                DirectX12ShaderBinding binding;
                CheckDX12(function->GetResourceBindingDesc(i, &binding.Desc), "Reflect DXR resource");
                binding.Name = binding.Desc.Name;
                binding.Desc.Name = nullptr;
                if (binding.Desc.Type == D3D_SIT_CBUFFER)
                {
                    D3D12_SHADER_BUFFER_DESC buffer{};
                    CheckDX12(function->GetConstantBufferByName(binding.Name.c_str())->GetDesc(&buffer), "Reflect DXR cbuffer");
                    binding.ConstantBufferSize = buffer.Size;
                }
                _bindings.push_back(std::move(binding));
            }
            return;
        }
        CheckDX12(container->GetPartReflection(index, IID_PPV_ARGS(&_reflection)), "Reflect DXIL");
        D3D12_SHADER_DESC shaderDesc{};
        CheckDX12(_reflection->GetDesc(&shaderDesc), "Reflect shader description");
        const auto assembly = DisassembleDXIL(_byteCode);
        for (UINT i = 0; i < shaderDesc.BoundResources; ++i)
        {
            DirectX12ShaderBinding binding;
            CheckDX12(_reflection->GetResourceBindingDesc(i, &binding.Desc), "Reflect shader binding");
            binding.Name = binding.Desc.Name;
            binding.bReadOnlyBufferUAV = IsReadOnlyBufferUAV(assembly, binding.Desc);
            binding.Desc.Name = nullptr; // Reflection owns the original name; keep our own copy.
            if (binding.Desc.Type == D3D_SIT_CBUFFER)
            {
                D3D12_SHADER_BUFFER_DESC bufferDesc{};
                CheckDX12(_reflection->GetConstantBufferByName(binding.Name.c_str())->GetDesc(&bufferDesc),
                          "Reflect constant buffer");
                binding.ConstantBufferSize = bufferDesc.Size;
            }
            _bindings.push_back(std::move(binding));
        }
    }

    DirectX12VertexShader::DirectX12VertexShader(const std::vector<char>& inByteCode) : DirectX12ShaderBase(inByteCode)
    {
    }

    DirectX12FragmentShader::DirectX12FragmentShader(const std::vector<char>& inByteCode)
        : DirectX12ShaderBase(inByteCode)
    {
    }

    DirectX12ComputeShader::DirectX12ComputeShader(const std::vector<char>& inByteCode)
        : DirectX12ShaderBase(inByteCode)
    {
    }

    DirectX12GeometryShader::DirectX12GeometryShader(const std::vector<char>& inByteCode)
        : DirectX12ShaderBase(inByteCode)
    {
    }

    DirectX12TessControlShader::DirectX12TessControlShader(const std::vector<char>& inByteCode)
        : DirectX12ShaderBase(inByteCode)
    {
    }

    DirectX12TessEvalShader::DirectX12TessEvalShader(const std::vector<char>& inByteCode)
        : DirectX12ShaderBase(inByteCode)
    {
    }

    DirectX12MeshShader::DirectX12MeshShader(const std::vector<char>& inByteCode) : DirectX12ShaderBase(inByteCode)
    {
    }

    DirectX12TaskShader::DirectX12TaskShader(const std::vector<char>& inByteCode) : DirectX12ShaderBase(inByteCode)
    {
    }

    DirectX12Swapchain::DirectX12Swapchain(DirectX12Device* inDevice, DirectX12Queue* graphicsQueue,
                                           DirectX12Queue* computeQueue, void* inWindowHandle, uint32_t width,
                                           uint32_t height, RHI::ERHIFormat format)
        : m_Device(inDevice), m_WindowHandle(inWindowHandle), m_Width(width), m_Height(height), m_Format(format),
          m_GraphicsQueue(graphicsQueue), m_ComputeQueue(computeQueue)
    {
        if (!m_Device || !IsWindow(static_cast<HWND>(m_WindowHandle)))
            throw std::runtime_error("Invalid DirectX12 swapchain window");
        Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
        CheckDX12(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)), "Create DXGI factory");
        DXGI_SWAP_CHAIN_DESC1 desc{};
        // A Win32 surface extent is the client area, not the requested outer
        // window size. Let DXGI resolve it just as Vulkan uses currentExtent.
        desc.Width = 0;
        desc.Height = 0;
        desc.Format = ToDxgiFormat(m_Format);
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        desc.Scaling = DXGI_SCALING_STRETCH;
        Microsoft::WRL::ComPtr<IDXGISwapChain1> swapchain;
        CheckDX12(factory->CreateSwapChainForHwnd(m_Device->GetGraphicsCommandQueue(), static_cast<HWND>(m_WindowHandle),
                                                  &desc, nullptr, nullptr, &swapchain),
                  "CreateSwapChainForHwnd");
        CheckDX12(swapchain.As(&m_Swapchain), "Query IDXGISwapChain3");
        DXGI_SWAP_CHAIN_DESC1 actualDesc{};
        CheckDX12(m_Swapchain->GetDesc1(&actualDesc), "Query swapchain extent");
        m_Width = actualDesc.Width;
        m_Height = actualDesc.Height;
        factory->MakeWindowAssociation(static_cast<HWND>(m_WindowHandle), DXGI_MWA_NO_ALT_ENTER);
        CreateBackBuffers();
        LOG_INFO("DirectX12 swapchain created: %ux%u, two buffers", m_Width, m_Height);
    }
    void DirectX12Swapchain::WaitForIdle()
    {
        if (m_ComputeQueue)
            m_ComputeQueue->WaitIdle();
        if (m_GraphicsQueue)
            m_GraphicsQueue->WaitIdle();
    }
    DirectX12Swapchain::~DirectX12Swapchain()
    {
        try
        {
            WaitForIdle();
        }
        catch (const std::exception& error)
        {
            LOG_ERROR("DirectX12 swapchain shutdown: %s", error.what());
        }
    }
    void DirectX12Swapchain::CreateBackBuffers()
    {
        RHI::RHITextureDesc desc{};
        desc.Width = m_Width;
        desc.Height = m_Height;
        desc.Format = m_Format;
        desc.Usage = RHI::ERHITextureCreateFlag::RenderTarget | RHI::ERHITextureCreateFlag::Presentable;
        for (UINT i = 0; i < 2; ++i)
        {
            Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
            CheckDX12(m_Swapchain->GetBuffer(i, IID_PPV_ARGS(&buffer)), "Get swapchain buffer");
            m_BackBuffers.push_back(std::make_shared<DirectX12Texture>(m_Device, desc, std::move(buffer)));
        }
    }
    RHI::RHISwapchain::RHISwapchainSlot DirectX12Swapchain::AcquireNextSlot()
    {
        RHISwapchainSlot slot{};
        slot.Texture = GetCurrentTexture();
        return slot;
    }
    DirectX12Texture* DirectX12Swapchain::GetCurrentTexture() const
    {
        return m_BackBuffers.at(m_Swapchain->GetCurrentBackBufferIndex()).get();
    }
    void DirectX12Swapchain::Present()
    {
        CheckDX12(m_Swapchain->Present(1, 0), "DXGI Present");
    }
    void DirectX12Swapchain::Resize(uint32_t width, uint32_t height)
    {
        if (!width || !height)
            return;
        RECT client{};
        if (!GetClientRect(static_cast<HWND>(m_WindowHandle), &client))
            throw std::runtime_error("Cannot query DirectX12 swapchain client extent");
        if (client.right <= client.left || client.bottom <= client.top)
            return; // Preserve the previous buffers while minimized.
        width = static_cast<uint32_t>(client.right - client.left);
        height = static_cast<uint32_t>(client.bottom - client.top);
        if (m_Width == width && m_Height == height)
            return;
        // The API's queues are drained before releasing swapchain references.
        WaitForIdle();
        m_BackBuffers.clear();
        CheckDX12(m_Swapchain->ResizeBuffers(2, 0, 0, ToDxgiFormat(m_Format), 0), "ResizeBuffers");
        DXGI_SWAP_CHAIN_DESC1 actualDesc{};
        CheckDX12(m_Swapchain->GetDesc1(&actualDesc), "Query resized swapchain extent");
        m_Width = actualDesc.Width;
        m_Height = actualDesc.Height;
        CreateBackBuffers();
    }
    DirectX12Texture::DirectX12Texture(DirectX12Device* inDevice, const RHI::RHITextureDesc& desc,
                                       Microsoft::WRL::ComPtr<ID3D12Resource> resource)
        : RHI::RHITexture(desc), m_Device(inDevice), m_Resource(std::move(resource)),
          m_NativeFormat(ToDxgiFormat(desc.Format))
    {
        if (m_Device) m_DeviceLifetime = m_Device->GetLifetimeToken();
        States.assign(1, D3D12_RESOURCE_STATE_PRESENT);
    }
    D3D12_CPU_DESCRIPTOR_HANDLE DirectX12Texture::GetRTV(uint32_t mip, uint32_t slice)
    {
        const uint64_t key = (uint64_t(slice) << 32) | mip;
        if (auto it = m_RTVs.find(key); it != m_RTVs.end())
            return it->second;
        D3D12_CPU_DESCRIPTOR_HANDLE handle{};
        if (!m_Device->AllocateRenderTargetDescriptor(handle))
            throw std::runtime_error("RTV heap exhausted");
        D3D12_RENDER_TARGET_VIEW_DESC desc{};
        desc.Format = m_NativeFormat;
        if (GetDesc().SampleCount > 1)
            desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMS;
        else if (GetDesc().ArraySize > 1)
        {
            desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
            desc.Texture2DArray.MipSlice = mip;
            desc.Texture2DArray.FirstArraySlice = slice;
            desc.Texture2DArray.ArraySize = 1;
        }
        else
        {
            desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
            desc.Texture2D.MipSlice = mip;
        }
        m_Device->GetNativeDevice()->CreateRenderTargetView(m_Resource.Get(), &desc, handle);
        return m_RTVs[key] = handle;
    }
    D3D12_CPU_DESCRIPTOR_HANDLE DirectX12Texture::GetDSV(uint32_t mip, uint32_t slice)
    {
        const uint64_t key = (uint64_t(slice) << 32) | mip;
        if (auto it = m_DSVs.find(key); it != m_DSVs.end())
            return it->second;
        D3D12_CPU_DESCRIPTOR_HANDLE handle{};
        if (!m_Device->AllocateDepthStencilDescriptor(handle))
            throw std::runtime_error("DSV heap exhausted");
        D3D12_DEPTH_STENCIL_VIEW_DESC desc{};
        desc.Format = m_NativeFormat;
        if (GetDesc().SampleCount > 1)
            desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMS;
        else if (GetDesc().ArraySize > 1)
        {
            desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
            desc.Texture2DArray.MipSlice = mip;
            desc.Texture2DArray.FirstArraySlice = slice;
            desc.Texture2DArray.ArraySize = 1;
        }
        else
        {
            desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
            desc.Texture2D.MipSlice = mip;
        }
        m_Device->GetNativeDevice()->CreateDepthStencilView(m_Resource.Get(), &desc, handle);
        return m_DSVs[key] = handle;
    }
    D3D12_CPU_DESCRIPTOR_HANDLE DirectX12Texture::GetDefaultSRV()
    {
        if (!m_DefaultSRV.ptr)
        {
            if (!m_Device->AllocateCbvSrvUavDescriptor(m_DefaultSRV))
                throw std::runtime_error("SRV heap exhausted");
            m_Device->GetNativeDevice()->CreateShaderResourceView(m_Resource.Get(), nullptr, m_DefaultSRV);
        }
        return m_DefaultSRV;
    }
    DirectX12ShaderResourceView::~DirectX12ShaderResourceView()
    {
        if (OwnerUseToken)
            --*OwnerUseToken;
        if (!m_DeviceLifetime.expired() && m_Device && m_Device->IsValid())
        {
            m_Device->FreeDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, m_CpuHandle);
            m_Device->FreeDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, m_ReadOnlyStorageUAV);
            m_Device->FreeDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, m_ReadOnlyRawStorageUAV);
        }
    }
    D3D12_CPU_DESCRIPTOR_HANDLE DirectX12ShaderResourceView::GetReadOnlyStorageUAV(D3D_SHADER_INPUT_TYPE type)
    {
        auto* buffer = dynamic_cast<DirectX12Buffer*>(GetResource());
        if (!m_bValid || !buffer ||
            !(buffer->GetNativeResource()->GetDesc().Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS))
            throw std::runtime_error("DXIL read-only UAV binding requires a UAV-capable buffer");
        const auto desc = ToReadOnlyStorageUAV(BufferDesc, type, buffer->GetDesc().Size);
        auto& handle = type == D3D_SIT_UAV_RWBYTEADDRESS ? m_ReadOnlyRawStorageUAV : m_ReadOnlyStorageUAV;
        if (!handle.ptr)
        {
            if (!m_Device->AllocateCbvSrvUavDescriptor(handle))
                throw std::runtime_error("Read-only storage UAV descriptor heap exhausted");
            m_Device->GetNativeDevice()->CreateUnorderedAccessView(buffer->GetNativeResource(), nullptr, &desc, handle);
        }
        return handle;
    }
    DirectX12UnorderedAccessView::~DirectX12UnorderedAccessView()
    {
        if (OwnerUseToken)
            --*OwnerUseToken;
        if (!m_DeviceLifetime.expired() && m_Device && m_Device->IsValid())
            m_Device->FreeDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, m_CpuHandle);
    }
    DirectX12Sampler::DirectX12Sampler(DirectX12Device* device, const RHI::RHISamplerDesc& desc)
        : RHI::RHISampler(desc), m_Device(device)
    {
        if (m_Device) m_DeviceLifetime = m_Device->GetLifetimeToken();
        if (!m_Device->AllocateSamplerDescriptor(m_CpuHandle))
            throw std::runtime_error("Sampler heap exhausted");
        D3D12_SAMPLER_DESC sampler{};
        sampler.Filter = ToFilter(desc.Filter, desc.bAnisotropyEnable, desc.bCompareEnable);
        sampler.AddressU = ToAddressMode(desc.AddressU);
        sampler.AddressV = ToAddressMode(desc.AddressV);
        sampler.AddressW = ToAddressMode(desc.AddressW);
        sampler.MipLODBias = desc.MipLodBias;
        sampler.MinLOD = desc.MinLod;
        sampler.MaxLOD = desc.MaxLod;
        sampler.MaxAnisotropy = std::clamp<UINT>(static_cast<UINT>(desc.MaxAnisotropy), 1, 16);
        sampler.ComparisonFunc = desc.bCompareEnable ? ToComparisonFunc(desc.CompareOp) : D3D12_COMPARISON_FUNC_NEVER;
        m_Device->GetNativeDevice()->CreateSampler(&sampler, m_CpuHandle);
    }
    DirectX12Sampler::~DirectX12Sampler()
    {
        if (!m_DeviceLifetime.expired() && m_Device && m_Device->IsValid())
            m_Device->FreeDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, m_CpuHandle);
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
} // namespace RHIDirectX12
