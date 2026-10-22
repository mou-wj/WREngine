#include "DrawSkyPass.h"
#include "RHIPipelineStateCache.h"
#include "RenderGraphBuilder.h"
#include "Shape.h"
#include "Transform.hpp"

namespace Renderer {
    IMPLEMENT_GLOBAL_SHADER_TYPE(
        DrawSkyVS,
        "DrawSkyVS",
        "/tools/DrawSkyVS.sf",
        "MainVS",
        RHI::ERHIShaderFrequency::Vertex
    );

    IMPLEMENT_GLOBAL_SHADER_TYPE(
        DrawSkyPS,
        "DrawSkyPS",
        "/tools/DrawSkyPS.sf",
        "MainPS",
        RHI::ERHIShaderFrequency::Fragment
    );


    void AddDrawSkyBoxPass(RenderCore::RenderGraphBuilder& builder, RenderCore::RenderTexture* envMap, RenderCore::RenderGraphTexture* colorAttachment, RenderCore::RenderGraphTexture* depthAttachment, Engine::SceneView& view)
    {
        auto vs = RenderCore::GShaderMap.GetShader<Renderer::DrawSkyVS>(0);
        auto ps = RenderCore::GShaderMap.GetShader<Renderer::DrawSkyPS>(0);
        // 创建光栅化状态
        RHI::RHIRasterizerStateDesc rasterizerDesc;
        rasterizerDesc.polygonMode = RHI::ERHIPolygonMode::Fill;
        rasterizerDesc.cullMode = RHI::ERHICullMode::None;
        rasterizerDesc.frontFace = RHI::ERHIFrontFace::Clockwise;
        rasterizerDesc.lineWidth = 1.0f;
        rasterizerDesc.depthBiasEnable = false;

        auto rasterizerState = RHIPipelineStateCache::GetOrCreateRasterizerState(rasterizerDesc);





        // 创建深度模板状态
        RHI::RHIDepthStencilStateDesc depthStencilDesc;
        depthStencilDesc.depthTestEnable = true;
        depthStencilDesc.depthWriteEnable = true;
        depthStencilDesc.depthCompareOp = RHI::ERHICompareOp::LessOrEqual;

        auto depthStencilState = RHIPipelineStateCache::GetOrCreateDepthStencilState(depthStencilDesc);
        RHI::RHIGraphicsPipelineStateDesc pipelineDesc;
        // 创建图形管线状态
        pipelineDesc.shaderStages.vertexShader = dynamic_cast<RHI::RHIVertexShader*>(vs->GetRHIShader());
        pipelineDesc.shaderStages.fragmentShader = dynamic_cast<RHI::RHIFragmentShader*>(ps->GetRHIShader());
        // 这里可以设置更多管线配置...
        pipelineDesc.rasterizerState = rasterizerState;

        pipelineDesc.depthStencilState = depthStencilState;
        RHI::RHIVertexDescStateDesc Desc;

        //
        // 创建 Binding Descriptions
        //

        RHI::RHIVertexBindingDesc BindingDesc;
        BindingDesc.binding = 0;
        BindingDesc.stride = sizeof(float) * 3;
        BindingDesc.inputRate = ERHIInputRate::PerVertex;

        Desc.bindings.push_back(BindingDesc);


        //
        // 创建 Attribute Descriptions
        //
        RHI::RHIVertexAttributeDesc AttrDesc;

        AttrDesc.location = 0;
        AttrDesc.binding = 0;
        AttrDesc.offset = 0;
        AttrDesc.format = ERHIFormat::R32G32B32_Float;

        Desc.attributes.push_back(AttrDesc);
        auto vertexDescState = RHIPipelineStateCache::GetOrCreateVertexDescState(Desc);
        pipelineDesc.vertexDescState = vertexDescState;

        RHI::RHIColorBlendStateDesc blendDesc;
        // 创建颜色混合状态
        RHI::RHIColorBlendAttachmentDesc blendAttachDesc;
        blendAttachDesc.blendEnable = false;
        std::vector<RHI::RHIColorBlendAttachmentDesc> attachments = { blendAttachDesc };
        blendDesc.attachments = attachments;
        auto colorBlendState = RHIPipelineStateCache::GetOrCreateColorBlendState(blendDesc);
        pipelineDesc.colorBlendState = colorBlendState;

        //rendertarget info
        pipelineDesc.attachmentDesc.colorAttachmentCount = 1;
        pipelineDesc.attachmentDesc.colorAttachments[0].format = RHI::ERHIFormat::R8G8B8A8_UNorm;
        pipelineDesc.attachmentDesc.colorAttachments[0].actions = ERenderTargetActions::Load_Store;
        pipelineDesc.attachmentDesc.depthActions = ERenderTargetActions::Load_DontCare;
        pipelineDesc.attachmentDesc.enableDepth = true;
        pipelineDesc.attachmentDesc.depthStencilFormat = RHI::ERHIFormat::D32_Float;
        auto pipeline = RHIPipelineStateCache::GetOrCreateGraphicsPipelineState(pipelineDesc);
        BEGIN_SHADER_PARAMETER_STRUCT(DrawSkyPassParameters)
            SHADER_PARAMETER_STRUCT_REFERENCE(DrawSkyVertexParameters, vsParam)
            SHADER_PARAMETER_STRUCT_REFERENCE(DrawSkyPixelParameters, psParam)
        END_SHADER_PARAMETER_STRUCT(DrawSkyPassParameters);
        auto* params = builder.AllocateParameter<DrawSkyPassParameters>();


        auto skyboxEnvMap = builder.RegisterExternalTexture("SkyboxEnvMap", envMap);
        params->vsParam.View = view.ViewMatrix;

        auto project = Core::PerspectiveRhNo(
            Core::DegToRad(90.f),
            1,
            0.1,
            100);
        params->vsParam.Projection = project;
        //params->vertexFactoryParameters.LocalToWorld = meshSceneProxy->GetLocalToWorld();
        params->psParam.SkyCubemap = skyboxEnvMap;
        params->psParam.SkySampler = RenderCore::GlobalSampler.get();
        params->psParam.renderTargetSlots.NumColorRenderTargets = 1;
        params->psParam.renderTargetSlots.ColorRenderTargets[0].Action = ERenderTargetActions::Load_Store;
        params->psParam.renderTargetSlots[0].Texture = colorAttachment;
        params->psParam.renderTargetSlots.DepthStencil.Texture = depthAttachment;
        params->psParam.renderTargetSlots.DepthStencil.DepthAction = ERenderTargetActions::Load_DontCare;

        // -------------------------------------
        // 添加 pass
        // -------------------------------------
        builder.AddPass<DrawSkyPassParameters>(
            "StaticMeshDrawPass",
            DrawSkyPassParameters::GetMetaData(),
            params,
            RenderCore::EPassFlag::Graphic,
            [=](RHI::RHICommandListBase& RHICmdList)
            {
                auto& cmd = static_cast<RHI::RHIGraphicCommandList&>(RHICmdList);
                DrawSkyPassParameters* Params = params;

                cmd.SetGraphicPipelineState(pipeline);
                //绑定vertexfactory
                auto vertexbuffer = Engine::GStaticMesh_Cube->GetLODResource(0).VertexBuffers.PositionBuffer.Buffer.get();
                auto indexbuffer = Engine::GStaticMesh_Cube->GetLODResource(0).IndexBuffer.Buffer.get();
                cmd.SetStreamSource(0, vertexbuffer->GetRHI(), 0);
                cmd.SetViewport(view.Viewport.X, view.Viewport.Y, view.Viewport.Width, view.Viewport.Height, 0.0f, 1.0f);
                cmd.SetScissor(view.Viewport.X, view.Viewport.Y, view.Viewport.Width, view.Viewport.Height);
                //设置vertex参数
                SetShaderParameters(cmd, vs, &params->vsParam);

                //设置pixel参数
                SetShaderParameters(cmd, ps, &params->psParam);
                auto boundRenderTarget = params->psParam.renderTargetSlots.GetBoundRenderTarget();
                RHIRenderPassInfo passInfo;
                passInfo.RenderTargets = boundRenderTarget;
                passInfo.RenderTargets.DepthStencil.ClearBinding.Depth = 1.0f;
                passInfo.RenderArea.X = view.Viewport.X;
                passInfo.RenderArea.Y = view.Viewport.Y;
                passInfo.RenderArea.Width = view.Viewport.Width;
                passInfo.RenderArea.Height = view.Viewport.Height;
                cmd.BeginRenderPass(passInfo);
                //绘制
                cmd.DrawIndexed(indexbuffer->GetRHI(), 36);
                cmd.EndRenderPass();
            }
        );
    }


}
