#pragma once

#include "RHIResource.h"
#include "RHICommandContex.h"
#include <cstdint>
#include <memory>
#include <vector>
#include <unordered_map>
#include <atomic>
#include <d3d12shader.h>

#if defined(_WIN32)
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#endif

namespace RHIDirectX12
{
    class DirectX12Device;
    class DirectX12Queue;

    // Native state is authoritative even when an RHI transition starts at Unknown.
    struct DirectX12ResourceState
    {
        virtual ~DirectX12ResourceState() = default;
        std::vector<D3D12_RESOURCE_STATES> States;
        // Logical access comes only from RHITransition, independently of temporary native states.
        std::vector<RHI::ERHIResourceAccess> DeclaredAccess;
        std::shared_ptr<std::atomic_uint32_t> ContextUses = std::make_shared<std::atomic_uint32_t>(0);
        DirectX12Queue* LastQueue = nullptr;
        uint64_t LastFence = 0;
        bool UploadHeap = false;
    };

    class DirectX12Texture : public RHI::RHITexture, public DirectX12ResourceState
    {
    public:
        DirectX12Texture(DirectX12Device* inDevice, const RHI::RHITextureDesc& desc);
        DirectX12Texture(DirectX12Device* inDevice, const RHI::RHITextureDesc& desc,
                         Microsoft::WRL::ComPtr<ID3D12Resource> resource);
        ~DirectX12Texture() override;

        DirectX12Device* GetDevice() const
        {
            return Device;
        }

#if defined(_WIN32)
        ID3D12Resource* GetNativeResource() const
        {
            return Resource.Get();
        }
        DXGI_FORMAT GetNativeFormat() const
        {
            return NativeFormat;
        }
        D3D12_CPU_DESCRIPTOR_HANDLE GetRTV(uint32_t mip = 0, uint32_t slice = 0);
        D3D12_CPU_DESCRIPTOR_HANDLE GetDSV(uint32_t mip = 0, uint32_t slice = 0);
        D3D12_CPU_DESCRIPTOR_HANDLE GetDefaultSRV();
#endif

    private:
        DirectX12Device* Device = nullptr;

#if defined(_WIN32)
        Microsoft::WRL::ComPtr<ID3D12Resource> Resource;
        DXGI_FORMAT NativeFormat = DXGI_FORMAT_UNKNOWN;
        std::unordered_map<uint64_t, D3D12_CPU_DESCRIPTOR_HANDLE> RTVs, DSVs;
        D3D12_CPU_DESCRIPTOR_HANDLE DefaultSRV{};
#endif
    };
    using DirectX12TextureSP = std::shared_ptr<DirectX12Texture>;

    class DirectX12Buffer : public RHI::RHIBuffer, public DirectX12ResourceState
    {
    public:
        DirectX12Buffer(DirectX12Device* inDevice, const RHI::RHIBufferDesc& desc);
        ~DirectX12Buffer() override;

        DirectX12Device* GetDevice() const
        {
            return Device;
        }

#if defined(_WIN32)
        ID3D12Resource* GetNativeResource() const
        {
            return Resource.Get();
        }
#endif

    private:
        DirectX12Device* Device = nullptr;

#if defined(_WIN32)
        Microsoft::WRL::ComPtr<ID3D12Resource> Resource;
#endif
    };
    using DirectX12BufferSP = std::shared_ptr<DirectX12Buffer>;

    class DirectX12ShaderResourceView : public RHI::RHIShaderResourceView
    {
    public:
        DirectX12ShaderResourceView(RHI::RHIViewableResource* resource, const RHI::RHITexSRVCreateInfo& createInfo);
        DirectX12ShaderResourceView(RHI::RHIViewableResource* resource, const RHI::RHIBufferSRVCreateInfo& createInfo);
        ~DirectX12ShaderResourceView() override;
        RHI::RHIBufferSRVCreateInfo BufferDesc{};
        std::vector<uint32_t> Subresources;
        std::shared_ptr<std::atomic_uint32_t> OwnerUseToken;

#if defined(_WIN32)
        const D3D12_CPU_DESCRIPTOR_HANDLE& GetCpuHandle() const
        {
            return CpuHandle;
        }
        bool IsValid() const
        {
            return bValid;
        }
        D3D12_CPU_DESCRIPTOR_HANDLE GetReadOnlyStorageUAV(D3D_SHADER_INPUT_TYPE type);
#endif

    private:
#if defined(_WIN32)
        D3D12_CPU_DESCRIPTOR_HANDLE CpuHandle{};
        D3D12_CPU_DESCRIPTOR_HANDLE ReadOnlyStorageUAV{};
        D3D12_CPU_DESCRIPTOR_HANDLE ReadOnlyRawStorageUAV{};
        DirectX12Device* Device = nullptr;
        bool bValid = false;
#endif
    };
    using DirectX12ShaderResourceViewSP = std::shared_ptr<DirectX12ShaderResourceView>;

    class DirectX12UnorderedAccessView : public RHI::RHIUnorderedAccessView
    {
    public:
        DirectX12UnorderedAccessView(RHI::RHIViewableResource* resource, const RHI::RHITexUAVCreateInfo& createInfo);
        DirectX12UnorderedAccessView(RHI::RHIViewableResource* resource, const RHI::RHIBufferUAVCreateInfo& createInfo);
        ~DirectX12UnorderedAccessView() override;
        std::vector<uint32_t> Subresources;
        std::shared_ptr<std::atomic_uint32_t> OwnerUseToken;

#if defined(_WIN32)
        const D3D12_CPU_DESCRIPTOR_HANDLE& GetCpuHandle() const
        {
            return CpuHandle;
        }
        bool IsValid() const
        {
            return bValid;
        }
#endif

    private:
#if defined(_WIN32)
        D3D12_CPU_DESCRIPTOR_HANDLE CpuHandle{};
        DirectX12Device* Device = nullptr;
        bool bValid = false;
#endif
    };
    using DirectX12UnorderedAccessViewSP = std::shared_ptr<DirectX12UnorderedAccessView>;

    class DirectX12StagingBuffer : public RHI::RHIStagingBuffer
    {
    public:
        DirectX12StagingBuffer(DirectX12Device* inDevice, uint32_t size,
                               D3D12_HEAP_TYPE heapType = D3D12_HEAP_TYPE_UPLOAD);
        ~DirectX12StagingBuffer();

        void* Map(uint32_t offset, uint32_t numBytes) override;
        void Unmap() override;

#if defined(_WIN32)
        ID3D12Resource* GetNativeResource() const
        {
            return Resource.Get();
        }
#endif

    private:
        DirectX12Device* Device = nullptr;

#if defined(_WIN32)
        Microsoft::WRL::ComPtr<ID3D12Resource> Resource;
        void* MappedPtr = nullptr;
        D3D12_HEAP_TYPE HeapType = D3D12_HEAP_TYPE_UPLOAD;
#endif

        std::vector<uint8_t> Storage;
        bool bMapped = false;
    };
    using DirectX12StagingBufferSP = std::shared_ptr<DirectX12StagingBuffer>;

    struct DirectX12ShaderBinding
    {
        D3D12_SHADER_INPUT_BIND_DESC Desc{};
        std::string Name;
        uint32_t ConstantBufferSize = 0;
        bool ReadOnlyBufferUAV = false;
    };

    class DirectX12ShaderBase
    {
    public:
        explicit DirectX12ShaderBase(const std::vector<char>& code);
        virtual ~DirectX12ShaderBase() = default;
        const std::vector<char>& GetByteCode() const
        {
            return ByteCode;
        }
        const std::vector<DirectX12ShaderBinding>& GetBindings() const
        {
            return Bindings;
        }
        ID3D12ShaderReflection* GetReflection() const
        {
            return Reflection.Get();
        }

    protected:
        std::vector<char> ByteCode;
        std::vector<DirectX12ShaderBinding> Bindings;
        Microsoft::WRL::ComPtr<ID3D12ShaderReflection> Reflection;
    };

    class DirectX12VertexShader : public RHI::RHIVertexShader, public DirectX12ShaderBase
    {
    public:
        explicit DirectX12VertexShader(const std::vector<char>& inByteCode);
        ~DirectX12VertexShader() override = default;

        const std::vector<char>& GetByteCode() const
        {
            return ByteCode;
        }
    };
    using DirectX12VertexShaderSP = std::shared_ptr<DirectX12VertexShader>;

    class DirectX12FragmentShader : public RHI::RHIFragmentShader, public DirectX12ShaderBase
    {
    public:
        explicit DirectX12FragmentShader(const std::vector<char>& inByteCode);
        ~DirectX12FragmentShader() override = default;

        const std::vector<char>& GetByteCode() const
        {
            return ByteCode;
        }
    };
    using DirectX12FragmentShaderSP = std::shared_ptr<DirectX12FragmentShader>;

    class DirectX12ComputeShader : public RHI::RHIComputeShader, public DirectX12ShaderBase
    {
    public:
        explicit DirectX12ComputeShader(const std::vector<char>& inByteCode);
        ~DirectX12ComputeShader() override = default;

        const std::vector<char>& GetByteCode() const
        {
            return ByteCode;
        }
    };
    using DirectX12ComputeShaderSP = std::shared_ptr<DirectX12ComputeShader>;

    class DirectX12GeometryShader : public RHI::RHIGeometryShader, public DirectX12ShaderBase
    {
    public:
        explicit DirectX12GeometryShader(const std::vector<char>& inByteCode);
        ~DirectX12GeometryShader() override = default;

        const std::vector<char>& GetByteCode() const
        {
            return ByteCode;
        }
    };
    using DirectX12GeometryShaderSP = std::shared_ptr<DirectX12GeometryShader>;

    class DirectX12TessControlShader : public RHI::RHITessControlShader, public DirectX12ShaderBase
    {
    public:
        explicit DirectX12TessControlShader(const std::vector<char>& inByteCode);
        ~DirectX12TessControlShader() override = default;

        const std::vector<char>& GetByteCode() const
        {
            return ByteCode;
        }
    };
    using DirectX12TessControlShaderSP = std::shared_ptr<DirectX12TessControlShader>;

    class DirectX12TessEvalShader : public RHI::RHITessEvalShader, public DirectX12ShaderBase
    {
    public:
        explicit DirectX12TessEvalShader(const std::vector<char>& inByteCode);
        ~DirectX12TessEvalShader() override = default;

        const std::vector<char>& GetByteCode() const
        {
            return ByteCode;
        }
    };
    using DirectX12TessEvalShaderSP = std::shared_ptr<DirectX12TessEvalShader>;

    class DirectX12MeshShader : public RHI::RHIMeshShader, public DirectX12ShaderBase
    {
    public:
        explicit DirectX12MeshShader(const std::vector<char>& inByteCode);
        ~DirectX12MeshShader() override = default;

        const std::vector<char>& GetByteCode() const
        {
            return ByteCode;
        }
    };
    using DirectX12MeshShaderSP = std::shared_ptr<DirectX12MeshShader>;

    class DirectX12TaskShader : public RHI::RHITaskShader, public DirectX12ShaderBase
    {
    public:
        explicit DirectX12TaskShader(const std::vector<char>& inByteCode);
        ~DirectX12TaskShader() override = default;

        const std::vector<char>& GetByteCode() const
        {
            return ByteCode;
        }
    };
    using DirectX12TaskShaderSP = std::shared_ptr<DirectX12TaskShader>;

    class DirectX12Swapchain : public RHI::RHISwapchain
    {
    public:
        DirectX12Swapchain(DirectX12Device* inDevice, DirectX12Queue* graphicsQueue, DirectX12Queue* computeQueue,
                           void* inWindowHandle, uint32_t width, uint32_t height, RHI::ERHIFormat format);
        ~DirectX12Swapchain() override;

        RHISwapchainSlot AcquireNextSlot() override;
        void Resize(uint32_t width, uint32_t height) override;

        uint32_t GetWidth() const
        {
            return Width;
        }
        uint32_t GetHeight() const
        {
            return Height;
        }
        void Present();
        DirectX12Texture* GetCurrentTexture() const;

    private:
        DirectX12Device* Device = nullptr;
        void* WindowHandle = nullptr;
        uint32_t Width = 0;
        uint32_t Height = 0;
        RHI::ERHIFormat Format = RHI::ERHIFormat::Unknown;
        DirectX12Queue* GraphicsQueue = nullptr;
        DirectX12Queue* ComputeQueue = nullptr;
        Microsoft::WRL::ComPtr<IDXGISwapChain3> Swapchain;
        std::vector<DirectX12TextureSP> BackBuffers;
        void CreateBackBuffers();
        void WaitForIdle();
    };
    using DirectX12SwapchainSP = std::shared_ptr<DirectX12Swapchain>;

    class DirectX12Sampler : public RHI::RHISampler
    {
    public:
        DirectX12Sampler(DirectX12Device* device, const RHI::RHISamplerDesc& desc);
        ~DirectX12Sampler() override;
        D3D12_CPU_DESCRIPTOR_HANDLE GetCpuHandle() const
        {
            return CpuHandle;
        }

    private:
        DirectX12Device* Device = nullptr;
        D3D12_CPU_DESCRIPTOR_HANDLE CpuHandle{};
    };
    using DirectX12SamplerSP = std::shared_ptr<DirectX12Sampler>;

    class DirectX12VertexDescState : public RHI::RHIVertexDescState
    {
    public:
        explicit DirectX12VertexDescState(const RHI::RHIVertexDescStateDesc& desc);
        ~DirectX12VertexDescState() override = default;
    };
    using DirectX12VertexDescStateSP = std::shared_ptr<DirectX12VertexDescState>;

    class DirectX12RasterizerState : public RHI::RHIRasterizerState
    {
    public:
        explicit DirectX12RasterizerState(const RHI::RHIRasterizerStateDesc& desc);
        ~DirectX12RasterizerState() override = default;
    };
    using DirectX12RasterizerStateSP = std::shared_ptr<DirectX12RasterizerState>;

    class DirectX12ColorBlendState : public RHI::RHIColorBlendState
    {
    public:
        explicit DirectX12ColorBlendState(const RHI::RHIColorBlendStateDesc& desc);
        ~DirectX12ColorBlendState() override = default;
    };
    using DirectX12ColorBlendStateSP = std::shared_ptr<DirectX12ColorBlendState>;

    class DirectX12DepthStencilState : public RHI::RHIDepthStencilState
    {
    public:
        explicit DirectX12DepthStencilState(const RHI::RHIDepthStencilStateDesc& desc);
        ~DirectX12DepthStencilState() override = default;
    };
    using DirectX12DepthStencilStateSP = std::shared_ptr<DirectX12DepthStencilState>;
} // namespace RHIDirectX12
