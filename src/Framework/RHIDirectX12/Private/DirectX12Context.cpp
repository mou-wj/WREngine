#include "DirectX12Context.h"
#include "DirectX12Device.h"
#include "DirectX12PipelineState.h"
#include "DirectX12Queue.h"
#include "DirectX12Resource.h"
#include "DirectX12RHIUtils.h"

#if defined(_WIN32)
#include <algorithm>
#endif

namespace RHIDirectX12
{

    DirectX12CommandContext::DirectX12CommandContext(DirectX12Queue* inQueue) : Queue(inQueue)
    {
    }
    DirectX12CommandContext::~DirectX12CommandContext()
    {
        // Submitted contexts are completed by their queue before destruction.
        // Abandoned recordings must also release their pool-use tokens.
        for (const auto& token : ResourceUseTokens)
            --*token;
    }

    void DirectX12CommandContext::Begin()
    {
        if (!Queue)
            throw std::runtime_error("DirectX12 context has no queue");
        if (bRecording)
            throw std::runtime_error("DirectX12 context already recording");
        if (LastSubmission)
            Queue->WaitValue(LastSubmission);
        Complete();
        LastSubmission = 0;
        RetainedResources.clear();
        UsedResources.clear();
        ResourceWaits.clear();
        ComputeBindings.clear();
        GraphicsBindings.clear();
        CurrentGraphicsPipeline = nullptr;
        CurrentComputePipeline = nullptr;
        GraphicsParameterCache.clear();
        ComputeReadOnlyStorage.clear();
        GraphicsReadOnlyStorage.clear();
        VertexBuffers.clear();
        CurrentViewport.reset();
        CurrentScissor.reset();
        ResourceDescriptorCursor = SamplerDescriptorCursor = 0;
        Reusable = false;
        auto* device = Queue->GetDevice()->GetNativeDevice();
        const auto type = Queue->GetNativeQueue()->GetDesc().Type;
        if (!CommandAllocator)
            CheckDX12(device->CreateCommandAllocator(type, IID_PPV_ARGS(&CommandAllocator)),
                      "Create command allocator");
        if (!CommandList)
            CheckDX12(device->CreateCommandList(0, type, CommandAllocator.Get(), nullptr, IID_PPV_ARGS(&CommandList)),
                      "Create command list");
        else
        {
            CheckDX12(CommandAllocator->Reset(), "Reset command allocator");
            CheckDX12(CommandList->Reset(CommandAllocator.Get(), nullptr), "Reset command list");
        }
        if (!ResourceHeap)
        {
            D3D12_DESCRIPTOR_HEAP_DESC desc{};
            desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            desc.NumDescriptors = 4096;
            desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            CheckDX12(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&ResourceHeap)),
                      "Create shader-visible resource heap");
            desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
            desc.NumDescriptors = 256;
            CheckDX12(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&SamplerHeap)),
                      "Create shader-visible sampler heap");
        }
        ID3D12DescriptorHeap* heaps[] = {ResourceHeap.Get(), SamplerHeap.Get()};
        CommandList->SetDescriptorHeaps(2, heaps);
        bRecording = true;
    }
    void DirectX12CommandContext::End()
    {
        if (!bRecording)
            return;
        CheckDX12(CommandList->Close(), "Close command list");
        bRecording = false;
        Queue->GetDevice()->CheckDebugMessages();
    }
    void DirectX12CommandContext::TrackResource(RHI::RHIViewableResource* resource)
    {
        auto* state = GetResourceState(resource);
        if (!state || !GetNativeResource(resource))
            throw std::runtime_error("Invalid DirectX12 resource");
        if (std::find(UsedResources.begin(), UsedResources.end(), state) != UsedResources.end())
            return;
        UsedResources.push_back(state);
        ++*state->ContextUses;
        ResourceUseTokens.push_back(state->ContextUses);
        if (state->LastQueue && state->LastQueue != Queue && state->LastFence)
            ResourceWaits.push_back({state->LastQueue->GetSyncPoint(), state->LastQueue->GetType(), state->LastFence,
                                     RHI::ERHIPipelineStage::AllCommands});
        Microsoft::WRL::ComPtr<ID3D12Resource> retained = GetNativeResource(resource);
        RetainedResources.push_back(std::move(retained));
    }
    void DirectX12CommandContext::RequireAccess(RHI::RHIViewableResource* resource, RHI::ERHIResourceAccess access,
                                                uint32_t subresource)
    {
        auto* state = GetResourceState(resource);
        if (!state)
            throw std::runtime_error("Invalid DirectX12 resource access");
        const auto required = ToResourceState(access, resource);
        const uint32_t first = subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES ? 0 : subresource;
        const uint32_t end = subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES
                                 ? static_cast<uint32_t>(state->States.size())
                                 : subresource + 1;
        if (state->DeclaredAccess.size() != state->States.size() || end > state->States.size())
            throw std::runtime_error("Resource access requires an explicit RHITransition");
        for (uint32_t i = first; i < end; ++i)
        {
            if (!RHI::ERHIResourceAccessFlags(state->DeclaredAccess[i]).HasAll(access))
                throw std::runtime_error("Resource binding conflicts with declared RHI access; add an RHITransition");
            if (!state->UploadHeap && (state->States[i] & required) != required)
                throw std::runtime_error("Native resource state conflicts with declared RHI access");
        }
        TrackResource(resource);
    }
    void DirectX12CommandContext::TransitionResource(RHI::RHIViewableResource* resource, D3D12_RESOURCE_STATES target,
                                                     uint32_t subresource)
    {
        TrackResource(resource);
        auto* state = GetResourceState(resource);
        if (state->UploadHeap)
            return; // UPLOAD resources must remain in GENERIC_READ.
        const uint32_t first = subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES ? 0 : subresource;
        const uint32_t end = subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES
                                 ? static_cast<uint32_t>(state->States.size())
                                 : subresource + 1;
        if (end > state->States.size())
            throw std::out_of_range("DirectX12 subresource index");
        for (uint32_t i = first; i < end; ++i)
        {
            D3D12_RESOURCE_BARRIER barrier{};
            if (state->States[i] == target)
            {
                if (target != D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
                    continue;
                barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
                barrier.UAV.pResource = GetNativeResource(resource);
            }
            else
            {
                barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                barrier.Transition = {GetNativeResource(resource), i, state->States[i], target};
                state->States[i] = target;
            }
            CommandList->ResourceBarrier(1, &barrier);
        }
    }
    void DirectX12CommandContext::BeginTransitions(std::vector<const RHI::RHITransition*> transitions)
    {
        for (auto* transition : transitions)
            if (transition)
                transition->MarkBegin();
    }
    void DirectX12CommandContext::EndTransitions(std::vector<const RHI::RHITransition*> transitions)
    {
        for (auto* transition : transitions)
        {
            if (!transition || !transition->MarkEnd())
                continue;
            for (const auto& info : transition->GetPrivateData<DirectX12TransitionData>()->Infos)
            {
                auto* resource =
                    info.Type == RHI::RHITransitionInfo::EType::UAV ? info.UAV->GetResource() : info.ViewableResource;
                if (!resource)
                    continue;
                const auto target = ToResourceState(info.AccessAfter, resource);
                auto* state = GetResourceState(resource);
                if (!state)
                    throw std::runtime_error("Unsupported transition resource");
                if (state->DeclaredAccess.empty())
                    state->DeclaredAccess.resize(state->States.size(), RHI::ERHIResourceAccess::Unknown);
                auto* texture = dynamic_cast<DirectX12Texture*>(resource);
                std::vector<uint32_t> indices;
                if (!texture)
                    indices.push_back(0);
                else
                {
                    const auto& desc = texture->GetDesc();
                    const UINT arrays = desc.Type == RHI::ERHITextureType::Texture3D ? 1 : desc.ArraySize;
                    const UINT planes = static_cast<UINT>(state->States.size()) / (arrays * desc.MipLevels);
                    if ((!info.IsAllMips() && info.MipIndex >= desc.MipLevels) ||
                        (!info.IsAllArraySlices() && info.ArraySlice >= arrays) ||
                        (!info.IsAllPlaneSlices() && info.PlaneSlice >= planes))
                        throw std::out_of_range("RHI transition subresource range");
                    for (UINT plane = 0; plane < planes; ++plane)
                        for (UINT slice = 0; slice < arrays; ++slice)
                            for (UINT mip = 0; mip < desc.MipLevels; ++mip)
                                if ((info.IsAllMips() || info.MipIndex == mip) &&
                                    (info.IsAllArraySlices() || info.ArraySlice == slice) &&
                                    (info.IsAllPlaneSlices() || info.PlaneSlice == plane))
                                    indices.push_back(mip + slice * desc.MipLevels + plane * desc.MipLevels * arrays);
                    if (info.Type == RHI::RHITransitionInfo::EType::UAV)
                    {
                        const auto& selected = static_cast<DirectX12UnorderedAccessView*>(info.UAV)->Subresources;
                        indices.erase(std::remove_if(indices.begin(), indices.end(),
                                                     [&](uint32_t i) {
                                                         return std::find(selected.begin(), selected.end(), i) ==
                                                                selected.end();
                                                     }),
                                      indices.end());
                    }
                }
                for (auto i : indices)
                {
                    if (info.AccessBefore != RHI::ERHIResourceAccess::Unknown &&
                        state->DeclaredAccess[i] != RHI::ERHIResourceAccess::Unknown &&
                        state->DeclaredAccess[i] != info.AccessBefore)
                        throw std::runtime_error("RHITransition AccessBefore does not match tracked logical access");
                    TransitionResource(resource, target, i);
                    state->DeclaredAccess[i] = info.AccessAfter;
                }
            }
        }
    }
    void DirectX12CommandContext::CopyTexture(RHI::RHITexture* src, RHI::RHITexture* dst,
                                              const RHI::RHICopyTextureDesc& desc)
    {
        auto* source = dynamic_cast<DirectX12Texture*>(src);
        auto* dest = dynamic_cast<DirectX12Texture*>(dst);
        if (!source || !dest)
            throw std::runtime_error("Invalid CopyTexture resources");
        for (UINT layer = 0; layer < desc.LayerCount; ++layer)
        {
            D3D12_TEXTURE_COPY_LOCATION from{}, to{};
            from.pResource = source->GetNativeResource();
            from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            from.SubresourceIndex = desc.SrcMipIndex + (desc.SrcArraySlice + layer) * src->GetDesc().MipLevels;
            to.pResource = dest->GetNativeResource();
            to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            to.SubresourceIndex = desc.DstMipIndex + (desc.DstArraySlice + layer) * dst->GetDesc().MipLevels;
            RequireAccess(src, RHI::ERHIResourceAccess::TransferSrc, from.SubresourceIndex);
            RequireAccess(dst, RHI::ERHIResourceAccess::TransferDest, to.SubresourceIndex);
            const auto& r = desc.SrcRegion;
            D3D12_BOX box{static_cast<UINT>(r.OffsetX),
                          static_cast<UINT>(r.OffsetY),
                          static_cast<UINT>(r.OffsetZ),
                          static_cast<UINT>(r.OffsetX + r.Width),
                          static_cast<UINT>(r.OffsetY + r.Height),
                          static_cast<UINT>(r.OffsetZ + r.Depth)};
            CommandList->CopyTextureRegion(&to, desc.DstRegion.OffsetX, desc.DstRegion.OffsetY, desc.DstRegion.OffsetZ,
                                           &from, &box);
        }
    }
    D3D12_GPU_DESCRIPTOR_HANDLE DirectX12CommandContext::CopyDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE source,
                                                                        bool sampler)
    {
        auto* device = Queue->GetDevice()->GetNativeDevice();
        auto* heap = sampler ? SamplerHeap.Get() : ResourceHeap.Get();
        auto& cursor = sampler ? SamplerDescriptorCursor : ResourceDescriptorCursor;
        if (cursor >= heap->GetDesc().NumDescriptors)
            throw std::runtime_error("DirectX12 per-command descriptor heap exhausted");
        const auto type = heap->GetDesc().Type;
        const UINT size = device->GetDescriptorHandleIncrementSize(type);
        auto cpu = heap->GetCPUDescriptorHandleForHeapStart();
        cpu.ptr += SIZE_T(cursor) * size;
        auto gpu = heap->GetGPUDescriptorHandleForHeapStart();
        gpu.ptr += UINT64(cursor++) * size;
        device->CopyDescriptorsSimple(1, cpu, source, type);
        return gpu;
    }
    void DirectX12CommandContext::BlitTexture(RHI::RHITexture* src, RHI::RHITexture* dst,
                                              const RHI::RHIBlitTextureDesc& desc)
    {
        auto* source = dynamic_cast<DirectX12Texture*>(src);
        auto* dest = dynamic_cast<DirectX12Texture*>(dst);
        if (!source || !dest || src->GetDesc().Type != RHI::ERHITextureType::Texture2D ||
            dst->GetDesc().Type != RHI::ERHITextureType::Texture2D || desc.LayerCount != 1)
            throw std::runtime_error("DirectX12 graphics blit currently requires single-layer 2D textures");
        RequireAccess(src, RHI::ERHIResourceAccess::TransferSrc,
                      desc.SrcMipIndex + desc.SrcArraySlice * src->GetDesc().MipLevels);
        RequireAccess(dst, RHI::ERHIResourceAccess::TransferDest,
                      desc.DstMipIndex + desc.DstArraySlice * dst->GetDesc().MipLevels);
        auto* device = Queue->GetDevice()->GetNativeDevice();
        if (!BlitRoot)
        {
            D3D12_DESCRIPTOR_RANGE range{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0};
            D3D12_ROOT_PARAMETER roots[2]{};
            roots[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            roots[0].DescriptorTable = {1, &range};
            roots[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
            roots[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
            roots[1].Constants = {0, 0, 6};
            roots[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
            D3D12_STATIC_SAMPLER_DESC samplers[2]{};
            for (UINT i = 0; i < 2; ++i)
            {
                samplers[i].Filter = i ? D3D12_FILTER_MIN_MAG_MIP_POINT : D3D12_FILTER_MIN_MAG_MIP_LINEAR;
                samplers[i].AddressU = samplers[i].AddressV = samplers[i].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
                samplers[i].ShaderRegister = i;
                samplers[i].MaxLOD = D3D12_FLOAT32_MAX;
                samplers[i].MaxAnisotropy = 1;
                samplers[i].ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
                samplers[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
            }
            D3D12_ROOT_SIGNATURE_DESC root{2, roots, 2, samplers, D3D12_ROOT_SIGNATURE_FLAG_NONE};
            Microsoft::WRL::ComPtr<ID3DBlob> blob, errors;
            CheckDX12(D3D12SerializeRootSignature(&root, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors),
                      "Serialize blit root");
            CheckDX12(device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                                                  IID_PPV_ARGS(&BlitRoot)),
                      "Create blit root");
        }
        auto& pipeline = BlitPipelines[dest->GetNativeFormat()];
        if (!pipeline)
        {
            const char* code = R"(
                Texture2D<float4> Source : register(t0);
                SamplerState LinearSampler : register(s0); SamplerState PointSampler : register(s1);
                cbuffer Region : register(b0) { uint2 Offset; uint2 Size; uint Mip; uint Point; };
                struct Vertex { float4 Position : SV_Position; float2 UV : TEXCOORD0; };
                Vertex VSMain(uint id : SV_VertexID) { Vertex o; o.UV = float2((id << 1) & 2, id & 2); o.Position = float4(o.UV * float2(2,-2) + float2(-1,1),0,1); return o; }
                float4 PSMain(Vertex v) : SV_Target {
                    uint w,h,levels; Source.GetDimensions(Mip,w,h,levels);
                    float2 uv = (float2(Offset) + v.UV * float2(Size)) / float2(w,h);
                    return Point ? Source.SampleLevel(PointSampler,uv,Mip) : Source.SampleLevel(LinearSampler,uv,Mip);
                }
            )";
            auto vs = CompileDX12Shader(code, L"VSMain", L"vs_6_0"), ps = CompileDX12Shader(code, L"PSMain", L"ps_6_0");
            D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
            pso.pRootSignature = BlitRoot.Get();
            pso.VS = {vs.data(), vs.size()};
            pso.PS = {ps.data(), ps.size()};
            pso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
            pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
            pso.RasterizerState.DepthClipEnable = TRUE;
            auto& blend = pso.BlendState.RenderTarget[0];
            blend.SrcBlend = blend.SrcBlendAlpha = D3D12_BLEND_ONE;
            blend.DestBlend = blend.DestBlendAlpha = D3D12_BLEND_ZERO;
            blend.BlendOp = blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
            blend.LogicOp = D3D12_LOGIC_OP_NOOP;
            blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
            pso.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
            pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            pso.NumRenderTargets = 1;
            pso.RTVFormats[0] = dest->GetNativeFormat();
            pso.SampleDesc.Count = 1;
            pso.SampleMask = UINT_MAX;
            CheckDX12(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&pipeline)), "Create blit PSO");
        }
        const auto srcState = source->States, dstState = dest->States;
        TransitionResource(src, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        TransitionResource(dst, D3D12_RESOURCE_STATE_RENDER_TARGET);
        CommandList->SetGraphicsRootSignature(BlitRoot.Get());
        CommandList->SetPipelineState(pipeline.Get());
        CommandList->SetGraphicsRootDescriptorTable(0, CopyDescriptor(source->GetDefaultSRV()));
        const UINT constants[] = {static_cast<UINT>(desc.SrcRegion.OffsetX),
                                  static_cast<UINT>(desc.SrcRegion.OffsetY),
                                  desc.SrcRegion.Width,
                                  desc.SrcRegion.Height,
                                  desc.SrcMipIndex,
                                  desc.Filter == RHI::ERHIFilter::Nearest ? 1u : 0u};
        CommandList->SetGraphicsRoot32BitConstants(1, 6, constants, 0);
        const auto rtv = dest->GetRTV(desc.DstMipIndex, desc.DstArraySlice);
        CommandList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        const auto& r = desc.DstRegion;
        D3D12_VIEWPORT viewport{float(r.OffsetX), float(r.OffsetY), float(r.Width), float(r.Height), 0, 1};
        D3D12_RECT scissor{r.OffsetX, r.OffsetY, r.OffsetX + LONG(r.Width), r.OffsetY + LONG(r.Height)};
        CommandList->RSSetViewports(1, &viewport);
        CommandList->RSSetScissorRects(1, &scissor);
        CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        CommandList->DrawInstanced(3, 1, 0, 0);
        CommandList->OMSetRenderTargets(0, nullptr, FALSE, nullptr);
        for (UINT i = 0; i < srcState.size(); ++i)
            TransitionResource(src, srcState[i], i);
        for (UINT i = 0; i < dstState.size(); ++i)
            TransitionResource(dst, dstState[i], i);
        // BlitTexture is a transfer operation in the RHI. Restore the caller's
        // pipeline and dynamic state after its graphics-based implementation.
        if (CurrentComputePipeline)
        {
            auto* pipeline = dynamic_cast<DirectX12ComputePipelineState*>(CurrentComputePipeline);
            CommandList->SetPipelineState(pipeline->GetPipelineState());
        }
        else if (CurrentGraphicsPipeline)
        {
            auto* pipeline = dynamic_cast<DirectX12GraphicsPipelineState*>(CurrentGraphicsPipeline);
            CommandList->SetGraphicsRootSignature(pipeline->GetRootSignature());
            CommandList->SetPipelineState(pipeline->GetPipelineState());
            CommandList->IASetPrimitiveTopology(ToPrimitiveTopology(pipeline->GetDesc().primitiveTopology));
            for (const auto& binding : VertexBuffers)
                CommandList->IASetVertexBuffers(binding.first, 1, &binding.second);
            if (CurrentViewport)
                CommandList->RSSetViewports(1, &*CurrentViewport);
            if (CurrentScissor)
                CommandList->RSSetScissorRects(1, &*CurrentScissor);
            const auto cached = GraphicsParameterCache;
            for (const auto& parameters : cached)
                BindParameters(parameters.first, parameters.second, GraphicsBindings, false);
        }
    }
    void DirectX12CommandContext::BindParameters(const DirectX12ShaderBase* shader,
                                                 const RHI::RHIBatchedShaderParameters& parameters,
                                                 const std::vector<DirectX12RootBinding>& bindings, bool compute)
    {
        if (!compute)
            GraphicsParameterCache[shader] = parameters;
        std::vector<DirectX12Buffer*> readOnlyStorage;
        auto* device = Queue->GetDevice()->GetNativeDevice();
        for (const auto& root : bindings)
        {
            if (root.Shader != shader)
                continue;
            const auto& binding = root.Binding;
            const auto& desc = binding.Desc;
            auto bindCBV = [&](D3D12_GPU_VIRTUAL_ADDRESS address) {
                if (compute)
                    CommandList->SetComputeRootConstantBufferView(root.RootIndex, address);
                else
                    CommandList->SetGraphicsRootConstantBufferView(root.RootIndex, address);
            };
            if (desc.Type == D3D_SIT_CBUFFER)
            {
                const auto found = std::find_if(
                    parameters.ResourceParameters.begin(), parameters.ResourceParameters.end(), [&](const auto& p) {
                        return p.Type == RHI::RHIShaderResourceParameter::EType::UniformBuffer &&
                               p.Index == desc.BindPoint;
                    });
                if (found != parameters.ResourceParameters.end())
                {
                    auto* buffer = dynamic_cast<DirectX12Buffer*>(found->Resource);
                    if (!buffer || buffer->GetDesc().Size < binding.ConstantBufferSize)
                        throw std::runtime_error("Constant buffer too small");
                    if (!EnumHasAnyFlags(buffer->GetDesc().Usage, RHI::ERHIBufferUsageFlag::Constant))
                        throw std::runtime_error("UniformBuffer binding requires Constant buffer usage");
                    RequireAccess(buffer, RHI::ERHIResourceAccess::SRV);
                    bindCBV(buffer->GetNativeResource()->GetGPUVirtualAddress());
                }
                else
                {
                    const UINT size = (binding.ConstantBufferSize + 255) & ~255u;
                    auto upload = std::make_shared<DirectX12StagingBuffer>(Queue->GetDevice(), size);
                    auto* data = static_cast<uint8_t*>(upload->Map(0, size));
                    std::memset(data, 0, size);
                    for (const auto& uniform : parameters.UniformParameters)
                        if (uniform.BufferIndex == desc.BindPoint)
                        {
                            if (uniform.BaseIndex + uniform.Size > binding.ConstantBufferSize ||
                                uniform.Offset + uniform.Size > parameters.Data.size())
                                throw std::runtime_error("Uniform parameter out of range");
                            std::memcpy(data + uniform.BaseIndex, parameters.Data.data() + uniform.Offset,
                                        uniform.Size);
                        }
                    bindCBV(upload->GetNativeResource()->GetGPUVirtualAddress());
                    RetainResource(upload->GetNativeResource());
                }
                continue;
            }
            D3D12_GPU_DESCRIPTOR_HANDLE table{};
            for (UINT element = 0; element < desc.BindCount; ++element)
            {
                const RHI::RHIShaderResourceParameter* found = nullptr;
                for (const auto& p : parameters.ResourceParameters)
                {
                    if (p.Index != desc.BindPoint || p.ArrayIndex != element)
                        continue;
                    using T = RHI::RHIShaderResourceParameter::EType;
                    const bool match = desc.Type == D3D_SIT_SAMPLER ? p.Type == T::Sampler
                                       : IsUAV(desc.Type)           ? p.Type == T::UAV
                                                                    : (p.Type == T::Texture || p.Type == T::SRV);
                    if (match)
                    {
                        found = &p;
                        break;
                    }
                    if (!found && binding.ReadOnlyBufferUAV && p.Type == T::SRV)
                        if (auto* view = dynamic_cast<DirectX12ShaderResourceView*>(p.Resource))
                            if (dynamic_cast<DirectX12Buffer*>(view->GetResource()))
                                found = &p;
                }
                if (!found || !found->Resource)
                {
                    const bool srvForUav = IsUAV(desc.Type) &&
                                           std::any_of(parameters.ResourceParameters.begin(),
                                                       parameters.ResourceParameters.end(), [&](const auto& p) {
                                                           return p.Index == desc.BindPoint &&
                                                                  p.ArrayIndex == element &&
                                                                  p.Type == RHI::RHIShaderResourceParameter::EType::SRV;
                                                       });
                    if (srvForUav)
                        throw std::runtime_error("Shader resource '" + binding.Name +
                                                 "' requires UAV: DXIL writes or cannot prove read-only buffer access");
                    throw std::runtime_error("Missing shader resource: " + binding.Name);
                }
                D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
                using ParameterType = RHI::RHIShaderResourceParameter::EType;
                if ((found->Type == ParameterType::Texture && !dynamic_cast<DirectX12Texture*>(found->Resource)) ||
                    (found->Type == ParameterType::SRV &&
                     !dynamic_cast<DirectX12ShaderResourceView*>(found->Resource)) ||
                    (found->Type == ParameterType::UAV &&
                     !dynamic_cast<DirectX12UnorderedAccessView*>(found->Resource)))
                    throw std::runtime_error("Shader parameter type does not match its resource object: " +
                                             binding.Name);
                if (desc.Type == D3D_SIT_SAMPLER)
                {
                    auto* sampler = dynamic_cast<DirectX12Sampler*>(found->Resource);
                    if (!sampler)
                        throw std::runtime_error("Invalid DirectX12 sampler");
                    cpu = sampler->GetCpuHandle();
                }
                else if (auto* texture = dynamic_cast<DirectX12Texture*>(found->Resource))
                {
                    RequireAccess(texture, RHI::ERHIResourceAccess::SRV);
                    cpu = texture->GetDefaultSRV();
                }
                else if (auto* view = dynamic_cast<DirectX12UnorderedAccessView*>(found->Resource))
                {
                    if (!view->IsValid())
                        throw std::runtime_error("Invalid DirectX12 UAV");
                    if (view->Subresources.empty())
                        RequireAccess(view->GetResource(), RHI::ERHIResourceAccess::UAV);
                    else
                        for (auto i : view->Subresources)
                            RequireAccess(view->GetResource(), RHI::ERHIResourceAccess::UAV, i);
                    cpu = view->GetCpuHandle();
                }
                else if (auto* view = dynamic_cast<DirectX12ShaderResourceView*>(found->Resource))
                {
                    if (!view->IsValid())
                        throw std::runtime_error("Invalid DirectX12 SRV");
                    if (view->Subresources.empty())
                        RequireAccess(view->GetResource(), RHI::ERHIResourceAccess::SRV);
                    else
                        for (auto i : view->Subresources)
                            RequireAccess(view->GetResource(), RHI::ERHIResourceAccess::SRV, i);
                    if (IsUAV(desc.Type))
                    {
                        if (!binding.ReadOnlyBufferUAV)
                            throw std::runtime_error("Writable DXIL resource cannot use an RHI SRV");
                        cpu = view->GetReadOnlyStorageUAV(desc.Type);
                        readOnlyStorage.push_back(static_cast<DirectX12Buffer*>(view->GetResource()));
                    }
                    else
                        cpu = view->GetCpuHandle();
                }
                else
                    throw std::runtime_error("Unsupported DirectX12 shader resource");
                auto gpu = CopyDescriptor(cpu, desc.Type == D3D_SIT_SAMPLER);
                if (!element)
                    table = gpu;
            }
            if (compute)
                CommandList->SetComputeRootDescriptorTable(root.RootIndex, table);
            else
                CommandList->SetGraphicsRootDescriptorTable(root.RootIndex, table);
        }
        (compute ? ComputeReadOnlyStorage : GraphicsReadOnlyStorage)[shader] = std::move(readOnlyStorage);
    }
    std::vector<DirectX12CommandContext::NativeStorageState> DirectX12CommandContext::BeginReadOnlyStorageAccess(
        bool compute)
    {
        std::vector<NativeStorageState> result;
        for (const auto& entry : compute ? ComputeReadOnlyStorage : GraphicsReadOnlyStorage)
            for (auto* buffer : entry.second)
                if (std::none_of(result.begin(), result.end(),
                                 [buffer](const auto& state) { return state.Buffer == buffer; }))
                {
                    RequireAccess(buffer, RHI::ERHIResourceAccess::SRV);
                    result.push_back({buffer, buffer->States[0]});
                }
        for (const auto& state : result)
            TransitionResource(state.Buffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        return result;
    }
    void DirectX12CommandContext::EndReadOnlyStorageAccess(const std::vector<NativeStorageState>& states)
    {
        for (const auto& state : states)
            TransitionResource(state.Buffer, state.State);
    }
    void DirectX12CommandContext::Complete()
    {
        for (auto& callback : CompletionCallbacks)
            callback();
        CompletionCallbacks.clear();
        for (const auto& readback : DiagnosticReadbacks)
        {
            float* data = nullptr;
            D3D12_RANGE range{0, 16};
            CheckDX12(readback.Resource->Map(0, &range, reinterpret_cast<void**>(&data)), "Map diagnostic readback");
            LOG_INFO("DirectX12 UAV readback %s: %.6f %.6f %.6f %.6f", readback.Name.c_str(), data[0], data[1], data[2],
                     data[3]);
            D3D12_RANGE written{0, 0};
            readback.Resource->Unmap(0, &written);
        }
        DiagnosticReadbacks.clear();
        RetainedResources.clear();
        for (const auto& token : ResourceUseTokens)
            --*token;
        ResourceUseTokens.clear();
        UsedResources.clear();
    }

    DirectX12ComputeContext::DirectX12ComputeContext(DirectX12Queue* inQueue) : DirectX12CommandContext(inQueue)
    {
    }

    void DirectX12ComputeContext::SetComputePipelineState(RHI::RHIComputePipelineState* pipelineState)
    {
#if defined(_WIN32)
        auto* dxPipeline = dynamic_cast<DirectX12ComputePipelineState*>(pipelineState);
        if (!dxPipeline || !CommandList)
        {
            return;
        }

        CurrentComputePipeline = pipelineState;
        ComputeBindings = dxPipeline->GetBindings();
        ComputeReadOnlyStorage.clear();
        CommandList->SetComputeRootSignature(dxPipeline->GetRootSignature());
        CommandList->SetPipelineState(dxPipeline->GetPipelineState());
#else
        (void)pipelineState;
#endif
    }

    void DirectX12ComputeContext::SetBatchedShaderParameters(RHI::RHIComputeShader* shader,
                                                             const RHI::RHIBatchedShaderParameters& parameter)
    {
        auto* dxShader = dynamic_cast<DirectX12ShaderBase*>(shader);
        if (!dxShader)
            throw std::runtime_error("Invalid compute shader");
        BindParameters(dxShader, parameter, ComputeBindings, true);
    }

    void DirectX12ComputeContext::Dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
    {
#if defined(_WIN32)
        if (!CommandList)
        {
            return;
        }

        const auto storageStates = BeginReadOnlyStorageAccess(true);
        CommandList->Dispatch(groupCountX, groupCountY, groupCountZ);
        EndReadOnlyStorageAccess(storageStates);
        static uint32_t diagnosticDispatches = 0;
        if (DX12Diagnostics() && diagnosticDispatches++ < 4)
        {
            for (auto* state : UsedResources)
            {
                auto* buffer = dynamic_cast<DirectX12Buffer*>(state);
                if (!buffer || !EnumHasAnyFlags(buffer->GetDesc().Usage, RHI::ERHIBufferUsageFlag::UnorderedAccess) ||
                    buffer->GetDesc().Size < 16)
                    continue;
                const auto oldState = buffer->States[0];
                auto readback =
                    std::make_shared<DirectX12StagingBuffer>(Queue->GetDevice(), 16, D3D12_HEAP_TYPE_READBACK);
                TransitionResource(buffer, D3D12_RESOURCE_STATE_COPY_SOURCE);
                CommandList->CopyBufferRegion(readback->GetNativeResource(), 0, buffer->GetNativeResource(), 0, 16);
                TransitionResource(buffer, oldState);
                DiagnosticReadbacks.push_back({readback->GetNativeResource(), "buffer"});
                RetainResource(readback->GetNativeResource());
            }
        }
#else
        (void)groupCountX;
        (void)groupCountY;
        (void)groupCountZ;
#endif
    }

    DirectX12GraphicContext::DirectX12GraphicContext(DirectX12Queue* inQueue) : DirectX12CommandContext(inQueue)
    {
    }

    void DirectX12GraphicContext::SetBatchedShaderParameters(RHI::RHIGraphicShader* shader,
                                                             const RHI::RHIBatchedShaderParameters& parameter)
    {
        auto* dxShader = dynamic_cast<DirectX12ShaderBase*>(shader);
        if (!dxShader)
            throw std::runtime_error("Invalid graphics shader");
        BindParameters(dxShader, parameter, GraphicsBindings, false);
    }

    void DirectX12GraphicContext::SetStreamSource(uint32_t streamIndex, RHI::RHIBuffer* vertexBuffer, uint32_t offset)
    {
#if defined(_WIN32)
        auto* dxBuffer = dynamic_cast<DirectX12Buffer*>(vertexBuffer);
        if (!dxBuffer || !dxBuffer->GetNativeResource() || !CommandList)
        {
            return;
        }

        const auto& bufferDesc = dxBuffer->GetDesc();
        uint32_t stride = bufferDesc.Stride;
        if (CurrentGraphicsPipeline && CurrentGraphicsPipeline->GetDesc().vertexDescState)
            for (const auto& binding : CurrentGraphicsPipeline->GetDesc().vertexDescState->GetDesc().bindings)
                if (binding.binding == streamIndex)
                    stride = binding.stride;
        RequireAccess(vertexBuffer, RHI::ERHIResourceAccess::VertexOrIndexBuffer);
        if (stride == 0 || offset >= bufferDesc.Size)
        {
            return;
        }

        D3D12_VERTEX_BUFFER_VIEW vbView{};
        vbView.BufferLocation = dxBuffer->GetNativeResource()->GetGPUVirtualAddress() + offset;
        vbView.SizeInBytes = static_cast<UINT>(bufferDesc.Size - offset);
        vbView.StrideInBytes = stride;
        VertexBuffers[streamIndex] = vbView;
        CommandList->IASetVertexBuffers(streamIndex, 1, &vbView);
#else
        (void)streamIndex;
        (void)vertexBuffer;
        (void)offset;
#endif
    }

    void DirectX12GraphicContext::SetGraphicPipelineState(RHI::RHIGraphicsPipelineState* pipelineState)
    {
#if defined(_WIN32)
        auto* dxPipeline = dynamic_cast<DirectX12GraphicsPipelineState*>(pipelineState);
        if (!dxPipeline || !CommandList)
        {
            return;
        }

        CurrentGraphicsPipeline = pipelineState;
        GraphicsBindings = dxPipeline->GetBindings();
        GraphicsReadOnlyStorage.clear();
        CommandList->SetGraphicsRootSignature(dxPipeline->GetRootSignature());
        CommandList->SetPipelineState(dxPipeline->GetPipelineState());
        CommandList->IASetPrimitiveTopology(ToPrimitiveTopology(dxPipeline->GetDesc().primitiveTopology));
#else
        (void)pipelineState;
#endif
    }

    void DirectX12GraphicContext::SetViewport(float x, float y, float w, float h, float minDepth, float maxDepth)
    {
#if defined(_WIN32)
        if (!CommandList)
        {
            return;
        }

        D3D12_VIEWPORT viewport{};
        viewport.TopLeftX = x;
        viewport.TopLeftY = y;
        viewport.Width = w;
        viewport.Height = h;
        viewport.MinDepth = minDepth;
        viewport.MaxDepth = maxDepth;
        CurrentViewport = viewport;
        CommandList->RSSetViewports(1, &viewport);
#else
        (void)x;
        (void)y;
        (void)w;
        (void)h;
        (void)minDepth;
        (void)maxDepth;
#endif
    }

    void DirectX12GraphicContext::SetScissor(int32_t x, int32_t y, uint32_t w, uint32_t h)
    {
#if defined(_WIN32)
        if (!CommandList)
        {
            return;
        }

        D3D12_RECT rect{};
        rect.left = x;
        rect.top = y;
        rect.right = x + static_cast<LONG>(w);
        rect.bottom = y + static_cast<LONG>(h);
        CurrentScissor = rect;
        CommandList->RSSetScissorRects(1, &rect);
#else
        (void)x;
        (void)y;
        (void)w;
        (void)h;
#endif
    }

    void DirectX12GraphicContext::Draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex,
                                       uint32_t firstInstance)
    {
#if defined(_WIN32)
        if (!CommandList)
        {
            return;
        }

        const auto storageStates = BeginReadOnlyStorageAccess(false);
        CommandList->DrawInstanced(vertexCount, instanceCount, firstVertex, firstInstance);
        EndReadOnlyStorageAccess(storageStates);
#else
        (void)vertexCount;
        (void)instanceCount;
        (void)firstVertex;
        (void)firstInstance;
#endif
    }

    void DirectX12GraphicContext::DrawIndexed(RHI::RHIBuffer* indexBuffer, uint32_t indexCount, uint32_t instanceCount,
                                              uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance)
    {
#if defined(_WIN32)
        auto* dxIndexBuffer = dynamic_cast<DirectX12Buffer*>(indexBuffer);
        if (!dxIndexBuffer || !dxIndexBuffer->GetNativeResource() || !CommandList)
        {
            return;
        }

        const auto& desc = dxIndexBuffer->GetDesc();
        RequireAccess(indexBuffer, RHI::ERHIResourceAccess::VertexOrIndexBuffer);
        D3D12_INDEX_BUFFER_VIEW ibView{};
        ibView.BufferLocation = dxIndexBuffer->GetNativeResource()->GetGPUVirtualAddress();
        ibView.SizeInBytes = static_cast<UINT>(desc.Size);
        ibView.Format = desc.Stride == 2 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
        CommandList->IASetIndexBuffer(&ibView);
        const auto storageStates = BeginReadOnlyStorageAccess(false);
        CommandList->DrawIndexedInstanced(indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
        EndReadOnlyStorageAccess(storageStates);
#else
        (void)indexBuffer;
        (void)indexCount;
        (void)instanceCount;
        (void)firstIndex;
        (void)vertexOffset;
        (void)firstInstance;
#endif
    }

    void DirectX12GraphicContext::BeginRenderPass(const RHI::RHIRenderPassInfo& renderPassInfo)
    {
#if defined(_WIN32)
        if (!CommandList || !Queue || !Queue->GetDevice())
        {
            return;
        }

        const auto& targets = renderPassInfo.RenderTargets;

        std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> rtvHandles;
        rtvHandles.reserve(targets.NumColorAttachments);

        for (uint8_t i = 0; i < targets.NumColorAttachments; ++i)
        {
            const auto& colorAttachment = targets.ColorAttachments[i];
            auto* texture = dynamic_cast<DirectX12Texture*>(colorAttachment.Texture);
            if (!texture || !texture->GetNativeResource())
            {
                continue;
            }

            RequireAccess(texture, RHI::ERHIResourceAccess::RenderTargetView,
                          colorAttachment.MipIndex + colorAttachment.ArraySlice * texture->GetDesc().MipLevels);
            auto rtvHandle = texture->GetRTV(colorAttachment.MipIndex, colorAttachment.ArraySlice);

            const auto loadOp =
                static_cast<RHI::ERenderTargetLoadOp>(static_cast<uint8_t>(colorAttachment.Actions) >>
                                                      static_cast<uint8_t>(RHI::ERenderTargetActions::LoadOpShift));
            if (loadOp == RHI::ERenderTargetLoadOp::Clear)
            {
                const float* color = colorAttachment.ClearBinding.Color;
                CommandList->ClearRenderTargetView(rtvHandle, color, 0, nullptr);
            }

            rtvHandles.push_back(rtvHandle);
        }

        D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle{};
        bool hasDepthStencil = false;
        if (targets.DepthStencil.Texture)
        {
            auto* depthTexture = dynamic_cast<DirectX12Texture*>(targets.DepthStencil.Texture);
            if (depthTexture && depthTexture->GetNativeResource())
            {
                {
                    RequireAccess(depthTexture, RHI::ERHIResourceAccess::DSVWrite,
                                  targets.DepthStencil.MipIndex +
                                      targets.DepthStencil.ArraySlice * depthTexture->GetDesc().MipLevels);
                    dsvHandle = depthTexture->GetDSV(targets.DepthStencil.MipIndex, targets.DepthStencil.ArraySlice);
                    hasDepthStencil = true;

                    const auto depthLoadOp = static_cast<RHI::ERenderTargetLoadOp>(
                        static_cast<uint8_t>(targets.DepthStencil.Actions) >>
                        static_cast<uint8_t>(RHI::ERenderTargetActions::LoadOpShift));
                    if (depthLoadOp == RHI::ERenderTargetLoadOp::Clear)
                    {
                        CommandList->ClearDepthStencilView(
                            dsvHandle,
                            depthTexture->GetDesc().Format == RHI::ERHIFormat::D24_UNorm_S8_UInt
                                ? D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL
                                : D3D12_CLEAR_FLAG_DEPTH,
                            targets.DepthStencil.ClearBinding.Depth,
                            static_cast<UINT8>(targets.DepthStencil.ClearBinding.Stencil), 0, nullptr);
                    }
                }
            }
        }

        if (renderPassInfo.RenderArea.Width > 0 && renderPassInfo.RenderArea.Height > 0)
        {
            D3D12_VIEWPORT viewport{};
            viewport.TopLeftX = static_cast<float>(renderPassInfo.RenderArea.X);
            viewport.TopLeftY = static_cast<float>(renderPassInfo.RenderArea.Y);
            viewport.Width = static_cast<float>(renderPassInfo.RenderArea.Width);
            viewport.Height = static_cast<float>(renderPassInfo.RenderArea.Height);
            viewport.MinDepth = 0.0f;
            viewport.MaxDepth = 1.0f;
            CurrentViewport = viewport;
            CommandList->RSSetViewports(1, &viewport);

            D3D12_RECT scissor{};
            scissor.left = renderPassInfo.RenderArea.X;
            scissor.top = renderPassInfo.RenderArea.Y;
            scissor.right = renderPassInfo.RenderArea.X + static_cast<LONG>(renderPassInfo.RenderArea.Width);
            scissor.bottom = renderPassInfo.RenderArea.Y + static_cast<LONG>(renderPassInfo.RenderArea.Height);
            CurrentScissor = scissor;
            CommandList->RSSetScissorRects(1, &scissor);
        }

        CommandList->OMSetRenderTargets(static_cast<UINT>(rtvHandles.size()),
                                        rtvHandles.empty() ? nullptr : rtvHandles.data(), FALSE,
                                        hasDepthStencil ? &dsvHandle : nullptr);
#else
        (void)renderPassInfo;
#endif
    }

    void DirectX12GraphicContext::EndRenderPass()
    {
#if defined(_WIN32)
        if (!CommandList)
        {
            return;
        }

        CommandList->OMSetRenderTargets(0, nullptr, FALSE, nullptr);
#endif
    }

    void DirectX12GraphicContext::SetBatchedShaderParameters(RHI::RHIRayTracingShader* shader,
                                                             const RHI::RHIBatchedShaderParameters& parameter)
    {
        (void)shader;
        (void)parameter;
    }

    void DirectX12GraphicContext::SetRayTracingPipelineState(RHI::RHIRayTracingPipelineState* pipelineState)
    {
        (void)pipelineState;
    }

    void DirectX12GraphicContext::BuildAccelerationStructure(
        RHI::RHIRayTracingAccelerationStructure* accelerationStructure)
    {
        (void)accelerationStructure;
    }

    void DirectX12GraphicContext::UpdateAccelerationStructure(
        RHI::RHIRayTracingAccelerationStructure* accelerationStructure)
    {
        (void)accelerationStructure;
    }

    void DirectX12GraphicContext::TraceRays(uint32_t width, uint32_t height, uint32_t depth)
    {
        (void)width;
        (void)height;
        (void)depth;
    }
} // namespace RHIDirectX12
