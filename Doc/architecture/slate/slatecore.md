# SlateCore 模块架构说明

## 1. 模块定位

`SlateCore` 是整个 UI / 窗口系统的基础层，它负责把“窗口、事件、控件、视口”这些概念抽象成统一接口，供上层的 `Application` 和 `ImGUISlate` 使用。

从源码结构看，`src/Framework/SlateCore/PublicHeader` 中的关键内容包括：

- `Window.h`：窗口抽象与创建工厂
- `Widget.h`：控件基类、几何体、可见性与 native widget
- `EventHandler.h`：输入事件与 resize/focus 处理接口
- `SlateWidget.h`：可含子控件的窗口部件
- `SlateViewport.h`：视口抽象，负责渲染目标纹理
- `SlateRenderer.h`：渲染器接口和 renderer module
- `ApplicationBase.h`：应用层基类，负责全局应用实例

从架构上看，`SlateCore` 并不直接做具体 UI 绘制，它更像是一个“平台窗口开发框架”和“事件分发框架”。

---

## 2. 设计目标

`SlateCore` 的设计目标是把以下系统抽象统一起来：

- 窗口管理：`Window` / `PlatformSurface`
- 控件树：`Widget` / `NativeWidget`
- 事件分发：`EventHandler`
- 视口与渲染目标：`SlateViewport`
- 应用生命周期：`ApplicationBase`

它本质上是一个轻量版 UI 框架，结构上类似 Unreal Slate 的核心概念，但这里的实现更偏“最小可用 UI 抽象”。

---

## 3. 窗口体系：`Window` 与 `WindowFactory`

`Window` 是 SlateCore 的最顶层对象，定义了一个平台窗口的通用接口：

```cpp
class SLATECORE_API Window : public PlatformSurfaceOwner
{
public:
    Window(int width, int height, const std::string& title);
    virtual bool Initialize();
    void PollEvents();
    void Shutdown();
    void Show();
    void Hide();
    void Close();

    void SetRootWidget(Widget* widget);
    Widget* GetRootWidgets() const;
};
```

关键点在于：

- `Window` 不是直接绑定某个平台 API，而是通过 `PlatformSurface` 和 `PlatformSurfaceOwner` 抽象平台层
- 它持有 `_rootWidget`，允许窗口拥有一个树形 UI 根节点
- `WindowFactory` 负责创建不同平台窗口实现，使用注册机制 `REGISTER_WINDOW_PLATFORM(...)`

也就是说，`SlateCore` 设计上把“平台实现”和“窗口抽象”分离，便于后续扩展到 Win32、GLFW 或其它窗口后端。

---

## 4. 输入事件体系：`EventHandler`

`SlateCore::EventHandler` 是所有 UI 对象共享的事件基类。它定义了统一的输入与生命周期事件：

```cpp
class SLATECORE_API EventHandler
{
public:
    virtual bool OnMouseMove(const MouseMoveEvent& event);
    virtual bool OnMouseButton(const MouseButtonEvent& event);
    virtual bool OnMouseWheel(const MouseWheelEvent& event);
    virtual bool OnKeyDown(const KeyEvent& event);
    virtual bool OnKeyUp(const KeyEvent& event);
    virtual bool OnFocusReceived();
    virtual bool OnFocusLost();
    virtual bool OnResize(uint32_t width, uint32_t height);
};
```

其中事件数据结构包括：

- `MouseMoveEvent`
- `MouseButtonEvent`
- `MouseWheelEvent`
- `KeyEvent`
- `ModifierKeys`

这种结构非常符合 UI 事件系统的抽象方式：事件是独立的数据对象，控件只关心自己是否处理该事件。这样可以让 `Window`、`Widget`、`SlateViewport` 共享同一套事件契约。

---

## 5. 控件树：`Widget` 与 `NativeWidget`

`Widget` 是 SlateCore 的核心抽象：

```cpp
class Widget : public EventHandler
{
public:
    virtual void Tick(float dt);
    virtual void Draw() = 0;
    virtual void Resize(float width, float height);
    virtual bool HitTest(float x, float y) const;

    void SetGeometry(float x, float y, float width, float height);
    const WidgetGeometry& GetGeometry() const;
    void SetVisibility(EVisibility visibility);
    EVisibility GetVisibility() const;
};
```

`Widget` 中包含：

- `WidgetGeometry`：位置与尺寸
- `EVisibility`：显示/隐藏/折叠状态
- `HitTest`：用于判断输入是否落在当前控件区域

### 5.1 `NativeWidget`

`NativeWidget` 进一步抽象“拥有原生平台句柄”的控件：

```cpp
class NativeWidget : public Widget, public PlatformSurfaceOwner
{
public:
    void SetNativeHandle(void* nativeHandle);
    void* GetNativeHandle() const override;
};
```

这意味着某些 widget 可以直接绑定平台窗口或原生控件句柄，从而在窗口系统中与平台层协同。

---

## 6. `SlateWidget` 与 `SlateViewport`

### 6.1 `SlateWidget`

`SlateWidget` 是 SlateCore 中更具体的容器控件：

```cpp
class SLATECORE_API SlateWidget : public NativeWidget
{
public:
    bool AddChildWidget(SlateViewport* child);
    void SetViewportChild(SlateViewport* viewport);
    SlateViewport* GetViewportChild() const;
    void Draw() override;
};
```

它表现出一种层级式 UI 容器的思路：

- 可挂载子窗口/视口
- 负责网络式分发输入事件
- 负责尺寸和焦点变化同步

### 6.2 `SlateViewport`

`SlateViewport` 是一个更强的抽象：

```cpp
class SLATECORE_API SlateViewport : public Widget
{
public:
    virtual int GetWidth() const = 0;
    virtual int GetHeight() const = 0;
    virtual void Resize(int Width, int Height) = 0;
    virtual void* GetViewportRenderTargetTexture() const = 0;
};
```

它强调的是“渲染目标视口”，意味着节点不仅是 UI 组件，还可以成为可渲染区域，例如 3D 场景视口。其作用与现代图形应用中的 viewport 非常接近：

- 持有渲染目标纹理
- 可调整大小
- 可作为 render target 让上层渲染器定向输出

---

## 7. 渲染桥接：`SlateRenderer`

`SlateCore` 定义了一个抽象渲染器接口：

```cpp
class SLATECORE_API SlateRenderer {
public:
    virtual void Render(SlateWidget* slateWidget) = 0;
    virtual void CreateViewport(SlateWidget* slateWidget) = 0;
};
```

和 `SlateRendererModule` 一起构成了渲染器扩展点：

```cpp
class SLATECORE_API SlateRendererModule : public Core::Module {
public:
    virtual SlateRenderer* CreateSlateRenderer() = 0;
};
```

这说明 `SlateCore` 把“UI 框架”和“UI 渲染实现”严格分开：

- `SlateCore` 提供通用 UI 抽象与生命周期机制
- 具体渲染实现可以由不同模块提供，例如 `SlateRHIRenderer`

这是一种典型的模块化架构：框架层为抽象，后端层为实现。

---

## 8. 应用层基类：`ApplicationBase`

`SlateCore` 中还有一个应用基类：

```cpp
class SLATECORE_API ApplicationBase
{
public:
    virtual ~ApplicationBase() = default;
    virtual void TickFrame() = 0;
};
```

它把全局应用实例单例化，便于顶层 `Launcher` 或主循环访问：

- `SetApplication(...)`
- `GetApplication()`

这意味着 `SlateCore` 也负责抽象“应用运行时”，形成 UI 层和业务应用层之间的桥接。上层 `Application` 会继承该对象，并在 main loop 中驱动实际逻辑。

---

## 9. 架构总结

`SlateCore` 的核心价值在于：

1. 抽象窗口和原生平台表面：`Window` / `PlatformSurface` / `PlatformSurfaceOwner`
2. 统一输入事件模型：`EventHandler` / `MouseKeyEvent` / `KeyEvent`
3. 提供可组合的控件结构：`Widget` / `NativeWidget` / `SlateWidget`
4. 把渲染目标视口抽象出来：`SlateViewport`
5. 让渲染实现从 UI 框架中解耦：`SlateRenderer` / `SlateRendererModule`
6. 为上层 `Application` 提供通用运行时基类

因此，`SlateCore` 是整个 UI / 窗口系统的“框架中枢”，而具体可视化和布局逻辑则由更上层的 `ImGUISlate` 或专门的渲染器来实现。
