# Engine 模块架构说明

## 1. 模块定位

`Engine` 模块是整个项目中的核心运行时层，负责承载场景管理、视口管理、资源管理、摄像机和渲染相关的基础能力。它并不是单一类，而是由多个相互协作的抽象层和具体实现共同组成的模块体系。

从源码结构看，`src/Framework/Engine` 主要分为两部分：

- `PublicHeader/`：定义对外接口和核心类型
- `Private/`：实现模块注册、引擎实例和具体逻辑

对应的关键文件包括：

- `Engine.h`：定义抽象 `Engine` 基类
- `EngineModule.h`：定义模块入口，继承自 `Core::Module`
- `IEngine.h / IEngine.cpp`：具体引擎实例实现
- `SceneComponent.h` / `PrimitiveComponent.h` / `StaticMeshComponent.h`：场景和渲染对象层
- `Viewport.h` / `ViewportClient.h` / `SceneViewport.h`：视口与渲染窗口层
- `SceneInterface.h`：场景接口，连接游戏线程和渲染线程
- `Camera.h`：摄像机逻辑
- `AssetManager.h`：资产管理

---

## 2. 模块入口：`EngineModule`

`EngineModule` 是 `Engine` 模块的入口类，继承自 `Core::Module`：

```cpp
class ENGINE_API EngineModule : public Core::Module
```

它主要负责：

1. 模块注册：通过 `IMPLEMENT_SIMPLE_MODULE(EngineModule, "Engine")` 注册到 `Core::ModuleManager`
2. 启动时创建 `IEngine` 实例，并调用 `Init()`
3. 关闭时调用 `Shutdown()` 并释放相关资源

启动逻辑在 `EngineModule.cpp` 中：

```cpp
void EngineModule::StartupModule()
{
    EngineObj = std::make_unique<IEngine>();
    EngineObj->Init();
    GDistanceFieldMgr.Initialize();
    m_bLoaded = true;
}
```

这说明 `Engine` 模块的生命周期是由 `ModuleManager` 驱动的，而不是由业务层直接创建和销毁。

---

## 3. 抽象引擎接口：`Engine` 与 `IEngine`

### 3.1 基类 `Engine`

`Engine.h` 中定义了抽象接口：

```cpp
class ENGINE_API Engine
{
public:
    virtual void Init() = 0;
    virtual void Tick(float deltaTime) = 0;
    virtual void Shutdown() = 0;
};
```

它定义了引擎的统一生命周期接口：

- `Init()`：模块初始化
- `Tick(float deltaTime)`：每帧更新
- `Shutdown()`：退出清理

这是整个引擎模块的顶层抽象，可以视为“引擎运行时协议”。

### 3.2 派生实现 `IEngine`

`IEngine` 继承自 `Engine`：

```cpp
class ENGINE_API IEngine : public Engine
```

`IEngine::Tick()` 是当前项目中引擎帧更新的实际入口，`Loop::Run()` 中直接调用：

```cpp
Engine::GetEngineInstance()->Tick(deltaTime);
```

因此，`IEngine` 是当前实现版本的具体引擎对象，承担了运行时的帧调度和逻辑更新入口。

---

## 4. 引擎内部的核心派生关系

### 4.1 场景对象体系

整个场景系统的核心继承链如下：

```text
SceneComponent
    └── Camera

SceneComponent
    └── PrimitiveComponent
            └── StaticMeshComponent
```

#### `SceneComponent`

`SceneComponent` 是场景节点的基础类型，提供：

- 组件挂接关系：`AttachTo()` / `Detach()`
- 局部/世界变换：位置、旋转、缩放
- 场景归属：`SetSceneOwner()` / `GetSceneOwner()`
- 变换更新：`OnTransformChanged()`、`MarkTransformDirty()`

它是最底层的场景对象基类，用于表达节点树结构。

#### `Camera`

`Camera` 继承自 `SceneComponent`，属于视角与投影控制类。它负责：

- 视点位置、目标、上向量
- 透视/正交投影设置
- view/projection/view-projection matrix 计算
- WVP 矩阵和坐标变换

这意味着 `Camera` 本质上是一种“带有投影和观察逻辑的场景组件”。

#### `PrimitiveComponent`

`PrimitiveComponent` 也继承自 `SceneComponent`，用于表示可渲染对象的通用描述：

- 自身有 `_localTransform` / `_worldTransform`
- 具备 `Bounds` 与可见性标志
- 承担场景注册和 SceneProxy 创建流程
- 提供 `CreateSceneProxy()`、`UpdateTransform()` 等接口

它是渲染对象的游戏线程描述层，不直接持有 GPU 资源；真正的渲染数据由 `PrimitiveSceneProxy` 承担。

#### `StaticMeshComponent`

`StaticMeshComponent` 继承自 `PrimitiveComponent`，是可渲染静态网格的具体实现：

- 持有 `StaticMesh* m_Mesh`
- 支持材质覆盖：`SetMaterial()`
- 支持 LOD 控制：`SetForcedLOD()`
- 实现 `CreateSceneProxy()` 与 `GetBounds()`

这体现了引擎中“通用渲染组件 -> 特定渲染组件”的继承扩展方式。

---

### 4.2 SceneProxy 渲染快照体系

与 `PrimitiveComponent` 对应，项目中还存在一条 `SceneProxy` 的派生体系：

```text
PrimitiveSceneProxy
    └── StaticMeshProxy
```

#### `PrimitiveSceneProxy`

`PrimitiveSceneProxy` 是渲染线程侧的快照对象，负责存储渲染所需的结构化数据：

- `_primitiveId`
- `_proxyBounds`
- `_localToWorld` / `_worldToLocal`
- `_bVisible` / `_bCastShadow` / `_bOpaque`
- 需要渲染时的统一参数等

它提供了 `IsVisible()`、`CastsShadow()`、`IsOpaque()` 等接口，说明它是渲染线程的抽象视图。

#### `StaticMeshProxy`

`StaticMeshProxy` 继承自 `PrimitiveSceneProxy`，用于表示静态网格的渲染快照：

```cpp
class ENGINE_API StaticMeshProxy : public PrimitiveSceneProxy
```

它重写了：

- `HasStaticGeometry()`
- `IsDynamic()`

并且持有 `StaticMeshComponent*` 作为对应资源关联。

这套设计体现了典型的 GameThread / RenderThread 分层：

- `PrimitiveComponent`：游戏线程中的组件描述
- `PrimitiveSceneProxy`：渲染线程中的一份快照数据

两个层次之间通过显式的代理对象来解耦。

---

### 4.3 视口系统

视口层的继承关系如下：

```text
RenderTarget
    └── Viewport
            └── SceneViewport

ViewportClient : public SlateCore::EventHandler
```

#### `RenderTarget`

`RenderTarget` 定义了渲染目标接口：

```cpp
virtual RenderCore::RenderTexture* GetRenderTarget() = 0;
```

它是所有视口/渲染目标的基础契约。

#### `Viewport`

`Viewport` 继承自 `RenderTarget`，定义统一视口接口：

- `Draw()`
- `GetWidth()`
- `GetHeight()`

它的职责是封装窗口中的渲染视口逻辑。

#### `SceneViewport`

`SceneViewport` 继承自：

```cpp
class SceneViewport final : public Viewport, public SlateCore::SlateViewport
```

这说明它同时具备：

- 引擎视口能力（`Viewport`）
- Slate UI 窗口能力（`SlateCore::SlateViewport`）

它可以直接处理窗口尺寸、输入事件和渲染目标绑定，是整个引擎渲染输出的核心窗口对象。

#### `ViewportClient`

`ViewportClient` 继承自 `SlateCore::EventHandler`，表示它是一个事件处理器：

```cpp
class ENGINE_API ViewportClient : public SlateCore::EventHandler
```

它负责：

- `Draw(Viewport* InViewport)`
- `OnViewportResized()`
- `Tick(float DeltaTime)`

它与 `SceneViewport` 之间是典型的“客户端/视口控制器”关系。

---

## 5. 场景交互接口：`SceneInterface`

`SceneInterface` 不是具体类，而是一个抽象接口，用于连接游戏线程与渲染线程之间的场景数据流：

```cpp
class ENGINE_API SceneInterface {
public:
    virtual void AddPrimitive(PrimitiveComponent* Component) = 0;
    virtual void RemovePrimitive(PrimitiveComponent* Component) = 0;
    virtual void AddLight(LightComponent* Component) = 0;
    virtual void RemoveLight(LightComponent* Component) = 0;
    virtual void FlushPendingUpdates() = 0;
    virtual void NotifyComponentChanged(SceneComponent* Component) = 0;
};
```

它的价值在于：

- 游戏线程负责组件更新
- 渲染线程负责读取相应的 `SceneProxy`
- 通过 `SceneInterface` 统一触发场景变更、更新和同步

从设计上看，它起到了“场景管理协议”的作用，属于引擎模块的中间枢纽。

---

## 6. `SceneObject`：场景对象封装层

`SceneObject` 不是派生自 `SceneComponent`，而是对 `SceneComponent` 的外部包装：

```cpp
class ENGINE_API SceneObject
```

其内部持有：

```cpp
SceneComponent* m_RootComponent = nullptr;
```

它的职责是：

- 作为一个逻辑对象，将一个根组件挂接到对象上
- 通过 `GetRootComponent<T>()` 提供类型安全访问
- 使得场景对象具有“组件树”组织方式

它是一个更高层的对象封装层，通常用于对组件树的统一管理。

---

## 7. 资源管理：`AssetManager` / `AssetRegistry`

资产管理保留在 Engine 内部，不新增独立编译模块。资产是实际资源的拥有者，管理器负责资产的定位、加载请求与驻留；组件拥有仅供自身使用的运行时材质实例。

### 7.1 文件与职责

| 文件 | 职责 |
| --- | --- |
| `Asset.h` | `IAsset`、资产共享引用、`AssetInfo` 元数据 |
| `AssetRegistry.h/.cpp` | 路径/名称元数据查询，返回快照；不持有实际资源 |
| `AssetManager.h/.cpp` | 规范化路径、类型检查、请求合并、驻留和关闭；模板只包装通用实现 |
| `TextureAsset.h/.cpp` | 拥有纹理，封装纹理加载 |
| `StaticMeshAsset.h/.cpp` | 拥有网格，封装 glTF 加载和材质依赖构建 |
| `MaterialAsset.h/.cpp` | 独占拥有 `Material`，读取材质 JSON，保留纹理资产依赖 |
| `SkyLightAsset.h/.cpp` | 拥有环境纹理及 IBL 结果，封装现有预计算流程 |

为兼容原调用方，`AssetManager.h` 暂时包含上述资产头；这些头主要使用前置声明，文件解析及图形资源相关依赖位于具体实现中。没有新增 `MaterialInstanceAsset`，也没有引入尚无实际用途的加载器注册框架。

### 7.2 资源所有权

```text
AssetManager --驻留引用--> StaticMeshAsset --拥有--> StaticMesh
StaticMeshComponent --使用引用--> StaticMeshAsset
StaticMesh --材质依赖引用--> MaterialAsset --独占拥有--> Material
MaterialAsset --纹理依赖引用--> TextureAsset --拥有--> RenderTexture
StaticMeshComponent --独占拥有--> MaterialInstance（按槽位、按需创建）
MaterialInstance --父资产引用--> MaterialAsset
```

`StaticMesh` 和 `Material` 到所属 Asset 的反向引用为弱引用，避免所有权环。兼容接口仍允许传入资产资源的裸指针，但组件会据此获取并保留所属资产；不接受无资产归属的网格或材质。

glTF 内嵌材质使用 `MaterialAsset::Create` 创建子资产，由网格保存依赖引用，不创建材质实例。子资产路径形如 `模型路径#material/索引`，当前不单独写盘或注册到全局管理器。缺失材质的 primitive 显式加载并引用默认 `MaterialAsset`，调用方不必预加载默认材质。

### 7.3 网格材质编辑

有效材质按以下优先级选择：组件该槽位的材质实例、组件材质资产覆盖、网格默认材质资产。

```cpp
// 默认共享网格的材质，不创建实例。
component.SetStaticMesh(meshAsset->GetMesh());

// 首次编辑创建组件独占实例；后续编辑复用它。
component.SetMaterialParameterValue<
    Engine::EMaterialParameterSemantic::Roughness, Engine::FScalarType>(
        0, "Roughness", 0.8f);

// 丢弃该槽位的实例，恢复共享材质。
component.ResetMaterialInstance(0);
```

实例只保存参数覆盖，并强引用父 `MaterialAsset`；父材质版本变化后重建有效参数，保留实例自己的覆盖。参数内部键区分名称、语义和类型，允许同名颜色值与纹理参数共存。材质 JSON 接受旧 `parameters` 和现有分组格式 `scalarParameters/vectorParameters/textureParameters`，常用名称统一映射到渲染侧的 `BaseColor/Roughness/Metallic/Normal`。

`StaticMeshDrawBuild` 从组件 `GetMaterial` 取有效材质，使运行时实例参与实际绘制。编辑器以后应调用组件编辑接口；直接修改 `MaterialAsset::GetMaterial()` 会修改共享母材质，不代表只编辑选中的网格。

DefaultWhite 显式使用金属度 0.8、粗糙度 0.2，保留重构前实际使用的渲染外观。接通新的文件参数解析时，需要迁移原先回退参数的实际效果，不能默认认为修正解析就保持了视觉行为。

当前没有新增选中或属性面板 UI。材质数据已提供统一参数定义、运行时类型编辑、覆盖恢复及保存接口；纹理参数持有 TextureAsset 引用，批次持有不可变材质快照，Renderer 不再回读业务材质状态。材质编辑和快照发布仍须与帧构建串行，StaticMeshProxy 的其他组件访问尚未改为任意跨线程安全。详见 [材质系统与编辑器数据接口](material.md)。

### 7.4 加载、驻留与关闭

- 路径转换为绝对路径并做词法规范化，相同路径的别名共享加载记录。当前不提供 GUID、移动重定向或完整的 Windows 路径大小写归一。
- 同一路径只允许一种资产类型，缓存命中和加载中请求均检查类型，避免错误的静态指针转换。
- 同步与异步请求复用一个加载中记录；相同线程的递归加载环明确报错。普通 `Load()` 返回 false 时保持空结果语义，抛出的异常向同步等待者传播。
- `LoadAsync` 保存任务 future，所有请求都有各自的完成回调。回调在工作线程、管理器锁外执行，包括缓存命中；失败结果为空。任务异常在回收 future 时记录。
- `Unload` 和 `Clear` 释放驻留引用，不强制销毁仍由组件或其他资产使用的资源。弱索引允许重新加载仍存活的资产而不创建重复对象；清理前已开始的加载不能重新填充已清理的驻留。
- `WaitForLoads` 和 `Shutdown` 由控制线程调用，不能在加载回调中调用。Engine 关闭时停止接收新请求、收束任务并清理驻留；之后仍须遵循现有场景与 RHI 的释放顺序。

当前异步接口仍执行具体资产的完整 `Load()`，没有实现后台 CPU 导入与渲染线程初始化的分阶段调度；本轮并发验证使用不依赖 GPU 的资产。启动场景继续使用同步加载。twoSided 以外的静态开关、新的材质程序、跨线程依赖环检测、重导入及派生数据缓存尚不支持。

### 7.5 验证入口

`Tests.exe -test=AssetOwnershipTest` 使用独立生成的测试材质，验证材质格式、索引、路径别名、按需实例、组件间隔离、父材质更新、清理后的保活、重复请求回调和类型错误；该测试不初始化 RHI，也不依赖场景默认材质的美术数值。

渲染回归使用 `Langcher.exe`。2026-10-22 修复默认材质的外观变化后，分别运行保留的重构前程序和当前程序，等待场景完成绘制后抓取窗口；本次场景区域像素差为 0，两次均通过 WM_CLOSE 退出 0。日志仍含既有 Shader 与退出销毁校验错误，不能记为 Vulkan Validation 全通过。根因、验证修正及证据见 [故障与修复记录](../dev/bugfix/故障与修复记录-2026-10-22.md)。

---

## 8. 引擎模块的架构分层

综合源码分析，当前 `Engine` 模块可以抽象成以下分层：

```text
[Engine 模块入口]
    EngineModule
        ↓
    Engine / IEngine
        ↓
    场景与对象层
        SceneObject
        SceneComponent
        Camera
        PrimitiveComponent
        StaticMeshComponent
        ↓
    渲染快照层
        PrimitiveSceneProxy
        StaticMeshProxy
        ↓
    视口与窗口层
        RenderTarget
        Viewport
        SceneViewport
        ViewportClient
        ↓
    资源管理层
        AssetRegistry
        AssetManager
        ↓
    场景接口层
        SceneInterface
```

可以看到，当前引擎模块的核心思路并不是“一个大类塞满所有功能”，而是采用了典型的分层设计：

- 组件层：负责逻辑对象和场景节点
- 代理层：负责渲染线程访问
- 视口层：负责窗口和渲染输出
- 资源层：负责内容加载与登记
- 模块入口层：负责生命周期管理

---

## 9. 总结

`Engine` 模块的架构特点可以概括为：

1. 以 `Core::Module` 为入口，由 `EngineModule` 注册并管理生命周期。
2. 由 `Engine` 抽象接口统一定义 `Init / Tick / Shutdown`。 
3. `IEngine` 作为具体实现，负责引擎主循环的实际运行。
4. 场景系统采用从 `SceneComponent` 到 `PrimitiveComponent` 再到 `StaticMeshComponent` 的递进继承设计。
5. 渲染侧采用 `PrimitiveSceneProxy` 到 `StaticMeshProxy` 的快照代理模式来解耦游戏线程和渲染线程。
6. 视口系统采用 `Viewport` / `ViewportClient` / `SceneViewport` 的桥接结构，实现渲染输出和输入事件控制。
7. 资源管理和场景接口为引擎的各类功能提供统一扩展入口。

因此，当前 `Engine` 模块不是简单的“单一逻辑类”，而是一个以分层对象体系和模块化生命周期为核心的中间层架构。

## 相关核心实现细节

本篇保留架构职责说明；执行时序、数学原理及源码限制见以下专题。本轮为源码整理，专题不等于运行验证通过。

- [资产并发、材质与实例数据](../key_implemetion/performance/05-assets-materials-and-instance-data.md)
- [局部/全局距离场](../key_implemetion/algorithm/04-distance-fields-and-jump-flood.md)
- [网格导入与切线空间](../key_implemetion/algorithm/08-mesh-import-and-geometry.md)
- [层级变换与视锥](../key_implemetion/algorithm/03-transforms-frustum-and-depth.md)
