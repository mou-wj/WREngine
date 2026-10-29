#pragma once
#include "SceneView.h"

namespace Editor::ViewAxes
{
    // Endpoint order: +X, -X, +Y, -Y, +Z, -Z. Coordinates are viewport-local.
    Engine::ViewRect GetRect(int width, int height);
    int HitTest(const Core::Float4x4& view, int width, int height, const Core::Float2& position);
    void AddPass(RenderCore::RenderGraphBuilder& builder, const Engine::SceneView& view,
                 RenderCore::RenderGraphTextureRef sceneColor);
}
