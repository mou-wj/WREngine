#pragma once
#include "Viewport.h"
#include "EventHandler.h"
namespace Engine {
    // Engine/ViewportClient.h
    class ENGINE_API ViewportClient : public SlateCore::EventHandler
    {
    public:
        virtual ~ViewportClient() = default;

        // Build the view family and draw it through the viewport.
        virtual void Draw(Viewport* inViewport) = 0;

        // Resize ֪ͨ
        virtual void OnViewportResized(int width, int height) {}

        // Per-frame client update.
        virtual void Tick(float deltaTime) {}
    };



}