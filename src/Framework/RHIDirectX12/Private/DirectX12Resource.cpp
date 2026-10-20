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
    DirectX12Texture::DirectX12Texture(DirectX12Device* inDevice, const RHI::RHITextureDesc& desc)
        : RHI::RHITexture(desc), Device(inDevice)
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
        resourceDesc.DepthOrArraySize =
            static_cast<UINT16>(desc.Type == RHI::ERHITextureType::Texture3D ? std::max<uint32_t>(1u, desc.Depth)
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

        const bool created =
            Device->CreateCommittedResource(heapProperties, D3D12_HEAP_FLAG_NONE, resourceDesc,
                                            ToTextureInitialState(desc.Usage), clearValuePtr, Resource);
        States.assign(resourceDesc.MipLevels *
                          (desc.Type == RHI::ERHITextureType::Texture3D ? 1u : resourceDesc.DepthOrArraySize) *
                          (desc.Format == RHI::ERHIFormat::D24_UNorm_S8_UInt ? 2u : 1u),
                      ToTextureInitialState(desc.Usage));
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
        if (Device && Device->IsValid())
        {
            for (const auto& view : RTVs)
                Device->FreeDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, view.second);
            for (const auto& view : DSVs)
                Device->FreeDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, view.second);
            Device->FreeDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, DefaultSRV);
        }
        Resource.Reset();
#endif
    }

    DirectX12Buffer::DirectX12Buffer(DirectX12Device* inDevice, const RHI::RHIBufferDesc& desc)
        : RHI::RHIBuffer(desc), Device(inDevice)
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

        const bool created = Device->CreateCommittedResource(heapProperties, D3D12_HEAP_FLAG_NONE, resourceDesc,
                                                             initialState, nullptr, Resource);
        UploadHeap = heapProperties.Type == D3D12_HEAP_TYPE_UPLOAD;
        States.assign(1, initialState);
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

        texture->GetDevice()->GetNativeDevice()->CreateShaderResourceView(texture->GetNativeResource(), &srvDesc,
                                                                          CpuHandle);
        Device = texture->GetDevice();
        OwnerUseToken = GetResourceState(resource)->ContextUses;
        ++*OwnerUseToken;
        bValid = true;
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
        if (createInfo.Stride)
            srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        BufferDesc = createInfo;
        srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

        buffer->GetDevice()->GetNativeDevice()->CreateShaderResourceView(buffer->GetNativeResource(), &srvDesc,
                                                                         CpuHandle);
        Device = buffer->GetDevice();
        OwnerUseToken = GetResourceState(resource)->ContextUses;
        ++*OwnerUseToken;
        bValid = true;
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

        texture->GetDevice()->GetNativeDevice()->CreateUnorderedAccessView(texture->GetNativeResource(), nullptr,
                                                                           &uavDesc, CpuHandle);
        Device = texture->GetDevice();
        OwnerUseToken = GetResourceState(resource)->ContextUses;
        ++*OwnerUseToken;
        bValid = true;
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
        if (createInfo.Stride)
            uavDesc.Format = DXGI_FORMAT_UNKNOWN;
        uavDesc.Buffer.CounterOffsetInBytes = 0;
        uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

        buffer->GetDevice()->GetNativeDevice()->CreateUnorderedAccessView(buffer->GetNativeResource(), nullptr,
                                                                          &uavDesc, CpuHandle);
        Device = buffer->GetDevice();
        OwnerUseToken = GetResourceState(resource)->ContextUses;
        ++*OwnerUseToken;
        bValid = true;
#else
        (void)createInfo;
#endif
    }

    DirectX12StagingBuffer::DirectX12StagingBuffer(DirectX12Device* inDevice, uint32_t size, D3D12_HEAP_TYPE heapType)
        : RHI::RHIStagingBuffer(size), Device(inDevice), Storage(size == 0 ? 1u : size, 0)
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

        const D3D12_RESOURCE_STATES initialState =
            heapType == D3D12_HEAP_TYPE_READBACK ? D3D12_RESOURCE_STATE_COPY_DEST : D3D12_RESOURCE_STATE_GENERIC_READ;
        if (Device->CreateCommittedResource(heapProperties, D3D12_HEAP_FLAG_NONE, resourceDesc, initialState, nullptr,
                                            Resource))
        {
            D3D12_RANGE range{0, heapType == D3D12_HEAP_TYPE_READBACK ? size : 0};
            CheckDX12(Resource->Map(0, &range, &MappedPtr), "Map staging buffer");
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
    }

    DirectX12ShaderBase::DirectX12ShaderBase(const std::vector<char>& code)
    {
        ByteCode = code.size() >= 4 && std::memcmp(code.data(), "DXBC", 4) == 0 ? code : UnpackDXIL(code);
        if (ByteCode.empty())
            throw std::runtime_error("Empty DXIL shader");
        Microsoft::WRL::ComPtr<IDxcUtils> utils;
        CheckDX12(DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils)), "Create DXC reflection utils");
        Microsoft::WRL::ComPtr<IDxcBlobEncoding> blob;
        CheckDX12(utils->CreateBlob(ByteCode.data(), static_cast<UINT32>(ByteCode.size()), DXC_CP_ACP, &blob),
                  "Create DXIL blob");
        Microsoft::WRL::ComPtr<IDxcContainerReflection> container;
        CheckDX12(DxcCreateInstance(CLSID_DxcContainerReflection, IID_PPV_ARGS(&container)), "Create DXIL reflection");
        CheckDX12(container->Load(blob.Get()), "Load DXIL container");
        UINT32 index;
        CheckDX12(container->FindFirstPartKind(DXC_PART_DXIL, &index), "Find DXIL part");
        CheckDX12(container->GetPartReflection(index, IID_PPV_ARGS(&Reflection)), "Reflect DXIL");
        D3D12_SHADER_DESC shaderDesc{};
        CheckDX12(Reflection->GetDesc(&shaderDesc), "Reflect shader description");
        const auto assembly = DisassembleDXIL(ByteCode);
        for (UINT i = 0; i < shaderDesc.BoundResources; ++i)
        {
            DirectX12ShaderBinding binding;
            CheckDX12(Reflection->GetResourceBindingDesc(i, &binding.Desc), "Reflect shader binding");
            binding.Name = binding.Desc.Name;
            binding.ReadOnlyBufferUAV = IsReadOnlyBufferUAV(assembly, binding.Desc);
            binding.Desc.Name = nullptr; // Reflection owns the original name; keep our own copy.
            if (binding.Desc.Type == D3D_SIT_CBUFFER)
            {
                D3D12_SHADER_BUFFER_DESC bufferDesc{};
                CheckDX12(Reflection->GetConstantBufferByName(binding.Name.c_str())->GetDesc(&bufferDesc),
                          "Reflect constant buffer");
                binding.ConstantBufferSize = bufferDesc.Size;
            }
            Bindings.push_back(std::move(binding));
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
        : Device(inDevice), WindowHandle(inWindowHandle), Width(width), Height(height), Format(format),
          GraphicsQueue(graphicsQueue), ComputeQueue(computeQueue)
    {
        if (!Device || !IsWindow(static_cast<HWND>(WindowHandle)))
            throw std::runtime_error("Invalid DirectX12 swapchain window");
        Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
        CheckDX12(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)), "Create DXGI factory");
        DXGI_SWAP_CHAIN_DESC1 desc{};
        // A Win32 surface extent is the client area, not the requested outer
        // window size. Let DXGI resolve it just as Vulkan uses currentExtent.
        desc.Width = 0;
        desc.Height = 0;
        desc.Format = ToDxgiFormat(Format);
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        desc.Scaling = DXGI_SCALING_STRETCH;
        Microsoft::WRL::ComPtr<IDXGISwapChain1> swapchain;
        CheckDX12(factory->CreateSwapChainForHwnd(Device->GetGraphicsCommandQueue(), static_cast<HWND>(WindowHandle),
                                                  &desc, nullptr, nullptr, &swapchain),
                  "CreateSwapChainForHwnd");
        CheckDX12(swapchain.As(&Swapchain), "Query IDXGISwapChain3");
        DXGI_SWAP_CHAIN_DESC1 actualDesc{};
        CheckDX12(Swapchain->GetDesc1(&actualDesc), "Query swapchain extent");
        Width = actualDesc.Width;
        Height = actualDesc.Height;
        factory->MakeWindowAssociation(static_cast<HWND>(WindowHandle), DXGI_MWA_NO_ALT_ENTER);
        CreateBackBuffers();
        LOG_INFO("DirectX12 swapchain created: %ux%u, two buffers", Width, Height);
    }
    void DirectX12Swapchain::WaitForIdle()
    {
        if (ComputeQueue)
            ComputeQueue->WaitIdle();
        if (GraphicsQueue)
            GraphicsQueue->WaitIdle();
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
        desc.Width = Width;
        desc.Height = Height;
        desc.Format = Format;
        desc.Usage = RHI::ERHITextureCreateFlag::RenderTarget | RHI::ERHITextureCreateFlag::Presentable;
        for (UINT i = 0; i < 2; ++i)
        {
            Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
            CheckDX12(Swapchain->GetBuffer(i, IID_PPV_ARGS(&buffer)), "Get swapchain buffer");
            BackBuffers.push_back(std::make_shared<DirectX12Texture>(Device, desc, std::move(buffer)));
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
        return BackBuffers.at(Swapchain->GetCurrentBackBufferIndex()).get();
    }
    void DirectX12Swapchain::Present()
    {
        CheckDX12(Swapchain->Present(1, 0), "DXGI Present");
    }
    void DirectX12Swapchain::Resize(uint32_t width, uint32_t height)
    {
        if (!width || !height)
            return;
        RECT client{};
        if (!GetClientRect(static_cast<HWND>(WindowHandle), &client))
            throw std::runtime_error("Cannot query DirectX12 swapchain client extent");
        if (client.right <= client.left || client.bottom <= client.top)
            return; // Preserve the previous buffers while minimized.
        width = static_cast<uint32_t>(client.right - client.left);
        height = static_cast<uint32_t>(client.bottom - client.top);
        if (Width == width && Height == height)
            return;
        // The API's queues are drained before releasing swapchain references.
        WaitForIdle();
        BackBuffers.clear();
        CheckDX12(Swapchain->ResizeBuffers(2, 0, 0, ToDxgiFormat(Format), 0), "ResizeBuffers");
        DXGI_SWAP_CHAIN_DESC1 actualDesc{};
        CheckDX12(Swapchain->GetDesc1(&actualDesc), "Query resized swapchain extent");
        Width = actualDesc.Width;
        Height = actualDesc.Height;
        CreateBackBuffers();
    }
    DirectX12Texture::DirectX12Texture(DirectX12Device* inDevice, const RHI::RHITextureDesc& desc,
                                       Microsoft::WRL::ComPtr<ID3D12Resource> resource)
        : RHI::RHITexture(desc), Device(inDevice), Resource(std::move(resource)),
          NativeFormat(ToDxgiFormat(desc.Format))
    {
        States.assign(1, D3D12_RESOURCE_STATE_PRESENT);
    }
    D3D12_CPU_DESCRIPTOR_HANDLE DirectX12Texture::GetRTV(uint32_t mip, uint32_t slice)
    {
        const uint64_t key = (uint64_t(slice) << 32) | mip;
        if (auto it = RTVs.find(key); it != RTVs.end())
            return it->second;
        D3D12_CPU_DESCRIPTOR_HANDLE handle{};
        if (!Device->AllocateRenderTargetDescriptor(handle))
            throw std::runtime_error("RTV heap exhausted");
        D3D12_RENDER_TARGET_VIEW_DESC desc{};
        desc.Format = NativeFormat;
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
        Device->GetNativeDevice()->CreateRenderTargetView(Resource.Get(), &desc, handle);
        return RTVs[key] = handle;
    }
    D3D12_CPU_DESCRIPTOR_HANDLE DirectX12Texture::GetDSV(uint32_t mip, uint32_t slice)
    {
        const uint64_t key = (uint64_t(slice) << 32) | mip;
        if (auto it = DSVs.find(key); it != DSVs.end())
            return it->second;
        D3D12_CPU_DESCRIPTOR_HANDLE handle{};
        if (!Device->AllocateDepthStencilDescriptor(handle))
            throw std::runtime_error("DSV heap exhausted");
        D3D12_DEPTH_STENCIL_VIEW_DESC desc{};
        desc.Format = NativeFormat;
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
        Device->GetNativeDevice()->CreateDepthStencilView(Resource.Get(), &desc, handle);
        return DSVs[key] = handle;
    }
    D3D12_CPU_DESCRIPTOR_HANDLE DirectX12Texture::GetDefaultSRV()
    {
        if (!DefaultSRV.ptr)
        {
            if (!Device->AllocateCbvSrvUavDescriptor(DefaultSRV))
                throw std::runtime_error("SRV heap exhausted");
            Device->GetNativeDevice()->CreateShaderResourceView(Resource.Get(), nullptr, DefaultSRV);
        }
        return DefaultSRV;
    }
    DirectX12ShaderResourceView::~DirectX12ShaderResourceView()
    {
        if (OwnerUseToken)
            --*OwnerUseToken;
        if (Device && Device->IsValid())
        {
            Device->FreeDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, CpuHandle);
            Device->FreeDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, ReadOnlyStorageUAV);
            Device->FreeDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, ReadOnlyRawStorageUAV);
        }
    }
    D3D12_CPU_DESCRIPTOR_HANDLE DirectX12ShaderResourceView::GetReadOnlyStorageUAV(D3D_SHADER_INPUT_TYPE type)
    {
        auto* buffer = dynamic_cast<DirectX12Buffer*>(GetResource());
        if (!bValid || !buffer ||
            !(buffer->GetNativeResource()->GetDesc().Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS))
            throw std::runtime_error("DXIL read-only UAV binding requires a UAV-capable buffer");
        const auto desc = ToReadOnlyStorageUAV(BufferDesc, type, buffer->GetDesc().Size);
        auto& handle = type == D3D_SIT_UAV_RWBYTEADDRESS ? ReadOnlyRawStorageUAV : ReadOnlyStorageUAV;
        if (!handle.ptr)
        {
            if (!Device->AllocateCbvSrvUavDescriptor(handle))
                throw std::runtime_error("Read-only storage UAV descriptor heap exhausted");
            Device->GetNativeDevice()->CreateUnorderedAccessView(buffer->GetNativeResource(), nullptr, &desc, handle);
        }
        return handle;
    }
    DirectX12UnorderedAccessView::~DirectX12UnorderedAccessView()
    {
        if (OwnerUseToken)
            --*OwnerUseToken;
        if (Device && Device->IsValid())
            Device->FreeDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, CpuHandle);
    }
    DirectX12Sampler::DirectX12Sampler(DirectX12Device* device, const RHI::RHISamplerDesc& desc)
        : RHI::RHISampler(desc), Device(device)
    {
        if (!Device->AllocateSamplerDescriptor(CpuHandle))
            throw std::runtime_error("Sampler heap exhausted");
        D3D12_SAMPLER_DESC sampler{};
        sampler.Filter = ToFilter(desc.filter, desc.anisotropyEnable, desc.CompareEnable);
        sampler.AddressU = ToAddressMode(desc.addressU);
        sampler.AddressV = ToAddressMode(desc.addressV);
        sampler.AddressW = ToAddressMode(desc.addressW);
        sampler.MipLODBias = desc.mipLodBias;
        sampler.MinLOD = desc.minLod;
        sampler.MaxLOD = desc.maxLod;
        sampler.MaxAnisotropy = std::clamp<UINT>(static_cast<UINT>(desc.maxAnisotropy), 1, 16);
        sampler.ComparisonFunc = desc.CompareEnable ? ToComparisonFunc(desc.CompareOp) : D3D12_COMPARISON_FUNC_NEVER;
        Device->GetNativeDevice()->CreateSampler(&sampler, CpuHandle);
    }
    DirectX12Sampler::~DirectX12Sampler()
    {
        if (Device && Device->IsValid())
            Device->FreeDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, CpuHandle);
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
