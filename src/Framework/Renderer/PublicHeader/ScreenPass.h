#pragma once
#include "ShaderParameter.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphResource.h"
namespace Renderer {
	using namespace RenderCore;

	RENDERER_API void AddScreenPass(RenderGraphBuilder& builder, RenderGraphTexture* inTexture, RenderGraphTexture* outTexture);


}
