#include "HZBOcclusionTest.h"

#include "DepthPyramidGenerate.h"
#include "RHIPipelineStateCache.h"
#include "Shader.h"
#include "RHIApi.h"

#include <algorithm>
#include <cstring>
#include <limits>

using namespace RenderCore;
using namespace RHI;

namespace Renderer {

    IMPLEMENT_GLOBAL_SHADER_TYPE(
        HZBOcclusionTestCS,
        "HZBOcclusionTestCS",
        "/tools/HZBOcclusionTestCS.sf",
        "MainCS",
        RHI::ERHIShaderFrequency::Compute
    );

    static inline bool IsInsideClipSpace(const Core::Float4& clip)
    {
        return clip.W > 0.0f &&
            clip.X >= -clip.W && clip.X <= clip.W &&
            clip.Y >= -clip.W && clip.Y <= clip.W &&
            clip.Z >= -clip.W && clip.Z <= clip.W;
    }

    static inline Core::Float4 TransformPoint(const Core::Float4x4& matrix, const Core::Float3& point)
    {
        const float x = matrix(0, 0) * point.X + matrix(0, 1) * point.Y + matrix(0, 2) * point.Z + matrix(0, 3);
        const float y = matrix(1, 0) * point.X + matrix(1, 1) * point.Y + matrix(1, 2) * point.Z + matrix(1, 3);
        const float z = matrix(2, 0) * point.X + matrix(2, 1) * point.Y + matrix(2, 2) * point.Z + matrix(2, 3);
        const float w = matrix(3, 0) * point.X + matrix(3, 1) * point.Y + matrix(3, 2) * point.Z + matrix(3, 3);
        return Core::Float4(x, y, z, w);
    }

    static inline void AddAABBPoint(const Core::Float3& p, float& minX, float& minY, float& minZ, float& maxX, float& maxY, float& maxZ)
    {
        minX = std::min(minX, p.X); maxX = std::max(maxX, p.X);
        minY = std::min(minY, p.Y); maxY = std::max(maxY, p.Y);
        minZ = std::min(minZ, p.Z); maxZ = std::max(maxZ, p.Z);
    }

    HZBProjectedBox ProjectAABBToScreenSpace(const Core::Float4x4& viewProjection, const Core::Float2& screenSize, const HZBInstanceAABB& instance)
    {
        HZBProjectedBox projected;
        projected.bIsValid = false;

        const Core::Float3 corners[8] = {
            Core::Float3(instance.Min.X, instance.Min.Y, instance.Min.Z),
            Core::Float3(instance.Max.X, instance.Min.Y, instance.Min.Z),
            Core::Float3(instance.Min.X, instance.Max.Y, instance.Min.Z),
            Core::Float3(instance.Max.X, instance.Max.Y, instance.Min.Z),
            Core::Float3(instance.Min.X, instance.Min.Y, instance.Max.Z),
            Core::Float3(instance.Max.X, instance.Min.Y, instance.Max.Z),
            Core::Float3(instance.Min.X, instance.Max.Y, instance.Max.Z),
            Core::Float3(instance.Max.X, instance.Max.Y, instance.Max.Z),
        };

        float minX = std::numeric_limits<float>::max();
        float minY = std::numeric_limits<float>::max();
        float maxX = -std::numeric_limits<float>::max();
        float maxY = -std::numeric_limits<float>::max();
        float minZ = std::numeric_limits<float>::max();
        float maxZ = -std::numeric_limits<float>::max();

        bool anyInside = false;

        for (int i = 0; i < 8; ++i)
        {
            Core::Float4 p = TransformPoint(instance.Transform, corners[i]);
            Core::Float4 clip = TransformPoint(viewProjection, Core::Float3(p.X, p.Y, p.Z));

            if (clip.W <= 0.0f)
            {
                continue;
            }

            const float ndcX = clip.X / clip.W;
            const float ndcY = clip.Y / clip.W;

            if (ndcX < -1.0f || ndcX > 1.0f || ndcY < -1.0f || ndcY > 1.0f)
            {
                continue;
            }

            anyInside = true;
            const float screenX = (ndcX * 0.5f + 0.5f) * screenSize.X;
            const float screenY = (ndcY * 0.5f + 0.5f) * screenSize.Y;

            minX = std::min(minX, screenX);
            maxX = std::max(maxX, screenX);
            minY = std::min(minY, screenY);
            maxY = std::max(maxY, screenY);
            minZ = std::min(minZ, clip.Z / clip.W);
            maxZ = std::max(maxZ, clip.Z / clip.W);
        }

        if (!anyInside)
        {
            return projected;
        }

        projected.MinX = minX;
        projected.MinY = minY;
        projected.MaxX = maxX;
        projected.MaxY = maxY;
        projected.bIsValid = true;
        return projected;
    }

    bool ExecuteHZBOcclusionTestPass(
        const HZBOcclusionTestInput& input,
        std::vector<uint32_t>& outVisibilityFlags)
    {
        outVisibilityFlags.clear();

        if (input.PrimitiveCount == 0 || !input.InstanceData || !input.VisibilityFlagsBuffer)
        {
            return true;
        }

        auto shaderTypeFlagIt = ShaderType::GetRegisterMap().find(ShaderType::EShaderTypeFlag::Global);
        if (shaderTypeFlagIt == ShaderType::GetRegisterMap().end())
        {
            return false;
        }

        auto shaderTypeIt = shaderTypeFlagIt->second.find("HZBOcclusionTestCS");
        if (shaderTypeIt == shaderTypeFlagIt->second.end() || !shaderTypeIt->second)
        {
            return false;
        }

        auto* hzbShader = RenderCore::GShaderMap.GetShader(shaderTypeIt->second, 0);
        if (!hzbShader)
        {
            return false;
        }

        auto* computeShader = dynamic_cast<RHI::RHIComputeShader*>(hzbShader->GetRHIShader());
        if (!computeShader)
        {
            return false;
        }

        RHIBufferSRVCreateInfo instanceSRVDesc;
        instanceSRVDesc.Offset = 0;
        instanceSRVDesc.NumElements = input.PrimitiveCount;
        instanceSRVDesc.Stride = sizeof(HZBInstanceParameters);
        instanceSRVDesc.Format = RHI::ERHIFormat::Unknown;

        RHIBufferUAVCreateInfo visibilityUAVDesc;
        visibilityUAVDesc.Offset = 0;
        visibilityUAVDesc.NumElements = input.PrimitiveCount;
        visibilityUAVDesc.Stride = sizeof(uint32_t);
        visibilityUAVDesc.Format = RHI::ERHIFormat::Unknown;

        RenderCore::RenderBuffer* instanceBoundsBuffer = input.InstanceBoundsBuffer;
        if (!instanceBoundsBuffer)
        {
            return false;
        }

        auto* instanceSRV = instanceBoundsBuffer->GetViewCache().GetOrCreateSRV(
            instanceBoundsBuffer->GetRHI(),
            instanceSRVDesc);
        auto* visibilityUAV = input.VisibilityFlagsBuffer->GetViewCache().GetOrCreateUAV(
            input.VisibilityFlagsBuffer->GetRHI(),
            visibilityUAVDesc);

        if (!instanceSRV || !visibilityUAV)
        {
            return false;
        }

        RenderCore::TransitionBufferImmediate(RHI::GRHIApi, instanceBoundsBuffer, RHI::ERHIResourceAccess::SRV, RHI::EQueueType::Compute);
        RenderCore::TransitionBufferImmediate(RHI::GRHIApi, input.VisibilityFlagsBuffer, RHI::ERHIResourceAccess::UAV, RHI::EQueueType::Compute);

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

        HZBOcclusionTestParameters params;
        params.ViewProjection = input.ViewProjection;
        params.ScreenSize = input.ScreenSize;
        params.PrimitiveCount = input.PrimitiveCount;
        params.DepthPyramidTexture = input.DepthPyramidTexture;
        params.Instances = instanceSRV;
        params.VisibilityFlags = visibilityUAV;

        SetShaderParameters(cmd, hzbShader, &params);

        const uint32_t groupX = (input.PrimitiveCount + 63u) / 64u;
        cmd.Dispatch(groupX, 1, 1);
        cmd.End();
		RHI::RHIFence computeFence;
		computeFence.QueueType = RHI::EQueueType::Compute;
        computeFence.Value = computeQueue->ExecuteContext({ computeContext }, {});
        computeQueue->WaitValue(computeFence.Value);

        instanceBoundsBuffer->GetTracker().UpdateAccess(RHI::ERHIResourceAccess::SRV);
        instanceBoundsBuffer->GetTracker().UpdateLastAccessFence(computeFence);
        input.VisibilityFlagsBuffer->GetTracker().UpdateAccess(RHI::ERHIResourceAccess::UAV);
        input.VisibilityFlagsBuffer->GetTracker().UpdateLastAccessFence(computeFence);

        RenderCore::TransitionBufferImmediate(RHI::GRHIApi, input.VisibilityFlagsBuffer, RHI::ERHIResourceAccess::TransferSrc, RHI::EQueueType::Compute);

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
        void* mapped = RHI::GRHIApi->MapReadBuffer(readbackCmd, input.VisibilityFlagsBuffer->GetRHI(), readInfo);
        if (!mapped)
        {
            return false;
        }

        readbackCmd.End();
        auto readbackFenceValue = computeQueue->ExecuteContext({ readbackContext }, {});
        computeQueue->WaitValue(readbackFenceValue);

        outVisibilityFlags.resize(input.PrimitiveCount);
        std::memcpy(outVisibilityFlags.data(), mapped, input.PrimitiveCount * sizeof(uint32_t));
        RHI::GRHIApi->Unmap(mapped);
        RHI::RHIFence readbackFence;
        readbackFence.Value = readbackFenceValue;
        readbackFence.QueueType = RHI::EQueueType::Compute;
        input.VisibilityFlagsBuffer->GetTracker().UpdateAccess(RHI::ERHIResourceAccess::TransferSrc);
        input.VisibilityFlagsBuffer->GetTracker().UpdateLastAccessFence(readbackFence);

        return true;
    }

} // namespace Renderer
