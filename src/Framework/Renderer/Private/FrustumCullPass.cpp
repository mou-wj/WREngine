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
        const FrustumCullPassInput& Input,
        std::vector<uint32_t>& OutVisibilityFlags)
    {
        OutVisibilityFlags.clear();

        if (Input.PrimitiveCount == 0)
        {
            return true;
        }

        if (!Input.PrimitiveBoundsBuffer || !Input.VisibilityFlagsBuffer)
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
        aabbSRVDesc.NumElements = Input.PrimitiveCount;
        aabbSRVDesc.Stride = sizeof(AABBParameters);
        aabbSRVDesc.Format = RHI::ERHIFormat::Unknown;

        RHI::RHIBufferUAVCreateInfo visibilityUAVDesc;
        visibilityUAVDesc.Offset = 0;
        visibilityUAVDesc.NumElements = Input.PrimitiveCount;
        visibilityUAVDesc.Stride = sizeof(uint32_t);
        visibilityUAVDesc.Format = RHI::ERHIFormat::Unknown;

        auto* aabbSRV = Input.PrimitiveBoundsBuffer->GetViewCache().GetOrCreateSRV(
            Input.PrimitiveBoundsBuffer->GetRHI(),
            aabbSRVDesc);
        auto* visibilityUAV = Input.VisibilityFlagsBuffer->GetViewCache().GetOrCreateUAV(
            Input.VisibilityFlagsBuffer->GetRHI(),
            visibilityUAVDesc);

        if (!aabbSRV || !visibilityUAV)
        {
            return false;
        }

        RenderCore::TransitionBufferImmediate(
            RHI::GRHIApi,
            Input.PrimitiveBoundsBuffer,
            RHI::ERHIResourceAccess::SRV,
            RHI::EQueueType::Compute);

        RenderCore::TransitionBufferImmediate(
            RHI::GRHIApi,
            Input.VisibilityFlagsBuffer,
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
        params.ViewProjection = Input.ViewProjection;
        params.PrimitiveCount = Input.PrimitiveCount;
        params.AABBs = aabbSRV;
        params.VisibilityFlags = visibilityUAV;

        SetShaderParameters(cmd, cullingShader, &params);

        const uint32_t groupX = (Input.PrimitiveCount + 63u) / 64u;
        cmd.Dispatch(groupX, 1, 1);
        cmd.End();

        RHI::RHIFence computeFence;
        computeFence.QueueType = RHI::EQueueType::Compute;
        computeFence.Value = computeQueue->ExecuteContext({ computeContext }, {});
        computeQueue->WaitValue(computeFence.Value);

        Input.PrimitiveBoundsBuffer->GetTracker().UpdateAccess(RHI::ERHIResourceAccess::SRV);
        Input.PrimitiveBoundsBuffer->GetTracker().UpdateLastAccessFence(computeFence);
        Input.VisibilityFlagsBuffer->GetTracker().UpdateAccess(RHI::ERHIResourceAccess::UAV);
        Input.VisibilityFlagsBuffer->GetTracker().UpdateLastAccessFence(computeFence);

        RenderCore::TransitionBufferImmediate(
            RHI::GRHIApi,
            Input.VisibilityFlagsBuffer,
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
        readInfo.Size = Input.PrimitiveCount * sizeof(uint32_t);
        void* mapped = RHI::GRHIApi->MapReadBuffer(
            readbackCmd,
            Input.VisibilityFlagsBuffer->GetRHI(),
            readInfo);

        if (!mapped)
        {
            return false;
        }

        readbackCmd.End();
        auto readbackFenceValue = computeQueue->ExecuteContext({ readbackContext }, {});
        computeQueue->WaitValue(readbackFenceValue);

        OutVisibilityFlags.resize(Input.PrimitiveCount);
        std::memcpy(
            OutVisibilityFlags.data(),
            mapped,
            Input.PrimitiveCount * sizeof(uint32_t));

        RHI::GRHIApi->Unmap(mapped);

        Input.VisibilityFlagsBuffer->GetTracker().UpdateAccess(RHI::ERHIResourceAccess::TransferSrc);
		RHI::RHIFence readbackFence;
		readbackFence.QueueType = RHI::EQueueType::Compute;
		readbackFence.Value = readbackFenceValue;
        Input.VisibilityFlagsBuffer->GetTracker().UpdateLastAccessFence(readbackFence);

        return true;
    }

} // namespace Renderer
