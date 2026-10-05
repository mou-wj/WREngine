#include "DirectX12PipelineState.h"
#include "DirectX12Device.h"
#include "DirectX12Resource.h"

#if defined(_WIN32)
#include <array>
#include <vector>
#endif

namespace
{
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

    static D3D12_FILL_MODE ToFillMode(RHI::ERHIPolygonMode mode)
    {
        switch (mode)
        {
        case RHI::ERHIPolygonMode::Line: return D3D12_FILL_MODE_WIREFRAME;
        case RHI::ERHIPolygonMode::Point:
        case RHI::ERHIPolygonMode::Fill:
        default:
            return D3D12_FILL_MODE_SOLID;
        }
    }

    static D3D12_CULL_MODE ToCullMode(RHI::ERHICullMode mode)
    {
        switch (mode)
        {
        case RHI::ERHICullMode::Front: return D3D12_CULL_MODE_FRONT;
        case RHI::ERHICullMode::Back: return D3D12_CULL_MODE_BACK;
        case RHI::ERHICullMode::FrontAndBack:
        case RHI::ERHICullMode::None:
        default:
            return D3D12_CULL_MODE_NONE;
        }
    }

    static D3D12_COMPARISON_FUNC ToComparisonFunc(RHI::ERHICompareOp op)
    {
        switch (op)
        {
        case RHI::ERHICompareOp::Never: return D3D12_COMPARISON_FUNC_NEVER;
        case RHI::ERHICompareOp::Less: return D3D12_COMPARISON_FUNC_LESS;
        case RHI::ERHICompareOp::Equal: return D3D12_COMPARISON_FUNC_EQUAL;
        case RHI::ERHICompareOp::LessOrEqual: return D3D12_COMPARISON_FUNC_LESS_EQUAL;
        case RHI::ERHICompareOp::Greater: return D3D12_COMPARISON_FUNC_GREATER;
        case RHI::ERHICompareOp::NotEqual: return D3D12_COMPARISON_FUNC_NOT_EQUAL;
        case RHI::ERHICompareOp::GreaterOrEqual: return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
        case RHI::ERHICompareOp::Always:
        default:
            return D3D12_COMPARISON_FUNC_ALWAYS;
        }
    }

    static D3D12_PRIMITIVE_TOPOLOGY_TYPE ToPrimitiveTopologyType(RHI::EPrimitiveTopology topology)
    {
        switch (topology)
        {
        case RHI::EPrimitiveTopology::PointList: return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
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
        case RHI::EPrimitiveTopology::TriangleList:
        case RHI::EPrimitiveTopology::TriangleStrip:
        case RHI::EPrimitiveTopology::TriangleFan:
        case RHI::EPrimitiveTopology::TriangleListWithAdjacency:
        case RHI::EPrimitiveTopology::TriangleStripWithAdjacency:
        default:
            return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        }
    }

    static bool CreateEmptyRootSignature(ID3D12Device* device, bool allowIA, Microsoft::WRL::ComPtr<ID3D12RootSignature>& outRootSignature)
    {
        if (!device)
        {
            return false;
        }

        D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc{};
        rootSignatureDesc.NumParameters = 0;
        rootSignatureDesc.pParameters = nullptr;
        rootSignatureDesc.NumStaticSamplers = 0;
        rootSignatureDesc.pStaticSamplers = nullptr;
        rootSignatureDesc.Flags = allowIA
            ? D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
            : D3D12_ROOT_SIGNATURE_FLAG_NONE;

        Microsoft::WRL::ComPtr<ID3DBlob> serialized;
        Microsoft::WRL::ComPtr<ID3DBlob> error;
        if (FAILED(D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &error)))
        {
            return false;
        }

        return SUCCEEDED(device->CreateRootSignature(
            0,
            serialized->GetBufferPointer(),
            serialized->GetBufferSize(),
            IID_PPV_ARGS(&outRootSignature)));
    }
#endif
}

namespace RHIDirectX12
{
    DirectX12GraphicsPipelineState::DirectX12GraphicsPipelineState(DirectX12Device* inDevice, const RHI::RHIGraphicsPipelineStateDesc& desc)
        : RHI::RHIGraphicsPipelineState(desc)
        , Device(inDevice)
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

        if (!CreateEmptyRootSignature(Device->GetNativeDevice(), true, RootSignature))
        {
            return;
        }

        std::vector<D3D12_INPUT_ELEMENT_DESC> inputElements;
        std::vector<std::string> semanticNames;
        if (desc.vertexDescState)
        {
            const auto& vertexDesc = desc.vertexDescState->GetDesc();
            inputElements.reserve(vertexDesc.attributes.size());
            semanticNames.reserve(vertexDesc.attributes.size());

            for (size_t i = 0; i < vertexDesc.attributes.size(); ++i)
            {
                const auto& attribute = vertexDesc.attributes[i];
                semanticNames.push_back("TEXCOORD" + std::to_string(i));

                D3D12_INPUT_ELEMENT_DESC element{};
                element.SemanticName = semanticNames.back().c_str();
                element.SemanticIndex = 0;
                element.Format = ToDxgiFormat(attribute.format);
                element.InputSlot = attribute.binding;
                element.AlignedByteOffset = attribute.offset;
                element.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
                element.InstanceDataStepRate = 0;
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
            psoDesc.RasterizerState.FrontCounterClockwise = rsDesc.frontFace == RHI::ERHIFrontFace::CounterClockwise ? TRUE : FALSE;
            psoDesc.RasterizerState.DepthBias = rsDesc.depthBiasEnable ? static_cast<INT>(rsDesc.depthBiasConstantFactor) : 0;
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
            psoDesc.DepthStencilState.DepthWriteMask = dsDesc.depthWriteEnable
                ? D3D12_DEPTH_WRITE_MASK_ALL
                : D3D12_DEPTH_WRITE_MASK_ZERO;
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

        auto res = Device->GetNativeDevice()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&PipelineState));
        
#endif
    }

    DirectX12ComputePipelineState::DirectX12ComputePipelineState(DirectX12Device* inDevice, const RHI::RHIComputePipelineStateDesc& desc)
        : RHI::RHIComputePipelineState(desc)
        , Device(inDevice)
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

        if (!CreateEmptyRootSignature(Device->GetNativeDevice(), false, RootSignature))
        {
            return;
        }

        D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
        psoDesc.pRootSignature = RootSignature.Get();
        psoDesc.CS.pShaderBytecode = computeShader->GetByteCode().data();
        psoDesc.CS.BytecodeLength = computeShader->GetByteCode().size();

        Device->GetNativeDevice()->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&PipelineState));
#endif
    }
}
