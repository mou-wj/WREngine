#include "DepthPyramidGenerate.h"

#include "RHIPipelineStateCache.h"
#include "RHIApi.h"

#include <algorithm>

namespace Renderer {

    IMPLEMENT_GLOBAL_SHADER_TYPE(
        DepthPyramidGenerateCS,
        "DepthPyramidGenerateCS",
        "/tools/DepthPyramidGenerateCS.sf",
        "MainCS",
        RHI::ERHIShaderFrequency::Compute
    );

    void AddBuildDepthPyramidPass(RenderCore::RenderGraphBuilder& GraphBuilder, const BuildDepthPyramidPassInput& Input)
    {
        if (!Input.SceneDepthTexture || !Input.DepthPyramidTexture)
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

        const auto& sceneDepthDesc = Input.SceneDepthTexture->GetDesc();
        const auto& pyramidDesc = Input.DepthPyramidTexture->GetDesc();

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
        mipUAVDesc.Texture = Input.DepthPyramidTexture;
        mipUAVDesc.ViewType = RHI::ERHITextureViewType::TextureView2D;

        pyramidMipUAV = GraphBuilder.CreateTextureUAV("DepthPyramidGenerateUAV", mipUAVDesc);

        RHI::RHIComputePipelineStateDesc computeDesc;
        computeDesc.ComputeShader = computeShader;
        auto* pipelineState = RHI::RHIPipelineStateCache::GetOrCreateComputePipelineState(computeDesc);

        auto InputCopy = Input;
        auto* passParameter = GraphBuilder.AllocateParameter<DepthPyramidGenerateParameters>();
        *passParameter = DepthPyramidGenerateParameters{};
        passParameter->InputDepthTexture = InputCopy.SceneDepthTexture;
        passParameter->InDepthMipTexture = pyramidMipUAV;
        auto pyramidViewCache = GraphBuilder.GetExternalTextureViewCache(Input.DepthPyramidTexture);
        auto pyramidTexture = Input.DepthPyramidTexture;
        GraphBuilder.AddPass<DepthPyramidGenerateParameters>(
            "BuildDepthPyramidPass",
            DepthPyramidGenerateParameters::GetMetaData(),
            passParameter,
            RenderCore::EPassFlag::Compute,
            [InputCopy, shader, pipelineState, mipCount, sceneDepthDesc, pyramidDesc,passParameter, pyramidViewCache, pyramidTexture](RHI::RHICommandListBase& RHICmdList)
            {
                auto& cmd = static_cast<RHI::RHIComputeCommandList&>(RHICmdList);
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
					RHI::RHITexUAVCreateInfo depthMipUAVDesc;
                    depthMipUAVDesc.Format = pyramidDesc.Format;
                    depthMipUAVDesc.ArraySize = pyramidDesc.ArraySize;
                    depthMipUAVDesc.FirstMipSlice = mip;
                    depthMipUAVDesc.MipCount = 1;
                    depthMipUAVDesc.ViewType = RHI::ERHITextureViewType::TextureView2D;
                    auto curOutMipUAV = pyramidViewCache->GetOrCreateUAV(pyramidTexture->GetRHITexture(), depthMipUAVDesc);
                    params.OutDepthMipTexture = curOutMipUAV;
                    if (mip > 0) {
						depthMipUAVDesc.FirstMipSlice = mip - 1;
                    }
                    auto curSrcMipUAV = pyramidViewCache->GetOrCreateUAV(pyramidTexture->GetRHITexture(), depthMipUAVDesc);
                    params.SrcDepthMipTexture = curSrcMipUAV;

                    SetShaderParameters(cmd, shader, &params);

                    const uint32_t groupX = (dstWidth + 7u) / 8u;
                    const uint32_t groupY = (dstHeight + 7u) / 8u;
                    cmd.Dispatch(groupX, groupY, 1u);
                }
            }
        );
    }
} // namespace Renderer
