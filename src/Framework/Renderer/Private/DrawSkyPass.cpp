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
        depthStencilDesc.DepthCompareOp = RHI::ERHICompareOp::LessOrEqual;

        auto depthStencilState = RHIPipelineStateCache::GetOrCreateDepthStencilState(depthStencilDesc);
        RHI::RHIGraphicsPipelineStateDesc pipelineDesc;
        // 创建图形管线状态
        pipelineDesc.ShaderStages.VertexShader = dynamic_cast<RHI::RHIVertexShader*>(vs->GetRHIShader());
        pipelineDesc.ShaderStages.FragmentShader = dynamic_cast<RHI::RHIFragmentShader*>(ps->GetRHIShader());
        // 这里可以设置更多管线配置...
        pipelineDesc.RasterizerState = rasterizerState;

        pipelineDesc.DepthStencilState = depthStencilState;
        RHI::RHIVertexDescStateDesc desc;

        //
        // 创建 Binding Descriptions
        //

        RHI::RHIVertexBindingDesc bindingDesc;
        bindingDesc.Binding = 0;
        bindingDesc.Stride = sizeof(float) * 3;
        bindingDesc.InputRate = ERHIInputRate::PerVertex;

        desc.Bindings.push_back(bindingDesc);


        //
        // 创建 Attribute Descriptions
        //
        RHI::RHIVertexAttributeDesc attrDesc;

        attrDesc.Location = 0;
        attrDesc.Binding = 0;
        attrDesc.Offset = 0;
        attrDesc.Format = ERHIFormat::R32G32B32_Float;

        desc.Attributes.push_back(attrDesc);
        auto vertexDescState = RHIPipelineStateCache::GetOrCreateVertexDescState(desc);
        pipelineDesc.VertexDescState = vertexDescState;

        RHI::RHIColorBlendStateDesc blendDesc;
        // 创建颜色混合状态
        RHI::RHIColorBlendAttachmentDesc blendAttachDesc;
        blendAttachDesc.bBlendEnable = false;
        std::vector<RHI::RHIColorBlendAttachmentDesc> attachments = { blendAttachDesc };
        blendDesc.Attachments = attachments;
        auto colorBlendState = RHIPipelineStateCache::GetOrCreateColorBlendState(blendDesc);
        pipelineDesc.ColorBlendState = colorBlendState;

        //rendertarget info
        pipelineDesc.AttachmentDesc.ColorAttachmentCount = 1;
        pipelineDesc.AttachmentDesc.ColorAttachments[0].Format = RHI::ERHIFormat::R8G8B8A8_UNorm;
        pipelineDesc.AttachmentDesc.ColorAttachments[0].Actions = ERenderTargetActions::Load_Store;
        pipelineDesc.AttachmentDesc.DepthActions = ERenderTargetActions::Load_Store;
        pipelineDesc.AttachmentDesc.bEnableDepth = true;
        pipelineDesc.AttachmentDesc.DepthStencilFormat = RHI::ERHIFormat::D32_Float;
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
        // Scene-view extensions may depth-test their geometry after the sky.
        params->psParam.renderTargetSlots.DepthStencil.DepthAction = ERenderTargetActions::Load_Store;

        // -------------------------------------
        // 添加 pass
        // -------------------------------------
        builder.AddPass<DrawSkyPassParameters>(
            "StaticMeshDrawPass",
            DrawSkyPassParameters::GetMetaData(),
            params,
            RenderCore::EPassFlag::Graphic,
            [=](RHI::RHICommandListBase& rhiCmdList)
            {
                auto& cmd = static_cast<RHI::RHIGraphicCommandList&>(rhiCmdList);
                DrawSkyPassParameters* passParams = params;

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
