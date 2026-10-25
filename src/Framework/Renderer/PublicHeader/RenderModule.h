#pragma once
#include "RenderInterface.h"
#include "SceneView.h"
namespace Renderer {


    class RENDERER_API RenderModule final : public RenderCore::RenderInterface
    {
    public:
        RenderModule();
        ~RenderModule() override;

        // Module
        void StartupModule() override;
        void ShutdownModule() override;
        bool IsLoaded() const override;

        // RenderInterface
        void BeginRender(Engine::SceneViewFamily* views) override;
        Engine::SceneInterface* AllocateScene() override;
        void PreComputeIBL(RenderCore::RenderTexture* inHdrTexture, RenderCore::RenderTexture* outDiffuseIbl, RenderCore::RenderTexture* outSpecularIbl) override;

    private:
        void InitIBLLut();
    private:
        bool m_bLoaded = false;
        std::string m_ModuleName = "RenderModule";
    };





}
