# Application 模块架构说明

## 1. 模块定位

本篇描述保留的演示应用，可通过 `Langcher.exe -app=Application` 选择。默认应用已改为 [Editor](editor.md)，装配信息位于 src/Lancher/apps.json，两者不会同时作为应用入口启动。

`App::Application` 是当前项目中具体应用层的实现类，继承自 `SlateCore::ApplicationBase`。它负责把窗口、视口、编辑器 UI 面板和渲染器整合起来，形成最终可运行的应用程序入口。

对应源码：

- `src/Application/PublicHeader/Application.h`
- `src/Application/Private/Application.cpp`

从结构上看，它不是底层引擎，也不是纯粹 UI 框架，而是一个“业务应用组装器”。它将：

- `SlateCore::Window`
- `ImGUISlate::ImMainWindow`
- `SlateCore::SlateWidget`
- `Engine::SceneViewport`
- `AppViewportClient`
- `ImGUISlate::ImWidget`

组合为一个功能完整的窗口应用。

---

## 2. 设计目标

`App::Application` 的目标是：

- 创建和初始化主窗口
- 挂载场景视口
- 创建编辑器式 UI 面板
- 把仅有的渲染能力接入到窗口中
- 在主循环中 poll events，并执行视口与 UI 绘制

因此它是整个工程中最“面向业务”的实现类，直接服务于交互式编辑器或 demo 程序场景。

---

## 3. 继承关系

```cpp
class APPLICATION_API Application : public SlateCore::ApplicationBase
```

这说明：

- 它完全遵循 `ApplicationBase` 的生命周期约定
- 运行时入口统一在 `ApplicationBase` 的全局单例上
- `Lancher` 中的主循环不需要知道 `App::Application` 的具体实现细节

这是一种典型的“抽象基类 + 具体应用派生”结构。

---

## 4. 生命周期：`Initialize` / `Shutdown`

### 4.1 `Initialize()`

在 `Application::Initialize()` 中，代码执行了以下关键步骤：

```cpp
_sceneViewportClient = std::make_unique<AppViewportClient>();
_renderer = SlateRHIRenderer::GSlateRHIRendererModule->CreateSlateRenderer();
_window = CreateWindowSP(800, 600, "My Application");
_sceneViewportClient->InitResources();
```

这里表现出几个关键职责：

- 创建 viewport client，绑定渲染体及输入逻辑
- 通过 `SlateRHIRenderer` 创建渲染器实例
- 创建主窗口 `ImMainWindow`
- 初始化 viewport resources

也就是说，应用初始化阶段同时做了三件事：

1. 创建窗口
2. 初始化场景视口资源
3. 注入窗口渲染器

### 4.2 `Shutdown()`

`Shutdown()` 里清理顺序是：

```cpp
_window.reset(); // 等待提交、释放 GUI 和交换链，再关闭平台窗口
_imGuiWidget.reset();
_sceneSlateWidget.reset();
_sceneMainViewport.reset();
if (_sceneViewportClient) _sceneViewportClient->ReleaseResources();
_sceneViewportClient.reset();
```

窗口必须先完成使用场景纹理的提交，并释放 GUI 上传资源与交换链，再释放业务拥有的 Widget、SceneViewport 和 viewport client。这样异步后端不会访问已销毁的控件资源。

---

## 5. 窗口创建：`CreateWindowSP()`

`Application::CreateWindowSP()` 是实际构建窗口和布局的核心函数：

```cpp
std::shared_ptr<ImGUISlate::ImMainWindow> newWindow =
    std::make_shared<ImGUISlate::ImMainWindow>(width, height, std::string(title), _renderer);
newWindow->Initialize();
newWindow->Show();
```

随后它创建并挂载：

- `_sceneSlateWidget`
- `_sceneMainViewport`
- `_imGuiWidget`

同时用 `LayoutParams` 把组件组织进窗口布局中：

```cpp
newWindow->AddWidget(_sceneSlateWidget.get(), layoutParams);
newWindow->AddWidget(_imGuiWidget.get(), layoutParams);
```

这是一种“窗口中有主视口 + 右侧面板”的编辑器布局方式。

---

## 6. 视口与布局管理

### 6.1 `_sceneSlateWidget`

`_sceneSlateWidget` 是用来承载场景视口的 `SlateCore::SlateWidget`：

```cpp
_sceneSlateWidget = std::make_unique<SlateCore::SlateWidget>();
_sceneSlateWidget->SetViewportChild(_sceneMainViewport.get());
```

这里可理解为：

- 窗口中放置一个 `SlateWidget`
- 该 widget 的视口子节点指向 `Engine::SceneViewport`
- 这个 viewport 负责场景离屏输出；SlateWidget 是没有原生句柄的逻辑容器

### 6.2 `_sceneMainViewport`

`Engine::SceneViewport` 与 `AppViewportClient` 配合，形成 3D 场景渲染区域：

```cpp
_sceneMainViewport = std::make_unique<Engine::SceneViewport>(_sceneViewportClient.get(), slateWidgetSize);
```

这说明：

- 视口本身是场景展示区域
- viewport client 负责注册输入与底层资源初始化
- 调用 `_sceneMainViewport->Draw()` 时产生 RenderTexture，由 ImSlateRenderer 加入 GUI 背景绘制列表合成到主窗口

### 6.3 `_imGuiWidget`

右侧面板则通过 `ImGUISlate::ImWidget` 实现：

```cpp
_imGuiWidget = std::make_unique<ImGUISlate::ImWidget>([](int x, int y, int w, int h) {
    ImGui::SetNextWindowPos(...);
    ImGui::SetNextWindowSize(...);
    ImGui::Begin("RightPanel", ...);
    ...
    ImGui::End();
});
```

这意味着应用层并没有自己手写整个 ImGui 面板，而是把绘制逻辑做成可插拔的 callback，挂到 widget 上，这种设计相当灵活。

---

## 7. 主循环：`TickFrame()`

`TickFrame()` 是应用层的核心帧驱动入口：

```cpp
void Application::TickFrame()
{
    _window->PollEvents();

    _window->Draw();
}
```

这里体现了应用层的典型工作流：

1. `_window->PollEvents()`：轮询窗口事件
2. `_window->Draw()`：驱动可见控件、SceneViewport 离屏绘制和 ImGui 数据生成
3. ImGui RHI 后端将场景纹理和 GUI 一起绘制到 Window 的交换链，最终只 Present 一次

这说明 `App::Application` 属于“应用驱动的渲染主循环”，而不是引擎内部的渲染循环本身。

---

## 8. 退出状态：`RequestExit()`

`RequestExit()` 只简单返回：

```cpp
return _bQuitFlag;
```

`_bQuitFlag` 会在窗口关闭回调中被设置：

```cpp
newWindow->SetCloseCallback([this]() {
    _bQuitFlag = true;
});
```

这说明应用退出逻辑是“窗口关闭事件驱动”的，而非引擎强制结束。这使应用的关闭控制更贴近 GUI 生命周期。

---

## 9. 架构总结

`App::Application` 可以总结为：

1. 继承自 `SlateCore::ApplicationBase`，遵守统一的应用生命周期接口
2. 创建 `ImMainWindow` 作为主窗口
3. 组装 `_sceneSlateWidget` 与 `SceneViewport` 形成场景显示区域
4. 用 `_imGuiWidget` 组合右侧工具栏和编辑器面板
5. 在 `TickFrame()` 中统一处理事件、场景绘制和窗口绘制
6. 把底层 `SlateRHIRenderer`、`Engine::SceneViewport` 和窗口层组合起来，形成最终应用

因此，这个类是当前工程里真正的“应用入口层实现”，它把底层的图形、窗口和 UI 系统整合成一个可运行的程序框架。

在整个系统中，它位于：

```text
Lancher
    -> ApplicationBase
        -> App::Application
            -> ImMainWindow / SceneViewport / _imGuiWidget
``` 

是从框架到实际业务运行的最终组装点。

## 相关核心实现细节

本篇保留架构职责说明；执行时序、数学原理及源码限制见以下专题。本轮为源码整理，专题不等于运行验证通过。

- [窗口与离屏场景的渲染链路](../../key_implemetion/performance/08-ui-rendering-and-window-lifetime.md)
- [启动和模块依赖的执行顺序](../../key_implemetion/performance/09-module-build-and-startup.md)
