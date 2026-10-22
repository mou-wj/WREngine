#include "ScreenSpaceReflection.h"

#include "RHIPipelineStateCache.h"
#include "RHIApi.h"

#include <algorithm>
#include <vector>

namespace Renderer {

    IMPLEMENT_GLOBAL_SHADER_TYPE(
        ScreenSpaceReflectionDepthPyramidCS,
        "ScreenSpaceReflectionDepthPyramidCS",
        "/tools/ScreenSpaceReflectionDepthPyramidCS.sf",
        "MainCS",
        RHI::ERHIShaderFrequency::Compute
    );

    IMPLEMENT_GLOBAL_SHADER_TYPE(
        ScreenSpaceReflectionCS,
        "ScreenSpaceReflectionCS",
        "/tools/ScreenSpaceReflectionCS.sf",
        "MainCS",
        RHI::ERHIShaderFrequency::Compute
    );

    void AddBuildSSRDepthPyramidPass(RenderCore::RenderGraphBuilder& graphBuilder, const BuildSSRDepthPyramidPassInput& input)
    {
        if (!input.SceneDepthTexture || !input.DepthPyramidTexture)
        {
            return;
        }

        auto* shader = RenderCore::GShaderMap.GetShader<ScreenSpaceReflectionDepthPyramidCS>(0);
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
        const uint32_t mipCount = (input.MipCount == 0)
            ? maxPyramidMip
            : std::min(input.MipCount, maxPyramidMip);

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

        pyramidMipUAV =  graphBuilder.CreateTextureUAV("SSRDepthPyramidUAV", mipUAVDesc);


        RHI::RHIComputePipelineStateDesc computeDesc;
        computeDesc.ComputeShader = computeShader;
        auto* pipelineState = RHI::RHIPipelineStateCache::GetOrCreateComputePipelineState(computeDesc);

        auto inputCopy = input;
        auto* passParameter = graphBuilder.AllocateParameter<ScreenSpaceReflectionDepthPyramidParameters>();
        *passParameter = ScreenSpaceReflectionDepthPyramidParameters{};
        passParameter->InputDepthTexture = inputCopy.SceneDepthTexture;
        passParameter->OutputDepthMipTexture = pyramidMipUAV;

        graphBuilder.AddPass<ScreenSpaceReflectionDepthPyramidParameters>(
            "BuildSSRDepthPyramidPass",
            ScreenSpaceReflectionDepthPyramidParameters::GetMetaData(),
            passParameter,
            RenderCore::EPassFlag::Compute,
            [inputCopy, shader, pipelineState, mipCount, sceneDepthDesc, pyramidDesc](RHI::RHICommandListBase& rhiCmdList)
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

                    ScreenSpaceReflectionDepthPyramidParameters params;
                    params.InputExtent = Core::Int2(static_cast<int>(srcWidth), static_cast<int>(srcHeight));
                    params.OutputExtent = Core::Int2(static_cast<int>(dstWidth), static_cast<int>(dstHeight));
                    params.InputMipLevel = bIsFirstMip ? 0u : (mip - 1u);
                    params.IsFirstMip = bIsFirstMip ? 1u : 0u;
                    params.InputDepthTexture = bIsFirstMip ? inputCopy.SceneDepthTexture : inputCopy.DepthPyramidTexture;
                    params.DepthSampler = RenderCore::GlobalSampler.get();

                    SetShaderParameters(cmd, shader, &params);

                    const uint32_t groupX = (dstWidth + 7u) / 8u;
                    const uint32_t groupY = (dstHeight + 7u) / 8u;
                    cmd.Dispatch(groupX, groupY, 1u);
                }
            }
        );
    }

    void AddScreenSpaceReflectionPass(RenderCore::RenderGraphBuilder& graphBuilder, const ScreenSpaceReflectionPassInput& input)
    {
        if (!input.GBufferA || !input.GBufferB || !input.GBufferC || !input.SceneColor || !input.DepthPyramid || !input.OutputReflection)
        {
            return;
        }


        auto* shader = RenderCore::GShaderMap.GetShader<ScreenSpaceReflectionCS>(0);
        if (!shader)
        {
            return;
        }

        auto* computeShader = dynamic_cast<RHI::RHIComputeShader*>(shader->GetRHIShader());
        if (!computeShader)
        {
            return;
        }

        RHI::RHIComputePipelineStateDesc computeDesc;
        computeDesc.ComputeShader = computeShader;
        auto* pipelineState = RHI::RHIPipelineStateCache::GetOrCreateComputePipelineState(computeDesc);

        auto inputCopy = input;
        const auto outputDesc = inputCopy.OutputReflection->GetDesc();
        auto* passParameter = graphBuilder.AllocateParameter<ScreenSpaceReflectionParameters>();
        *passParameter = ScreenSpaceReflectionParameters{};

		RenderCore::RenderGraphTextureUAVDesc outputUAVDesc;
        outputUAVDesc.Format = outputDesc.Format;
        outputUAVDesc.ArraySize = outputDesc.ArraySize;
        outputUAVDesc.Texture = inputCopy.OutputReflection;
        auto outputUAV = graphBuilder.CreateTextureUAV("ScreenSpaceReflectionOutput", outputUAVDesc);
        

        passParameter->Textures.GBufferA = inputCopy.GBufferA;
        passParameter->Textures.GBufferB = inputCopy.GBufferB;
        passParameter->Textures.GBufferC = inputCopy.GBufferC;
        passParameter->Textures.SceneColorTexture = inputCopy.SceneColor;
        passParameter->Textures.DepthPyramidTexture = inputCopy.DepthPyramid;
        passParameter->Textures.OutputReflectionColor = outputUAV;
        passParameter->Textures.PointSampler = RenderCore::GlobalSampler.get();

        graphBuilder.AddPass<ScreenSpaceReflectionParameters>(
            "ScreenSpaceReflectionPass",
            ScreenSpaceReflectionParameters::GetMetaData(),
            passParameter,
            RenderCore::EPassFlag::Compute,
            [inputCopy, outputDesc, shader, pipelineState](RHI::RHICommandListBase& rhiCmdList)
            {
                RHI::RHITexUAVCreateInfo outputUAVDesc;
                outputUAVDesc.Format = outputDesc.Format;
                outputUAVDesc.ArraySize = outputDesc.ArraySize;

                auto& cmd = static_cast<RHI::RHIComputeCommandList&>(rhiCmdList);
                cmd.SetComputePipelineState(pipelineState);

                ScreenSpaceReflectionParameters params;
                params.ViewProj = inputCopy.ViewProj;
                params.InvViewProj = inputCopy.InvViewProj;
                params.CameraPos = inputCopy.CameraPos;
                params.ScreenSize = Core::Float2(static_cast<float>(outputDesc.Width), static_cast<float>(outputDesc.Height));
                params.MaxTraceSteps = std::max(1u, inputCopy.MaxTraceSteps);
                params.MaxTraceDistance = std::max(0.01f, inputCopy.MaxTraceDistance);
                params.TraceThickness = std::max(0.0001f, inputCopy.TraceThickness);
                params.RoughnessThreshold = std::clamp(inputCopy.RoughnessThreshold, 0.0f, 1.0f);


                SetShaderParameters(cmd, shader, &params);

                const uint32_t groupX = (outputDesc.Width + 7u) / 8u;
                const uint32_t groupY = (outputDesc.Height + 7u) / 8u;
                cmd.Dispatch(groupX, groupY, 1u);
            }
        );
    }

} // namespace Renderer
