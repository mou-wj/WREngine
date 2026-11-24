#include "DepthPyramidGenerate.h"

#include "RHIPipelineStateCache.h"
#include "RHIApi.h"

#include <algorithm>
#include <string>
#include <vector>

namespace Renderer {

    IMPLEMENT_GLOBAL_SHADER_TYPE(
        DepthPyramidGenerateCS,
        "DepthPyramidGenerateCS",
        "/tools/DepthPyramidGenerateCS.sf",
        "MainCS",
        RHI::ERHIShaderFrequency::Compute
    );

    void AddBuildDepthPyramidPass(RenderCore::RenderGraphBuilder& graphBuilder, const BuildDepthPyramidPassInput& input)
    {
        if (!input.SceneDepthTexture || !input.DepthPyramidTexture)
        {
            return;
        }

        auto* shader = RenderCore::GShaderMap.GetShader<DepthPyramidGenerateCS>(0);
        if (!shader)
        {
            return;
        }

        auto* computeShader = dynamic_cast<RHI::RHIComputeShader*>(shader->GetRHIShader());
        if (!computeShader)
        {
            return;
        }

        const auto& sceneDepthDesc = input.SceneDepthTexture->GetDesc();
        const auto& pyramidDesc = input.DepthPyramidTexture->GetDesc();

        const uint32_t mipCount = pyramidDesc.MipLevels;

        std::vector<RenderCore::RenderGraphTextureUAVRef> mipUAVs;
        mipUAVs.reserve(mipCount);
        for (uint32_t mip = 0; mip < mipCount; ++mip)
        {
            RenderCore::RenderGraphTextureUAVDesc mipDesc;
            mipDesc.Format = pyramidDesc.Format;
            mipDesc.ArraySize = pyramidDesc.ArraySize;
            mipDesc.FirstMipSlice = mip;
            mipDesc.MipCount = 1;
            mipDesc.Texture = input.DepthPyramidTexture;
            mipDesc.ViewType = RHI::ERHITextureViewType::TextureView2D;
            mipUAVs.push_back(graphBuilder.CreateTextureUAV(
                "DepthPyramidGenerateMip" + std::to_string(mip), mipDesc));
        }

        RHI::RHIComputePipelineStateDesc computeDesc;
        computeDesc.ComputeShader = computeShader;
        auto* pipelineState = RHI::RHIPipelineStateCache::GetOrCreateComputePipelineState(computeDesc);

        for (uint32_t mip = 0; mip < mipCount; ++mip)
        {
            const uint32_t previousMip = mip > 0 ? mip - 1 : 0;
            const auto& sourceDesc = mip == 0 ? sceneDepthDesc : pyramidDesc;
            const uint32_t dstWidth = std::max(1u, pyramidDesc.Width >> mip);
            const uint32_t dstHeight = std::max(1u, pyramidDesc.Height >> mip);

            auto* params = graphBuilder.AllocateParameter<DepthPyramidGenerateParameters>();
            params->InputExtent = {int(std::max(1u, sourceDesc.Width >> previousMip)),
                int(std::max(1u, sourceDesc.Height >> previousMip))};
            params->OutputExtent = {int(dstWidth), int(dstHeight)};
            params->InputMipLevel = mip;
            params->InputDepthTexture = input.SceneDepthTexture;
            // The shader binds two disjoint single-mip UAVs. The whole-chain intent
            // orders each dispatch and transitions the chain for the later SRV consumer.
            params->DepthMipChain = input.DepthPyramidTexture;
            const auto source = mipUAVs[previousMip];
            const auto target = mipUAVs[mip];
            graphBuilder.AddPass("BuildDepthPyramidMip" + std::to_string(mip),
                DepthPyramidGenerateParameters::GetMetaData(), params, RenderCore::EPassFlag::Compute,
                [=](RHI::RHICommandListBase& list)
                {
                    auto& cmd = static_cast<RHI::RHIComputeCommandList&>(list);
                    cmd.SetComputePipelineState(pipelineState);
                    params->SrcDepthMipTexture = source->GetRHIShaderResourceView();
                    params->OutDepthMipTexture = target->GetRHIShaderResourceView();
                    SetShaderParameters(cmd, shader, params);
                    cmd.Dispatch((dstWidth + 7) / 8, (dstHeight + 7) / 8, 1);
                });
        }
    }
} // namespace Renderer
