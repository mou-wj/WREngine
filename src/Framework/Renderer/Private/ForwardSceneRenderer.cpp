#include "ForwardSceneRenderer.h"
#include "ScreenPass.h"
#include "StaticMeshProcess.h"
#include "StaticMeshProxy.h"
#include "LocalVertexFactory.h"
#include "RHIPipelineStateCache.h"
#include "StaticMeshMaterialShader.h"
#include "DrawSkyPass.h"
#include "Scene.h"
#include <iostream>
using namespace RenderCore;
using namespace Engine;
namespace Renderer {

    void ForwardSceneRenderer::Build(RenderCore::RenderGraphBuilder& builder)
    {
        auto SceneColorTargetTexture = SceneTextures.SceneColor;
        //ClearRenderTarget
		RenderTargetBindingSlots renderTargetBindingSlots;
        renderTargetBindingSlots.NumColorRenderTargets = 1;
        renderTargetBindingSlots[0].Texture = SceneTextures.SceneColor;
        renderTargetBindingSlots.DepthStencil.Texture = SceneTextures.SceneDepth;
        AddClearRenderTargetsPass(builder, renderTargetBindingSlots);

        auto sceneEnvMap = Scene->GetGPUResourceInfo().LightResourceInfo.IBLSpecularTexture;
        //��renderer���Ƶ�DefferedOutputColor
        //��ȡ����static mesh primitive
		const auto& views = Views->GetViews();
        for (auto view : views) {
            UpdateCascadeShadowInfo(Scene,builder, view);
            MeshBatchList DrawMeshBatches;
            StaticMeshDrawBuild(Scene,view, DrawMeshBatches);
            for (auto MeshBatch : DrawMeshBatches) {
                auto vsfShaderType = ShaderType::GetRegisterMap()[ShaderType::EShaderTypeFlag::MeshMaterial]["StaticMeshMaterialShaderVS"];
                auto psfShaderType = ShaderType::GetRegisterMap()[ShaderType::EShaderTypeFlag::MeshMaterial]["StaticMeshMaterialShaderPS"];
                auto vfType = VertexFactoryType::GetRegisterMap()["LocalVertexFactory"];
                auto vfFlags = MeshBatch.VertexFactory->GetVertexFactoryFlags();
                bool supportInstance = MeshBatch.InstanceDataIds.size() > 1;
                if (supportInstance) {
                    LocalVertexFactoryFeatureFlags flags;
                    flags.PackedFlags= vfFlags;
					flags.SupportsInstanceData = true;
                    vfFlags = flags.PackedFlags;
                }
                auto shdingMode = MeshBatch.MaterialProxy->GetParent()->GetShadingModel();
                shdingMode = EShadingModel::Lit;
            
                MeshMaterialShaderKey vsKey;
                vsKey.ShaderType = static_cast<MeshMaterialShaderType*>(vsfShaderType);
                vsKey.VF = vfType;
                vsKey.PermutationId = 0;
                vsKey.VertexFactoryFlags = vfFlags;
                vsKey.MaterialParameter.ShadingModel = shdingMode;
            
                auto vertexShader = GMeshMaterialShaderMap.GetShader(vsKey);
            
                //����pixel����
                MeshMaterialShaderKey psKey;
                psKey.ShaderType = static_cast<MeshMaterialShaderType*>(psfShaderType);
                psKey.VF = vfType;
                psKey.PermutationId = 0;
                psKey.VertexFactoryFlags = vfFlags;
                psKey.MaterialParameter.ShadingModel = shdingMode;
                auto pixelShader = GMeshMaterialShaderMap.GetShader(psKey);
            
            
                // ������դ��״̬
                RHI::RHIRasterizerStateDesc rasterizerDesc;
                rasterizerDesc.PolygonMode = RHI::ERHIPolygonMode::Fill;
                rasterizerDesc.CullMode = RHI::ERHICullMode::Back;
                rasterizerDesc.FrontFace = MeshBatch.FrontFace;
                rasterizerDesc.LineWidth = 1.0f;
                rasterizerDesc.bDepthBiasEnable = false;
            
                auto rasterizerState = RHIPipelineStateCache::GetOrCreateRasterizerState(rasterizerDesc);
            
            
            
            
            
                // �������ģ��״̬
                RHI::RHIDepthStencilStateDesc depthStencilDesc;
                depthStencilDesc.bDepthTestEnable = true;
                depthStencilDesc.bDepthWriteEnable = true;
                depthStencilDesc.DepthCompareOp = RHI::ERHICompareOp::Less;
            
                auto depthStencilState = RHIPipelineStateCache::GetOrCreateDepthStencilState(depthStencilDesc);
                RHI::RHIGraphicsPipelineStateDesc pipelineDesc;
                // ����ͼ�ι���״̬
                pipelineDesc.ShaderStages.VertexShader = dynamic_cast<RHI::RHIVertexShader*>(vertexShader->GetRHIShader());
                pipelineDesc.ShaderStages.FragmentShader = dynamic_cast<RHI::RHIFragmentShader*>(pixelShader->GetRHIShader());
                // ����������ø����������...
                pipelineDesc.RasterizerState = rasterizerState;
            
                pipelineDesc.DepthStencilState = depthStencilState;
                pipelineDesc.VertexDescState = MeshBatch.VertexFactory->GetRHIVertexDescState();
                if (supportInstance) {
                    LocalVertexFactory* vf = dynamic_cast<LocalVertexFactory*>(MeshBatch.VertexFactory);
                    pipelineDesc.VertexDescState = vf->GetRHIInstancedVertexDescState();
                }

                //rendertarget info
                pipelineDesc.AttachmentDesc.ColorAttachmentCount = 1;
                pipelineDesc.AttachmentDesc.ColorAttachments[0].Format = RHI::ERHIFormat::R8G8B8A8_UNorm;
                pipelineDesc.AttachmentDesc.ColorAttachments[0].Actions = ERenderTargetActions::Load_Store;
                pipelineDesc.AttachmentDesc.DepthActions = ERenderTargetActions::Load_Store;
                pipelineDesc.AttachmentDesc.bEnableDepth = true;
                pipelineDesc.AttachmentDesc.DepthStencilFormat = RHI::ERHIFormat::D32_Float;
            
                BEGIN_SHADER_PARAMETER_STRUCT(PassParameters)
                    SHADER_PARAMETER_STRUCT_REFERENCE(StaticMeshMaterialShaderVSParameters, vertexParameters)
                    SHADER_PARAMETER_STRUCT_REFERENCE(StaticMeshMaterialShaderPSParameters, pixelParameters)
                END_SHADER_PARAMETER_STRUCT(PassParameters)
                auto* params = builder.AllocateParameter<PassParameters>();
                if (shdingMode == EShadingModel::Lit) {
                    //�������Լ�������Ϣ
                    BuildShaderParameters(Scene, builder, params->pixelParameters.Scene);
                    BuildShaderParameters(MeshBatch.MaterialProxy, builder, params->pixelParameters.Material);
                }
            
				auto& batch = MeshBatch;
                auto materialOwner = batch.MaterialProxy->GetParent();
            
                //��ɫ���
                if (materialOwner->GetBlendMode() == EBlendMode::Opaque)
                {
                    RHI::RHIColorBlendStateDesc blendDesc;
                    // ������ɫ���״̬
                    RHI::RHIColorBlendAttachmentDesc blendAttachDesc;
                    blendAttachDesc.bBlendEnable = false;
                    std::vector<RHI::RHIColorBlendAttachmentDesc> attachments = { blendAttachDesc };
                    blendDesc.Attachments = attachments;
                    auto colorBlendState = RHIPipelineStateCache::GetOrCreateColorBlendState(blendDesc);
                    pipelineDesc.ColorBlendState = colorBlendState;
                }
                else {
                    RHI::RHIColorBlendStateDesc blendDesc;
                    // ������ɫ���״̬
                    RHI::RHIColorBlendAttachmentDesc blendAttachDesc;
                    blendAttachDesc.bBlendEnable = true;
                    std::vector<RHI::RHIColorBlendAttachmentDesc> attachments = { blendAttachDesc };
                    blendDesc.Attachments = attachments;
                    auto colorBlendState = RHIPipelineStateCache::GetOrCreateColorBlendState(blendDesc);
                    pipelineDesc.ColorBlendState = colorBlendState;
                }
                auto pipeline = RHIPipelineStateCache::GetOrCreateGraphicsPipelineState(pipelineDesc);
                
                // ����
            
            
                params->vertexParameters.vertexFactoryParameters.CameraWorldPosition = view.CameraWorldPos;
                params->vertexParameters.vertexFactoryParameters.ViewProjection = view.ViewProjectionMatrix;
                params->vertexParameters.vertexFactoryParameters.LocalToWorld = batch.LocalToWorld;
                params->pixelParameters.vertexFactoryParameters.CameraWorldPosition = view.CameraWorldPos;
                params->pixelParameters.vertexFactoryParameters.ViewProjection = view.ViewProjectionMatrix;
                params->pixelParameters.vertexFactoryParameters.LocalToWorld = batch.LocalToWorld;
                if (supportInstance) {
                    params->vertexParameters.vertexFactoryParameters.LocalVFInstanceInfo.InstanceData = batch.InstanceDataBufferSRV;
                    params->pixelParameters.vertexFactoryParameters.LocalVFInstanceInfo.InstanceData = batch.InstanceDataBufferSRV;
                }
                params->pixelParameters.renderTargetSlots.NumColorRenderTargets = 1;
                params->pixelParameters.renderTargetSlots[0].Texture = SceneTextures.SceneColor;
                params->pixelParameters.renderTargetSlots[0].Action = ERenderTargetActions::Load_Store;
                params->pixelParameters.renderTargetSlots.DepthStencil.Texture = SceneTextures.SceneDepth;
                params->pixelParameters.renderTargetSlots.DepthStencil.DepthAction = ERenderTargetActions::Load_Store;
            
                // -------------------------------------
                // ���� pass
                // -------------------------------------
                builder.AddPass<PassParameters>(
                    "StaticMeshDrawPass",
                    PassParameters::GetMetaData(),
                    params,
                    EPassFlag::Graphic,
                    [=](RHI::RHICommandListBase& RHICmdList)
                    {
                        auto& cmd = static_cast<RHI::RHIGraphicCommandList&>(RHICmdList);
                        PassParameters* materialParams = params;
            
                        cmd.SetGraphicPipelineState(pipeline);
                        //��vertexfactory
                        batch.VertexFactory->Bind(cmd);
                        if (supportInstance) {
                            LocalVertexFactory* vf = dynamic_cast<LocalVertexFactory*>(MeshBatch.VertexFactory);
                            vf->BindInstanceBuffer(cmd,MeshBatch.InstanceDataBufferAccessor->GetInstanceIdBuffer()->GetRHI(), 0);
                        }
                        cmd.SetViewport(view.Viewport.X, view.Viewport.Y, view.Viewport.Width, view.Viewport.Height, 0.0f, 1.0f);
                        cmd.SetScissor(view.Viewport.X, view.Viewport.Y, view.Viewport.Width, view.Viewport.Height);
                        //����vertex����
                        SetShaderParameters(cmd, vertexShader, &params->vertexParameters);
            
                        //����pixel����
                        SetShaderParameters(cmd, pixelShader, &params->pixelParameters);
                        auto boundRenderTarget = params->pixelParameters.renderTargetSlots.GetBoundRenderTarget();
                        RHIRenderPassInfo passInfo;
                        passInfo.RenderTargets = boundRenderTarget;
                        passInfo.RenderTargets.DepthStencil.ClearBinding.Depth = 1.0f;
						passInfo.RenderArea.X = view.Viewport.X;
                        passInfo.RenderArea.Y = view.Viewport.Y;
                        passInfo.RenderArea.Width = view.Viewport.Width;
                        passInfo.RenderArea.Height = view.Viewport.Height;
                        cmd.BeginRenderPass(passInfo);
                        //����
                        for (auto element : batch.Elements) {
                            cmd.DrawIndexed(batch.IndexBuffer->GetRHI(), element.NumIndices, batch.InstanceDataIds.size(), element.FirstIndex, element.BaseVertexIndex, batch.StartInstance);
                        }
                        cmd.EndRenderPass();
                    }
                );
                
            
            
            }
            if (sceneEnvMap != nullptr) {
                AddDrawSkyBoxPass(builder, sceneEnvMap, SceneTextures.SceneColor, SceneTextures.SceneDepth, view);
            }
        }
    }
}