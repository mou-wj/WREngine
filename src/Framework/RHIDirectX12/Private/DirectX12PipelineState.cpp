#include "DirectX12PipelineState.h"
#include "DirectX12Device.h"
#include "DirectX12Resource.h"
#include "DirectX12RHIUtils.h"

#if defined(_WIN32)
#include <array>
#include <vector>
#endif

namespace
{
#if defined(_WIN32)

    static void CreateReflectedRootSignature(
        ID3D12Device* device,
        const std::vector<std::pair<const RHIDirectX12::DirectX12ShaderBase*, D3D12_SHADER_VISIBILITY>>& shaders,
        bool allowIA, Microsoft::WRL::ComPtr<ID3D12RootSignature>& signature,
        std::vector<RHIDirectX12::DirectX12RootBinding>& bindings, bool merge = false)
    {
        std::vector<D3D12_ROOT_PARAMETER> roots;
        std::vector<D3D12_DESCRIPTOR_RANGE> ranges;
        size_t count = 0;
        for (const auto& shader : shaders)
            if (shader.first)
                count += shader.first->GetBindings().size();
        roots.reserve(count);
        ranges.reserve(count);
        bindings.reserve(count);
        for (const auto& shader : shaders)
        {
            if (!shader.first)
                continue;
            for (const auto& binding : shader.first->GetBindings())
            {
                const auto& desc = binding.Desc;
                if (desc.Space != 0 && !merge)
                    throw std::runtime_error("RHI resource parameters cannot identify nonzero D3D12 register spaces");
                if (merge)
                {
                    auto found = std::find_if(bindings.begin(), bindings.end(), [&](const auto& existing) {
                        return existing.Binding.Desc.Type == desc.Type && existing.Binding.Desc.BindPoint == desc.BindPoint &&
                            existing.Binding.Desc.Space == desc.Space;
                    });
                    if (found != bindings.end())
                    {
                        if (found->Binding.Desc.BindCount != desc.BindCount || found->Binding.Name != binding.Name)
                            throw std::runtime_error("DXR global resource registers overlap between shaders");
                        bindings.push_back({shader.first, binding, found->RootIndex});
                        continue;
                    }
                }
                D3D12_ROOT_PARAMETER root{};
                root.ShaderVisibility = shader.second;
                if (desc.Type == D3D_SIT_CBUFFER && desc.BindCount != 1)
                    throw std::runtime_error("DirectX12 constant-buffer arrays are unsupported");
                if (desc.Type == D3D_SIT_RTACCELERATIONSTRUCTURE)
                {
                    if (desc.BindCount != 1) throw std::runtime_error("DXR acceleration structure arrays are unsupported");
                    root.ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
                    root.Descriptor.ShaderRegister = desc.BindPoint;
                    root.Descriptor.RegisterSpace = desc.Space;
                }
                else if (desc.Type == D3D_SIT_CBUFFER)
                {
                    root.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
                    root.Descriptor.ShaderRegister = desc.BindPoint;
                    root.Descriptor.RegisterSpace = desc.Space;
                }
                else
                {
                    if (!desc.BindCount || desc.BindCount == UINT_MAX)
                        throw std::runtime_error("Unbounded DirectX12 descriptor arrays are unsupported");
                    D3D12_DESCRIPTOR_RANGE range{};
                    range.RangeType = RHIDirectX12::IsUAV(desc.Type) ? D3D12_DESCRIPTOR_RANGE_TYPE_UAV
                                      : desc.Type == D3D_SIT_SAMPLER ? D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER
                                      : desc.Type == D3D_SIT_CBUFFER ? D3D12_DESCRIPTOR_RANGE_TYPE_CBV
                                                                     : D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
                    range.NumDescriptors = desc.BindCount;
                    range.BaseShaderRegister = desc.BindPoint;
                    range.RegisterSpace = desc.Space;
                    ranges.push_back(range);
                    root.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
                    root.DescriptorTable.NumDescriptorRanges = 1;
                    root.DescriptorTable.pDescriptorRanges = &ranges.back();
                }
                bindings.push_back({shader.first, binding, static_cast<uint32_t>(roots.size())});
                roots.push_back(root);
            }
        }
        D3D12_ROOT_SIGNATURE_DESC desc{};
        desc.NumParameters = static_cast<UINT>(roots.size());
        desc.pParameters = roots.data();
        desc.Flags =
            allowIA ? D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT : D3D12_ROOT_SIGNATURE_FLAG_NONE;
        Microsoft::WRL::ComPtr<ID3DBlob> blob, errors;
        HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors);
        if (FAILED(hr) && errors)
            throw std::runtime_error(static_cast<const char*>(errors->GetBufferPointer()));
        RHIDirectX12::CheckDX12(hr, "Serialize reflected root signature");
        RHIDirectX12::CheckDX12(
            device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&signature)),
            "Create reflected root signature");
    }

#endif
} // namespace

namespace RHIDirectX12
{
    DirectX12RayTracingPipelineState::DirectX12RayTracingPipelineState(DirectX12Device* device,
        const RHI::RHIRayTracingPipelineStateDesc& desc) : RHI::RHIRayTracingPipelineState(desc)
    {
        if (!device->SupportsRayTracing()) throw std::runtime_error("D3D12 adapter does not support DXR");
        if (desc.RayGenTable.size() != 1) throw std::runtime_error("DXR requires exactly one ray-generation shader");
        Microsoft::WRL::ComPtr<ID3D12Device5> dxr;
        CheckDX12(device->GetNativeDevice()->QueryInterface(IID_PPV_ARGS(&dxr)), "Query DXR device");
        std::vector<const DirectX12ShaderBase*> shaders;
        std::vector<std::pair<const DirectX12ShaderBase*, D3D12_SHADER_VISIBILITY>> reflected;
        const auto add = [&](RHI::RHIRayTracingShader* shader) {
            auto* native = dynamic_cast<DirectX12ShaderBase*>(shader);
            if (!native) throw std::runtime_error("Invalid DXR shader");
            if (std::find(shaders.begin(), shaders.end(), native) == shaders.end())
            { shaders.push_back(native); reflected.push_back({native, D3D12_SHADER_VISIBILITY_ALL}); }
            return native;
        };
        for (auto* shader : desc.RayGenTable) add(shader);
        for (auto* shader : desc.MissTable) add(shader);
        for (const auto& hitGroup : desc.HitGroupTable)
        {
            if (hitGroup.ClosestHitShader) add(hitGroup.ClosestHitShader);
            if (hitGroup.AnyHitShader) add(hitGroup.AnyHitShader);
            if (hitGroup.IntersectionShader) add(hitGroup.IntersectionShader);
        }
        for (auto* shader : desc.CallableTable) add(shader);
        CreateReflectedRootSignature(device->GetNativeDevice(), reflected, false, RootSignature, Bindings, true);
        std::vector<std::wstring> shaderNames;
        shaderNames.reserve(shaders.size());
        for (size_t i = 0; i < shaders.size(); ++i) shaderNames.push_back(L"RHIShader" + std::to_wstring(i));
        const auto exportName = [&](RHI::RHIRayTracingShader* shader) -> const std::wstring& {
            const auto* native = dynamic_cast<const DirectX12ShaderBase*>(shader);
            const auto it = std::find(shaders.begin(), shaders.end(), native);
            if (it == shaders.end()) throw std::runtime_error("DXR shader is not in the pipeline");
            return shaderNames[static_cast<size_t>(it - shaders.begin())];
        };
        std::vector<D3D12_EXPORT_DESC> exports(shaders.size());
        std::vector<D3D12_DXIL_LIBRARY_DESC> libraries(shaders.size());
        std::vector<std::wstring> hitNames;
        hitNames.reserve(desc.HitGroupTable.size());
        std::vector<D3D12_HIT_GROUP_DESC> hits(desc.HitGroupTable.size());
        std::vector<D3D12_STATE_SUBOBJECT> objects;
        objects.reserve(libraries.size() + hits.size() + 3);
        for (size_t i = 0; i < shaders.size(); ++i)
        {
            exports[i].Name = shaderNames[i].c_str();
            exports[i].ExportToRename = shaders[i]->ExportName.c_str();
            libraries[i].DXILLibrary = { shaders[i]->GetByteCode().data(), shaders[i]->GetByteCode().size() };
            libraries[i].NumExports = 1;
            libraries[i].pExports = &exports[i];
            objects.push_back({ D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY, &libraries[i] });
        }
        for (size_t i = 0; i < hits.size(); ++i)
        {
            hitNames.push_back(L"RHIHitGroup" + std::to_wstring(i));
            hits[i].HitGroupExport = hitNames.back().c_str();
            const auto& hitGroup = desc.HitGroupTable[i];
            hits[i].Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
            if (hitGroup.ClosestHitShader) hits[i].ClosestHitShaderImport = exportName(hitGroup.ClosestHitShader).c_str();
            if (hitGroup.AnyHitShader) hits[i].AnyHitShaderImport = exportName(hitGroup.AnyHitShader).c_str();
            if (hitGroup.IntersectionShader)
            {
                hits[i].Type = D3D12_HIT_GROUP_TYPE_PROCEDURAL_PRIMITIVE;
                hits[i].IntersectionShaderImport = exportName(hitGroup.IntersectionShader).c_str();
            }
            objects.push_back({ D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP, &hits[i] });
        }
        D3D12_GLOBAL_ROOT_SIGNATURE root{ RootSignature.Get() };
        // RHI currently exposes no payload/attribute size fields; reserve backend limits explicitly.
        D3D12_RAYTRACING_SHADER_CONFIG config{ 128, D3D12_RAYTRACING_MAX_ATTRIBUTE_SIZE_IN_BYTES };
        D3D12_RAYTRACING_PIPELINE_CONFIG pipeline{ 1 };
        objects.push_back({ D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE, &root });
        objects.push_back({ D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG, &config });
        objects.push_back({ D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG, &pipeline });
        D3D12_STATE_OBJECT_DESC state{ D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE,
            static_cast<UINT>(objects.size()), objects.data() };
        CheckDX12(dxr->CreateStateObject(&state, IID_PPV_ARGS(&StateObject)), "Create DXR state object");
        Microsoft::WRL::ComPtr<ID3D12StateObjectProperties> properties;
        CheckDX12(StateObject.As(&properties), "Query DXR shader identifiers");
        const UINT counts[] = { 1, static_cast<UINT>(desc.MissTable.size()), static_cast<UINT>(hits.size()), static_cast<UINT>(desc.CallableTable.size()) };
        uint64_t offsets[4]{}, size = 0;
        for (UINT i = 0; i < 4; ++i) { size = (size + 63) & ~uint64_t(63); offsets[i] = size; size += counts[i] * D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES; }
        ShaderTable = CreateRayTracingBuffer(device, size, D3D12_RESOURCE_STATE_GENERIC_READ, true);
        uint8_t* data = nullptr;
        D3D12_RANGE empty{};
        CheckDX12(ShaderTable->Map(0, &empty, reinterpret_cast<void**>(&data)), "Map DXR shader table");
        memset(data, 0, static_cast<size_t>(size));
        const auto record = [&](UINT table, UINT index, const std::wstring& name) {
            const void* identifier = properties->GetShaderIdentifier(name.c_str());
            if (!identifier) throw std::runtime_error("DXR shader identifier missing");
            memcpy(data + offsets[table] + index * D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES, identifier, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
        };
        record(0, 0, exportName(desc.RayGenTable[0]));
        for (UINT i = 0; i < counts[1]; ++i) record(1, i, exportName(desc.MissTable[i]));
        for (UINT i = 0; i < counts[2]; ++i) record(2, i, hitNames[i]);
        for (UINT i = 0; i < counts[3]; ++i) record(3, i, exportName(desc.CallableTable[i]));
        ShaderTable->Unmap(0, nullptr);
        const auto address = ShaderTable->GetGPUVirtualAddress();
        Dispatch.RayGenerationShaderRecord = { address, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES };
        D3D12_GPU_VIRTUAL_ADDRESS_RANGE_AND_STRIDE* tables[] = { &Dispatch.MissShaderTable, &Dispatch.HitGroupTable, &Dispatch.CallableShaderTable };
        for (UINT i = 1; i < 4; ++i)
            if (counts[i]) *tables[i-1] = { address + offsets[i], uint64_t(counts[i]) * D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES };
    }

    DirectX12GraphicsPipelineState::DirectX12GraphicsPipelineState(DirectX12Device* inDevice,
                                                                   const RHI::RHIGraphicsPipelineStateDesc& desc)
        : RHI::RHIGraphicsPipelineState(desc), m_Device(inDevice)
    {
#if defined(_WIN32)
        if (!m_Device || !m_Device->GetNativeDevice())
        {
            return;
        }

        auto* vertexShader = dynamic_cast<DirectX12VertexShader*>(desc.ShaderStages.VertexShader);
        auto* pixelShader = dynamic_cast<DirectX12FragmentShader*>(desc.ShaderStages.FragmentShader);
        if (!vertexShader || !pixelShader)
        {
            return;
        }

        CreateReflectedRootSignature(
            m_Device->GetNativeDevice(),
            {{vertexShader, D3D12_SHADER_VISIBILITY_VERTEX}, {pixelShader, D3D12_SHADER_VISIBILITY_PIXEL}}, true,
            m_RootSignature, m_Bindings);

        std::vector<D3D12_INPUT_ELEMENT_DESC> inputElements;
        std::vector<std::string> semanticNames;
        if (desc.VertexDescState)
        {
            const auto& vertexDesc = desc.VertexDescState->GetDesc();
            inputElements.reserve(vertexDesc.Attributes.size());
            semanticNames.reserve(vertexDesc.Attributes.size());

            D3D12_SHADER_DESC shaderDesc{};
            CheckDX12(vertexShader->GetReflection()->GetDesc(&shaderDesc), "Reflect vertex inputs");
            semanticNames.reserve(shaderDesc.InputParameters);
            for (UINT i = 0; i < shaderDesc.InputParameters; ++i)
            {
                D3D12_SIGNATURE_PARAMETER_DESC input{};
                CheckDX12(vertexShader->GetReflection()->GetInputParameterDesc(i, &input), "Reflect vertex semantic");
                if (input.SystemValueType != D3D_NAME_UNDEFINED)
                    continue;
                auto attribute = std::find_if(vertexDesc.Attributes.begin(), vertexDesc.Attributes.end(),
                                              [i](const auto& value) { return value.Location == i; });
                if (attribute == vertexDesc.Attributes.end())
                    throw std::runtime_error("Missing RHI vertex attribute for reflected input");
                semanticNames.emplace_back(input.SemanticName);
                D3D12_INPUT_ELEMENT_DESC element{};
                element.SemanticName = semanticNames.back().c_str();
                element.SemanticIndex = input.SemanticIndex;
                element.Format = ToDxgiFormat(attribute->Format);
                element.InputSlot = attribute->Binding;
                element.AlignedByteOffset = attribute->Offset;
                element.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
                for (const auto& binding : vertexDesc.Bindings)
                    if (binding.Binding == attribute->Binding &&
                        ToInputClassification(binding.InputRate) == D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA)
                    {
                        element.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA;
                        element.InstanceDataStepRate = 1;
                    }
                inputElements.push_back(element);
            }
        }

        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
        psoDesc.pRootSignature = m_RootSignature.Get();
        psoDesc.VS.pShaderBytecode = vertexShader->GetByteCode().data();
        psoDesc.VS.BytecodeLength = vertexShader->GetByteCode().size();
        psoDesc.PS.pShaderBytecode = pixelShader->GetByteCode().data();
        psoDesc.PS.BytecodeLength = pixelShader->GetByteCode().size();

        psoDesc.BlendState.AlphaToCoverageEnable = FALSE;
        psoDesc.BlendState.IndependentBlendEnable = FALSE;
        for (int i = 0; i < 8; ++i)
        {
            auto& rtDesc = psoDesc.BlendState.RenderTarget[i];
            rtDesc.BlendEnable = FALSE;
            rtDesc.LogicOpEnable = FALSE;
            rtDesc.SrcBlend = D3D12_BLEND_ONE;
            rtDesc.DestBlend = D3D12_BLEND_ZERO;
            rtDesc.BlendOp = D3D12_BLEND_OP_ADD;
            rtDesc.SrcBlendAlpha = D3D12_BLEND_ONE;
            rtDesc.DestBlendAlpha = D3D12_BLEND_ZERO;
            rtDesc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
            rtDesc.LogicOp = D3D12_LOGIC_OP_NOOP;
            rtDesc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        }
        if (desc.ColorBlendState)
        {
            const auto& blendDesc = desc.ColorBlendState->GetDesc();
            psoDesc.BlendState.AlphaToCoverageEnable = FALSE;
            psoDesc.BlendState.IndependentBlendEnable = TRUE;
            for (size_t i = 0; i < blendDesc.Attachments.size() && i < 8; ++i)
            {
                const auto& attachment = blendDesc.Attachments[i];
                auto& rtDesc = psoDesc.BlendState.RenderTarget[i];
                rtDesc.BlendEnable = attachment.bBlendEnable ? TRUE : FALSE;
                rtDesc.LogicOpEnable = FALSE;
                rtDesc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
                rtDesc.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
                rtDesc.BlendOp = D3D12_BLEND_OP_ADD;
                rtDesc.SrcBlendAlpha = D3D12_BLEND_ONE;
                rtDesc.DestBlendAlpha = D3D12_BLEND_ZERO;
                rtDesc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
                rtDesc.LogicOp = D3D12_LOGIC_OP_NOOP;
                rtDesc.RenderTargetWriteMask = static_cast<UINT8>(attachment.ColorWriteMask);
            }
        }

        psoDesc.SampleMask = UINT_MAX;

        psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
        psoDesc.RasterizerState.FrontCounterClockwise = FALSE;
        psoDesc.RasterizerState.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
        psoDesc.RasterizerState.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
        psoDesc.RasterizerState.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
        psoDesc.RasterizerState.DepthClipEnable = TRUE;
        psoDesc.RasterizerState.MultisampleEnable = FALSE;
        psoDesc.RasterizerState.AntialiasedLineEnable = FALSE;
        psoDesc.RasterizerState.ForcedSampleCount = 0;
        psoDesc.RasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
        if (desc.RasterizerState)
        {
            const auto& rsDesc = desc.RasterizerState->GetDesc();
            psoDesc.RasterizerState.FillMode = ToFillMode(rsDesc.PolygonMode);
            psoDesc.RasterizerState.CullMode = ToCullMode(rsDesc.CullMode);
            psoDesc.RasterizerState.FrontCounterClockwise = ToFrontCounterClockwise(rsDesc.FrontFace);
            psoDesc.RasterizerState.DepthBias =
                rsDesc.bDepthBiasEnable ? static_cast<INT>(rsDesc.DepthBiasConstantFactor) : 0;
            psoDesc.RasterizerState.DepthBiasClamp = rsDesc.DepthBiasClamp;
            psoDesc.RasterizerState.SlopeScaledDepthBias = rsDesc.DepthBiasSlopeFactor;
            psoDesc.RasterizerState.DepthClipEnable = rsDesc.bDepthClampEnable ? FALSE : TRUE;
        }

        psoDesc.DepthStencilState.DepthEnable = TRUE;
        psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
        psoDesc.DepthStencilState.StencilEnable = FALSE;
        psoDesc.DepthStencilState.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
        psoDesc.DepthStencilState.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
        psoDesc.DepthStencilState.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
        psoDesc.DepthStencilState.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
        psoDesc.DepthStencilState.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
        psoDesc.DepthStencilState.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        psoDesc.DepthStencilState.BackFace = psoDesc.DepthStencilState.FrontFace;
        if (desc.DepthStencilState)
        {
            const auto& dsDesc = desc.DepthStencilState->GetDesc();
            psoDesc.DepthStencilState.DepthEnable = dsDesc.bDepthTestEnable ? TRUE : FALSE;
            psoDesc.DepthStencilState.DepthWriteMask =
                dsDesc.bDepthWriteEnable ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
            psoDesc.DepthStencilState.DepthFunc = ToComparisonFunc(dsDesc.DepthCompareOp);
            psoDesc.DepthStencilState.StencilEnable = dsDesc.bStencilTestEnable ? TRUE : FALSE;
            psoDesc.DepthStencilState.StencilReadMask = static_cast<UINT8>(dsDesc.StencilReadMask);
            psoDesc.DepthStencilState.StencilWriteMask = static_cast<UINT8>(dsDesc.StencilWriteMask);
        }

        psoDesc.InputLayout.pInputElementDescs = inputElements.empty() ? nullptr : inputElements.data();
        psoDesc.InputLayout.NumElements = static_cast<UINT>(inputElements.size());
        psoDesc.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
        psoDesc.PrimitiveTopologyType = ToPrimitiveTopologyType(desc.PrimitiveTopology);

        psoDesc.NumRenderTargets = desc.AttachmentDesc.ColorAttachmentCount;
        for (uint32_t i = 0; i < desc.AttachmentDesc.ColorAttachmentCount && i < 8; ++i)
        {
            psoDesc.RTVFormats[i] = ToDxgiFormat(desc.AttachmentDesc.ColorAttachments[i].Format);
        }
        if (desc.AttachmentDesc.bEnableDepth || desc.AttachmentDesc.bEnableStencil)
        {
            psoDesc.DSVFormat = ToDxgiFormat(desc.AttachmentDesc.DepthStencilFormat);
        }

        psoDesc.SampleDesc.Count = std::max(1u, desc.AttachmentDesc.NumSamples);
        psoDesc.SampleDesc.Quality = 0;

        CheckDX12(m_Device->GetNativeDevice()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_PipelineState)),
                  "Create graphics PSO");
        m_Device->CheckDebugMessages();

#endif
    }

    DirectX12ComputePipelineState::DirectX12ComputePipelineState(DirectX12Device* inDevice,
                                                                 const RHI::RHIComputePipelineStateDesc& desc)
        : RHI::RHIComputePipelineState(desc), m_Device(inDevice)
    {
#if defined(_WIN32)
        if (!m_Device || !m_Device->GetNativeDevice())
        {
            return;
        }

        auto* computeShader = dynamic_cast<DirectX12ComputeShader*>(desc.ComputeShader);
        if (!computeShader)
        {
            return;
        }

        CreateReflectedRootSignature(m_Device->GetNativeDevice(), {{computeShader, D3D12_SHADER_VISIBILITY_ALL}}, false,
                                     m_RootSignature, m_Bindings);

        D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
        psoDesc.pRootSignature = m_RootSignature.Get();
        psoDesc.CS.pShaderBytecode = computeShader->GetByteCode().data();
        psoDesc.CS.BytecodeLength = computeShader->GetByteCode().size();

        CheckDX12(m_Device->GetNativeDevice()->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&m_PipelineState)),
                  "Create compute PSO");
        m_Device->CheckDebugMessages();
#endif
    }
} // namespace RHIDirectX12
