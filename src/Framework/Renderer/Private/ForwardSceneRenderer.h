#include "SceneRenderer.h"

namespace Renderer {
	class ForwardSceneRenderer : public SceneRenderer {
	public:
		// 一帧渲染入口
		void Build(RenderCore::RenderGraphBuilder& graphBuilder) override;
	};
}