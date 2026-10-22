#include "ScreenSpaceGI.h"

#include "RHIPipelineStateCache.h"
#include "RHIApi.h"

#include <algorithm>
#include <vector>

namespace Renderer {

    IMPLEMENT_GLOBAL_SHADER_TYPE(
        ScreenSpaceGICS,
        "ScreenSpaceGICS",
        "/tools/ScreenSpaceGICS.sf",
        "MainCS",
        RHI::ERHIShaderFrequency::Compute
    );

    void AddScreenSpaceGIPass(RenderCore::RenderGraphBuilder& graphBuilder, const ScreenSpaceGIPassInput& input)
    {
        if (!input.GBufferA || !input.GBufferB || !input.GBufferC || !input.SceneColor || !input.DepthPyramid || !input.OutputGI)
        {
            return;
        }

        auto* shader = RenderCore::GShaderMap.GetShader<ScreenSpaceGICS>(0);
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
        const auto outputDesc = inputCopy.OutputGI->GetDesc();
        auto* passParameter = graphBuilder.AllocateParameter<ScreenSpaceGIParameters>();
        *passParameter = ScreenSpaceGIParameters{};

        RenderCore::RenderGraphTextureUAVDesc outputUAVDesc;
        outputUAVDesc.Format = outputDesc.Format;
        outputUAVDesc.ArraySize = outputDesc.ArraySize;
        outputUAVDesc.Texture = inputCopy.OutputGI;
        auto outputUAV = graphBuilder.CreateTextureUAV("ScreenSpaceGIOuput", outputUAVDesc);

        passParameter->Textures.GBufferInput.GBufferA = inputCopy.GBufferA;
        passParameter->Textures.GBufferInput.GBufferB = inputCopy.GBufferB;
        passParameter->Textures.GBufferInput.GBufferC = inputCopy.GBufferC;
        passParameter->Textures.SceneColorTexture = inputCopy.SceneColor;
        passParameter->Textures.GBufferInput.Depth = inputCopy.DepthPyramid;
        passParameter->Textures.OutputGIColor = outputUAV;
        passParameter->Textures.GBufferInput.PointSampler = RenderCore::GlobalNearestSampler.get();

        graphBuilder.AddPass<ScreenSpaceGIParameters>(
            "ScreenSpaceGIPass",
            ScreenSpaceGIParameters::GetMetaData(),
            passParameter,
            RenderCore::EPassFlag::Compute,
            [inputCopy, outputDesc, shader, pipelineState, passParameter](RHI::RHICommandListBase& rhiCmdList)
            {
                RHI::RHITexUAVCreateInfo outputUAVDesc;
                outputUAVDesc.Format = outputDesc.Format;
                outputUAVDesc.ArraySize = outputDesc.ArraySize;

                auto& cmd = static_cast<RHI::RHIComputeCommandList&>(rhiCmdList);
                cmd.SetComputePipelineState(pipelineState);

                ScreenSpaceGIParameters& params = *passParameter;
                params.ViewProj = inputCopy.ViewProj;
                params.InvViewProj = inputCopy.InvViewProj;
                params.CameraPos = inputCopy.CameraPos;
                params.ScreenSize = Core::Float2(static_cast<float>(outputDesc.Width), static_cast<float>(outputDesc.Height));
                params.MaxTraceSteps = std::max(1u, inputCopy.MaxTraceSteps);
                params.MaxTraceDistance = std::max(0.01f, inputCopy.MaxTraceDistance);
                params.TraceThickness = std::max(0.0001f, inputCopy.TraceThickness);
                

                SetShaderParameters(cmd, shader, &params);

                const uint32_t groupX = (outputDesc.Width + 7u) / 8u;
                const uint32_t groupY = (outputDesc.Height + 7u) / 8u;
                cmd.Dispatch(groupX, groupY, 1u);
            }
        );
    }

} // namespace Renderer
