# RHI 模块架构说明

## 1. 模块定位

`RHI` 模块是整个渲染栈中的底层抽象层，负责统一封装各种图形 API 的公共能力。它不直接绑定某个具体平台（如 Vulkan、D3D12），而是定义一组统一的资源、命令、管线和同步接口，为上层的 `Renderer`、`Engine` 以及具体渲染实现提供稳定的访问协议。

从源码结构看，`src/Framework/RHI/PublicHeader` 中的核心文件包括：

- `RHIDefine.h`：定义资源类型、格式、纹理状态、队列类型、着色器类型等基础枚举与描述结构
- `RHIResource.h`：定义资源基类、纹理、缓冲区、视图、着色器等继承体系
- `RHICommandContex.h`：定义图形/计算命令上下文接口
- `RHIApi.h`：定义 RHI 的统一 API 抽象接口
- `RHITransition.h` / `RHITransientResource.h`：定义资源状态转换和临时资源管理

整体上，`RHI` 模块属于“抽象接口层”，其核心目标是让上层无需关心底层具体 API 实现细节。

---

## 2. 抽象接口：`RHIApi`

`RHIApi` 是 RHI 模块中最重要的接口类，定义了平台无关的资源创建和渲染能力：

```cpp
class RHI_API RHIApi
{
public:
    virtual bool Init() = 0;
    virtual void Shutdown() = 0;
    virtual const RHIPlatformInfo& GetPlatformInfo() const = 0;

    virtual RHITextureSP CreateTexture(const RHITextureDesc& desc) = 0;
    virtual RHIBufferSP CreateBuffer(const RHIBufferDesc& desc) = 0;
    virtual RHIGraphicsPipelineStateSP CreateGraphicsPipelineState(...) = 0;
    virtual RHIComputePipelineStateSP CreateComputePipelineState(...) = 0;
    virtual RHISwapchainSP CreateSwapchain(...) = 0;
    virtual RHIQueue* GetQueue(EQueueType Type) = 0;
};
```

它负责统一暴露以下能力：

- 创建纹理、缓冲区、交换链
- 创建顶点/像素/计算/光追管线状态
- 创建着色器和资源视图
- 分发命令、获取队列和同步对象
- 处理资源转换和临时资源管理

也就是说，`RHIApi` 是整个 RHI 模块的“统一驱动接口”。

---

## 3. 模块入口：`RHIModule`

`RHIModule` 继承自 `Core::Module`：

```cpp
class RHI_API RHIModule : public Core::Module {
public:
    virtual RHIApi* CreateRHIApi() = 0;
};
```

它的职责是：

1. 作为模块入口注册到 `ModuleManager`
2. 创建对应平台实现的 `RHIApi`
3. 让具体图形后端在模块启动时初始化自身设备和资源系统

这说明 `RHI` 模块本身依然保留模块化生命周期管理，而底层 API 细节由具体实现类负责。

---

## 4. 资源体系：`RHIResource` 与派生关系

`RHI` 资源层是架构的核心部分，主要遵循如下继承关系：

```text
RHIResource
    ├── RHIViewableResource
    │    ├── RHITexture
    │    ├── RHIBuffer
    │    └── RHIRayTracingAccelerationStructure
    │
    ├── RHIShader
    │    ├── RHIVertexShader
    │    ├── RHIFragmentShader
    │    ├── RHIComputeShader
    │    └── ...
    │
    ├── RHIShaderResourceView
    ├── RHIUnorderedAccessView
    └── ...
```

### 4.1 `RHIResource`

`RHIResource` 是基础资源基类，提供：

- `_resourceType`
- `GetResourceType()`

它使所有资源类型具备统一的类型标识，方便上层管理和调试。

### 4.2 `RHIViewableResource`

`RHIViewableResource` 继承自 `RHIResource`，用于表示可被绑定为视图（SRV/UAV）的资源。它是 `RHITexture` 与 `RHIBuffer` 的基类。

### 4.3 `RHITexture` / `RHIBuffer`

这两个类分别表示：

- `RHITexture`：纹理资源，持有 `RHITextureDesc`
- `RHIBuffer`：缓冲区资源，持有 `RHIBufferDesc`

它们都具备统一的资源查询接口，并由平台具体实现提供实例化。

### 4.4 `RHIShader` 与各类着色器

`RHIShader` 进一步衍生出不同的图形/计算着色器：

- `RHIVertexShader`
- `RHIFragmentShader`
- `RHIGeometryShader`
- `RHIComputeShader`
- `RHIRayGenShader` / `RHICloseHitShader` / `RHIMissShader` 等

这些类统一通过 `ERHIShaderFrequency` 标识阶段类型，使上层渲染代码能够按阶段创建和绑定着色器。

---

## 5. 命令上下文：`RHIContextBase`

`RHICommandContex.h` 定义了命令执行的统一抽象：

```cpp
class RHIContextBase
{
public:
    virtual void Begin() = 0;
    virtual void End() = 0;
    virtual void CopyTexture(...) = 0;
    virtual void BlitTexture(...) = 0;
};
```

进一步扩展出：

```text
RHIContextBase
    ├── RHIComputeContex
    │    └── Dispatch(...)
    └── RHIGraphicContex
         ├── SetGraphicPipelineState(...)
         ├── SetViewport(...)
         ├── Draw(...)
         ├── DrawIndexed(...)
         ├── BeginRenderPass(...)
         └── TraceRays(...)
```

这说明 RHI 把图形/计算/光追操作统一放在上下文接口中，不直接绑定具体 API 调用方式。上层代码只依赖抽象接口，而具体实现由 Vulkan 后端负责。

---

## 6. 资源视图与同步机制

### 6.1 视图

RHI 提供了两类典型视图：

- `RHIShaderResourceView`
- `RHIUnorderedAccessView`

它们从 `RHIResourceView` 扩展而来，用于绑定纹理/缓冲区到不同的着色器阶段。

### 6.2 同步与队列

RHI 还定义了：

- `EQueueType`：图形队列 / 计算队列
- `RHISyncPoint`：GPU 同步点
- `RHIWaitInfo`：等待依赖描述
- `RHIQueue`：队列接口
- `RHIPresentExecutor`：交换链呈现接口

这些对象构成了 RHI 底层的同步和提交模型，使上层能够在抽象层面进行资源屏障、队列等待、交换链展示等工作。

---

## 7. 管线状态与状态对象

RHI 定义了大量渲染状态对象，例如：

- `RHIGraphicsPipelineState`
- `RHIComputePipelineState`
- `RHIRayTracingPipelineState`
- `RHIVertexDescState`
- `RHIRasterizerState`
- `RHIColorBlendState`
- `RHIDepthStencilState`
- `RHISampler`

它们在 `RHIApi` 中被统一创建，说明 RHI 把管线和状态对象抽象成一层“平台无关的渲染配置模型”。

光追管线的 `RHIRayTracingPipelineStateDesc` 保留 RayGen、Miss、HitGroup、Callable 四张表。`HitGroupTable` 的每项为 `RHIRayTracingHitGroupDesc`，显式组合 `ClosestHitShader`、`AnyHitShader`、`IntersectionShader`，不再通过独立的 Intersection 表按下标配对。三个字段均为非拥有的阶段 Shader 指针，沿用现有生命周期约定，调用方负责保活；此描述不创建新的 Shader 资源，也不改变逐阶段编译流程。Intersection 非空表示程序化几何命中组，否则为三角形命中组；ClosestHit 和 AnyHit 可为空。每项组合对应一个原生命中组及其 SBT group handle，后端需处理所有非空阶段的反射与入口。

---

## 8. 资源转换与临时资源管理

RHI 不仅提供资源创建能力，还提供了资源转换和临时分配机制：

- `RHITransition`：描述资源状态转换
- `RHITransientResourceManager`：管理临时资源生命周期

这体现了现代图形 API 的常见做法：

- 资源布局由状态转换管理
- 临时对象由独立管理器负责生命周期
- 上层不直接关心具体平台的内存或资源布局细节

---

## 9. 架构总结

`RHI` 模块可以概括为一个“抽象图形接口层”，其核心设计特点是：

1. 用 `RHIApi` 定义统一 API 抽象
2. 用 `RHIModule` 作为模块化入口
3. 用 `RHIResource` 建立统一资源继承体系
4. 用 `RHIContextBase` / `RHIGraphicContex` / `RHIComputeContex` 抽象命令执行
5. 用状态对象、同步对象和队列接口统一管理渲染过程
6. 让具体后端（如 Vulkan）只需要实现这一套接口，而不影响上层代码结构

因此，`RHI` 模块本质上是整个渲染系统的“硬件接口抽象层”，它是 `Renderer` 和 `Engine` 之间的关键桥接层。

## 相关核心实现细节

本篇保留架构职责说明；执行时序、数学原理及源码限制见以下专题。本轮为源码整理，专题不等于运行验证通过。

- [队列、context 与完成点](../../key_implemetion/performance/02-rhi-queues-and-sync.md)
- [内存与 transient 分配复用](../../key_implemetion/performance/04-memory-and-transient-resources.md)
- [显式 transition 与 Tracker](../../key_implemetion/performance/03-rdg-execution-and-state.md)
- [原生 AS、scratch 与 SBT](../../key_implemetion/performance/07-ray-tracing-as-and-sbt.md)
- [管线状态缓存、深度状态组合与碰撞限制](../../key_implemetion/performance/06-shader-compilation-and-binding.md)
