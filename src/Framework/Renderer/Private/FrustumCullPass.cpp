#include "FrustumCullPass.h"

#include "RHIPipelineStateCache.h"
#include "Shader.h"
#include "RHIApi.h"
#include <cstring>
using namespace RenderCore;
using namespace RHI;
namespace Renderer {

    IMPLEMENT_GLOBAL_SHADER_TYPE(
        FrustumCullCS,
        "FrustumCullCS",
        "/tools/FrustumCullCS.sf",
        "MainCS",
        RHI::ERHIShaderFrequency::Compute
    );

    bool ExecuteFrustumCullPass(
        const FrustumCullPassInput& input,
        std::vector<uint32_t>& outVisibilityFlags)
    {
        outVisibilityFlags.clear();

        if (input.PrimitiveCount == 0)
        {
            return true;
        }

        if (!input.PrimitiveBoundsBuffer || !input.VisibilityFlagsBuffer)
        {
            return false;
        }

        auto shaderTypeFlagIt = ShaderType::GetRegisterMap().find(ShaderType::EShaderTypeFlag::Global);
        if (shaderTypeFlagIt == ShaderType::GetRegisterMap().end())
        {
            return false;
        }

        auto shaderTypeIt = shaderTypeFlagIt->second.find("FrustumCullCS");
        if (shaderTypeIt == shaderTypeFlagIt->second.end() || !shaderTypeIt->second)
        {
            return false;
        }

        auto* cullingShader = RenderCore::GShaderMap.GetShader(shaderTypeIt->second, 0);
        if (!cullingShader)
        {
            return false;
        }

        auto* computeShader = dynamic_cast<RHI::RHIComputeShader*>(cullingShader->GetRHIShader());
        if (!computeShader)
        {
            return false;
        }

        RHI::RHIBufferSRVCreateInfo aabbSRVDesc;
        aabbSRVDesc.Offset = 0;
        aabbSRVDesc.NumElements = input.PrimitiveCount;
        aabbSRVDesc.Stride = sizeof(AABBParameters);
        aabbSRVDesc.Format = RHI::ERHIFormat::Unknown;

        RHI::RHIBufferUAVCreateInfo visibilityUAVDesc;
        visibilityUAVDesc.Offset = 0;
        visibilityUAVDesc.NumElements = input.PrimitiveCount;
        visibilityUAVDesc.Stride = sizeof(uint32_t);
        visibilityUAVDesc.Format = RHI::ERHIFormat::Unknown;

        auto* aabbSRV = input.PrimitiveBoundsBuffer->GetViewCache().GetOrCreateSRV(
            input.PrimitiveBoundsBuffer->GetRHI(),
            aabbSRVDesc);
        auto* visibilityUAV = input.VisibilityFlagsBuffer->GetViewCache().GetOrCreateUAV(
            input.VisibilityFlagsBuffer->GetRHI(),
            visibilityUAVDesc);

        if (!aabbSRV || !visibilityUAV)
        {
            return false;
        }

        RenderCore::TransitionBufferImmediate(
            RHI::GRHIApi,
            input.PrimitiveBoundsBuffer,
            RHI::ERHIResourceAccess::SRV,
            RHI::EQueueType::Compute);

        RenderCore::TransitionBufferImmediate(
            RHI::GRHIApi,
            input.VisibilityFlagsBuffer,
            RHI::ERHIResourceAccess::UAV,
            RHI::EQueueType::Compute);

        auto* computeQueue = RHI::GRHIApi->GetQueue(RHI::EQueueType::Compute);
        auto* computeContext = computeQueue->AcquireCommandContext();
        auto* computeContextCasted = dynamic_cast<RHI::RHIComputeContex*>(computeContext);
        if (!computeContextCasted)
        {
            return false;
        }

        RHI::RHIComputeCommandList cmd(computeContextCasted);
        cmd.SetImmediate(true);
        cmd.Begin();

        RHI::RHIComputePipelineStateDesc computeDesc;
        computeDesc.ComputeShader = computeShader;
        auto* pipelineState = RHI::RHIPipelineStateCache::GetOrCreateComputePipelineState(computeDesc);
        cmd.SetComputePipelineState(pipelineState);

        FrustumCullParameters params;
        params.ViewProjection = input.ViewProjection;
        params.PrimitiveCount = input.PrimitiveCount;
        params.AABBs = aabbSRV;
        params.VisibilityFlags = visibilityUAV;

        SetShaderParameters(cmd, cullingShader, &params);

        const uint32_t groupX = (input.PrimitiveCount + 63u) / 64u;
        cmd.Dispatch(groupX, 1, 1);
        cmd.End();

        RHI::RHIFence computeFence;
        computeFence.QueueType = RHI::EQueueType::Compute;
        computeFence.Value = computeQueue->ExecuteContext({ computeContext }, {});
        computeQueue->WaitValue(computeFence.Value);

        input.PrimitiveBoundsBuffer->GetTracker().UpdateAccess(RHI::ERHIResourceAccess::SRV);
        input.PrimitiveBoundsBuffer->GetTracker().UpdateLastAccessFence(computeFence);
        input.VisibilityFlagsBuffer->GetTracker().UpdateAccess(RHI::ERHIResourceAccess::UAV);
        input.VisibilityFlagsBuffer->GetTracker().UpdateLastAccessFence(computeFence);

        RenderCore::TransitionBufferImmediate(
            RHI::GRHIApi,
            input.VisibilityFlagsBuffer,
            RHI::ERHIResourceAccess::TransferSrc,
            RHI::EQueueType::Compute);

        auto* readbackContextBase = computeQueue->AcquireCommandContext();
        auto* readbackContext = dynamic_cast<RHI::RHIComputeContex*>(readbackContextBase);
        if (!readbackContext)
        {
            return false;
        }

        RHI::RHIComputeCommandList readbackCmd(readbackContext);
        readbackCmd.SetImmediate(true);
        readbackCmd.Begin();

        RHI::RHIReadBufferInfo readInfo;
        readInfo.Offset = 0;
        readInfo.Size = input.PrimitiveCount * sizeof(uint32_t);
        void* mapped = RHI::GRHIApi->MapReadBuffer(
            readbackCmd,
            input.VisibilityFlagsBuffer->GetRHI(),
            readInfo);

        if (!mapped)
        {
            return false;
        }

        readbackCmd.End();
        auto readbackFenceValue = computeQueue->ExecuteContext({ readbackContext }, {});
        computeQueue->WaitValue(readbackFenceValue);

        outVisibilityFlags.resize(input.PrimitiveCount);
        std::memcpy(
            outVisibilityFlags.data(),
            mapped,
            input.PrimitiveCount * sizeof(uint32_t));

        RHI::GRHIApi->Unmap(mapped);

        input.VisibilityFlagsBuffer->GetTracker().UpdateAccess(RHI::ERHIResourceAccess::TransferSrc);
		RHI::RHIFence readbackFence;
		readbackFence.QueueType = RHI::EQueueType::Compute;
		readbackFence.Value = readbackFenceValue;
        input.VisibilityFlagsBuffer->GetTracker().UpdateLastAccessFence(readbackFence);

        return true;
    }

} // namespace Renderer
