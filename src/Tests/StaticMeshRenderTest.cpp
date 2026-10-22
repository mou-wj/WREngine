// StaticMeshRenderTest.cpp
// 测试StaticMesh的构建和执行：实现一个简单的拷贝纹理pass
#include "RenderGraphBuilder.h"
#include "RenderGraphPass.h"
#include "RenderGraphResource.h"
#include "ShaderParameter.h"
#include "GlobalShader.h"
#include "RenderResource.h"
#include "ShaderCompiler.h"
#include "RHIApi.h"
#include "VulkanRHIApi.h"
#include "ToolShaders.h"
#include "Window.h"
#include "AssetManager.h"
#include <unordered_map>
#include "TestBase.h"
#include "PathInfo.h"
#include "StaticMeshComponent.h"
#include "RenderModule.h"
#include "SceneInterface.h"
#include "Viewport.h"
#include "PrimitiveComponent.h"
#include "PrimitiveSceneProxy.h"
#include "LocalVertexFactory.h"
#include "RHIPipelineStateCache.h"
#include "Camera.h"
#include "StaticMeshProcess.h"
#include "StaticMeshProxy.h"
#include "../Private/StaticMeshMaterialShader.h"
#include "../Private/Scene.h"
#include "EngineGlobal.h"
using namespace RenderCore;
using namespace Engine;
using namespace Renderer;
using namespace RHI;
namespace Test {

    namespace {
        std::unordered_map<const RHI::RHIViewableResource*, RHI::ERHIResourceAccess> GTrackedResourceAccess;

        void TransitionResource(
            RHI::RHIApi* api,
            RHI::RHICommandListBase& cmdList,
            RHI::RHIViewableResource* resource,
            RHI::ERHIResourceAccess currentAccess,
            RHI::ERHIResourceAccess targetAccess)
        {
            if (!api || !resource)
            {
                return;
            }

            if (currentAccess == targetAccess)
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
            RHI::ERHIResourceAccess targetAccess)
        {
            if (!api || !resource)
            {
                return;
            }

            const auto it = GTrackedResourceAccess.find(resource);
            const RHI::ERHIResourceAccess currentAccess =
                it != GTrackedResourceAccess.end() ? it->second : RHI::ERHIResourceAccess::Unknown;
            TransitionResource(api, cmdList, resource, currentAccess, targetAccess);
        }
    }






    class StaticMeshRenderTest : public TestBase {
    public:
        int FrameWidth = 0;
        int FrameHeight = 0;
        int WindiwWidth = 512;
        int WindowHeight = 512;
        RHI::RHISamplerSP SamplerSP;
        void Setup() override {
            RHI::GRHIApi = CreateTestRHIApi();
            RHI::GRHIApi->Init();
            auto* api = RHI::GRHIApi;
            if (!api)
            {
                return;
            }
            GetRenderModuleInstance();
            InitSwapchain();
            InitGlobalResources();
            InitStaticMesh();
            InitPipelineState();


            // 只保存静态描述
            m_TexDesc.Width = FrameWidth;
            m_TexDesc.Height = FrameHeight;
            m_TexDesc.Format = ERHIFormat::R8G8B8A8_UNorm;
            m_TexDesc.Usage = ERHITextureCreateFlag::ShaderResource | ERHITextureCreateFlag::TransferSrc | ERHITextureCreateFlag::TransferDest;
        }

        void InitSwapchain()
        {
            // 
            m_Window = SlateCore::WindowFactory::CreateWindowSP(WindiwWidth, WindowHeight, "RHIRenderTriangleTest");
            m_Window->Show();
            // 创建交换链
            void* windowHandle = m_Window->GetNativeHandle(); // 获取窗口句柄的函数
            uint32_t width = WindiwWidth;
            uint32_t height = WindowHeight;
            ERHIFormat format = ERHIFormat::B8G8R8A8_UNorm;
            m_Swapchain = RHI::GRHIApi->CreateSwapchain(windowHandle, width, height, format);
            auto frameSize = m_Window->GetFramebufferSize();
            FrameWidth = frameSize.X;
            FrameHeight = frameSize.Y;
        }

        void InitGlobalResources()
        {
            GMeshMaterialShaderMap.Initialize();
        }

        void InitStaticMesh() {
			AssetManager::Get().LoadSync<MaterialAsset>(Core::GetProjectDir() + "/resources/material/DefaultWhite/material.json");

            m_StaticMeshAsset = AssetManager::Get().LoadSync<StaticMeshAsset>(Core::GetProjectDir() + "/resources/glb/sphere.glb");
            m_StaticMeshComponent = new StaticMeshComponent();
            m_StaticMeshComponent->SetStaticMesh(m_StaticMeshAsset->GetMesh());
            m_Scene = GetRenderModuleInstance()->AllocateScene();
            m_Scene->AddPrimitive(m_StaticMeshComponent);
        }

        void InitPipelineState() {
            auto vsfShaderType = ShaderType::GetRegisterMap()[ShaderType::EShaderTypeFlag::MeshMaterial]["StaticMeshMaterialShaderVS"];
            auto psfShaderType = ShaderType::GetRegisterMap()[ShaderType::EShaderTypeFlag::MeshMaterial]["StaticMeshMaterialShaderPS"];
            auto vfType = VertexFactoryType::GetRegisterMap()["LocalVertexFactory"];
            auto vfFlags = m_StaticMeshComponent->GetStaticMesh()->GetRenderData()->GetLODResource(0).VertexFactory->GetVertexFactoryFlags();
            MeshMaterialShaderKey vsKey;
            vsKey.ShaderType = static_cast<MeshMaterialShaderType*>(vsfShaderType);
            vsKey.VF = vfType;
            vsKey.PermutationId = 0;
            vsKey.VertexFactoryFlags = vfFlags;
            vsKey.MaterialParameter.ShadingModel = EShadingModel::Unlit;

            m_VertexShader = GMeshMaterialShaderMap.GetShader(vsKey);

            //设置pixel参数
            MeshMaterialShaderKey psKey;
            psKey.ShaderType = static_cast<MeshMaterialShaderType*>(psfShaderType);
            psKey.VF = vfType;
            psKey.PermutationId = 0;
            psKey.VertexFactoryFlags = vfFlags;
            psKey.MaterialParameter.ShadingModel = EShadingModel::Unlit;
            m_PixelShader = GMeshMaterialShaderMap.GetShader(psKey);


            // 创建光栅化状态
            RHI::RHIRasterizerStateDesc rasterizerDesc;
            rasterizerDesc.PolygonMode = RHI::ERHIPolygonMode::Fill;
            rasterizerDesc.CullMode = RHI::ERHICullMode::None;
            rasterizerDesc.FrontFace = RHI::ERHIFrontFace::Clockwise;
            rasterizerDesc.LineWidth = 1.0f;
            rasterizerDesc.bDepthBiasEnable = false;

            auto rasterizerState = RHIPipelineStateCache::GetOrCreateRasterizerState(rasterizerDesc);





            // 创建深度模板状态
            RHI::RHIDepthStencilStateDesc depthStencilDesc;
            depthStencilDesc.bDepthTestEnable = true;
            depthStencilDesc.bDepthWriteEnable = true;
            depthStencilDesc.DepthCompareOp = RHI::ERHICompareOp::Less;

            auto depthStencilState = RHIPipelineStateCache::GetOrCreateDepthStencilState(depthStencilDesc);

            // 创建图形管线状态
            m_PipelineDesc.ShaderStages.VertexShader = dynamic_cast<RHI::RHIVertexShader*>(m_VertexShader->GetRHIShader());
            m_PipelineDesc.ShaderStages.FragmentShader = dynamic_cast<RHI::RHIFragmentShader*>(m_PixelShader->GetRHIShader());
            // 这里可以设置更多管线配置...
            m_PipelineDesc.RasterizerState = rasterizerState;

            m_PipelineDesc.DepthStencilState = depthStencilState;
            m_PipelineDesc.VertexDescState = m_StaticMeshAsset->GetMesh()->GetRenderData()->LODResources[0].VertexFactory->GetRHIVertexDescState();

            //rendertarget info
            m_PipelineDesc.AttachmentDesc.ColorAttachmentCount = 1;
            m_PipelineDesc.AttachmentDesc.ColorAttachments[0].Format = RHI::ERHIFormat::R8G8B8A8_UNorm;
            m_PipelineDesc.AttachmentDesc.ColorAttachments[0].Actions = ERenderTargetActions::Clear_Store;
            m_PipelineDesc.AttachmentDesc.DepthActions = ERenderTargetActions::Clear_Store;
            m_PipelineDesc.AttachmentDesc.bEnableDepth = true;
            m_PipelineDesc.AttachmentDesc.DepthStencilFormat = RHI::ERHIFormat::D32_Float;



        }

        void Run() override {
            using namespace RHI;
            using namespace RenderCore;

            auto* api = GRHIApi;
            constexpr int kFrameCount = 1000;

            RHI::RHISamplerDesc samplerDesc{};
            SamplerSP = api->CreateSampler(samplerDesc);


            // =========================================
            // 2️⃣ 每帧执行 RDG
            // =========================================
            for (int frame = 0; frame < kFrameCount; ++frame)
            {

                // -------------------------------------
                // 从 pool 取 RT
                // -------------------------------------
                PoolRenderTargetDesc rtDesc{};
                rtDesc.Width = m_TexDesc.Width;
                rtDesc.Height = m_TexDesc.Height;
                rtDesc.Format = m_TexDesc.Format;
                rtDesc.Usage = ERHITextureCreateFlag::RenderTarget |
                    ERHITextureCreateFlag::TransferSrc |
                    ERHITextureCreateFlag::TransferDest;
                rtDesc.DebugName = "ColorTarget";

                auto renderTarget = GRenderTargetPool.GetFreeRenderTarget(rtDesc);
                renderTarget->MarkUsed();
                PoolRenderTargetDesc depthTargetDesc{};
                depthTargetDesc.Width = m_TexDesc.Width;
                depthTargetDesc.Height = m_TexDesc.Height;
                depthTargetDesc.Format = RHI::ERHIFormat::D32_Float;
                depthTargetDesc.Usage = ERHITextureCreateFlag::DepthStencil;
                depthTargetDesc.DebugName = "DepthTarget";

                auto depthRenderTarget = GRenderTargetPool.GetFreeRenderTarget(depthTargetDesc);
                depthRenderTarget->MarkUsed();

                Camera camera;
                camera.SetPosition({ 0.0f, 0.0f, -5.0f });
                camera.SetTarget({ 0.0f, 0.0f, 0.0f });
                camera.SetUp({ 0.0f, 1.0f, 0.0f });
                camera.SetPerspective(Core::DegToRad(45.0f), m_TexDesc.Width / (float)m_TexDesc.Height, 0.1f, 100.0f);
                auto view = camera.GetViewMatrix();
                auto proj = camera.GetProjectionMatrix();
                //绘制场景
                SceneView sceneView;
				sceneView.CameraWorldPos = camera.GetPosition();
                sceneView.ViewMatrix = view;
                sceneView.ProjectionMatrix = proj;
                sceneView.ViewProjectionMatrix = view * proj;
                SceneViewFamily sceneViewFamily;
                sceneViewFamily.Scene = m_Scene;
                sceneViewFamily.AddView(sceneView);
                auto meshSceneProxy = m_StaticMeshComponent->CreateSceneProxy();
                MeshBatchList list;
                std::vector<Engine::StaticMeshProxy*> meshs;
                meshs.push_back(dynamic_cast<Engine::StaticMeshProxy*>(meshSceneProxy));
                Scene* s = dynamic_cast<Scene*>(m_Scene);
                Renderer::StaticMeshDrawBuild(s, sceneView, list);
                // -------------------------------------
                // RDG 构建
                // -------------------------------------
                RenderGraphBuilder builder;

                for (auto& batch : list) {
					auto materialOwner = batch.MaterialProxy->GetParent();
                    
                    //颜色混合
                    if (materialOwner->GetBlendMode() == EBlendMode::Opaque)
                    {
                        RHI::RHIColorBlendStateDesc blendDesc;
                        // 创建颜色混合状态
                        RHI::RHIColorBlendAttachmentDesc blendAttachDesc;
                        blendAttachDesc.bBlendEnable = false;
                        std::vector<RHI::RHIColorBlendAttachmentDesc> attachments = { blendAttachDesc };
                        blendDesc.Attachments = attachments;
                        auto colorBlendState = RHIPipelineStateCache::GetOrCreateColorBlendState(blendDesc);
                        m_PipelineDesc.ColorBlendState = colorBlendState;
                    }
                    else {
                        RHI::RHIColorBlendStateDesc blendDesc;
                        // 创建颜色混合状态
                        RHI::RHIColorBlendAttachmentDesc blendAttachDesc;
                        blendAttachDesc.bBlendEnable = true;
                        std::vector<RHI::RHIColorBlendAttachmentDesc> attachments = { blendAttachDesc };
                        blendDesc.Attachments = attachments;
                        auto colorBlendState = RHIPipelineStateCache::GetOrCreateColorBlendState(blendDesc);
                        m_PipelineDesc.ColorBlendState = colorBlendState;
                    }
                    auto pipeline = RHIPipelineStateCache::GetOrCreateGraphicsPipelineState(m_PipelineDesc);
                    auto state = pipeline;
                    // 参数
                    

                    BEGIN_SHADER_PARAMETER_STRUCT(PassParameters)
                        SHADER_PARAMETER_STRUCT_REFERENCE(StaticMeshMaterialShaderVSParameters, vertexParameters)
                        SHADER_PARAMETER_STRUCT_REFERENCE(StaticMeshMaterialShaderPSParameters, pixelParameters)
                    END_SHADER_PARAMETER_STRUCT(PassParameters)
                    auto* params = builder.AllocateParameter<PassParameters>();
                    params->vertexParameters.vertexFactoryParameters.CameraWorldPosition = sceneView.CameraWorldPos;
                    params->vertexParameters.vertexFactoryParameters.ViewProjection = sceneView.ViewProjectionMatrix;
                    params->vertexParameters.vertexFactoryParameters.ViewProjection = Core::Float4x4::Identity();
					params->vertexParameters.vertexFactoryParameters.LocalToWorld = meshSceneProxy->GetLocalToWorld();
                    params->vertexParameters.vertexFactoryParameters.LocalToWorld = Core::Float4x4::Identity();
                    params->pixelParameters.vertexFactoryParameters.CameraWorldPosition = sceneView.CameraWorldPos;
                    params->pixelParameters.vertexFactoryParameters.ViewProjection = sceneView.ViewProjectionMatrix;
                    params->pixelParameters.vertexFactoryParameters.ViewProjection = Core::Float4x4::Identity();
                    params->pixelParameters.vertexFactoryParameters.LocalToWorld = meshSceneProxy->GetLocalToWorld();
                    params->pixelParameters.vertexFactoryParameters.LocalToWorld = Core::Float4x4::Identity();
                    auto rdgDst = builder.RegisterExternalTexture("DstTex", renderTarget.get());
                    auto depthRdgDst = builder.RegisterExternalTexture("DstDepthTex", depthRenderTarget.get());
                    params->pixelParameters.renderTargetSlots.NumColorRenderTargets = 1;
                    params->pixelParameters.renderTargetSlots[0].Texture = rdgDst;
                    params->pixelParameters.renderTargetSlots.DepthStencil.Texture = depthRdgDst;
                    


                    // -------------------------------------
                    // 添加 pass
                    // -------------------------------------
                    builder.AddPass<PassParameters>(
                        "BlitPass",
                        PassParameters::GetMetaData(),
                        params,
                        EPassFlag::Graphic,
                        [=](RHI::RHICommandListBase& rhiCmdList)
                        {
                            auto& cmd = static_cast<RHI::RHIGraphicCommandList&>(rhiCmdList);
                            PassParameters* passParams = params;



                            cmd.SetGraphicPipelineState(state);
                            //绑定vertexfactory
                            batch.VertexFactory->Bind(cmd);
                            cmd.SetViewport(0, 0, FrameWidth, FrameHeight, 0.0f, 1.0f);
                            cmd.SetScissor(0, 0, FrameWidth, FrameHeight);
                            //设置vertex参数
                            SetShaderParameters(cmd, m_VertexShader, &passParams->vertexParameters);

                            //设置pixel参数
                            SetShaderParameters(cmd, m_PixelShader, &passParams->pixelParameters);
                            auto boundRenderTarget = params->pixelParameters.renderTargetSlots.GetBoundRenderTarget();
                            RHIRenderPassInfo passInfo;
                            passInfo.RenderTargets = boundRenderTarget;
							passInfo.RenderTargets.DepthStencil.ClearBinding.Depth = 1.0f;
                            passInfo.RenderArea.Width = FrameWidth;
                            passInfo.RenderArea.Height = FrameHeight;
                            cmd.BeginRenderPass(passInfo);
                            //绘制
                            for (auto element : batch.Elements) {
                                cmd.DrawIndexed(batch.IndexBuffer->GetRHI(), element.NumIndices, batch.InstanceDataIds.size(), element.FirstIndex, element.BaseVertexIndex, batch.StartInstance);
                            }
                            cmd.EndRenderPass();
                        }
                    );
                }
                // -------------------------------------
                // 执行 RDG
                // -------------------------------------
                builder.Execute();

                // =========================================
                // 3️⃣ Blit 到 swapchain
                // =========================================
                auto slot = m_Swapchain->AcquireNextSlot();
                auto* backTexture = slot.Texture;

                auto* computeQueue = api->GetQueue(EQueueType::Compute);
                auto* ctx = computeQueue->AcquireCommandContext();
                RHIComputeCommandList cmd(dynamic_cast<RHIComputeContex*>(ctx));
                cmd.SetImmediate(true);
                cmd.Begin();

                TransitionResource(api, cmd, renderTarget->GetRHI(), renderTarget->GetTracker().GetSubresourceAccess(RHI::RHISubresourceRange{}), ERHIResourceAccess::TransferSrc);


                TransitionResource(api, cmd, backTexture, ERHIResourceAccess::TransferDest);
                RHI::RHIBlitTextureDesc blit{};
                blit.SrcRegion.Width = m_TexDesc.Width;
                blit.SrcRegion.Height = m_TexDesc.Height;
                blit.DstRegion.Width = FrameWidth;
                blit.DstRegion.Height = FrameHeight;
                cmd.BlitTexture(renderTarget->GetRHI(), backTexture, blit);

                TransitionResource(api, cmd, backTexture, ERHIResourceAccess::Present);

                cmd.End();
                std::vector<RHI::RHIWaitInfo> waitInfos;

                RHI::RHIWaitInfo renderTargetFinish;
                renderTargetFinish.QueueType = renderTarget->GetTracker().GetLastAccessFence().QueueType;
                renderTargetFinish.Value = renderTarget->GetTracker().GetLastAccessFence().Value;
                waitInfos.push_back(renderTargetFinish);
                if (slot.ReadySync)
                {
                    waitInfos.push_back({ slot.ReadySync, EQueueType::Graphics, 0,RHI::ERHIPipelineStage::ColorAttachmentOutput });
                }
                RHI::RHIFence fence;
                fence.QueueType = EQueueType::Compute;
                fence.Value = computeQueue->ExecuteContext({ ctx }, waitInfos);

                // 更新 tracker（关键！）
                renderTarget->GetTracker().UpdateLastAccessFence(fence);
                renderTarget->GetTracker().UpdateSubresourceAccess(RHI::RHISubresourceRange{}, ERHIResourceAccess::TransferSrc);
                RHI::RHIWaitInfo presentWait;
                presentWait.QueueType = fence.QueueType;
                presentWait.Value = fence.Value;
                api->GetPresentExecutor()->Present(m_Swapchain.get(), { presentWait });
                renderTarget->MarkUsed(false);
                depthRenderTarget->MarkUsed(false);
            }

            std::cout << "RenderGraph CopyTexturePass test finished for " << kFrameCount << " frames." << std::endl;
        }

        void Teardown() override {
            // 资源清理如有需要可补充
            // 先等 worker 把两条队列所有已提交命令（含 Present）执行完，
            // 再销毁任何 GPU 对象——命令 lambda 捕获的是裸指针，等不到
            // 这一步就清理，worker 还在跑就会读到已析构的 shader/pipeline。
            {
                auto* graphicsQueue = RHI::GRHIApi->GetQueue(EQueueType::Graphics);
                auto* computeQueue = RHI::GRHIApi->GetQueue(EQueueType::Compute);
                if (graphicsQueue) graphicsQueue->WaitIdle();
                if (computeQueue) computeQueue->WaitIdle();
            }
            GMeshMaterialShaderMap.Clear();
            RHIPipelineStateCache::ClearAll();
            GRenderTargetPool.Clear();
            m_Swapchain.reset();
            m_Window.reset();
            SamplerSP.reset();
            RHI::GRHIApi->Shutdown();
            delete RHI::GRHIApi;
        }

    private:
        RenderGraphTextureDesc m_TexDesc;
        RHI::RHISwapchainSP m_Swapchain;
        SlateCore::WindowSP m_Window;
        RenderCore::Shader* m_BlitTextureShader0;
        std::shared_ptr<StaticMeshAsset> m_StaticMeshAsset;
        StaticMeshComponent* m_StaticMeshComponent;
        Engine::SceneInterface* m_Scene;
        RHI::RHIGraphicsPipelineStateDesc m_PipelineDesc;
        Shader* m_VertexShader;
        Shader* m_PixelShader;

    };

    REGISTER_RENDER_TEST("StaticMeshRenderTest", StaticMeshRenderTest);
}
