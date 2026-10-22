#pragma once

#include <memory>
#include "ApplicationBase.h"
#include "SceneViewport.h"
#include "AppViewportClient.h"
#include "SlateWidget.h"
#include "ImMainWindow.h"
#include "SceneHierarchyWidget.h"
#include "PropertyInspectorWidget.h"
#include "AssetBrowserWidget.h"

namespace ImGUISlate {
    class ImSlateRenderer;
    class ImWidget;
}

namespace App {

    class APPLICATION_API Application : public SlateCore::ApplicationBase
    {
    public:
        Application();
        virtual ~Application();

        // 生命周期
        bool Initialize() override;
        bool RequestExit() override;
        void Shutdown() override;

        // 窗口管理
        std::shared_ptr<ImGUISlate::ImMainWindow> CreateWindowSP(int width, int height, const char* title);

        // 帧更新
        void TickFrame() override;

    protected:
        std::shared_ptr<ImGUISlate::ImMainWindow> _window;
        std::unique_ptr<SlateCore::SlateWidget> _sceneSlateWidget;
        std::unique_ptr<Engine::SceneViewport> _sceneMainViewport;
        std::unique_ptr<AppViewportClient> _sceneViewportClient;
		std::unique_ptr<ImGUISlate::ImWidget> _imGuiWidget;
        std::unique_ptr<SceneHierarchyWidget> _sceneHierarchyPanel;
        std::unique_ptr<PropertyInspectorWidget> _propertyInspectorPanel;
        std::unique_ptr<AssetBrowserWidget> _assetBrowserPanel;
		bool _bQuitFlag = false;
    };

using ApplicationSP = std::shared_ptr<Application>;

} // namespace Runtime