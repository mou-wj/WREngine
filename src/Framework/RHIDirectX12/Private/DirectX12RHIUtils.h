#pragma once
#include "DirectX12Resource.h"
#include "Log.h"
#include <dxcapi.h>
#include <stdexcept>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace RHIDirectX12
{
    std::vector<uint32_t> TextureViewSubresources(const RHI::RHITextureDesc& desc, uint32_t mip, uint32_t mipCount,
                                                  uint32_t slice, uint32_t sliceCount);
    inline void CheckDX12(HRESULT result, const char* operation)
    {
        if (FAILED(result))
        {
            char message[256];
            std::snprintf(message, sizeof(message), "%s failed (HRESULT 0x%08lX)", operation,
                          static_cast<unsigned long>(result));
            throw std::runtime_error(message);
        }
    }
    inline std::string DX12DiagnosticDirectory()
    {
        const DWORD length = GetEnvironmentVariableA("RHI_DX12_DIAGNOSTICS", nullptr, 0);
        if (!length)
            return {};
        std::string value(length, '\0');
        GetEnvironmentVariableA("RHI_DX12_DIAGNOSTICS", value.data(), length);
        value.resize(length - 1);
        return value;
    }
    inline bool DX12Diagnostics()
    {
        return !DX12DiagnosticDirectory().empty();
    }
    inline DirectX12ResourceState* GetResourceState(RHI::RHIViewableResource* resource)
    {
        if (auto* texture = dynamic_cast<DirectX12Texture*>(resource))
            return texture;
        return dynamic_cast<DirectX12Buffer*>(resource);
    }
    inline ID3D12Resource* GetNativeResource(RHI::RHIViewableResource* resource)
    {
        if (auto* texture = dynamic_cast<DirectX12Texture*>(resource))
            return texture->GetNativeResource();
        if (auto* buffer = dynamic_cast<DirectX12Buffer*>(resource))
            return buffer->GetNativeResource();
        return nullptr;
    }

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS ToRayTracingBuildFlags(bool update, bool compaction);
    Microsoft::WRL::ComPtr<ID3D12Resource> CreateRayTracingBuffer(DirectX12Device* device, uint64_t size,
        D3D12_RESOURCE_STATES state, bool upload = false);
    RHI::ERHIFormat ToStorageImageFormat(RHI::ERHIFormat preferred);
    DXGI_FORMAT ToDxgiFormat(RHI::ERHIFormat format);
    D3D12_RESOURCE_DIMENSION ToResourceDimension(RHI::ERHITextureType type);
    D3D12_RESOURCE_FLAGS ToTextureFlags(RHI::ERHITextureCreateFlags flags);
    D3D12_RESOURCE_STATES ToTextureInitialState(RHI::ERHITextureCreateFlags flags);
    D3D12_RESOURCE_FLAGS ToBufferFlags(RHI::ERHIBufferUsageFlags usage, bool uploadHeap = false);
    DXGI_FORMAT ToDxgiSrvFormat(RHI::ERHIFormat format, DXGI_FORMAT fallback);
    D3D12_SRV_DIMENSION ToSrvDimension(RHI::ERHITextureType textureType, RHI::ERHITextureViewType viewType);
    D3D12_UAV_DIMENSION ToUavDimension(RHI::ERHITextureType textureType, RHI::ERHITextureViewType viewType);
    D3D12_FILL_MODE ToFillMode(RHI::ERHIPolygonMode mode);
    D3D12_CULL_MODE ToCullMode(RHI::ERHICullMode mode);
    D3D12_COMPARISON_FUNC ToComparisonFunc(RHI::ERHICompareOp op);
    D3D12_PRIMITIVE_TOPOLOGY_TYPE ToPrimitiveTopologyType(RHI::EPrimitiveTopology topology);
    D3D_PRIMITIVE_TOPOLOGY ToPrimitiveTopology(RHI::EPrimitiveTopology topology);
    D3D12_RESOURCE_STATES ToResourceState(RHI::ERHIResourceAccess access, RHI::RHIViewableResource* resource);
    D3D12_TEXTURE_ADDRESS_MODE ToAddressMode(RHI::ERHIAddressMode mode);
    D3D12_FILTER ToFilter(RHI::ERHIFilter filter, bool anisotropic, bool comparison);
    D3D12_INPUT_CLASSIFICATION ToInputClassification(RHI::ERHIInputRate rate);
    BOOL ToFrontCounterClockwise(RHI::ERHIFrontFace face);
    D3D12_UNORDERED_ACCESS_VIEW_DESC ToReadOnlyStorageUAV(const RHI::RHIBufferSRVCreateInfo& view,
                                                          D3D_SHADER_INPUT_TYPE type, uint64_t bufferSize);
    inline bool IsUAV(D3D_SHADER_INPUT_TYPE type)
    {
        return type == D3D_SIT_UAV_RWTYPED || type == D3D_SIT_UAV_RWSTRUCTURED || type == D3D_SIT_UAV_RWBYTEADDRESS ||
               type == D3D_SIT_UAV_APPEND_STRUCTURED || type == D3D_SIT_UAV_CONSUME_STRUCTURED ||
               type == D3D_SIT_UAV_RWSTRUCTURED_WITH_COUNTER;
    }
    inline std::vector<char> CompileDX12Shader(const char* source, const wchar_t* entry, const wchar_t* target)
    {
        Microsoft::WRL::ComPtr<IDxcCompiler3> compiler;
        CheckDX12(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler)), "Create DXC");
        DxcBuffer buffer{source, std::strlen(source), DXC_CP_UTF8};
        const wchar_t* args[] = {L"-E", entry, L"-T", target, L"-HV", L"2021"};
        Microsoft::WRL::ComPtr<IDxcResult> result;
        CheckDX12(compiler->Compile(&buffer, args, _countof(args), nullptr, IID_PPV_ARGS(&result)),
                  "Compile blit shader");
        HRESULT status;
        CheckDX12(result->GetStatus(&status), "DXC status");
        if (FAILED(status))
        {
            Microsoft::WRL::ComPtr<IDxcBlobUtf8> errors;
            result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);
            throw std::runtime_error(errors ? errors->GetStringPointer() : "DXC compile failed");
        }
        Microsoft::WRL::ComPtr<IDxcBlob> code;
        CheckDX12(result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&code), nullptr), "DXC bytecode");
        const char* data = static_cast<const char*>(code->GetBufferPointer());
        return {data, data + code->GetBufferSize()};
    }
} // namespace RHIDirectX12
