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

        const uint32_t maxPyramidMip = pyramidDesc.MipLevels;
        const uint32_t mipCount = maxPyramidMip;

        if (mipCount <= 1)
        {
            return;
        }

        RenderCore::RenderGraphTextureUAVRef pyramidMipUAV;

        RenderCore::RenderGraphTextureUAVDesc mipUAVDesc;
        mipUAVDesc.Format = pyramidDesc.Format;
        mipUAVDesc.ArraySize = pyramidDesc.ArraySize;
        mipUAVDesc.FirstMipSlice = 0;
        mipUAVDesc.MipCount = mipCount;
        mipUAVDesc.Texture = input.DepthPyramidTexture;
        mipUAVDesc.ViewType = RHI::ERHITextureViewType::TextureView2D;

        pyramidMipUAV = graphBuilder.CreateTextureUAV("DepthPyramidGenerateUAV", mipUAVDesc);

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

        auto inputCopy = input;
        auto* passParameter = graphBuilder.AllocateParameter<DepthPyramidGenerateParameters>();
        *passParameter = DepthPyramidGenerateParameters{};
        passParameter->InputDepthTexture = inputCopy.SceneDepthTexture;
        passParameter->InDepthMipTexture = pyramidMipUAV;
        graphBuilder.AddPass<DepthPyramidGenerateParameters>(
            "BuildDepthPyramidPass",
            DepthPyramidGenerateParameters::GetMetaData(),
            passParameter,
            RenderCore::EPassFlag::Compute,
            [inputCopy, shader, pipelineState, mipCount, sceneDepthDesc, pyramidDesc, passParameter, mipUAVs](RHI::RHICommandListBase& rhiCmdList)
            {
                auto& cmd = static_cast<RHI::RHIComputeCommandList&>(rhiCmdList);
                cmd.SetComputePipelineState(pipelineState);

                for (uint32_t mip = 0; mip < mipCount; ++mip)
                {
                    const bool bIsFirstMip = (mip == 0u);
                    const uint32_t srcWidth = bIsFirstMip
                        ? std::max(1u, sceneDepthDesc.Width)
                        : std::max(1u, pyramidDesc.Width >> (mip - 1u));
                    const uint32_t srcHeight = bIsFirstMip
                        ? std::max(1u, sceneDepthDesc.Height)
                        : std::max(1u, pyramidDesc.Height >> (mip - 1u));
                    const uint32_t dstWidth = std::max(1u, pyramidDesc.Width >> mip);
                    const uint32_t dstHeight = std::max(1u, pyramidDesc.Height >> mip);

                    DepthPyramidGenerateParameters& params = *passParameter;
                    params.InputExtent = Core::Int2(static_cast<int>(srcWidth), static_cast<int>(srcHeight));
                    params.OutputExtent = Core::Int2(static_cast<int>(dstWidth), static_cast<int>(dstHeight));
                    params.InputMipLevel = mip;
                    params.DepthSampler = RenderCore::GlobalSampler.get();
                    params.OutDepthMipTexture = mipUAVs[mip]->GetRHIShaderResourceView();
                    params.SrcDepthMipTexture = mipUAVs[mip > 0 ? mip - 1 : 0]->GetRHIShaderResourceView();

                    SetShaderParameters(cmd, shader, &params);

                    const uint32_t groupX = (dstWidth + 7u) / 8u;
                    const uint32_t groupY = (dstHeight + 7u) / 8u;
                    cmd.Dispatch(groupX, groupY, 1u);
                }
            }
        );
    }
} // namespace Renderer
