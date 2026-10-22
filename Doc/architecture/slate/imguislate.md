# ImGuiSlate 模块架构说明

## 1. 模块定位

`ImGUISlate` 模块位于 `SlateCore` 之上，是“基于 Dear ImGui 的 Slate 实现层”。它并不是替代 `SlateCore`，而是在其抽象接口基础上提供一套更具体的窗口、布局与 UI 渲染实现。

从源码结构看，`src/Framework/ImGUISlate` 主要包含：

- `ImMainWindow.h`：ImGui 版主窗口，继承自 `SlateCore::Window`
- `ImSlateRenderer.h`：把 `SlateCore` 事件和 `ImGui` 输入输出桥接起来的 renderer
- `ImWidget.h`：ImGui widget 抽象，继承自 `SlateCore::Widget`
- `LayoutMgr.h`：布局系统，支持垂直/水平/网格布局
- `ImFileDialogWidget.h`：更具体的 ImGui 对话框控件

从架构上看，`ImGUISlate` 是当前项目中“可视化 UI 具体实现”的一层：它使用 ImGui 作为实际渲染和交互机制，但仍遵循 `SlateCore` 的窗口、控件、事件体系。

---

## 2. 设计目标

`ImGUISlate` 的目标是：

- 在 `SlateCore` 的抽象系统之上完成具体 UI 组织
- 利用 ImGui 做真正的绘制和交互
- 通过 layout manager 管理多 widget 的堆叠与排列
- 把 UI 事件转换成 `ImGui` 的输入事件（鼠标、键盘、滚轮）
- 让应用层能通过 Slate 风格的 widget API 快速构建编辑器式界面

因此，`ImGUISlate` 就是“Slate 框架 + ImGui 具体实现”的桥接层。

---

## 3. 主窗口：`ImMainWindow`

`ImMainWindow` 是 `ImGUISlate` 的核心入口，它继承自 `SlateCore::Window`：

```cpp
class IMGUISLATE_API ImMainWindow final : public SlateCore::Window
{
public:
    ImMainWindow(int width, int height, const std::string& title);
    void Draw();
    bool Initialize() override;
    void AddWidget(SlateCore::Widget* widget, const LayoutParams& params);
};
```

它的职责包括：

- 创建窗口本体
- 维护 `LayoutManager`
- 维护一个 `ImSlateRenderer`
- 将多个 widget 组合进根窗口
- 转发窗口事件到各个 widget / ImGui

这里有一个关键点：`ImMainWindow` 明确禁用了 `SetRootWidget` 和 `GetRootWidgets`，因为它使用的是 `LayoutManager` 分层布局，而不是简单的单根 widget 方案。这说明它更偏“编辑器型界面容器”，而不是一个普通窗口。

---

## 4. 布局系统：`LayoutManager`

`ImGUISlate` 中的布局系统使用 `LayoutManager` 作为抽象父类：

```cpp
class IMGUISLATE_API LayoutManager
{
public:
    void AddWidget(SlateCore::Widget* widget, const LayoutParams& params = {});
    void RemoveWidget(SlateCore::Widget* widget);
    virtual void Layout(float width, float height) = 0;
};
```

布局参数 `LayoutParams` 包含：

- `Width`, `Height`
- `Weight`
- `Margin`
- `Row`, `Column`
- `RowSpan`, `ColumnSpan`

具体布局实现：

- `VerticalLayout`
- `HorizontalLayout`
- `GridLayout`

这说明 `ImGUISlate` 不是单纯的 ImGui 直接调用，而是额外在 `SlateCore` 的控件树基础上新增了布局管理器。这样 UI 可以按编辑器常见方式安排：垂直堆放、横向堆放、网格布局。

---

## 5. 控件抽象：`ImWidget` 与 `PopupImWidget`

`ImWidget.h` 中定义了两个重要类型：

```cpp
class IMGUISLATE_API ImWidget : public SlateCore::Widget, public ImWidgetBase
class IMGUISLATE_API PopupImWidget : public SlateCore::NativeWidget, public ImWidgetBase
```

### 5.1 `ImWidgetBase`

`ImWidgetBase` 继承自 `SlateCore::EventHandler`，负责把事件转成 ImGui 输入：

```cpp
virtual bool OnMouseMove(const SlateCore::MouseMoveEvent& event) override;
virtual bool OnMouseButton(const SlateCore::MouseButtonEvent& event) override;
virtual bool OnMouseWheel(const SlateCore::MouseWheelEvent& event) override;
virtual bool OnKeyDown(const SlateCore::KeyEvent& event) override;
virtual bool OnKeyUp(const SlateCore::KeyEvent& event) override;
```

它同时持有：

```cpp
using DrawCallback = std::function<void(int x, int y, int w, int h)>;
DrawCallback _drawHandler;
```

这说明每个 ImGui widget 都可以绑定一个自定义绘制回调，用于绘制真正的界面内容。

### 5.2 `ImWidget`

`ImWidget` 是最基本的可绘制控件，用于完成常规区域绘制逻辑：

- `Draw()`
- `OnResize()`

### 5.3 `PopupImWidget`

`PopupImWidget` 继承自 `SlateCore::NativeWidget`，更接近“原生窗口/弹出层”的语义，适合弹出对话框、悬浮面板等场景。

---

## 6. 事件桥接：`ImSlateRenderer`

`ImSlateRenderer` 是 `ImGUISlate` 中的关键桥接对象，它继承自 `SlateCore::EventHandler`：

```cpp
class ImSlateRenderer : public SlateCore::EventHandler
{
public:
    explicit ImSlateRenderer(SlateCore::Window* window);
    void Render();

    bool OnMouseMove(const SlateCore::MouseMoveEvent& Event) override;
    bool OnMouseButton(const SlateCore::MouseButtonEvent& Event) override;
    bool OnMouseWheel(const SlateCore::MouseWheelEvent& Event) override;
    bool OnKeyDown(const SlateCore::KeyEvent& Event) override;
    bool OnKeyUp(const SlateCore::KeyEvent& Event) override;
};
```

它完成的核心工作是：

- 将 `SlateCore` 的事件类型转换成 ImGui 的键值和鼠标值
- 调整 `ImGuiIO` 的状态
- 让 ImGui 接收窗口输入
- 在 render pass 中绘制整个 widget 树

例如在 `ImSlateRenderer.cpp` 中，代码对 `SlateCore::EKey` 和 `SlateCore::EMouseButton` 进行了转换：

```cpp
ImGuiKey ConvertKey(SlateCore::EKey key)
int ConvertMouseButton(SlateCore::EMouseButton button)
```

这说明 `ImGUISlate` 用“桥接函数”的方式把 Slate 的输入模型转换成 ImGui 的输入模型，是典型的适配层设计。

---

## 7. 具体绘制流转

在 `ImMainWindow` 中，窗口本身并不直接负责所有渲染，而是通过 `LayoutManager` + `ImSlateRenderer` 的组合来完成：

```text
ImMainWindow
    -> LayoutManager
        -> Widget tree
    -> ImSlateRenderer
        -> ImGuiIO / ImGui::Render()
```

整体流程大致为：

1. 用户输入进入 `ImMainWindow` 的事件回调
2. 事件沿着 `SlateCore` 的 event handler 传递
3. `ImWidgetBase` 将事件转换为 ImGui 输入
4. `Draw()` 回调或 `ImGui::Begin/End` 逻辑执行 UI 绘制
5. `ImSlateRenderer` 负责最终把 UI 输出到窗口表面

这是一种标准的“框架事件 + ImGui 渲染”的组合结构。

---

## 8. 架构总结

`ImGUISlate` 模块可以概括为：

1. 依赖 `SlateCore` 的窗口抽象、控件体系和事件架构
2. 通过 `ImMainWindow` 建立实际窗口实例
3. 利用 `LayoutManager` 管理窗口中的 widget 布局
4. 通过 `ImWidgetBase` 和 `ImSlateRenderer` 把 Slate 事件桥接到 ImGui
5. 使用 `DrawCallback` 把实际 UI 逻辑插入到 widget 中

因此，`ImGUISlate` 不是独立的 UI 系统，而是 `SlateCore` 的一套具体实现方案：它保持了 Slate 风格的架构，最终落地为基于 ImGui 的编辑器界面和工具面板系统。

从工程结构上看，它处于“Slate 框架层”与“应用业务层”之间，是当前项目中最贴近编辑器 UI 实现的一层。