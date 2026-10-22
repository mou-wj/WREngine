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

    static inline bool IsInsideClipSpace(const Core::Float4& Clip)
    {
        return Clip.W > 0.0f &&
            Clip.X >= -Clip.W && Clip.X <= Clip.W &&
            Clip.Y >= -Clip.W && Clip.Y <= Clip.W &&
            Clip.Z >= -Clip.W && Clip.Z <= Clip.W;
    }

    static inline Core::Float4 TransformPoint(const Core::Float4x4& Matrix, const Core::Float3& Point)
    {
        const float x = Matrix(0, 0) * Point.X + Matrix(0, 1) * Point.Y + Matrix(0, 2) * Point.Z + Matrix(0, 3);
        const float y = Matrix(1, 0) * Point.X + Matrix(1, 1) * Point.Y + Matrix(1, 2) * Point.Z + Matrix(1, 3);
        const float z = Matrix(2, 0) * Point.X + Matrix(2, 1) * Point.Y + Matrix(2, 2) * Point.Z + Matrix(2, 3);
        const float w = Matrix(3, 0) * Point.X + Matrix(3, 1) * Point.Y + Matrix(3, 2) * Point.Z + Matrix(3, 3);
        return Core::Float4(x, y, z, w);
    }

    static inline void AddAABBPoint(const Core::Float3& P, float& MinX, float& MinY, float& MinZ, float& MaxX, float& MaxY, float& MaxZ)
    {
        MinX = std::min(MinX, P.X); MaxX = std::max(MaxX, P.X);
        MinY = std::min(MinY, P.Y); MaxY = std::max(MaxY, P.Y);
        MinZ = std::min(MinZ, P.Z); MaxZ = std::max(MaxZ, P.Z);
    }

    HZBProjectedBox ProjectAABBToScreenSpace(const Core::Float4x4& ViewProjection, const Core::Float2& ScreenSize, const HZBInstanceAABB& Instance)
    {
        HZBProjectedBox Projected;
        Projected.bIsValid = false;

        const Core::Float3 corners[8] = {
            Core::Float3(Instance.Min.X, Instance.Min.Y, Instance.Min.Z),
            Core::Float3(Instance.Max.X, Instance.Min.Y, Instance.Min.Z),
            Core::Float3(Instance.Min.X, Instance.Max.Y, Instance.Min.Z),
            Core::Float3(Instance.Max.X, Instance.Max.Y, Instance.Min.Z),
            Core::Float3(Instance.Min.X, Instance.Min.Y, Instance.Max.Z),
            Core::Float3(Instance.Max.X, Instance.Min.Y, Instance.Max.Z),
            Core::Float3(Instance.Min.X, Instance.Max.Y, Instance.Max.Z),
            Core::Float3(Instance.Max.X, Instance.Max.Y, Instance.Max.Z),
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
            Core::Float4 p = TransformPoint(Instance.Transform, corners[i]);
            Core::Float4 clip = TransformPoint(ViewProjection, Core::Float3(p.X, p.Y, p.Z));

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
            const float screenX = (ndcX * 0.5f + 0.5f) * ScreenSize.X;
            const float screenY = (ndcY * 0.5f + 0.5f) * ScreenSize.Y;

            minX = std::min(minX, screenX);
            maxX = std::max(maxX, screenX);
            minY = std::min(minY, screenY);
            maxY = std::max(maxY, screenY);
            minZ = std::min(minZ, clip.Z / clip.W);
            maxZ = std::max(maxZ, clip.Z / clip.W);
        }

        if (!anyInside)
        {
            return Projected;
        }

        Projected.MinX = minX;
        Projected.MinY = minY;
        Projected.MaxX = maxX;
        Projected.MaxY = maxY;
        Projected.bIsValid = true;
        return Projected;
    }

    bool ExecuteHZBOcclusionTestPass(
        const HZBOcclusionTestInput& Input,
        std::vector<uint32_t>& OutVisibilityFlags)
    {
        OutVisibilityFlags.clear();

        if (Input.PrimitiveCount == 0 || !Input.InstanceData || !Input.VisibilityFlagsBuffer)
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
        instanceSRVDesc.NumElements = Input.PrimitiveCount;
        instanceSRVDesc.Stride = sizeof(HZBInstanceParameters);
        instanceSRVDesc.Format = RHI::ERHIFormat::Unknown;

        RHIBufferUAVCreateInfo visibilityUAVDesc;
        visibilityUAVDesc.Offset = 0;
        visibilityUAVDesc.NumElements = Input.PrimitiveCount;
        visibilityUAVDesc.Stride = sizeof(uint32_t);
        visibilityUAVDesc.Format = RHI::ERHIFormat::Unknown;

        RenderCore::RenderBuffer* instanceBoundsBuffer = Input.InstanceBoundsBuffer;
        if (!instanceBoundsBuffer)
        {
            return false;
        }

        auto* instanceSRV = instanceBoundsBuffer->GetViewCache().GetOrCreateSRV(
            instanceBoundsBuffer->GetRHI(),
            instanceSRVDesc);
        auto* visibilityUAV = Input.VisibilityFlagsBuffer->GetViewCache().GetOrCreateUAV(
            Input.VisibilityFlagsBuffer->GetRHI(),
            visibilityUAVDesc);

        if (!instanceSRV || !visibilityUAV)
        {
            return false;
        }

        RenderCore::TransitionBufferImmediate(RHI::GRHIApi, instanceBoundsBuffer, RHI::ERHIResourceAccess::SRV, RHI::EQueueType::Compute);
        RenderCore::TransitionBufferImmediate(RHI::GRHIApi, Input.VisibilityFlagsBuffer, RHI::ERHIResourceAccess::UAV, RHI::EQueueType::Compute);

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
        computeDesc.computeShader = computeShader;
        auto* pipelineState = RHI::RHIPipelineStateCache::GetOrCreateComputePipelineState(computeDesc);
        cmd.SetComputePipelineState(pipelineState);

        HZBOcclusionTestParameters params;
        params.ViewProjection = Input.ViewProjection;
        params.ScreenSize = Input.ScreenSize;
        params.PrimitiveCount = Input.PrimitiveCount;
        params.DepthPyramidTexture = Input.DepthPyramidTexture;
        params.Instances = instanceSRV;
        params.VisibilityFlags = visibilityUAV;

        SetShaderParameters(cmd, hzbShader, &params);

        const uint32_t groupX = (Input.PrimitiveCount + 63u) / 64u;
        cmd.Dispatch(groupX, 1, 1);
        cmd.End();
		RHI::RHIFence computeFence;
		computeFence.QueueType = RHI::EQueueType::Compute;
        computeFence.Value = computeQueue->ExecuteContext({ computeContext }, {});
        computeQueue->WaitValue(computeFence.Value);

        instanceBoundsBuffer->GetTracker().UpdateAccess(RHI::ERHIResourceAccess::SRV);
        instanceBoundsBuffer->GetTracker().UpdateLastAccessFence(computeFence);
        Input.VisibilityFlagsBuffer->GetTracker().UpdateAccess(RHI::ERHIResourceAccess::UAV);
        Input.VisibilityFlagsBuffer->GetTracker().UpdateLastAccessFence(computeFence);

        RenderCore::TransitionBufferImmediate(RHI::GRHIApi, Input.VisibilityFlagsBuffer, RHI::ERHIResourceAccess::TransferSrc, RHI::EQueueType::Compute);

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
        readInfo.offset = 0;
        readInfo.size = Input.PrimitiveCount * sizeof(uint32_t);
        void* mapped = RHI::GRHIApi->MapReadBuffer(readbackCmd, Input.VisibilityFlagsBuffer->GetRHI(), readInfo);
        if (!mapped)
        {
            return false;
        }

        readbackCmd.End();
        auto readbackFenceValue = computeQueue->ExecuteContext({ readbackContext }, {});
        computeQueue->WaitValue(readbackFenceValue);

        OutVisibilityFlags.resize(Input.PrimitiveCount);
        std::memcpy(OutVisibilityFlags.data(), mapped, Input.PrimitiveCount * sizeof(uint32_t));
        RHI::GRHIApi->Unmap(mapped);
        RHI::RHIFence readbackFence;
        readbackFence.Value = readbackFenceValue;
        readbackFence.QueueType = RHI::EQueueType::Compute;
        Input.VisibilityFlagsBuffer->GetTracker().UpdateAccess(RHI::ERHIResourceAccess::TransferSrc);
        Input.VisibilityFlagsBuffer->GetTracker().UpdateLastAccessFence(readbackFence);

        return true;
    }

} // namespace Renderer
