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
        std::vector<RHIDirectX12::DirectX12RootBinding>& bindings)
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
                if (desc.Space != 0)
                    throw std::runtime_error("RHI resource parameters cannot identify nonzero D3D12 register spaces");
                D3D12_ROOT_PARAMETER root{};
                root.ShaderVisibility = shader.second;
                if (desc.Type == D3D_SIT_CBUFFER && desc.BindCount != 1)
                    throw std::runtime_error("DirectX12 constant-buffer arrays are unsupported");
                if (desc.Type == D3D_SIT_CBUFFER)
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
    DirectX12GraphicsPipelineState::DirectX12GraphicsPipelineState(DirectX12Device* inDevice,
                                                                   const RHI::RHIGraphicsPipelineStateDesc& desc)
        : RHI::RHIGraphicsPipelineState(desc), Device(inDevice)
    {
#if defined(_WIN32)
        if (!Device || !Device->GetNativeDevice())
        {
            return;
        }

        auto* vertexShader = dynamic_cast<DirectX12VertexShader*>(desc.shaderStages.vertexShader);
        auto* pixelShader = dynamic_cast<DirectX12FragmentShader*>(desc.shaderStages.fragmentShader);
        if (!vertexShader || !pixelShader)
        {
            return;
        }

        CreateReflectedRootSignature(
            Device->GetNativeDevice(),
            {{vertexShader, D3D12_SHADER_VISIBILITY_VERTEX}, {pixelShader, D3D12_SHADER_VISIBILITY_PIXEL}}, true,
            RootSignature, Bindings);

        std::vector<D3D12_INPUT_ELEMENT_DESC> inputElements;
        std::vector<std::string> semanticNames;
        if (desc.vertexDescState)
        {
            const auto& vertexDesc = desc.vertexDescState->GetDesc();
            inputElements.reserve(vertexDesc.attributes.size());
            semanticNames.reserve(vertexDesc.attributes.size());

            D3D12_SHADER_DESC shaderDesc{};
            CheckDX12(vertexShader->GetReflection()->GetDesc(&shaderDesc), "Reflect vertex inputs");
            semanticNames.reserve(shaderDesc.InputParameters);
            for (UINT i = 0; i < shaderDesc.InputParameters; ++i)
            {
                D3D12_SIGNATURE_PARAMETER_DESC input{};
                CheckDX12(vertexShader->GetReflection()->GetInputParameterDesc(i, &input), "Reflect vertex semantic");
                if (input.SystemValueType != D3D_NAME_UNDEFINED)
                    continue;
                auto attribute = std::find_if(vertexDesc.attributes.begin(), vertexDesc.attributes.end(),
                                              [i](const auto& value) { return value.location == i; });
                if (attribute == vertexDesc.attributes.end())
                    throw std::runtime_error("Missing RHI vertex attribute for reflected input");
                semanticNames.emplace_back(input.SemanticName);
                D3D12_INPUT_ELEMENT_DESC element{};
                element.SemanticName = semanticNames.back().c_str();
                element.SemanticIndex = input.SemanticIndex;
                element.Format = ToDxgiFormat(attribute->format);
                element.InputSlot = attribute->binding;
                element.AlignedByteOffset = attribute->offset;
                element.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
                for (const auto& binding : vertexDesc.bindings)
                    if (binding.binding == attribute->binding &&
                        ToInputClassification(binding.inputRate) == D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA)
                    {
                        element.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA;
                        element.InstanceDataStepRate = 1;
                    }
                inputElements.push_back(element);
            }
        }

        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
        psoDesc.pRootSignature = RootSignature.Get();
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
        if (desc.colorBlendState)
        {
            const auto& blendDesc = desc.colorBlendState->GetDesc();
            psoDesc.BlendState.AlphaToCoverageEnable = FALSE;
            psoDesc.BlendState.IndependentBlendEnable = TRUE;
            for (size_t i = 0; i < blendDesc.attachments.size() && i < 8; ++i)
            {
                const auto& attachment = blendDesc.attachments[i];
                auto& rtDesc = psoDesc.BlendState.RenderTarget[i];
                rtDesc.BlendEnable = attachment.blendEnable ? TRUE : FALSE;
                rtDesc.LogicOpEnable = FALSE;
                rtDesc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
                rtDesc.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
                rtDesc.BlendOp = D3D12_BLEND_OP_ADD;
                rtDesc.SrcBlendAlpha = D3D12_BLEND_ONE;
                rtDesc.DestBlendAlpha = D3D12_BLEND_ZERO;
                rtDesc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
                rtDesc.LogicOp = D3D12_LOGIC_OP_NOOP;
                rtDesc.RenderTargetWriteMask = static_cast<UINT8>(attachment.colorWriteMask);
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
        if (desc.rasterizerState)
        {
            const auto& rsDesc = desc.rasterizerState->GetDesc();
            psoDesc.RasterizerState.FillMode = ToFillMode(rsDesc.polygonMode);
            psoDesc.RasterizerState.CullMode = ToCullMode(rsDesc.cullMode);
            psoDesc.RasterizerState.FrontCounterClockwise = ToFrontCounterClockwise(rsDesc.frontFace);
            psoDesc.RasterizerState.DepthBias =
                rsDesc.depthBiasEnable ? static_cast<INT>(rsDesc.depthBiasConstantFactor) : 0;
            psoDesc.RasterizerState.DepthBiasClamp = rsDesc.depthBiasClamp;
            psoDesc.RasterizerState.SlopeScaledDepthBias = rsDesc.depthBiasSlopeFactor;
            psoDesc.RasterizerState.DepthClipEnable = rsDesc.depthClampEnable ? FALSE : TRUE;
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
        if (desc.depthStencilState)
        {
            const auto& dsDesc = desc.depthStencilState->GetDesc();
            psoDesc.DepthStencilState.DepthEnable = dsDesc.depthTestEnable ? TRUE : FALSE;
            psoDesc.DepthStencilState.DepthWriteMask =
                dsDesc.depthWriteEnable ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
            psoDesc.DepthStencilState.DepthFunc = ToComparisonFunc(dsDesc.depthCompareOp);
            psoDesc.DepthStencilState.StencilEnable = dsDesc.stencilTestEnable ? TRUE : FALSE;
            psoDesc.DepthStencilState.StencilReadMask = static_cast<UINT8>(dsDesc.stencilReadMask);
            psoDesc.DepthStencilState.StencilWriteMask = static_cast<UINT8>(dsDesc.stencilWriteMask);
        }

        psoDesc.InputLayout.pInputElementDescs = inputElements.empty() ? nullptr : inputElements.data();
        psoDesc.InputLayout.NumElements = static_cast<UINT>(inputElements.size());
        psoDesc.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
        psoDesc.PrimitiveTopologyType = ToPrimitiveTopologyType(desc.primitiveTopology);

        psoDesc.NumRenderTargets = desc.attachmentDesc.colorAttachmentCount;
        for (uint32_t i = 0; i < desc.attachmentDesc.colorAttachmentCount && i < 8; ++i)
        {
            psoDesc.RTVFormats[i] = ToDxgiFormat(desc.attachmentDesc.colorAttachments[i].format);
        }
        if (desc.attachmentDesc.enableDepth || desc.attachmentDesc.enableStencil)
        {
            psoDesc.DSVFormat = ToDxgiFormat(desc.attachmentDesc.depthStencilFormat);
        }

        psoDesc.SampleDesc.Count = std::max(1u, desc.attachmentDesc.numSamples);
        psoDesc.SampleDesc.Quality = 0;

        CheckDX12(Device->GetNativeDevice()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&PipelineState)),
                  "Create graphics PSO");
        Device->CheckDebugMessages();

#endif
    }

    DirectX12ComputePipelineState::DirectX12ComputePipelineState(DirectX12Device* inDevice,
                                                                 const RHI::RHIComputePipelineStateDesc& desc)
        : RHI::RHIComputePipelineState(desc), Device(inDevice)
    {
#if defined(_WIN32)
        if (!Device || !Device->GetNativeDevice())
        {
            return;
        }

        auto* computeShader = dynamic_cast<DirectX12ComputeShader*>(desc.computeShader);
        if (!computeShader)
        {
            return;
        }

        CreateReflectedRootSignature(Device->GetNativeDevice(), {{computeShader, D3D12_SHADER_VISIBILITY_ALL}}, false,
                                     RootSignature, Bindings);

        D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
        psoDesc.pRootSignature = RootSignature.Get();
        psoDesc.CS.pShaderBytecode = computeShader->GetByteCode().data();
        psoDesc.CS.BytecodeLength = computeShader->GetByteCode().size();

        CheckDX12(Device->GetNativeDevice()->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&PipelineState)),
                  "Create compute PSO");
        Device->CheckDebugMessages();
#endif
    }
} // namespace RHIDirectX12
