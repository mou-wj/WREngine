#include "Application.h"
#include "SlateRHIModule.h"
#include "ImSlateRenderer.h"
#include "ImWidget.h"
#include "ImMainWindow.h"

#include "imgui.h"

namespace App {

Application::Application()
{
    SetApplication(this);
}

Application::~Application()
{
    Shutdown();
}

// 初始化
bool Application::Initialize()
{
    // 可以在这里初始化 Slate / Input / RHI
    _sceneViewportClient = std::make_unique<AppViewportClient>();
	_renderer = SlateRHIRenderer::GSlateRHIRendererModule->CreateSlateRenderer();

    _window =  CreateWindowSP(800, 600, "My Application");
    
	_sceneViewportClient->InitResources();
    return true;
}

bool Application::RequestExit()
{
    return _bQuitFlag;
}

// 关闭
void Application::Shutdown()
{
    _imGuiWidget.reset();
    _sceneSlateWidget.reset();
    _sceneMainViewport.reset();
    _sceneViewportClient->ReleaseResources();
    _sceneViewportClient.reset();
    _window = nullptr;
}

// 创建窗口
std::shared_ptr<ImGUISlate::ImMainWindow> Application::CreateWindowSP(int width, int height, const char* title)
{
    // WindowFactory 会根据平台创建对应 Surface 的 Window
    std::shared_ptr<ImGUISlate::ImMainWindow> newWindow = std::make_shared<ImGUISlate::ImMainWindow>(width, height, std::string(title));
    newWindow->Initialize(); // 初始化 OS 窗口
    newWindow->Show();
    newWindow->SetResizeCallback([this](int w, int h) {
        if (_sceneMainViewport);
            //SceneMainViewport->Resize(W, H);
        });
    newWindow->SetCloseCallback([this]() {
        _bQuitFlag = true;
	});
    auto framebufferSize = newWindow->GetFramebufferSize();

    if (!_sceneSlateWidget)
    {
        _sceneSlateWidget = std::make_unique<SlateCore::SlateWidget>(newWindow.get());
    }
    _sceneSlateWidget->SetSize(framebufferSize.X - 200, framebufferSize.Y);
    Core::Int2 slateWidgetSize;
    slateWidgetSize.X = framebufferSize.X - 200;
    slateWidgetSize.Y = framebufferSize.Y;
    // 同步创建 SceneViewport
    if (!_sceneMainViewport)
    {
        _sceneMainViewport = std::make_unique<Engine::SceneViewport>(_sceneViewportClient.get(), slateWidgetSize);
    }
    _sceneSlateWidget->SetViewportChild(_sceneMainViewport.get());
    //RootSlateWidget->OnResize(static_cast<uint32_t>(framebufferSize.x), static_cast<uint32_t>(framebufferSize.y));
	ImGUISlate::LayoutParams layoutParams;
    layoutParams.Weight = 2;
    newWindow->AddWidget(_sceneSlateWidget.get(), layoutParams);
    _renderer->CreateViewport(_sceneSlateWidget.get());
    if (!_imGuiWidget)
    {
        _imGuiWidget = std::make_unique<ImGUISlate::ImWidget>([](int x, int y, int w, int h) {
            // 1. 强制设定窗口的位置和大小
            ImGui::SetNextWindowPos(ImVec2(x, y), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Always);

            // 2. 纯容器模式（无标题栏、无缩放、不可移动）
            ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar;

            if (ImGui::Begin("RightPanel", nullptr, flags)) {
                ImGui::TextUnformatted("ImWidget Panel");

                // 3. 核心：宽度传入 -FLT_MIN，按钮会自动撑满整个区域的宽度
                if (ImGui::Button("xxx", ImVec2(-FLT_MIN, 0.0f))) {
                    // 点击事件
                }
            }
            ImGui::End();
        });
    }

    _imGuiWidget->SetGeometry(
        static_cast<float>(framebufferSize.X - 100),
        0.0f,
        100.0f,
        static_cast<float>(framebufferSize.Y));
    layoutParams.Weight = 1;
    newWindow->AddWidget(_imGuiWidget.get(), layoutParams);
    
    return newWindow;
}



// TickFrame
void Application::TickFrame()
{
    // Poll Window / Input

    _window->PollEvents();
    if (_sceneMainViewport)
        _sceneMainViewport->Draw();
    // Draw 主 SceneViewport
    if (_sceneSlateWidget)
    {
        _renderer->Render(_sceneSlateWidget.get());
    }
    _window->Draw();

}


} // namespace Runtime