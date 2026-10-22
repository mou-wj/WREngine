#pragma once 
#include "Viewport.h"
#include "SlateViewport.h"
#include "Math.hpp"
namespace Engine {
    // Engine/SceneViewport.h
    class ENGINE_API SceneViewport final : public Viewport,public SlateCore::SlateViewport
    {
    public:
        SceneViewport(
            ViewportClient* inClient,
            Core::Int2 inSize
        );

        virtual ~SceneViewport();

        // Viewport
        int GetWidth() const override;
        int GetHeight() const override;

        void Resize(int width, int height) override;
		RenderCore::RenderTexture* GetRenderTarget() override;
        void* GetViewportRenderTargetTexture() const override;
        void Draw() override;

        void InitRHIResource();
        void ReleaseRHIResource();
    public:
        // Input
        bool OnMouseMove(
            const SlateCore::MouseMoveEvent&
            event) override;

        bool OnMouseButton(
            const SlateCore::MouseButtonEvent&
            event) override;

        bool OnMouseWheel(
            const SlateCore::MouseWheelEvent&
            event) override;

        bool OnKeyDown(
            const SlateCore::KeyEvent&
            event) override;

        bool OnKeyUp(
            const SlateCore::KeyEvent&
            event) override;

        bool OnFocusReceived()
            override;

        bool OnFocusLost()
            override;

        bool OnResize(
            uint32_t width,
            uint32_t height)
            override;
    private:

    private:
		RenderCore::RenderTextureSP m_ViewportTexture;
        int m_Width = 0;
        int m_Height = 0;

    };



}