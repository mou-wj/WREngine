#pragma once
#include "SlateRenderer.h"
#include "SlateViewport.h"
#include "RHIResource.h"
#include "RHICommandContex.h"
#include "Window.h"
#include "SlateWidget.h"
#include <map>
namespace SlateRHIRenderer {


	struct ViewportDrawItem
	{
		SlateCore::SlateViewport* Viewport;
		int X, Y;
		int Width, Height;
	};

	struct WindowViewportInfo {
		RHI::RHISwapchainSP SwapchainRHI;
		int Width, Height;

	};

	class SlateRHIRenderer : public SlateCore::SlateRenderer {
	public:
		SlateRHIRenderer();
		~SlateRHIRenderer() override;
		virtual void Render(SlateCore::SlateWidget* slateWidget) override;
		void CreateViewport(SlateCore::SlateWidget* slateWidget) override;
	private:

		std::map<SlateCore::SlateWidget*, WindowViewportInfo> m_Viewports;
	};



}