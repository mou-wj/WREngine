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

    void AddScreenSpaceGIPass(RenderCore::RenderGraphBuilder& GraphBuilder, const ScreenSpaceGIPassInput& Input)
    {
        if (!Input.GBufferA || !Input.GBufferB || !Input.GBufferC || !Input.SceneColor || !Input.DepthPyramid || !Input.OutputGI)
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

        auto InputCopy = Input;
        const auto outputDesc = InputCopy.OutputGI->GetDesc();
        auto* passParameter = GraphBuilder.AllocateParameter<ScreenSpaceGIParameters>();
        *passParameter = ScreenSpaceGIParameters{};

        RenderCore::RenderGraphTextureUAVDesc outputUAVDesc;
        outputUAVDesc.Format = outputDesc.Format;
        outputUAVDesc.ArraySize = outputDesc.ArraySize;
        outputUAVDesc.Texture = InputCopy.OutputGI;
        auto outputUAV = GraphBuilder.CreateTextureUAV("ScreenSpaceGIOuput", outputUAVDesc);

        passParameter->Textures.GBufferInput.GBufferA = InputCopy.GBufferA;
        passParameter->Textures.GBufferInput.GBufferB = InputCopy.GBufferB;
        passParameter->Textures.GBufferInput.GBufferC = InputCopy.GBufferC;
        passParameter->Textures.SceneColorTexture = InputCopy.SceneColor;
        passParameter->Textures.GBufferInput.Depth = InputCopy.DepthPyramid;
        passParameter->Textures.OutputGIColor = outputUAV;
        passParameter->Textures.GBufferInput.PointSampler = RenderCore::GlobalNearestSampler.get();

        GraphBuilder.AddPass<ScreenSpaceGIParameters>(
            "ScreenSpaceGIPass",
            ScreenSpaceGIParameters::GetMetaData(),
            passParameter,
            RenderCore::EPassFlag::Compute,
            [InputCopy, outputDesc, shader, pipelineState, passParameter](RHI::RHICommandListBase& RHICmdList)
            {
                RHI::RHITexUAVCreateInfo outputUAVDesc;
                outputUAVDesc.Format = outputDesc.Format;
                outputUAVDesc.ArraySize = outputDesc.ArraySize;

                auto& cmd = static_cast<RHI::RHIComputeCommandList&>(RHICmdList);
                cmd.SetComputePipelineState(pipelineState);

                ScreenSpaceGIParameters& params = *passParameter;
                params.ViewProj = InputCopy.ViewProj;
                params.InvViewProj = InputCopy.InvViewProj;
                params.CameraPos = InputCopy.CameraPos;
                params.ScreenSize = Core::Float2(static_cast<float>(outputDesc.Width), static_cast<float>(outputDesc.Height));
                params.MaxTraceSteps = std::max(1u, InputCopy.MaxTraceSteps);
                params.MaxTraceDistance = std::max(0.01f, InputCopy.MaxTraceDistance);
                params.TraceThickness = std::max(0.0001f, InputCopy.TraceThickness);
                

                SetShaderParameters(cmd, shader, &params);

                const uint32_t groupX = (outputDesc.Width + 7u) / 8u;
                const uint32_t groupY = (outputDesc.Height + 7u) / 8u;
                cmd.Dispatch(groupX, groupY, 1u);
            }
        );
    }

} // namespace Renderer
