#include "SceneRenderer.h"
#include "StaticMeshMaterialShader.h"
#include "LightSceneProxy.h"
#include "Transform.hpp"
#include "StaticMeshProcess.h"
#include "StaticMeshProxy.h"
#include "MaterialCore.h"
#include "RHIPipelineStateCache.h"
#include "SceneShaderParameters.h"
#include "Log.h"
using namespace RenderCore;
using namespace Engine;
namespace Renderer {
    
    void SceneRenderer::AddClearRenderTargetsPass(RenderCore::RenderGraphBuilder& bulder, RenderCore::RenderTargetBindingSlots& renderTargetBindingSlots)
    {
        BEGIN_SHADER_PARAMETER_STRUCT(ClearTargetParameters)
            SHADER_PARAMETER_RENDER_TARGET_BINDING_SLOTS(renderTargetSlots)
        END_SHADER_PARAMETER_STRUCT(ClearTargetParameters)
        auto params = bulder.AllocateParameter<ClearTargetParameters>();
        params->renderTargetSlots = renderTargetBindingSlots;
        bulder.AddPass<ClearTargetParameters>(
            "ClearTargetPass",
            ClearTargetParameters::GetMetaData(),
            params,
            EPassFlag::Graphic,
            [=](RHI::RHICommandListBase& rhiCmdList)
            {
                auto& cmd = static_cast<RHI::RHIGraphicCommandList&>(rhiCmdList);
                auto boundRenderTarget = params->renderTargetSlots.GetBoundRenderTarget();
                RHIRenderPassInfo passInfo;
                passInfo.RenderTargets = boundRenderTarget;
                passInfo.RenderTargets.DepthStencil.ClearBinding.Depth = 1.0f;
                passInfo.RenderArea.Width = boundRenderTarget.Dimensions.X;
                passInfo.RenderArea.Height = boundRenderTarget.Dimensions.Y;

                cmd.BeginRenderPass(passInfo);
                cmd.SetViewport(
                    0,
                    0,
                    boundRenderTarget.Dimensions.X,
                    boundRenderTarget.Dimensions.Y,
                    0.0f,
                    1.0f);
                cmd.SetScissor(0, 0, boundRenderTarget.Dimensions.X, boundRenderTarget.Dimensions.Y);
                cmd.EndRenderPass();
            });

    }
    void SceneRenderer::BuildSceneLightShadowMap(Renderer::Scene* scene, RenderCore::RenderGraphBuilder& builder)
    {
        std::vector<ShadowRenderView> shadowRenderViews;
        scene->ForEachLight(
            [&scene, &shadowRenderViews,&builder](Engine::LightSceneProxy* light)
            {
                if (!light)
                    return;

                if (!light->IsCastShadow())
                    return;
                if (!light->bUpdateCastShadow)
                    return;
                light->bUpdateCastShadow = false;
                auto lightType = light->GetLightType();
                auto& allocator = scene->GetShadowMapAllocator();

                switch (lightType)
                {
                    //---------------------------------------------------
                    // Spot Light
                    //---------------------------------------------------
                case Engine::ELightType::Spot:
                {
                    
                    auto* spotLight =
                        dynamic_cast<Engine::SpotLightSceneProxy*>(light);

                    if (!spotLight)
                        return;
                    ShadowAllocation allocation;

                    LightShadowInfo& shadowInfo = scene->GetLightShadowInfo(light);
                    if (!shadowInfo.Allocation.IsValid()) {
                        allocation = allocator.AllocateSpotShadow(1024);
                        shadowInfo.Allocation = allocation;
                        if (!allocation.IsValid())
                            return;
                    }
                    else {
                        allocation = shadowInfo.Allocation;
                    }
                    ShadowRenderView view;
                    auto rdgT = builder.RegisterExternalTexture(std::string("ShadowPassTarget") + std::to_string((uint64_t)allocation.Texture), allocation.Texture);
                    view.Allocation.Texture = rdgT;
                    view.Allocation.X = allocation.Slices[0].X;
                    view.Allocation.Y = allocation.Slices[0].Y;
                    view.Allocation.Width = allocation.Slices[0].Width;
					view.Allocation.Height = allocation.Slices[0].Height;


                    shadowInfo.ShadowMatrices.resize(1);
                    auto proj =
                        Core::PerspectiveRhZo(
                            spotLight->GetOuterConeAngle() * 2.0f,
                            1.0f,
                            0.1f,
                            spotLight->GetAttenuationRadius());

                    auto viewMat =
                        Core::LookAtRH(
                            spotLight->GetPosition(),
                            spotLight->GetPosition() +
                            spotLight->GetDirection(),
                            Core::Float3(0, 1, 0));

                    view.ViewProjection =
                        proj * viewMat;
                    shadowInfo.ShadowMatrices[0] = view.ViewProjection;
                    
                    Core::Float4 test = viewMat * Core::Float4(3, 3, 3, 1).Data;
                    Core::Float4 restest = proj * test.Data;
                    float z = restest.Z / restest.W;
                    view.TargetHeight = allocator.GetDesc().SpotShadowAtlas.Width;
                    view.TargetWidth = allocator.GetDesc().SpotShadowAtlas.Height;
                    shadowRenderViews.push_back(view);
                    view.CameraPos = spotLight->GetPosition();
                    view.bWantRawDepth = false;
                    break;
                }

                //---------------------------------------------------
                // Point Light (6 faces)
                //---------------------------------------------------
                case Engine::ELightType::Point:
                {
                    
                    auto* pointLight =
                        dynamic_cast<Engine::PointLightSceneProxy*>(light);

                    if (!pointLight)
                        return;

                    ShadowAllocation allocation; 


                    LightShadowInfo& shadowInfo = scene->GetLightShadowInfo(light);
                    if (!shadowInfo.Allocation.IsValid()) {
                        allocation = allocator.AllocatePointShadow(1024);
                        shadowInfo.Allocation = allocation;
                        if (!allocation.IsValid())
                            return;
                    }
                    else {
                        allocation = shadowInfo.Allocation;
                    }

                    
                    static Core::Float3 directions[6] =
                    {
                        { 1, 0, 0 },
                        {-1, 0, 0 },
                        { 0, 1, 0 },
                        { 0,-1, 0 },
                        { 0, 0, 1 },
                        { 0, 0,-1 }
                    };

                    static Core::Float3 ups[6] =
                    {
                        { 0,1,0 }, // +X
                        { 0,1,0 }, // -X

                        { 0,0,1 },  // +Y
                        { 0,0,-1 }, // -Y

                        { 0,1,0 }, // +Z
                        { 0,1,0 }  // -Z
                    };

                    float radius = pointLight->GetAttenuationRadius();
                    auto proj =
                        Core::PerspectiveRhZo(
                            Core::DegToRad(90.0f),
                            1.0f,
                            0.1f,
                            radius);
                    
                    shadowInfo.ShadowMatrices.resize(6);
                    for (uint32_t i = 0; i < 6; i++)
                    {
                        ShadowRenderView view;
                        auto rdgT = builder.RegisterExternalTexture(std::string("ShadowPassTarget") + std::to_string((uint64_t)allocation.Texture), allocation.Texture);
                        view.Allocation.Texture = rdgT;
                        view.Allocation.X = allocation.Slices[i].X;
                        view.Allocation.Y = allocation.Slices[i].Y;
                        view.Allocation.Width = allocation.Slices[i].Width;
                        view.Allocation.Height = allocation.Slices[i].Height;
						view.Allocation.Layer = allocation.Slices[i].Layer;
						view.Allocation.Mip = allocation.Slices[i].Mip;
                        auto viewMat =
                            Core::LookAtRH(
                                pointLight->GetPosition(),
                                pointLight->GetPosition() + directions[i],
                                ups[i]);

                        view.ViewProjection =
                            proj * viewMat;

						// 不能在再翻转，否则会导致裁剪面翻转，导致背面剔除错误
                        //view.ViewProjection = Core::MakeScaleMatrix(flip) * view.ViewProjection;
                       
                        auto clipPos = proj * viewMat * Core::Float4(-1, -1, -1, 1).Data;
                        Core::Float4 test = view.ViewProjection * Core::Float4(3, 3, 3, 1).Data;

                        float z = test.Z / test.W;
                        shadowInfo.ShadowMatrices[i] = view.ViewProjection;
                        view.CameraPos = pointLight->GetPosition();
                        view.bWantRawDepth = true;
                        view.RawDepthClearValue = Core::Float4(radius, radius,0,0);
                        
                        shadowRenderViews.push_back(view);
                    }
                    
                    break;
                }
                //---------------------------------------------------
                // Directional Light (CSM)
                //---------------------------------------------------
                case Engine::ELightType::Directional:
                {
                    auto* dirLight =
                        dynamic_cast<Engine::DirectionalLightSceneProxy*>(light);

                    if (!dirLight)
                        return;

                    constexpr uint32_t cascadeCount = 4;
                    LightShadowInfo& shadowInfo = scene->GetLightShadowInfo(light);
                    ShadowAllocation allocation;
                    if (!shadowInfo.Allocation.IsValid()) {
                        allocation = allocator.AllocateDirectionalCSMShadow(
                            2048,
                            cascadeCount);
                        shadowInfo.Allocation = allocation;
                        if (!allocation.IsValid())
                            return;
                    }
                    else {
                        allocation = shadowInfo.Allocation;
                    }

                    shadowInfo.ShadowMatrices.resize(cascadeCount);
                    break;
                }
                default:
                    break;
                }
            });

        if (!shadowRenderViews.empty())
        {
            BuildLightShadow(scene,builder, shadowRenderViews);
        }
    }
    void SceneRenderer::BuildSceneLightCascadeShadowMap(Renderer::Scene* scene, RenderCore::RenderGraphBuilder& builder, const SceneView& sceneView)
    {
        std::vector<ShadowRenderView> shadowRenderViews;

        scene->ForEachLight(
            [scene, &shadowRenderViews,&sceneView,&builder](Engine::LightSceneProxy* light)
            {
                if (!light)
                    return;
                
                if (!light->IsCastShadow())
                    return;
                auto lightType = light->GetLightType();
                auto& allocator = scene->GetShadowMapAllocator();

                switch (lightType)
                {
                //---------------------------------------------------
                // Directional Light (CSM)
                //---------------------------------------------------
                case Engine::ELightType::Directional:
                {
                    auto* dirLight =
                        dynamic_cast<Engine::DirectionalLightSceneProxy*>(light);

                    if (!dirLight)
                        return;

                    constexpr uint32_t cascadeCount = 4;
                    const std::array<float, 5>& splitDepths = sceneView.SplitDepths;
                    
					LightShadowInfo& shadowInfo = scene->GetLightShadowInfo(light);
                    ShadowAllocation allocation = shadowInfo.Allocation;
                    if (!allocation.IsValid())
                        return;

                    shadowInfo.ShadowMatrices.resize(cascadeCount);
                    
                    auto& sceneBound = scene->GetSceneBounds();
                    float sceneMaxExtent = sceneBound.Sphere.Radius * 2;
                    for (uint32_t i = 0; i < cascadeCount; i++)
                    {

                        ShadowRenderView view;
                        auto rdgT = builder.RegisterExternalTexture(std::string("ShadowPassTarget") + std::to_string((uint64_t)allocation.Texture), allocation.Texture);
                        view.Allocation.Texture = rdgT;
                        view.Allocation.X = allocation.Slices[i].X;
                        view.Allocation.Y = allocation.Slices[i].Y;
                        view.Allocation.Width = allocation.Slices[i].Width;
                        view.Allocation.Height = allocation.Slices[i].Height;
                        view.Allocation.Layer = allocation.Slices[i].Layer;
                        view.Allocation.Mip = allocation.Slices[i].Mip;

                        auto corners =
                            sceneView.GetFrustumCornersWS(
                                splitDepths[i],
                                splitDepths[i + 1]);

                        Core::Float3 center(0, 0, 0);
                        for (auto& c : corners)
                            center += c;
                        center /= 8.0f;


                        auto lightDir =
                            dirLight->GetDirection();

                        Core::Float3 targetDir = Core::Normalize(center);
                       
                        auto normalizeLightDir = Core::Normalize(lightDir);
                        if (targetDir.Length() > 0 && Core::Dot(targetDir, -normalizeLightDir) < 0) {
                            float centerDis = Core::Distance(center, Core::Float3(0, 0, 0));
                            sceneMaxExtent += centerDis;
                        }

                        auto lightPos =
                            center - lightDir * sceneMaxExtent;

                        auto lightView =
                            Core::LookAtRH(
                                lightPos,
                                center,
                                Core::Float3(0, 1, 0));

                        Core::AABB bounds;
                        
                        for (auto& corner : corners)
                        {
                            Core::Float4 p =
                                lightView *
                                Core::Float4(corner, 1.0f).Data;

                            bounds.ExpandBy(
                                Core::Float3(p.X, p.Y, p.Z));
                        }
                        //bounds.Min.z -= ExtendNear;
                        //bounds.Min.z -= 10;
                        //前面通过调整光源位置，得到的view矩阵一定确定可以看到完整的从视锥体中心到光源的这块区域以及场景，这样
                        //得到的z一定是负数，且最远的负数越小，最近的直接设置为0，所以投影需要将越大的映射为0，所以线性平行投影映射需要将
                        //翻转min和max
                        auto proj =
                            Core::OrthoRhBoundsZo(
                                bounds.Min.X,
                                bounds.Max.X,
                                bounds.Min.Y,
                                bounds.Max.Y,
                                0,
                                bounds.Min.Z);
                        Core::Float4 proj1 = lightView * Core::Float4(0, 0, 0, 1).Data;
                        Core::Float4 proj2 = lightView * Core::Float4(3, 3, 3, 1).Data;

                        view.ViewProjection =
                            proj * lightView;
						// view.ViewProjection 不能在绘制时翻转uv，否则会导致裁剪面翻转，导致背面剔除错误
                        //view.ViewProjection = Core::MakeScaleMatrix(Core::Float3(1, 1, 1)) * view.ViewProjection;
                        Core::Float4 dPosition1 = view.ViewProjection * Core::Float4(0, 0, 0, 1).Data ;
                        Core::Float4 dPosition2 = view.ViewProjection * Core::Float4(3, 3, 3, 1).Data;
                        shadowInfo.ShadowMatrices[i] = view.ViewProjection;
                        view.CameraPos = lightPos;
                        view.bWantRawDepth = false;
                        shadowRenderViews.push_back(view);
                    }
                    

                    break;
                }
                default:
                    break;
                }
            });

        if (!shadowRenderViews.empty())
        {
            BuildLightShadow(scene,builder, shadowRenderViews);
        }
    }
    void SceneRenderer::UploadShadowMapInfo(Renderer::Scene* scene,RenderCore::RenderGraphBuilder& graphBuilder)
    {
		auto lightCount = scene->m_LightIndexs.size();
        std::vector<LightShadowAccessInfo> lightShadowAccessInfos(lightCount);
        lightShadowAccessInfos.reserve(lightCount);
        std::vector<AtlasShadowTextureAccessInfo> spotShadowAccessInfos;

        //
        int atlasIndex = 0, directionalLightShadowViewInfoIndex = 0;
        for (auto& shadowInfo : scene->m_GPUResourceInfo.ShadowResourceInfo.LightShadowInfos) {
			auto index = scene->m_LightIndexs[shadowInfo.first];
            LightShadowAccessInfo& lightShadowAccessInfo = lightShadowAccessInfos[index];
            lightShadowAccessInfo.ShadowType = (uint32_t)shadowInfo.second.Allocation.ShadowType;
            AtlasShadowTextureAccessInfo spotShadowAccessInfo;
            DirectionalLightCascadeShadowViewInfo directionalLightShadowViewInfo;
            if (shadowInfo.second.Allocation.ShadowType == EShadowType::Spot) {
                lightShadowAccessInfo.ShadowInfoIndex = atlasIndex;
				spotShadowAccessInfo.Layer = shadowInfo.second.Allocation.Slices[0].Layer;
				spotShadowAccessInfo.mip = shadowInfo.second.Allocation.Slices[0].Mip;
				spotShadowAccessInfo.UVScale = shadowInfo.second.Allocation.Slices[0].UVScale;
				spotShadowAccessInfo.UVBias = shadowInfo.second.Allocation.Slices[0].UVOffset;
				spotShadowAccessInfo.ViewProj = shadowInfo.second.ShadowMatrices[0];
                
				spotShadowAccessInfos.push_back(spotShadowAccessInfo);
				atlasIndex++; 
                
			}
			else if (shadowInfo.second.Allocation.ShadowType == EShadowType::Directional) {
				lightShadowAccessInfo.ShadowInfoIndex = directionalLightShadowViewInfoIndex;
				directionalLightShadowViewInfoIndex += shadowInfo.second.ShadowMatrices.size();
                lightShadowAccessInfo.CascadeCount = shadowInfo.second.ShadowMatrices.size();
			}
            
            lightShadowAccessInfo.ShadowTextureIndex = shadowInfo.second.Allocation.TextureIndex;

        }
        auto& lightShadowInfoBuffer = scene->m_GPUResourceInfo.ShadowResourceInfo.LightShadowInfoBuffer;
        auto& atlasAccessInfoBuffer = scene->m_GPUResourceInfo.ShadowResourceInfo.AtlasAccessInfoBuffer;
        
        if (lightShadowInfoBuffer == nullptr || lightShadowInfoBuffer->GetRHI()->GetDesc().Size < lightCount * sizeof(LightShadowAccessInfo)) {
            RHI::RHIBufferDesc desc;
            desc.Size = lightCount * sizeof(LightShadowAccessInfo);
            if (desc.Size == 0) {
                desc.Size = 1;
            }
            desc.Usage = RHI::ERHIBufferUsageFlag::ShaderResource | RHI::ERHIBufferUsageFlag::TransferDst;
            lightShadowInfoBuffer = std::make_shared<RenderCore::RenderBuffer>(desc);
            lightShadowInfoBuffer->InitRHIResource();
            auto bufferUpload = graphBuilder.RegisterExternalBuffer("LightShadowInfoBuffer",lightShadowInfoBuffer.get());
			graphBuilder.AddUploadBuffer(bufferUpload, lightShadowAccessInfos.data(), lightCount * sizeof(LightShadowAccessInfo));
        }
        if (atlasAccessInfoBuffer == nullptr || atlasAccessInfoBuffer->GetRHI()->GetDesc().Size < spotShadowAccessInfos.size() * sizeof(AtlasShadowTextureAccessInfo)) {
            RHI::RHIBufferDesc desc;
            desc.Size = spotShadowAccessInfos.size() * sizeof(AtlasShadowTextureAccessInfo);
            if (desc.Size == 0) {
                desc.Size = 1;
            }
            desc.Usage = RHI::ERHIBufferUsageFlag::ShaderResource | RHI::ERHIBufferUsageFlag::TransferDst;
            atlasAccessInfoBuffer = std::make_shared<RenderCore::RenderBuffer>(desc);
            atlasAccessInfoBuffer->InitRHIResource();
            auto bufferUpload = graphBuilder.RegisterExternalBuffer("AtlasAccessInfoBuffer", atlasAccessInfoBuffer.get());
            graphBuilder.AddUploadBuffer(bufferUpload, spotShadowAccessInfos.data(), spotShadowAccessInfos.size() * sizeof(AtlasShadowTextureAccessInfo));
        }
        

    }
    void SceneRenderer::AddPreDepthPass(RenderCore::RenderGraphBuilder& graphBuilder, const MeshBatchList& meshBatchList, const Core::Mat4& viewProj, RenderCore::RenderGraphTextureRef depth)
    {
        // 创建深度模板状态
        RHI::RHIDepthStencilStateDesc depthStencilDesc;
        depthStencilDesc.bDepthTestEnable = true;
        depthStencilDesc.bDepthWriteEnable = true;
        depthStencilDesc.DepthCompareOp = RHI::ERHICompareOp::Less;

        auto depthStencilState = RHIPipelineStateCache::GetOrCreateDepthStencilState(depthStencilDesc);
        RHI::RHIGraphicsPipelineStateDesc pipelineDesc;
        // 创建图形管线状态


        // 这里可以设置更多管线配置...


        pipelineDesc.DepthStencilState = depthStencilState;

        //rendertarget info
        pipelineDesc.AttachmentDesc.ColorAttachmentCount = 0;
        pipelineDesc.AttachmentDesc.DepthActions = ERenderTargetActions::Clear_Store;
        pipelineDesc.AttachmentDesc.bEnableDepth = true;
        pipelineDesc.AttachmentDesc.DepthStencilFormat = RHI::ERHIFormat::D32_Float;
        auto vsShaderType = ShaderType::GetRegisterMap() [ShaderType::EShaderTypeFlag::Global]["PositionOnlyVS"];
        for (auto meshBatch : meshBatchList) {
            
            auto vfType = VertexFactoryType::GetRegisterMap()["LocalVertexFactory"];
            auto vfFlags = meshBatch.VertexFactory->GetVertexFactoryFlags();
            bool supportInstance = meshBatch.InstanceDataIds.size() > 1;
            if (supportInstance) {
                LocalVertexFactoryFeatureFlags flags;
                flags.PackedFlags = vfFlags;
                flags.SupportsInstanceData = true;
                vfFlags = flags.PackedFlags;
            }
            if (meshBatch.MaterialProxy->GetParent()->GetBlendMode() == EBlendMode::Opaque) {
                // 创建光栅化状态
                RHI::RHIRasterizerStateDesc rasterizerDesc;
                rasterizerDesc.PolygonMode = RHI::ERHIPolygonMode::Fill;
                rasterizerDesc.CullMode = RHI::ERHICullMode::Back;
                rasterizerDesc.FrontFace = meshBatch.FrontFace;
                rasterizerDesc.LineWidth = 1.0f;
                rasterizerDesc.bDepthBiasEnable = false;

                auto rasterizerState = RHIPipelineStateCache::GetOrCreateRasterizerState(rasterizerDesc);
                pipelineDesc.RasterizerState = rasterizerState;
                bool supportInstance = meshBatch.InstanceDataIds.size() > 1;
                uint32_t vsPermutationId = 0;
                if (supportInstance) {
                    vsPermutationId = 1;
                }
                //auto vfFlags = batch.VertexFactory->GetVertexFactoryFlags();
                auto vertexShader = GShaderMap.GetShader(vsShaderType, vsPermutationId);
                pipelineDesc.ShaderStages.VertexShader = dynamic_cast<RHI::RHIVertexShader*>(vertexShader->GetRHIShader());

               
                pipelineDesc.VertexDescState = GetVertexOnlyState(supportInstance);
                RHI::RHIColorBlendStateDesc blendDesc;
                // 创建颜色混合状态
                blendDesc.Attachments = {};
                auto colorBlendState = RHIPipelineStateCache::GetOrCreateColorBlendState(blendDesc);
                pipelineDesc.ColorBlendState = colorBlendState;
                auto pipeline = RHIPipelineStateCache::GetOrCreateGraphicsPipelineState(pipelineDesc);


                // 这里 pipeline 创建建议提取缓存
                // 逻辑与你当前 StaticMeshDrawPass 一样

                BEGIN_SHADER_PARAMETER_STRUCT(PassParameters)
                    SHADER_PARAMETER_STRUCT_REFERENCE(PositionOnlyVSParameters, vertexParameters)
                    SHADER_PARAMETER_RENDER_TARGET_BINDING_SLOTS(renderTargetSlots)
                END_SHADER_PARAMETER_STRUCT(PassParameters)

                auto* params = graphBuilder.AllocateParameter<PassParameters>();
                params->vertexParameters.ViewProjection = viewProj;
                params->vertexParameters.Model = meshBatch.LocalToWorld;
                if (supportInstance) {
                    params->vertexParameters.LocalVFInstanceInfo.InstanceData = meshBatch.InstanceDataBufferSRV;
                }
                params->renderTargetSlots.DepthStencil.Texture = depth;
                params->renderTargetSlots.DepthStencil.DepthAction = ERenderTargetActions::Clear_Store;
                auto width = depth->GetDesc().Width;
				auto height = depth->GetDesc().Height;
                graphBuilder.AddPass<PassParameters>(
                    "DepthOnlyPass",
                    PassParameters::GetMetaData(),
                    params,
                    EPassFlag::Graphic,
                    [=](RHI::RHICommandListBase& rhiCmdList)
                    {
                        auto& cmd =
                            static_cast<RHI::RHIGraphicCommandList&>(rhiCmdList);
                        cmd.SetGraphicPipelineState(pipeline);
                        //绑定vertexfactory
                        meshBatch.VertexFactory->Bind(cmd);
                        if (supportInstance) {
                            cmd.SetStreamSource(1, meshBatch.InstanceDataBufferAccessor->GetInstanceIdBuffer()->GetRHI(), 0);
                        }
                        // Bind Pipeline
                        //设置vertex参数
                        SetShaderParameters(cmd, vertexShader, &params->vertexParameters);

                        //设置pixel参数
                        auto boundRenderTarget = params->renderTargetSlots.GetBoundRenderTarget();
                        RHIRenderPassInfo passInfo;
                        passInfo.RenderTargets = boundRenderTarget;
                        passInfo.RenderTargets.DepthStencil.ClearBinding.Depth = 1.0f;
                        passInfo.RenderArea.X = 0;
                        passInfo.RenderArea.Y = 0;
                        passInfo.RenderArea.Width = width;
                        passInfo.RenderArea.Height = height;
                        cmd.BeginRenderPass(passInfo);
                        cmd.SetViewport(
                            0,
                            0,
                            width,
                            height,
                            0.0f,
                            1.0f);
                        cmd.SetScissor(0, 0, width, height);
                        for (auto& element : meshBatch.Elements)
                        {
                            cmd.DrawIndexed(
                                meshBatch.IndexBuffer->GetRHI(),
                                element.NumIndices,
                                meshBatch.InstanceDataIds.size(),
                                element.FirstIndex,
                                element.BaseVertexIndex,
                                meshBatch.StartInstance);
                        }
                        cmd.EndRenderPass();
                    });
            }

        }

    }

    void SceneRenderer::UpdateCascadeShadowInfo(Renderer::Scene* scene, RenderCore::RenderGraphBuilder& graphBuilder, const Engine::SceneView& view)
	{
        BuildSceneLightCascadeShadowMap(scene, graphBuilder, view);
        auto& directionalLightShadowViewInfoBuffer = scene->m_GPUResourceInfo.ShadowResourceInfo.DirectionalLightShadowViewInfoBuffer;
        int directionalLightShadowViewInfoIndex = 0;
        std::vector<DirectionalLightCascadeShadowViewInfo> directionalLightShadowViewInfos;
        for (auto& shadowInfo : scene->m_GPUResourceInfo.ShadowResourceInfo.LightShadowInfos) {
            auto index = scene->m_LightIndexs[shadowInfo.first];
            DirectionalLightCascadeShadowViewInfo directionalLightShadowViewInfo;
            if (shadowInfo.second.Allocation.ShadowType == EShadowType::Directional) {
                for (uint32_t i = 0; i < shadowInfo.second.ShadowMatrices.size(); i++, directionalLightShadowViewInfoIndex++) {
                    directionalLightShadowViewInfo.ViewProj = shadowInfo.second.ShadowMatrices[i];
                    directionalLightShadowViewInfos.push_back(directionalLightShadowViewInfo);
                }
            }

        }
        if (directionalLightShadowViewInfoBuffer == nullptr || directionalLightShadowViewInfoBuffer->GetRHI()->GetDesc().Size < directionalLightShadowViewInfos.size() * sizeof(DirectionalLightCascadeShadowViewInfo)) {
            RHI::RHIBufferDesc desc;
            desc.Size = directionalLightShadowViewInfos.size() * sizeof(DirectionalLightCascadeShadowViewInfo);
            if (desc.Size == 0) {
                desc.Size = 1;
            }
            desc.Usage = RHI::ERHIBufferUsageFlag::ShaderResource | RHI::ERHIBufferUsageFlag::TransferDst;
            directionalLightShadowViewInfoBuffer = std::make_shared<RenderCore::RenderBuffer>(desc);
            directionalLightShadowViewInfoBuffer->InitRHIResource();

        }
        auto directLightBufferUpload = graphBuilder.RegisterExternalBuffer("DirectionalLightShadowViewInfoBuffer", directionalLightShadowViewInfoBuffer.get());
        
        
        //To do： 这里后续平行光的级联阴影贴图相关的splitbuffer与视角相关，所以场景中的管理方式也需要修改，
		auto& splitBuffer = scene->m_GPUResourceInfo.ShadowResourceInfo.SplitBuffer;
		if (splitBuffer == nullptr || splitBuffer->GetRHI()->GetDesc().Size < (view.SplitDepths.size() * sizeof(float))) {
			RHI::RHIBufferDesc desc;
            desc.DebugName = "SceneSplitBuffer";
			desc.Size = view.SplitDepths.size() * sizeof(float);
			desc.Usage = RHI::ERHIBufferUsageFlag::ShaderResource | RHI::ERHIBufferUsageFlag::TransferDst;
			splitBuffer = std::make_shared<RenderCore::RenderBuffer>(desc);
			splitBuffer->InitRHIResource();
			
		}
        auto splitBufferUpload = graphBuilder.RegisterExternalBuffer("SplitBuffer", splitBuffer.get());
		RenderGraphBuilder::UploadBufferDesc uploadDesc;
        uploadDesc.Buffer = directLightBufferUpload;
        uploadDesc.Data = directionalLightShadowViewInfos.data();
		uploadDesc.Size = directionalLightShadowViewInfos.size() * sizeof(DirectionalLightCascadeShadowViewInfo);
        RenderGraphBuilder::UploadBufferDesc uploadDescSplit;
        uploadDescSplit.Buffer = splitBufferUpload;
        uploadDescSplit.Data = (void*)view.SplitDepths.data();
        uploadDescSplit.Size = view.SplitDepths.size() * sizeof(float);
		Core::Float4 test = directionalLightShadowViewInfos[0].ViewProj * Core::Float4(3, 3, 3, 1).Data;
        graphBuilder.AddUploadBuffers({ uploadDesc ,uploadDescSplit }, RenderCore::RenderGraphBuilder::EUploadPolicy::Immediate);
        
    }
    void SceneRenderer::AddClearShadowMapPass(RenderCore::RenderGraphBuilder& graphBuilder, std::vector<ShadowRenderView>& shadowRenderViews)
    {
        uint16_t groupCount = shadowRenderViews.size() / 8 + 1;
        uint32_t shadowId = 0;
        for (uint32_t i = 0; i < groupCount ; i++) {
            RenderCore::RenderTargetBindingSlots renderTargetBindingSlots;
            uint32_t j = 0;
            for (; j < 8 && shadowId < shadowRenderViews.size(); j++, shadowId++) {
                renderTargetBindingSlots.ColorRenderTargets[j].Action = ERenderTargetActions::Clear_Store;
                renderTargetBindingSlots.ColorRenderTargets[j].ArraySlice = shadowRenderViews[shadowId].Allocation.Layer;
                renderTargetBindingSlots.ColorRenderTargets[j].MipIndex = shadowRenderViews[shadowId].Allocation.Mip;
                renderTargetBindingSlots.ColorRenderTargets[j].Texture = shadowRenderViews[shadowId].Allocation.Texture;
                renderTargetBindingSlots.ColorRenderTargets[j].ClearValue = Core::Float4(1, 1, 1, 1);

            }
            renderTargetBindingSlots.NumColorRenderTargets = j;
            AddClearRenderTargetsPass(graphBuilder, renderTargetBindingSlots);
        }

    }
    void SceneRenderer::BuildLightShadow(Renderer::Scene* scene, RenderCore::RenderGraphBuilder& graphBuilder, std::vector<ShadowRenderView>& shadowRenderViews)
    {

        auto vsShaderType =
            ShaderType::GetRegisterMap()
            [ShaderType::EShaderTypeFlag::Global]
            ["PositionOnlyVS"];

        auto psShaderType =
            ShaderType::GetRegisterMap()
            [ShaderType::EShaderTypeFlag::Global]
            ["DepthShadowPassPS"];

        //获取pipeline


        // 创建深度模板状态
        RHI::RHIDepthStencilStateDesc depthStencilDesc;
        depthStencilDesc.bDepthTestEnable = true;
        depthStencilDesc.bDepthWriteEnable = true;
        depthStencilDesc.DepthCompareOp = RHI::ERHICompareOp::Less;

        auto depthStencilState = RHIPipelineStateCache::GetOrCreateDepthStencilState(depthStencilDesc);
        RHI::RHIGraphicsPipelineStateDesc pipelineDesc;
        // 创建图形管线状态
        

        // 这里可以设置更多管线配置...


        pipelineDesc.DepthStencilState = depthStencilState;

        //rendertarget info
        pipelineDesc.AttachmentDesc.ColorAttachmentCount = 1;
        pipelineDesc.AttachmentDesc.ColorAttachments[0].Format = RHI::ERHIFormat::R16G16_Float;
        pipelineDesc.AttachmentDesc.ColorAttachments[0].Actions = ERenderTargetActions::Load_Store;
        pipelineDesc.AttachmentDesc.DepthActions = ERenderTargetActions::Clear_Store;
        pipelineDesc.AttachmentDesc.bEnableDepth = true;
        pipelineDesc.AttachmentDesc.DepthStencilFormat = RHI::ERHIFormat::D32_Float;


        /*
        ============================================================
            Shared Shadow Pass
        ============================================================
        */
        uint32_t i = 0;
        for (auto& shadowView : shadowRenderViews)
        {
            //创建临时深度纹理
            RenderGraphTextureDesc desc;
            desc.Width = shadowView.Allocation.Width;
            desc.Height = shadowView.Allocation.Height;
            if (shadowView.TargetHeight != 0 && shadowView.TargetWidth != 0) {
                //重新设置dimention
                desc.Width = shadowView.TargetWidth;
                desc.Height = shadowView.TargetHeight;
            }
            desc.Depth = 1;
            desc.MipLevels = 1;
            desc.ArraySize = 1;

            desc.Format = ERHIFormat::D32_Float;
            desc.Type = ERHITextureType::Texture2D;
            desc.SampleCount = 1;

            desc.Usage =
                ERHITextureCreateFlag::DepthStencil |
                ERHITextureCreateFlag::ShaderResource;
			std::string depthTargetName = std::string("ShadowMapPassDepthTarget") + std::to_string((uint64_t)shadowView.Allocation.Texture) + " curid: " + std::to_string(i);
            desc.bGenerateMips = false;
            desc.DebugName = depthTargetName.c_str();
            auto shadowMapPassDepthTarget = graphBuilder.CreateTexture(depthTargetName,desc);
            i++;

            auto& slice =
                shadowView.Allocation;
            uint32_t psPermutationId = 0;
            if (shadowView.bWantRawDepth) {
                psPermutationId = 1;
            }
            MeshBatchList drawMeshBatches;
			SceneView shadowSceneView;
			shadowSceneView.ViewProjectionMatrix = shadowView.ViewProjection;
            shadowSceneView.BuildFrustum();
            StaticMeshDrawBuild(scene, shadowSceneView, drawMeshBatches);
            bool isFirst = true;
            for (auto& batch : drawMeshBatches)
            {

                // 创建光栅化状态
                RHI::RHIRasterizerStateDesc rasterizerDesc;
                rasterizerDesc.PolygonMode = RHI::ERHIPolygonMode::Fill;
                rasterizerDesc.CullMode = RHI::ERHICullMode::Back;
                rasterizerDesc.FrontFace = batch.FrontFace;
                rasterizerDesc.LineWidth = 1.0f;
                rasterizerDesc.bDepthBiasEnable = false;

                auto rasterizerState = RHIPipelineStateCache::GetOrCreateRasterizerState(rasterizerDesc);
                pipelineDesc.RasterizerState = rasterizerState;
                bool supportInstance = batch.InstanceDataIds.size() > 1;
                uint32_t vsPermutationId = 0;
                if (supportInstance) {
                    vsPermutationId = 1;
                }
                //auto vfFlags = batch.VertexFactory->GetVertexFactoryFlags();
                auto vertexShader = GShaderMap.GetShader(vsShaderType, vsPermutationId);
                pipelineDesc.ShaderStages.VertexShader = dynamic_cast<RHI::RHIVertexShader*>(vertexShader->GetRHIShader());

                auto pixelShader =
                    GShaderMap.GetShader(psShaderType, psPermutationId);
                pipelineDesc.ShaderStages.FragmentShader = dynamic_cast<RHI::RHIFragmentShader*>(pixelShader->GetRHIShader());
                pipelineDesc.VertexDescState = GetVertexOnlyState(supportInstance);
                RHI::RHIColorBlendStateDesc blendDesc;
                // 创建颜色混合状态
                RHI::RHIColorBlendAttachmentDesc blendAttachDesc;
                blendAttachDesc.bBlendEnable = false;
                std::vector<RHI::RHIColorBlendAttachmentDesc> attachments = { blendAttachDesc };
                blendDesc.Attachments = attachments;
                auto colorBlendState = RHIPipelineStateCache::GetOrCreateColorBlendState(blendDesc);
                pipelineDesc.ColorBlendState = colorBlendState;
                auto pipeline = RHIPipelineStateCache::GetOrCreateGraphicsPipelineState(pipelineDesc);


                // 这里 pipeline 创建建议提取缓存
                // 逻辑与你当前 StaticMeshDrawPass 一样

                BEGIN_SHADER_PARAMETER_STRUCT(PassParameters)
                    SHADER_PARAMETER_STRUCT_REFERENCE(PositionOnlyVSParameters, vertexParameters)
                    SHADER_PARAMETER_STRUCT_REFERENCE(DepthShadowPassPSParameters, pixelParameters)
                END_SHADER_PARAMETER_STRUCT(PassParameters)

                auto* params = graphBuilder.AllocateParameter<PassParameters>();
                params->vertexParameters.ViewProjection = shadowView.ViewProjection;
                params->vertexParameters.Model = batch.LocalToWorld;
                params->pixelParameters.CameraWorldPosition = shadowView.CameraPos;
                params->pixelParameters.renderTargetSlots.NumColorRenderTargets = 1;
                if (supportInstance) {
                    params->vertexParameters.LocalVFInstanceInfo.InstanceData = batch.InstanceDataBufferSRV;
                }
                params->pixelParameters.renderTargetSlots[0].Texture = shadowView.Allocation.Texture;
                params->pixelParameters.renderTargetSlots[0].ArraySlice = shadowView.Allocation.Layer;
                params->pixelParameters.renderTargetSlots[0].MipIndex = shadowView.Allocation.Mip;
                params->pixelParameters.renderTargetSlots[0].Action = ERenderTargetActions::Load_Store;
                params->pixelParameters.renderTargetSlots.DepthStencil.Texture = shadowMapPassDepthTarget;
                params->pixelParameters.renderTargetSlots.DepthStencil.DepthAction = ERenderTargetActions::Load_Store;
                if (isFirst) {
                    isFirst = false;
                    params->pixelParameters.renderTargetSlots[0].Action = ERenderTargetActions::Clear_Store;
					params->pixelParameters.renderTargetSlots[0].ClearValue = Core::Float4(1, 1, 1, 1);
                    if (shadowView.bWantRawDepth) {
                        params->pixelParameters.renderTargetSlots[0].ClearValue = shadowView.RawDepthClearValue;
                    }
                    params->pixelParameters.renderTargetSlots.DepthStencil.DepthAction = ERenderTargetActions::Clear_Store;
                }

                graphBuilder.AddPass<PassParameters>(
                    "ShadowMapPass",
                    PassParameters::GetMetaData(),
                    params,
                    EPassFlag::Graphic,
                    [=](RHI::RHICommandListBase& rhiCmdList)
                    {
                        auto& cmd =
                            static_cast<RHI::RHIGraphicCommandList&>(rhiCmdList);
                        cmd.SetGraphicPipelineState(pipeline);
                        //绑定vertexfactory
                        batch.VertexFactory->Bind(cmd);
                        if (supportInstance) {
                            cmd.SetStreamSource(1, batch.InstanceDataBufferAccessor->GetInstanceIdBuffer()->GetRHI(),0);
                        }
                        // Bind Pipeline
                        //设置vertex参数
                        SetShaderParameters(cmd, vertexShader, &params->vertexParameters);

                        //设置pixel参数
                        SetShaderParameters(cmd, pixelShader, &params->pixelParameters);
                        auto boundRenderTarget = params->pixelParameters.renderTargetSlots.GetBoundRenderTarget();
                        RHIRenderPassInfo passInfo;
                        passInfo.RenderTargets = boundRenderTarget;
                        passInfo.RenderTargets.DepthStencil.ClearBinding.Depth = 1.0f;
                        passInfo.RenderArea.X = slice.X;
                        passInfo.RenderArea.Y = slice.Y;
                        passInfo.RenderArea.Width = slice.Width;
                        passInfo.RenderArea.Height = slice.Height;
                        if (shadowView.TargetHeight != 0 && shadowView.TargetWidth != 0) {
                            //重新设置dimention
                            passInfo.RenderTargets.Dimensions.X = shadowView.TargetWidth;
                            passInfo.RenderTargets.Dimensions.Y = shadowView.TargetHeight;
                        }
                        cmd.BeginRenderPass(passInfo);
                        cmd.SetViewport(
                            slice.X,
                            slice.Y,
                            slice.Width,
                            slice.Height,
                            0.0f,
                            1.0f);
                        cmd.SetScissor(slice.X, slice.Y, slice.Width, slice.Height);
                        for (auto& element : batch.Elements)
                        {
                            cmd.DrawIndexed(
                                batch.IndexBuffer->GetRHI(),
                                element.NumIndices,
                                batch.InstanceDataIds.size(),
                                element.FirstIndex,
                                element.BaseVertexIndex,
                                batch.StartInstance);
                        }
                        cmd.EndRenderPass();
                    });
            }
        }
    }
}
