# ImGUI / ImGUISlate 开发说明

## 1. 目标与分层

本项目中的 ImGUI 相关能力，建议按两层来设计：

- ImGUISlate：通用 UI 模块，负责定义可复用的 widget 与通用控件
- 上层定制 widget：基于 ImGUISlate 或 SlateWidget 体系，定义具体业务相关的窗口/面板/工具控件

核心原则：

- ImGUISlate 负责“基础组件与通用交互”，不绑定具体项目逻辑
- 上层 widget 负责“业务语义与具体功能”，与具体工具链/编辑器场景绑定
- 通用事件处理逻辑独立到基类，避免重复耦合到每个 widget 实现中

---

## 2. Widget 的两种形态

ImGUI 构建的 widget 一般分为两类：

### 2.1 普通 widget

适用于：

- 嵌入到 ImMainWindow
- 嵌入到任意其他 Slate / ImGUI 容器中
- 作为页面、面板、工具区的一部分

设计要求：

- 继承自 SlateCore::Widget
- 共同复用 ImWidgetBase 中的 ImGUI 事件逻辑
- 负责 Draw() 渲染和布局更新
- 适合放入父容器、窗口布局树中

典型用法：

- property panel
- tool bar
- file browser panel
- log viewer
- scene settings window

### 2.2 弹出 widget

适用于：

- 文件选择框
- 颜色选择器
- 模态/非模态弹窗
- 工具提示弹层
- 右键菜单、浮层面板

设计要求：

- 继承自 SlateCore::NativeWidget
- 共享 ImWidgetBase 中的事件、callback 和输入逻辑
- 通过 Draw() 实现弹出层绘制
- 适合作为独立的临时窗口或悬浮层

典型用法：

- PopupImWidget
- open file dialog
- save file dialog
- context menu
- viewport quick action popover

---

## 3. ImWidgetBase 的职责

ImWidgetBase 应当只保留“和 ImGUI 相关的通用接口”。

它应该包含：

- DrawCallback：通用绘制回调
- 事件接口：
  - OnMouseMove
  - OnMouseButton
  - OnMouseWheel
  - OnKeyDown
  - OnKeyUp
  - OnFocusReceived
  - OnFocusLost
- 通用状态：
  - DrawHandler
  - 可能的 focus / enabled / hovered / active 状态

原则：

- 不要把业务数据塞进这个基类
- 不要把具体项目逻辑耦合到这个基类
- 只保留 UI 层需要的通用输入与绘制抽象

推荐设计：

```cpp
class ImWidgetBase : public SlateCore::EventHandler
{
public:
    using DrawCallback = std::function<void(int x, int y, int w, int h)>;

    ImWidgetBase() = default;
    explicit ImWidgetBase(DrawCallback callback);

    void SetDrawCallback(DrawCallback callback);

    virtual bool OnMouseMove(const SlateCore::MouseMoveEvent& event) override;
    virtual bool OnMouseButton(const SlateCore::MouseButtonEvent& event) override;
    virtual bool OnMouseWheel(const SlateCore::MouseWheelEvent& event) override;
    virtual bool OnKeyDown(const SlateCore::KeyEvent& event) override;
    virtual bool OnKeyUp(const SlateCore::KeyEvent& event) override;
    virtual bool OnFocusReceived() override;
    virtual bool OnFocusLost() override;

protected:
    DrawCallback DrawHandler;
};
```

---

## 4. 正确的继承结构

### 4.1 普通 widget

```cpp
class ImWidget : public SlateCore::Widget, public ImWidgetBase
{
public:
    ImWidget() = default;
    explicit ImWidget(DrawCallback callback);

    virtual void Draw() override;
    virtual bool OnResize(uint32_t width, uint32_t height) override;
};
```

这里的含义：

- `SlateCore::Widget` 提供容器/布局/生命周期能力
- `ImWidgetBase` 提供统一的 ImGUI 通用事件和绘制回调机制
- `ImWidget` 负责实现“普通嵌入型 widget”的具体渲染和尺寸处理

### 4.2 弹出 widget

```cpp
class PopupImWidget : public SlateCore::NativeWidget, public ImWidgetBase
{
public:
    explicit PopupImWidget(SlateCore::PlatformSurfaceOwner* parentOwner = nullptr);

    virtual void Draw() override;
    virtual bool OnResize(uint32_t width, uint32_t height) override;
};
```

这里的含义：

- `SlateCore::NativeWidget` 负责原生窗口/平台层挂载能力
- `ImWidgetBase` 提供相同的通用事件抽象
- `PopupImWidget` 负责弹出层的绘制与生命周期

---

## 5. ImGUISlate 模块的职责边界

ImGUISlate 的重点不是写具体业务逻辑，而是定义“可复用的通用 UI 组件”。

它应该主要负责：

- 通用 widget 基类
- 通用输入和事件处理逻辑
- 常见交互控件模板
- 可复用的内容区域与布局抽象
- 文件、路径、选择器类通用控件

### 推荐内置类型

ImGUISlate 模块中常见的通用控件包括：

- 文件选择框
- 路径输入框
- 文件保存对话框
- 目录选择器
- 颜色选择器
- 数值滑块控件
- 组合框 / 下拉选择器
- 分组面板 / collapsible panel
- 日志面板
- 通用文本输入组件

这些控件应当满足：

- 可被多个上层 widget 复用
- 无固定业务上下文绑定
- 具有清晰的输入输出数据接口

---

## 6. 上层定制 widget 的设计方法

上层 widget 不应该直接重复造轮子，而应该基于 ImGUISlate 的通用组件进行组合。

典型做法：

- 先定义一个通用的 `ImWidgetBase`
- 再定义 `ImWidget` 和 `PopupImWidget`
- 然后在具体项目中扩展：
  - `MaterialInspectorWidget`
  - `SceneHierarchyWidget`
  - `AssetBrowserWidget`
  - `PropertyEditorWidget`
  - `QuickActionPopupWidget`
  - `FilePickerPopupWidget`

上层 widget 的关键职责：

- 管理业务数据绑定
- 提供特定 UI 语义与布局
- 调用 ImGUISlate 中的通用控件
- 连接到具体项目的事件/数据模型

注意：

- 上层 widget 继承体系不能把所有功能塞到一个大类里
- 应该保持“基础 ImGUI 事件层”和“业务 widget 层”的分离

---

## 7. 实施建议

### 优先级顺序

1. 建立 `ImWidgetBase` 作为通用事件与绘制回调基类
2. 定义 `ImWidget`（普通嵌入型）
3. 定义 `PopupImWidget`（弹出型）
4. 让 ImGUISlate 继续收敛通用控件
5. 在上层 UI 中使用这些控件组合出业务 widget

### 命名规范

- `ImWidgetBase`：通用基础类
- `ImWidget`：普通嵌入式 widget
- `PopupImWidget`：弹出式 widget
- `*Widget`：项目中具体的业务 widget
- `*Picker`, `*Dialog`, `*Panel`：通用交互组件

### 代码原则

- 尽量让通用 widget 与业务 widget 分离
- 让 `Draw()` 只负责渲染，不负责业务计算
- 尽量通过 callback / data binding 传递状态
- 对 popup 和 panel 分开设计，而不是一大类塞满所有逻辑

---

## 8. 最终设计意图

这套结构的目标是：

- 保持 ImGUISlate 是一个“通用 UI 组件库”
- 保持上层 widget 是“业务驱动的具体 UI”
- 让 `ImWidgetBase` 统一做通用事件与绘制接口
- 让 `ImWidget` 与 `PopupImWidget` 分别适配两种不同的容器机制

这样最容易扩展到：

- 运行时工具窗口
- 编辑器属性面板
- 实时调试面板
- 文件/资源选择器
- 弹出式工具层
- 通用可复用交互控件库

---

## 9. 结论

ImGUISlate 最合理的职责是：

- 定义通用 widget 结构
- 提供事件抽象和绘制接口
- 封装一些标准 UI 控件
- 为上层工具/编辑器服务

而上层 widget 则负责：

- 业务场景
- 数据绑定
- 特定功能显示
- 组合 ImGUISlate 的基础组件

这是一种清晰、可扩展、低耦合的 UI 分层方式。