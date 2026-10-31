# RenderCore 模块架构说明

## 1. 模块定位

`RenderCore` 模块位于 `RHI` 之上，是渲染系统的中间核心层，负责把底层图形 API 抽象能力进一步组织成更高层次的渲染资源、渲染图、纹理视图、资源跟踪和渲染线程调度能力。它不是最终渲染实现，而是渲染系统的“基础设施层”。

从源码结构看，`src/Framework/RenderCore/PublicHeader` 中的关键文件包括：

- `RenderGraphBuilder.h`：渲染图构建器，负责 pass 与资源依赖关系组织
- `RenderGraphPass.h`：渲染 pass 抽象与 lambda pass 实现
- `RenderGraphResource.h`：渲染图资源描述，如 texture / buffer / SRV / UAV
- `RenderResource.h`：真实渲染资源封装，例如 `RenderTexture`、`RenderBuffer`
- `RenderThread.h`：渲染线程管理与命令队列
- `GlobalShader.h`：全局 shader 的注册与缓存机制
- `Shader.h`、`ShaderParameter.h` 等：Shader 编译、参数反射和参数绑定基础设施

从架构上看，`RenderCore` 负责的是“渲染资源编排”和“渲染执行调度”，它是 `Renderer` 层实现真正渲染逻辑的基础设施。

---

## 2. 设计目标

`RenderCore` 的设计目标是把渲染过程抽象为：

- 资源：纹理、缓冲区、SRV、UAV
- pass：每个渲染步骤或计算步骤
- 依赖：pass 之间的资源读写依赖
- 执行：按图构建、排序和执行

因此，它非常接近现代渲染图框架（Render Graph）的设计思路，类似 UE 的 RDG（Render Dependency Graph）。

---

## 3. 渲染图核心：`RenderGraphBuilder`

`RenderGraphBuilder` 是 `RenderCore` 中最重要的类之一。它负责：

- 定义渲染资源：`CreateTexture()` / `CreateBuffer()`
- 定义资源视图：`CreateTextureSRV()` / `CreateBufferUAV()`
- 添加 pass：`AddPass(...)`
- 建立 pass 依赖：`AddPassDependency(...)`
- 执行渲染图：`Execute()`

关键结构：

```cpp
class RENDERCORE_API RenderGraphBuilder
{
public:
    template <typename TParameterStruct, typename TExecuteLambda>
    RenderGraphPassRef AddPass(...);

    RenderGraphTextureRef CreateTexture(...);
    RenderGraphBufferRef CreateBuffer(...);
    void AddPassDependency(RenderGraphPass* pass, RenderGraphPass* passConsumer);
    void Execute();
};
```

这说明它不直接执行渲染，而是先构建 render graph，再统一调度执行。这样的组织方式可以更好地控制：

- 资源生命周期
- pass 依赖顺序
- transition / barrier
- 资源复用和状态同步

---

## 4. Pass 体系：`RenderGraphPass`

`RenderGraphPass` 是渲染图中的执行单元：

```cpp
class RENDERCORE_API RenderGraphPass
{
public:
    virtual void Execute(RHI::RHICommandListBase& commandList) = 0;
};
```

它通过以下机制表达 pass 的语义：

- `_name`：pass 名称
- `_passFlag`：图形 pass 或计算 pass
- `_textureIntents`：资源访问意图
- `_bufferStates`：缓冲区访问意图
- `_passProducers` / `_passConsumers`：依赖前后关系

`RenderGraphLambdaPass` 是一个模板实现，它允许把渲染逻辑直接写成 lambda：

```cpp
auto pass = new RenderGraphLambdaPass<TParam, TLambda>(...);
```

这意味着 `RenderCore` 支持非常轻量的 pass 描述方式，方便 `Renderer` 层使用。

缓冲上传通过 AddUploadBuffer 将数据复制进图内 arena，再由 AddUploadPass 登记 TransferDest。上传描述符在 lambda 中复制，原列表在依赖登记后清空，使 Immediate 上传不会重复进入延迟队列。后续读取必须声明 RDGBuffer_SRV，SetupPassInternal 才会将同一图内缓冲的上传和读取连接，并生成必要转换；GPU 完成 Fence 仍由既有提交处记录。具体调用时序、类型分类及限制见 [RDG 核心实现](../../key_implemetion/performance/03-rdg-execution-and-state.md)。

---

## 5. 渲染图资源：`RenderGraphResource` 体系

RenderCore 通过 `RenderGraphResource` 抽象渲染图中的资源对象：

```text
RenderGraphResource
    ├── RenderGraphTexture
    ├── RenderGraphBuffer
    ├── RenderGraphView
    │    ├── RenderGraphSRV
    │    │    ├── RenderGraphTextureSRV
    │    │    └── RenderGraphBufferSRV
    │    └── RenderGraphUAV
    │         ├── RenderGraphTextureUAV
    │         └── RenderGraphBufferUAV
```

### 5.1 `RenderGraphTexture`

`RenderGraphTexture` 是渲染图中的纹理资源对象，它包含：

- `RenderGraphTextureDesc`
- 实际对应的 `RHI::RHITexture*` 资源
- `RenderTextureTracker` 记录其生命周期和访问状态

### 5.2 `RenderGraphBuffer`

同理，`RenderGraphBuffer` 表示渲染图中的缓冲区资源，并维护：

- `RenderGraphBufferDesc`
- `RHI::RHIBuffer*`
- `RenderBufferTracker`

### 5.3 SRV / UAV

`RenderGraphTextureSRV`、`RenderGraphTextureUAV`、`RenderGraphBufferSRV`、`RenderGraphBufferUAV` 扩展出不同的资源视图。它们是 pass 绑定资源时最常见的对象类型，作用是把资源变成可读或可写绑定点。

---

## 6. 真实资源封装：`RenderTexture` / `RenderBuffer`

在更高一层，`RenderCore` 又把底层 RHI 资源包装成更易用的资源类型：

```text
RenderResource
    ├── RenderTexture
    └── RenderBuffer
```

### 6.1 `RenderTexture`

`RenderTexture` 继承自 `RenderResource`，负责：

- 初始化/释放 RHI 纹理：`InitRHIResource()` / `ReleaseRHIResource()`
- 资源上传：`UploadData(...)`
- 资源读取：`ReadData(...)`
- mipmap 生成：`GenerateMipMaps()`
- 提供视图缓存：`GetViewCache()`

它内部持有：

```cpp
RHI::RHITextureSP m_Texture;
TextureViewCache m_ViewCache;
RenderTextureTracker m_Tracker;
```

这说明真实纹理资源是对底层 `RHITexture` 的进一步封装，并附带视图缓存和状态跟踪。

### 6.2 `RenderBuffer`

`RenderBuffer` 与之对应，负责：

- 底层 `RHI::RHIBuffer` 封装
- `UploadData()`
- `GetViewCache()`
- `RenderBufferTracker`

也就是说，`RenderCore` 把所有实际渲染资源都统一收敛到“资源类 + 视图缓存 + tracker”的结构中。

---

## 7. 资源跟踪器：`RenderResourceTracker` 系列

`RenderCore` 中有一组 `RenderTextureTracker`、`RenderBufferTracker` 等资源跟踪器：

```text
RenderResourceTrackerBase
    ├── RenderTextureTracker
    └── RenderBufferTracker
```

这些 tracker 的作用是：

- 跟踪资源的访问状态
- 管理生命周期与绑定关系
- 支持渲染图中的资源状态转换和 barrier 生成

它们是 `RenderGraphResource` 和 `RenderResource` 之间的重要桥接层，帮助实现基于状态的渲染图调度。

---

## 8. 渲染线程：`RenderThread`

`RenderThread.h` 中定义了渲染线程管理：

```cpp
class RENDERCORE_API RenderThread
{
public:
    void Start();
    void Stop();
    void EnqueueCommand(const RenderCommand& cmd);
    void ExecuteSync(const RenderCommand& cmd);
};
```

它负责：

- 创建渲染线程
- 维护 `m_CommandQueue`
- 执行后台渲染命令
- 管理同步和回调

同时提供了全局函数：

- `StartRenderThread()`
- `StopRenderThread()`
- `EnqueueRenderCommand(...)`
- `ExecuteSync(...)`

这说明 `RenderCore` 还承担了“渲染线程调度”的角色，使得渲染工作与游戏线程解耦。

---

## 9. Shader 体系：`Shader` / `GlobalShader`

`RenderCore` 中的 shader 体系为渲染模块提供统一的 shader 编译和缓存机制：

```text
ShaderType
    └── GlobalShaderType

Shader
    └── GlobalShader
```

### 9.1 `GlobalShaderType`

`GlobalShaderType` 用于描述全局着色器类型，包括：

- shader 名称
- 源文件路径
- entry point
- shader frequency（vertex、fragment、compute 等）
- 编译环境和 permutation 配置

### 9.2 `GlobalShader`

`GlobalShader` 继承自 `Shader`，作为全局 shader 的基础类；其对应的 `GlobalShaderMap` 则负责缓存已经编译好的实例。

这说明 `RenderCore` 既负责底层资源与图执行，也负责 shader 资源的编译、缓存和查询。

---

## 10. 公共默认资源

RenderResource 提供 GlobalWhiteTexture 和 GlobalFlatNormalTexture，分别上传白色与平面法线像素，由 InitGlobalRenderResource / ReleaseGlobalRenderResource 统一管理。Engine 的材质绑定只引用这些 RenderTexture，不创建默认 TextureAsset。

材质调整不改变 RenderGraphBuilder 的 Pass 管理、提交及资源 Tracker 更新流程。RenderResource 没有新增通用 retained 对象容器；同步和释放继续遵守既有约定，调用方仍须在实际访问提交处更新资源跟踪。

## 11. 架构总结

`RenderCore` 模块的本质是“渲染基础设施层”，其核心架构可以概括为：

1. `RenderGraphBuilder` 负责组织渲染图和 pass 依赖
2. `RenderGraphPass` 负责描述单个渲染步骤的执行逻辑
3. `RenderGraphResource` 负责描述纹理、缓冲区和视图资源
4. `RenderTexture` / `RenderBuffer` 则是底层 RHI 资源的高层封装
5. `RenderThread` 提供渲染线程和命令队列管理
6. `GlobalShader` / `Shader` 提供统一的 shader 编译与缓存接口
7. 这些能力共同为 `Renderer` 层提供更高层的渲染实现平台

因此，`RenderCore` 是整个渲染系统中最关键的中间层之一：它把底层 `RHI` 能力提升为可以进行渲染图编排和线程级调度的基础设施。

## 相关核心实现细节

本篇保留架构职责说明；执行时序、数学原理及源码限制见以下专题。本轮为源码整理，专题不等于运行验证通过。

- [CPU 任务和渲染线程](../../key_implemetion/performance/01-task-and-render-threads.md)
- [RDG 依赖、录制、提交和状态更新](../../key_implemetion/performance/03-rdg-execution-and-state.md)
- [内存和瞬态资源](../../key_implemetion/performance/04-memory-and-transient-resources.md)
- [参数布局、反射和 descriptor 回收](../../key_implemetion/performance/06-shader-compilation-and-binding.md)
