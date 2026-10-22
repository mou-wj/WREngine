#include "SceneRenderer.h"

namespace Renderer {
	class DefferedSceneRenderer : public SceneRenderer {
	public:
		// 一帧渲染入口
		void Build(RenderCore::RenderGraphBuilder& graphBuilder) override;
	};
}