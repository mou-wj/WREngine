#pragma once 
#include "SceneView.h"
#include "Scene.h"
#include "RenderGraphBuilder.h"
#include "IBLPrecomputeShader.h"
#include "StaticMeshProcess.h"
namespace Renderer {
    
    struct SceneTextures
    {
       
        // Primary scene render targets.
        RenderCore::RenderGraphTextureRef SceneColor;
        RenderCore::RenderGraphTextureRef SceneDepth;
        RenderCore::RenderGraphTextureRef SceneDepthPyramid;
        // GBuffer targets.
        RenderCore::RenderGraphTextureRef GBufferA;
        RenderCore::RenderGraphTextureRef GBufferB;
        RenderCore::RenderGraphTextureRef GBufferC;

        // Optional auxiliary targets.
        RenderCore::RenderGraphTextureRef Velocity;
        RenderCore::RenderGraphTextureRef CustomDepth;

        uint32_t Width = 0;
        uint32_t Height = 0;

        void Reset()
        {

        }
    };
    struct ShadowRenderSlice {
        RenderCore::RenderGraphTextureRef Texture;
        uint32_t Mip = 0;
        uint32_t Layer = 0;
        uint32_t X = 0;
        uint32_t Y = 0;
        uint32_t Width = 0;
        uint32_t Height = 0;
    };
    struct ShadowRenderView
    {
        ShadowRenderSlice Allocation;
        Core::Float4x4 ViewProjection;
        uint32_t TargetWidth = 0;
        uint32_t TargetHeight = 0;
        bool bWantRawDepth = false;
        Core::Float3 CameraPos;
        Core::Float4 RawDepthClearValue;
    };
    // SceneRenderer.h
    class SceneRenderer
    {
    public:
        Scene* Scene;
        Engine::SceneViewFamily* Views;
        SceneTextures SceneTextures;

        virtual ~SceneRenderer() = default;

        // Build the rendering passes for a frame.
        virtual void Build(RenderCore::RenderGraphBuilder& graphBuilder) = 0;
        static void AddClearRenderTargetsPass(RenderCore::RenderGraphBuilder& bulder, RenderCore::RenderTargetBindingSlots& renderTargetBindingSlots);
        static void BuildSceneLightShadowMap(Renderer::Scene* scene, RenderCore::RenderGraphBuilder& graphBuilder);
        
        static void UploadShadowMapInfo(Renderer::Scene* scene, RenderCore::RenderGraphBuilder& graphBuilder);
        static void AddPreDepthPass(RenderCore::RenderGraphBuilder& graphBuilder,const MeshBatchList& meshBatchList,const Core::Mat4& viewProj, RenderCore::RenderGraphTextureRef depth);
    protected:
        static void BuildSceneLightCascadeShadowMap(Renderer::Scene* scene, RenderCore::RenderGraphBuilder& graphBuilder, const Engine::SceneView& view);
        static void UpdateCascadeShadowInfo(Renderer::Scene* scene, RenderCore::RenderGraphBuilder& graphBuilder, const Engine::SceneView& view);
        static void AddClearShadowMapPass( RenderCore::RenderGraphBuilder& graphBuilder, std::vector<ShadowRenderView>& shadowRenderViews);
        static void BuildLightShadow(Renderer::Scene* scene, RenderCore::RenderGraphBuilder& graphBuilder , std::vector<ShadowRenderView>& shadowRenderViews);
    };
    using SceneRendererSP = std::shared_ptr<SceneRenderer>;


}