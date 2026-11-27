#include "SceneRenderer.h"

namespace Renderer {
	class DefferedSceneRenderer : public SceneRenderer {
	public:
		// 一帧渲染入口
		void Build(RenderCore::RenderGraphBuilder& graphBuilder) override;
		// A single-view caller supplies the specular environment term and final composition.
		RenderCore::RenderGraphTextureRef BuildWithDeferredSpecular(RenderCore::RenderGraphBuilder& builder);
		RenderCore::RenderGraphTextureRef BuildLightingOnly(RenderCore::RenderGraphBuilder& builder);
	private:
		RenderCore::RenderGraphTextureRef BuildPasses(RenderCore::RenderGraphBuilder& builder, bool bDeferSpecular, bool bLightingOnly = false);
	};
}
