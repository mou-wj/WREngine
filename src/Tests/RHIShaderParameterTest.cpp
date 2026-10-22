#include "TestBase.h"
#include "RHIApi.h"
#include "VulkanRHIApi.h"
#include "OpenGLRHIApi.h"
#include "DirectX12RHIApi.h"
#include "ShaderCompiler.h"
#include "Window.h"
#include "RHIPipelineStateCache.h"
#include "RHICaptureHelper.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cstring>
#include <unordered_map>

using namespace RenderCore;

namespace Test {

namespace {
    std::unordered_map<const RHI::RHIViewableResource*, RHI::ERHIResourceAccess> GTrackedResourceAccess;
    std::unordered_map<const RHI::RHIViewableResource*, RHI::EQueueType> GTrackedResourceQueueTypes;

    void TransitionResource(
        RHI::RHIApi* api,
        RHI::RHICommandListBase& cmdList,
        RHI::RHIViewableResource* resource,
        RHI::ERHIResourceAccess currentAccess,
        RHI::ERHIResourceAccess targetAccess,
        EQueueType currentQueueType,
        EQueueType targetQueueType)
    {
        if (!api || !resource)
        {
            return;
        }

        if (currentAccess == targetAccess && currentQueueType == targetQueueType)
        {
            return;
        }


        std::vector<RHI::RHITransitionInfo> infos;
        if (auto* texture = dynamic_cast<RHI::RHITexture*>(resource))
        {
            infos.emplace_back(texture, currentAccess, targetAccess);
        }
        else if (auto* buffer = dynamic_cast<RHI::RHIBuffer*>(resource))
        {
            infos.emplace_back(buffer, currentAccess, targetAccess);
        }
        else
        {
            return;
        }

        char* transitionMem = new char[RHI::G_RHITransition_TotalSize];
        auto* transition = new(transitionMem) RHI::RHITransition();
        api->RHICreateTransition(transition, RHI::RHITransitionCreateInfo(RHI::ERHITransitionCreateFlags::None, std::move(infos)));

        cmdList.BeginTransitions({ transition });
        cmdList.EndTransitions({ transition });

        GTrackedResourceAccess[resource] = targetAccess;
        api->RHIReleaseTransition(transition);
        delete[] transitionMem;
    }


    void TransitionResource(
        RHI::RHIApi* api,
        RHI::RHICommandListBase& cmdList,
        RHI::RHIViewableResource* resource,
        RHI::ERHIResourceAccess targetAccess,
        EQueueType targetQueueType)
    {
        if (!api || !resource)
        {
            return;
        }

        const auto it = GTrackedResourceAccess.find(resource);
        const RHI::ERHIResourceAccess currentAccess =
            it != GTrackedResourceAccess.end() ? it->second : RHI::ERHIResourceAccess::Unknown;
        const auto queueIt = GTrackedResourceQueueTypes.find(resource);
        const EQueueType currentQueueType =
            queueIt != GTrackedResourceQueueTypes.end() ? queueIt->second : EQueueType::Graphics;
        TransitionResource(api, cmdList, resource, currentAccess, targetAccess, currentQueueType, targetQueueType
        );
    }
}

// 常量缓冲结构体
struct ComputeShaderConstants
{
    uint32_t bFlipTextureVertical;   // 是否纹理上下颠倒
    uint32_t bFlipBufferHorizontal;  // 是否缓冲左右颠倒
    uint32_t Padding0;
    uint32_t Padding1;
};

class RHIShaderParameterTest : public TestBase
{
public:
    RHIShaderParameterTest() = default;
    virtual ~RHIShaderParameterTest() = default;

    void Setup() override
    {
        RHI::GRHIApi = CreateTestRHIApi();
        RHI::GRHIApi->Init();
        auto* api = RHI::GRHIApi;
        if (!api)
            return;

        CreateShaders(api);
        CreateTestResources(api);
        CreateComputeResources(api);
        CreateSwapchain(api);
    }

    void Run() override
    {
        auto* api = RHI::GRHIApi;
        if (!api || !m_ComputePipelineState)
            return;

        auto* queue = api->GetQueue(RHI::EQueueType::Compute);
        if (!queue)
            return;

        auto* cmdContext = queue->AcquireCommandContext();
        if (!cmdContext)
            return;

        auto* computeContext = dynamic_cast<RHI::RHIComputeContex*>(cmdContext);
        if (!computeContext)
        {
            queue->ReleaseCommandContext(cmdContext);
            return;
        }
        RHI::RHIComputeCommandList cmdList(computeContext);


        // ========================
        // 执行计算着色器 (4 次迭代)
        // ========================
        for (int iteration = 0; iteration < 1000000; ++iteration)
        {
            cmdList.SetImmediate(true);
            cmdList.Begin();

            // 设置计算管线状态
            cmdList.SetComputePipelineState(m_ComputePipelineState.get());

            // 准备计算着色器参数
            RHI::RHIBatchedShaderParameters computeParams;

            // 更新常量缓冲数据：ComputeConstants (UniformBuffer)
            ComputeShaderConstants cbData = { 0, 0, 0, 0 };
            if (iteration == 1)
                cbData.bFlipTextureVertical = 1;
            else if (iteration == 2)
                cbData.bFlipBufferHorizontal = 1;
            else if (iteration == 3)
            {
                cbData.bFlipTextureVertical = 1;
                cbData.bFlipBufferHorizontal = 1;
            }

            TransitionResource(api, cmdList, m_ConstantBuffer.get(), RHI::ERHIResourceAccess::TransferDest,EQueueType::Compute);
            api->UpdateBuffer(cmdList, m_ConstantBuffer.get(), &cbData, { 0, sizeof(cbData) });
            TransitionResource(api, cmdList, m_ConstantBuffer.get(), RHI::ERHIResourceAccess::SRV, EQueueType::Compute);

            TransitionResource(api, cmdList, m_TestBuffer.get(), RHI::ERHIResourceAccess::SRV, EQueueType::Compute);
            TransitionResource(api, cmdList, m_TestTexture.get(), RHI::ERHIResourceAccess::SRV, EQueueType::Compute);
            TransitionResource(api, cmdList, m_OutputTexture.get(), RHI::ERHIResourceAccess::UAV, EQueueType::Compute);
            TransitionResource(api, cmdList, m_OutputBuffer.get(), RHI::ERHIResourceAccess::UAV, EQueueType::Compute);


            std::optional<ShaderParameterAllocation> alloc;
            // 设置 ComputeConstants 作为 UniformBuffer
            if (alloc = m_ParameterMap.FindParameterAllocation("ComputeConstants"))
            {
                RHI::RHIShaderResourceParameter ubParam;
                ubParam.Type = RHI::RHIShaderResourceParameter::EType::UniformBuffer;
                ubParam.Resource = m_ConstantBuffer.get();
                ubParam.Index = alloc->BaseIndex;
                computeParams.ResourceParameters.push_back(ubParam);
            }

            // 设置离散参数：bExtraParam 和 bExtraParam1
            uint32_t bExtraParamValue = (iteration == 0) ? 0 : 1;  // 示例：根据迭代设置
            uint32_t bExtraParam1Value = (iteration == 1) ? 1 : 0; // 示例

            size_t dataOffset = 0;
            std::optional<size_t> extraParamOffset;
            std::optional<size_t> extraParam1Offset;
            if (alloc = m_ParameterMap.FindParameterAllocation("bExtraParam"))
            {
                RHI::RHIShaderUniformParameter param;
                param.BufferIndex = alloc->BufferIndex;
                param.BaseIndex = alloc->BaseIndex;
                param.Offset = dataOffset;
                param.Size = alloc->Size;
                computeParams.UniformParameters.push_back(param);
                extraParamOffset = dataOffset;
                dataOffset += alloc->Size;
            }

            if (alloc = m_ParameterMap.FindParameterAllocation("bExtraParam1"))
            {
                RHI::RHIShaderUniformParameter param;
                param.BufferIndex = alloc->BufferIndex;
                param.BaseIndex = alloc->BaseIndex;
                param.Offset = dataOffset;
                param.Size = alloc->Size;
                computeParams.UniformParameters.push_back(param);
                extraParam1Offset = dataOffset;
                dataOffset += alloc->Size;
            }

            // 设置 Data
            computeParams.Data.resize(dataOffset);
            if (extraParamOffset && (*extraParamOffset + sizeof(uint32_t) <= computeParams.Data.size()))
            {
                std::memcpy(computeParams.Data.data() + *extraParamOffset, &bExtraParamValue, sizeof(uint32_t));
            }
            if (extraParam1Offset && (*extraParam1Offset + sizeof(uint32_t) <= computeParams.Data.size()))
            {
                std::memcpy(computeParams.Data.data() + *extraParam1Offset, &bExtraParam1Value, sizeof(uint32_t));
            }

            // 设置资源参数：纹理 SRV
            if (m_TestTextureSRV && (alloc = m_ParameterMap.FindParameterAllocation("InputTexture")))
            {
                RHI::RHIShaderResourceParameter texParam;
                texParam.Type = RHI::RHIShaderResourceParameter::EType::Texture;
                texParam.Resource = m_TestTexture.get();
                texParam.Index = alloc->BaseIndex;
                computeParams.ResourceParameters.push_back(texParam);
            }

            // 纹理 UAV
            if (m_OutputTextureUAV && (alloc = m_ParameterMap.FindParameterAllocation("OutputTexture")))
            {
                RHI::RHIShaderResourceParameter texUavParam;
                texUavParam.Type = RHI::RHIShaderResourceParameter::EType::UAV;
                texUavParam.Resource = m_OutputTextureUAV.get();
                texUavParam.Index = alloc->BaseIndex;
                computeParams.ResourceParameters.push_back(texUavParam);
            }

            // 缓冲区 SRV
            if (m_TestBufferSRV && (alloc = m_ParameterMap.FindParameterAllocation("InputBuffer")))
            {
                RHI::RHIShaderResourceParameter bufSrvParam;
                bufSrvParam.Type = RHI::RHIShaderResourceParameter::EType::SRV;
                bufSrvParam.Resource = m_TestBufferSRV.get();
                bufSrvParam.Index = alloc->BaseIndex;
                computeParams.ResourceParameters.push_back(bufSrvParam);
            }

            // 缓冲区 UAV
            if (m_OutputBufferUAV && (alloc = m_ParameterMap.FindParameterAllocation("OutputBuffer")))
            {
                RHI::RHIShaderResourceParameter bufUavParam;
                bufUavParam.Type = RHI::RHIShaderResourceParameter::EType::UAV;
                bufUavParam.Resource = m_OutputBufferUAV.get();
                bufUavParam.Index = alloc->BaseIndex;
                computeParams.ResourceParameters.push_back(bufUavParam);
            }

            // 设置批量参数
            cmdList.SetBatchedShaderParameters(m_ComputeShader.get(), computeParams);

            // 分发计算任务 (2x2 纹理，每个线程组 8x8，分发 1 个线程组)
            cmdList.Dispatch(1, 1, 1);
            auto swapchainSlot = m_Swapchain->AcquireNextSlot();
            auto* backTexture = swapchainSlot.Texture;
            if (backTexture)
            {
                TransitionResource(api, cmdList, m_OutputTexture.get(), RHI::ERHIResourceAccess::TransferSrc, EQueueType::Compute);
                TransitionResource(api, cmdList, backTexture, RHI::ERHIResourceAccess::TransferDest, EQueueType::Compute);
                RHI::RHIBlitTextureDesc blit{};
                blit.SrcRegion.Width = 2;
                blit.SrcRegion.Height = 2;
                blit.DstRegion.Width = m_FrameWidth;
                blit.DstRegion.Height = m_FrameHeight;
                cmdList.BlitTexture(m_OutputTexture.get(), backTexture, blit);
                TransitionResource(api, cmdList, backTexture, RHI::ERHIResourceAccess::Present, EQueueType::Graphics);
            }



            // 提交命令
			cmdList.End();
            cmdList.ExecuteAll();
            std::vector<RHI::RHIWaitInfo> waitInfos;
            if (swapchainSlot.ReadySync)
            {
                waitInfos.push_back({ swapchainSlot.ReadySync, EQueueType::Graphics, 0,RHI::ERHIPipelineStage::ColorAttachmentOutput });
            }
            auto fenceValue = queue->ExecuteContext({ computeContext }, waitInfos);
            queue->WaitValue(fenceValue);
            RHI::RHIWaitInfo presentWait;
            presentWait.SyncPoint = queue->GetSyncPoint();
            presentWait.Value = fenceValue;
            presentWait.WaitStage = RHI::ERHIPipelineStage::ComputeShader;
            GRHIApi->GetPresentExecutor()->Present(m_Swapchain.get(), presentWait);
            cmdList.Clear();
        }

        queue->WaitIdle();
        queue->ReleaseCommandContext(cmdContext);
    }

    void Teardown() override
    {
        m_ComputeShader.reset();
        m_ComputePipelineState.reset();
        m_TestTexture.reset();
        m_TestTextureSRV.reset();
        m_TestSampler.reset();
        m_TestBuffer.reset();
        m_TestBufferSRV.reset();
        m_TestBufferUAV.reset();
        m_OutputTexture.reset();
        m_OutputTextureUAV.reset();
        m_OutputBuffer.reset();
        m_OutputBufferUAV.reset();
        m_ConstantBuffer.reset();
        GTrackedResourceAccess.clear();
        m_Swapchain.reset();
        m_Window.reset();
        auto* api = RHI::GRHIApi;
        if (!api)
            return;

        api->Shutdown();
        //delete api; //delete 会报错
        RHI::GRHIApi = nullptr;
        
    }

private:
    // 用于测试 SetBatchedShaderParameters 的各种资源类型
    RHI::RHITextureSP m_TestTexture;
    RHI::RHIShaderResourceViewSP m_TestTextureSRV;
    RHI::RHISamplerSP m_TestSampler;
    RHI::RHIBufferSP m_TestBuffer;
    RHI::RHIShaderResourceViewSP m_TestBufferSRV;
    RHI::RHIUnorderedAccessViewSP m_TestBufferUAV;

    // 计算着色器相关资源
    RHI::RHIComputeShaderSP m_ComputeShader;
    RHI::RHIComputePipelineStateSP m_ComputePipelineState;
    RHI::RHITextureSP m_OutputTexture;
    RHI::RHIUnorderedAccessViewSP m_OutputTextureUAV;
    RHI::RHIBufferSP m_OutputBuffer;
    RHI::RHIUnorderedAccessViewSP m_OutputBufferUAV;
    RHI::RHIBufferSP m_ConstantBuffer;
    RHI::RHISwapchainSP m_Swapchain;
    SlateCore::WindowSP m_Window;
    int m_WindiwWidth = 512;
    int m_WindowHeight = 512;
    int m_FrameWidth = 0;
    int m_FrameHeight = 0;
    // Shader 参数映射
    RenderCore::ShaderParameterAllocationMap m_ParameterMap;

    void CreateShaders(RHI::RHIApi* api)
    {
        ShaderCompiler compiler;

        // =============================
        // Compute Shader
        // =============================
        ShaderCompileInput csInput;
        csInput.Frequency = ERHIShaderFrequency::Compute;
        csInput.EntryPoint = "CSMain";
        csInput.Platform = GShaderPlatform;
        csInput.VirtualSourceFilePath = "param_cs.hlsl";

        csInput.Environment.VirtualIncludes["param_cs.hlsl"] = R"(
        Texture2D InputTexture : register(t0);
        [[vk::image_format("rgba8")]]
        RWTexture2D<float4> OutputTexture : register(u0);
        RWStructuredBuffer<float4> InputBuffer : register(u1);
        RWStructuredBuffer<float4> OutputBuffer : register(u2);

        cbuffer ComputeConstants : register(b0)
        {
            uint bFlipTextureVertical;
            uint bFlipBufferHorizontal;
            uint2 padding ;
        };
        uint bExtraParam;
        uint bExtraParam1;
        [numthreads(8, 8, 1)]
        void CSMain(uint3 DTid : SV_DispatchThreadID)
        {
            uint2 texCoord = DTid.xy;
            
            // 处理纹理拷贝（带翻转控制）
            uint2 readCoord = texCoord;
            if (bFlipTextureVertical)
            {
                readCoord.y = 1 - texCoord.y;  // 上下颠倒
            }
            
            float4 texColor = InputTexture.Load(int3(readCoord, 0));
            OutputTexture[texCoord] = texColor;
            
            // 处理缓冲区拷贝（带左右翻转控制）
            if (DTid.x == 0 && DTid.y == 0)
            {
                float4 bufData = InputBuffer[0];
                if (bFlipBufferHorizontal)
                {
                    // 左右翻转：交换 x 和 y 坐标
                    bufData = float4(bufData.y, bufData.x, bufData.w, bufData.z);
                }
                OutputBuffer[0] = bufData;
            }
            if(bExtraParam)
            {
                OutputBuffer[0] = float4(0, 1, 1, 1);
                // 这里可以测试更多的参数绑定类型，比如纹理数组、结构化缓冲区等
            }
            if(bExtraParam1)
            {
                OutputBuffer[0] = float4(0, 1, 1, 1);
                // 这里可以测试更多的参数绑定类型，比如纹理数组、结构化缓冲区等
            }
        }
        )";

        ShaderCompilationOutput csOutput = compiler.Compile(csInput);
        if (!csOutput.bSuccess)
            return;

        m_ParameterMap = csOutput.ParameterMap;
        m_ComputeShader = api->CreateComputeShader(csOutput.PackedBinaryData);
    }

    void CreateTestResources(RHI::RHIApi* api)
    {
        // 创建一个简单的 2x2 RGBA 纹理并上传颜色数据
        RHI::RHITextureDesc texDesc;
        texDesc.Width = 2;
        texDesc.Height = 2;
        texDesc.Format = RHI::ERHIFormat::R8G8B8A8_UNorm;
        texDesc.Usage = RHI::ERHITextureCreateFlag::ShaderResource | RHI::ERHITextureCreateFlag::TransferDest;
        m_TestTexture = api->CreateTexture(texDesc);

        uint8_t texData[16] = {
            0xFF, 0x00, 0x00, 0xFF,
            0x00, 0xFF, 0x00, 0xFF,
            0x00, 0x00, 0xFF, 0xFF,
            0xFF, 0xFF, 0xFF, 0xFF,
        };

        auto* queue = api->GetQueue(RHI::EQueueType::Compute);
        if (!queue)
            return;
        auto* cmdContext = queue->AcquireCommandContext();
        if (!cmdContext)
            return;
        auto* computeContext = dynamic_cast<RHI::RHIComputeContex*>(cmdContext);
        if (!computeContext)
        {
            queue->ReleaseCommandContext(cmdContext);
            return;
        }
        RHI::RHIComputeCommandList cmdList(computeContext);
        cmdList.SetImmediate(true);
        cmdList.Begin();
        TransitionResource(api, cmdList, m_TestTexture.get(), RHI::ERHIResourceAccess::TransferDest, EQueueType::Compute);
        api->UpdateTexture(cmdList, m_TestTexture.get(), texData, RHI::RHIUpdateTextureRegion::Create2DRegion(2, 2));
        TransitionResource(api, cmdList, m_TestTexture.get(), RHI::ERHIResourceAccess::SRV, EQueueType::Compute);
        cmdList.End();
        cmdList.ExecuteAll();
        queue->ExecuteContext(computeContext);
        queue->WaitIdle();
        // 采样器
        RHI::RHISamplerDesc samplerDesc;
        m_TestSampler = api->CreateSampler(samplerDesc);

        // 创建 SRV
        RHI::RHITexSRVCreateInfo srvDesc;
        srvDesc.FirstMipSlice = 0;
        srvDesc.Format = RHI::ERHIFormat::R8G8B8A8_UNorm;
        m_TestTextureSRV = api->CreateTextureShaderResourceView(m_TestTexture.get(), srvDesc);

        // 创建一个简单的 Buffer，用于测试 SRV/UAV 绑定
        RHI::RHIBufferDesc bufDesc;
        bufDesc.Size = sizeof(glm::vec4);
        bufDesc.Usage = RHI::ERHIBufferUsageFlag::ShaderResource | RHI::ERHIBufferUsageFlag::UnorderedAccess | RHI::ERHIBufferUsageFlag::TransferDst;
        m_TestBuffer = api->CreateBuffer(bufDesc);

        glm::vec4 bufferData = glm::vec4(0.25f, 0.5f, 0.75f, 1.0f);
        cmdList.SetImmediate(true);
        cmdList.Begin();
        TransitionResource(api, cmdList, m_TestBuffer.get(), RHI::ERHIResourceAccess::TransferDest, EQueueType::Compute);
        api->UpdateBuffer(cmdList, m_TestBuffer.get(), &bufferData, { 0, sizeof(bufferData) });
        TransitionResource(api, cmdList, m_TestBuffer.get(), RHI::ERHIResourceAccess::SRV, EQueueType::Compute);
        cmdList.End();
        cmdList.ExecuteAll();
        queue->ExecuteContext(computeContext);
        queue->WaitIdle();
        queue->ReleaseCommandContext(cmdContext);

        RHI::RHIBufferSRVCreateInfo bufSrvDesc;
        bufSrvDesc.Offset = 0;
        bufSrvDesc.NumElements = 1;
        bufSrvDesc.Stride = sizeof(glm::vec4);
        bufSrvDesc.Format = RHI::ERHIFormat::R32G32B32A32_Float;
        m_TestBufferSRV = api->CreateBufferShaderResourceView(m_TestBuffer.get(), bufSrvDesc);

        RHI::RHIBufferUAVCreateInfo bufUavDesc;
        bufUavDesc.Offset = 0;
        bufUavDesc.NumElements = 1;
        bufUavDesc.Stride = sizeof(glm::vec4);
        bufUavDesc.Format = RHI::ERHIFormat::R32G32B32A32_Float;
        m_TestBufferUAV = api->CreateBufferUnorderedAccessView(m_TestBuffer.get(), bufUavDesc);
    }

    void CreateComputeResources(RHI::RHIApi* api)
    {
        // 创建输出纹理（UAV）
        RHI::RHITextureDesc outputTexDesc;
        outputTexDesc.Width = 2;
        outputTexDesc.Height = 2;
        outputTexDesc.Format = RHI::ERHIFormat::R8G8B8A8_UNorm;
        outputTexDesc.Usage = RHI::ERHITextureCreateFlag::UAV | RHI::ERHITextureCreateFlag::TransferSrc;
        m_OutputTexture = api->CreateTexture(outputTexDesc);

        // 创建输出纹理的 UAV
        RHI::RHITexUAVCreateInfo texUavDesc;
        texUavDesc.Format = RHI::ERHIFormat::R8G8B8A8_UNorm;
        m_OutputTextureUAV = api->CreateTextureUnorderedAccessView(m_OutputTexture.get(), texUavDesc);

        // 创建输出缓冲区（UAV）
        RHI::RHIBufferDesc outputBufDesc;
        outputBufDesc.Size = sizeof(glm::vec4);
        outputBufDesc.Usage = RHI::ERHIBufferUsageFlag::UnorderedAccess;
        m_OutputBuffer = api->CreateBuffer(outputBufDesc);

        // 创建输出缓冲区的 UAV
        RHI::RHIBufferUAVCreateInfo bufUavDesc;
        bufUavDesc.Offset = 0;
        bufUavDesc.NumElements = 1;
        bufUavDesc.Stride = sizeof(glm::vec4);
        bufUavDesc.Format = RHI::ERHIFormat::R32G32B32A32_Float;
        m_OutputBufferUAV = api->CreateBufferUnorderedAccessView(m_OutputBuffer.get(), bufUavDesc);

        // 创建常量缓冲
        RHI::RHIBufferDesc cbDesc;
        cbDesc.Size = sizeof(ComputeShaderConstants);
        cbDesc.Usage = RHI::ERHIBufferUsageFlag::Constant | RHI::ERHIBufferUsageFlag::TransferDst;
        m_ConstantBuffer = api->CreateBuffer(cbDesc);

        // 初始化常量缓冲数据（第一次运行：不翻转）
        ComputeShaderConstants cbData = { 0, 0, 0, 0 };
        auto* queue = api->GetQueue(RHI::EQueueType::Compute);
        if (!queue)
            return;
        auto* cmdContext = queue->AcquireCommandContext();
        if (!cmdContext)
            return;
        auto* computeContext = dynamic_cast<RHI::RHIComputeContex*>(cmdContext);
        if (!computeContext)
        {
            queue->ReleaseCommandContext(cmdContext);
            return;
        }
        RHI::RHIComputeCommandList cmdList(computeContext);
        cmdList.SetImmediate(true);
        cmdList.Begin();
        TransitionResource(api, cmdList, m_ConstantBuffer.get(), RHI::ERHIResourceAccess::TransferDest, EQueueType::Compute);
        api->UpdateBuffer(cmdList, m_ConstantBuffer.get(), &cbData, { 0, sizeof(cbData) });
        TransitionResource(api, cmdList, m_ConstantBuffer.get(), RHI::ERHIResourceAccess::SRV, EQueueType::Compute);
        cmdList.End();
        cmdList.ExecuteAll();
        queue->ExecuteContext(computeContext);
        queue->WaitIdle();
        queue->ReleaseCommandContext(cmdContext);

        // 创建计算管线状态
        RHI::RHIComputePipelineStateDesc computeDesc;
        computeDesc.ComputeShader = m_ComputeShader.get();
        m_ComputePipelineState = api->CreateComputePipelineState(computeDesc);
    }
    void CreateSwapchain(RHI::RHIApi* api)
    {
        // 
        m_Window = SlateCore::WindowFactory::CreateWindowSP(m_WindiwWidth, m_WindowHeight, "RHIShaderParameterTest");
        m_Window->Show();
        // 创建交换链
        void* windowHandle = m_Window->GetNativeHandle(); // 获取窗口句柄的函数
        uint32_t width = m_WindiwWidth;
        uint32_t height = m_WindowHeight;
        ERHIFormat format = ERHIFormat::B8G8R8A8_UNorm;
        m_Swapchain = api->CreateSwapchain(windowHandle, width, height, format);
        auto frameSize = m_Window->GetFramebufferSize();
        m_FrameWidth = frameSize.X;
        m_FrameHeight = frameSize.Y;

    }
};
REGISTER_RENDER_TEST("RHIShaderParameterTest", RHIShaderParameterTest);

} // namespace Test
