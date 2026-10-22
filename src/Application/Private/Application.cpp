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
    SceneViewportClient = std::make_unique<AppViewportClient>();
	_renderer = SlateRHIRenderer::GSlateRHIRendererModule->CreateSlateRenderer();

    Window =  CreateWindowSP(800, 600, "My Application");
    
	SceneViewportClient->InitResources();
    return true;
}

bool Application::RequestExit()
{
    return QuitFlag;
}

// 关闭
void Application::Shutdown()
{
    ImGuiWidget.reset();
    SceneSlateWidget.reset();
    SceneMainViewport.reset();
    SceneViewportClient->ReleaseResources();
    SceneViewportClient.reset();
    Window = nullptr;
}

// 创建窗口
std::shared_ptr<ImGUISlate::ImMainWindow> Application::CreateWindowSP(int Width, int Height, const char* Title)
{
    // WindowFactory 会根据平台创建对应 Surface 的 Window
    std::shared_ptr<ImGUISlate::ImMainWindow> NewWindow = std::make_shared<ImGUISlate::ImMainWindow>(Width, Height, std::string(Title));
    NewWindow->Initialize(); // 初始化 OS 窗口
    NewWindow->Show();
    NewWindow->SetResizeCallback([this](int W, int H) {
        if (SceneMainViewport);
            //SceneMainViewport->Resize(W, H);
        });
    NewWindow->SetCloseCallback([this]() {
        QuitFlag = true;
	});
    auto framebufferSize = NewWindow->GetFramebufferSize();

    if (!SceneSlateWidget)
    {
        SceneSlateWidget = std::make_unique<SlateCore::SlateWidget>(NewWindow.get());
    }
    SceneSlateWidget->SetSize(framebufferSize.X - 200, framebufferSize.Y);
    Core::Int2 slateWidgetSize;
    slateWidgetSize.X = framebufferSize.X - 200;
    slateWidgetSize.Y = framebufferSize.Y;
    // 同步创建 SceneViewport
    if (!SceneMainViewport)
    {
        SceneMainViewport = std::make_unique<Engine::SceneViewport>(SceneViewportClient.get(), slateWidgetSize);
    }
    SceneSlateWidget->SetViewportChild(SceneMainViewport.get());
    //RootSlateWidget->OnResize(static_cast<uint32_t>(framebufferSize.x), static_cast<uint32_t>(framebufferSize.y));
	ImGUISlate::LayoutParams layoutParams;
    layoutParams.Weight = 2;
    NewWindow->AddWidget(SceneSlateWidget.get(), layoutParams);
    _renderer->CreateViewport(SceneSlateWidget.get());
    if (!ImGuiWidget)
    {
        ImGuiWidget = std::make_unique<ImGUISlate::ImWidget>([](int x, int y, int w, int h) {
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

    ImGuiWidget->SetGeometry(
        static_cast<float>(framebufferSize.X - 100),
        0.0f,
        100.0f,
        static_cast<float>(framebufferSize.Y));
    layoutParams.Weight = 1;
    NewWindow->AddWidget(ImGuiWidget.get(), layoutParams);
    
    return NewWindow;
}



// TickFrame
void Application::TickFrame()
{
    // Poll Window / Input

    Window->PollEvents();
    if (SceneMainViewport)
        SceneMainViewport->Draw();
    // Draw 主 SceneViewport
    if (SceneSlateWidget)
    {
        _renderer->Render(SceneSlateWidget.get());
    }
    Window->Draw();

}


} // namespace Runtime