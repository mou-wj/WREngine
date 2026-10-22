# ApplicationBase 模块架构说明

## 1. 模块定位

`ApplicationBase` 位于 `SlateCore` 之下，是整个应用层的统一抽象基类。它定义了“一个应用应该具备什么能力”的公共接口，并为上层业务应用提供全局单例入口。

对应源码：

- `src/Framework/SlateCore/PublicHeader/ApplicationBase.h`

从语义上看，`ApplicationBase` 不是具体业务逻辑，而是一个运行时框架契约。它约束了：

- 应用初始化
- 退出请求
- 关闭流程
- 每帧更新
- 全局应用对象访问

它是 `App::Application` 这类派生类的基底，也是主循环与业务层之间的桥接对象。

---

## 2. 设计目标

`ApplicationBase` 的目标是把应用生命周期统一抽象成标准接口，降低 `Lancher` 和 `Engine` 对具体应用实现的耦合。

在本项目中，主循环并不直接持有某个具体应用，而是通过 `ApplicationBase` 访问当前应用实例。这样可以把：

- 启动流程
- 退出状态
- 主循环驱动
- window / input / scene 交互

集中到一个统一接口中。

---

## 3. 核心接口定义

`ApplicationBase` 的接口非常简洁：

```cpp
class SLATECORE_API ApplicationBase
{
public:
    virtual ~ApplicationBase() = default;

    virtual bool Initialize() = 0;
    virtual bool RequestExit() = 0;
    virtual void Shutdown() = 0;

    virtual void TickFrame() = 0;

    static ApplicationBase* GetApplication();
    static void SetApplication(ApplicationBase* InApplication);
};
```

这几个函数分别代表：

- `Initialize()`：应用初始化，通常在启动时创建窗口、绑定场景和 UI
- `RequestExit()`：返回是否退出请求
- `Shutdown()`：释放资源并关闭底层对象
- `TickFrame()`：每帧驱动应用逻辑

也就是说，`ApplicationBase` 把应用视为一个标准的帧驱动对象，而不是任意脚本或全局状态。

---

## 4. 全局单例机制

`ApplicationBase` 提供了一个静态全局访问入口：

```cpp
static ApplicationBase* GetApplication();
static ApplicationBase* SetApplication(ApplicationBase* InApplication);
```

这种方式的意义是：

- `Lancher` / 主循环可以通过统一的接口访问当前应用
- 业务代码可以获取当前应用对象，而无需知道具体派生类型
- `Application` 与底层框架之间保持较弱耦合

这个设计与很多游戏引擎中的 `GetGameInstance()` / `GetApplication()` 模式相似，属于典型的“全局运行时对象”设计。

---

## 5. 与 `SlateRenderer` 的协同

`ApplicationBase` 内部保留了一个 `SlateRenderer* _renderer` 成员：

```cpp
protected:
    SlateRenderer* _renderer = nullptr;
```

这说明应用层的基础类并不直接绑定某个具体渲染器，而是允许派生类在初始化阶段注入：

- `SlateRHIRenderer`
- 或其它窗口渲染实现

这样做的好处是：

- 事件循环和 UI 渲染是可插拔的
- `ApplicationBase` 对渲染后端保持抽象
- 应用层只关心“我有一个 renderer，用于绘制窗口内容”，而不写死底层实现

---

## 6. 层级定位

从架构角度看，`ApplicationBase` 属于“应用框架层”，它位于：

```text
SlateCore / App Framework
    └── ApplicationBase
        └── App::Application
```

其上层是具体应用逻辑，下层是：

- `SlateCore` 的窗口、事件和 UI 抽象
- `Renderer` / `SlateRHIRenderer` 的绘制实现
- `Engine` 的场景、viewport、世界逻辑

这意味着它不是最底层的系统，而是“业务应用入口的抽象接口层”。

---

## 7. 与 `Lancher` 的关系

主循环在 `Lancher` 中大致按如下方式驱动：

```cpp
Engine::GetEngineInstance()->Tick(deltaTime);
ApplicationBase::GetApplication()->TickFrame();
```

也就是说：

- `Engine` 负责运行时和场景更新
- `ApplicationBase` 负责应用层帧驱动
- `Lancher` 负责协调两者的更新顺序

这种结构使应用层与引擎层能保持清晰分离：

- 引擎负责底层世界逻辑
- 应用负责窗口/界面/交互/编辑器行为

---

## 8. 架构总结

`ApplicationBase` 的本质是：

1. 提供统一的应用生命周期接口
2. 允许全局访问当前应用对象
3. 将应用视为一个帧驱动系统
4. 跨越底层 `SlateCore` 与上层具体业务之间建立中间桥接

它是整个应用层的“抽象根”——所有实际业务应用都应继承它，并在其生命周期中负责：

- 窗口创建
- viewport 绑定
- UI 面板配置
- 事件处理
- shutdown 清理

因此，`ApplicationBase` 是 `App::Application` 这类具体程序的基础骨架，也是项目中从“框架层”走向“业务应用层”的关键边界。