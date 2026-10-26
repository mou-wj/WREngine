# RHIDirectX12 模块架构说明

## 1. 模块定位

`RHIDirectX12` 是 `RHI` 抽象层在 Windows 上的 Direct3D 12 实现，负责将统一的资源、管线、命令、状态转换及同步接口落实为 D3D12 / DXGI 调用。上层通过 `RHI::RHIApi` 使用后端，无需接触描述符堆、Root Signature 或原生 Fence。

模块位于 `src/Framework/RHIDirectX12`，主要文件如下：

| 文件 | 职责 |
| --- | --- |
| `PublicHeader/DirectX12RHIApi.h` | 后端统一入口与模块生命周期 |
| `PublicHeader/DirectX12Device.h` | DXGI 工厂、适配器、设备、原生队列及 CPU 描述符分配 |
| `PublicHeader/DirectX12Resource.h` | 纹理、缓冲区、视图、着色器、交换链及资源状态 |
| `PublicHeader/DirectX12PipelineState.h` | 图形 / 计算 PSO、Root Signature 与反射绑定信息 |
| `PublicHeader/DirectX12Context.h` | 命令录制、资源访问校验与提交前资源保活 |
| `PublicHeader/DirectX12Queue.h` | 上下文池、队列提交、Fence 及呈现执行器 |
| `Private/DirectX12TransientResource.h` | 瞬态纹理 / 缓冲区的安全复用池 |
| `Private/DirectX12RHIUtils.h/.cpp` | RHI 到 D3D12 / DXGI 的枚举转换及公共辅助函数 |
| `Private/DirectX12ShaderAccess.h/.cpp` | DXIL 缓冲区实际访问分析，区分原生 UAV 绑定类别与逻辑读写权限 |

当前 CMake 将其构建为动态库，依赖 `RHI`、`RenderCore`，并在 Windows 下链接 `d3d12`、`dxgi`；着色器编译与反射使用 DXC。

---

## 2. 模块入口：`DirectX12RHIModule`

`DirectX12RHIModule` 继承自 `RHI::RHIModule`，通过模块系统注册 `RHIDirectX12`。

启动阶段创建 `DirectX12RHIApi`，将其赋给 `RHI::GRHIApi` 并初始化；退出阶段调用后端的 `Shutdown()`，销毁 API 对象。

`DirectX12RHIApi::Init()` 负责：

1. 将着色器平台设置为 `ERHIShaderPlatform::D3D12`。
2. 设置平台信息：深度范围为 `[0, 1]`，光线追踪能力关闭。
3. 设置 `RHITransition` 后端私有数据的大小和对齐。
4. 初始化 `DirectX12Device`、图形 / 计算逻辑队列与呈现执行器。

`Shutdown()` 等待两个逻辑队列空闲，清理读回暂存、呈现执行器、队列和设备。调用方应在关闭 API 前释放资源及瞬态资源管理器。

---

## 3. 统一入口：`DirectX12RHIApi`

`DirectX12RHIApi : RHI::RHIApi` 提供资源、视图、着色器、管线、交换链、队列和瞬态管理器的创建接口。

上传和读回通过后端命令对象加入 `RHICommandListBase`，保留统一命令列表的即时 / 延迟执行语义。延迟上传会保存调用方数据的副本，避免执行时读取已经失效或被修改的 CPU 内存。

该对象协调后端各部分；设备对象管理原生设备与描述符分配，资源对象封装原生资源，命令上下文负责录制实际命令。

---

## 4. 资源实现体系

```text
RHITexture                 -> DirectX12Texture
RHIBuffer                  -> DirectX12Buffer
RHIShaderResourceView      -> DirectX12ShaderResourceView
RHIUnorderedAccessView     -> DirectX12UnorderedAccessView
RHIStagingBuffer           -> DirectX12StagingBuffer
RHISwapchain               -> DirectX12Swapchain
RHISampler                 -> DirectX12Sampler
```

### 4.1 纹理与缓冲区

`DirectX12Texture` 和 `DirectX12Buffer` 封装 `ID3D12Resource`。普通资源使用 committed allocation；CPU 可访问缓冲区使用 UPLOAD heap，原生状态固定为 `GENERIC_READ`。常量缓冲区的原生分配按 256 字节对齐。

纹理按需缓存 RTV、DSV 和默认 SRV，避免每帧重复分配描述符。资源对象保存最后一次提交的逻辑队列及 Fence 值，用于同步和安全复用。

### 4.2 SRV / UAV 视图

SRV 和 UAV 分别创建对应的 CPU 描述符，并保存纹理视图覆盖的 mip / array slice 子资源索引。参数绑定校验视图实际覆盖的范围，无需把整张纹理都转换到同一状态。

SRV 表达逻辑只读访问，UAV 表达无序读写访问；它们不要求与原生 API 的描述符类别一一对应。例如 Vulkan 的结构化 Buffer SRV 使用 `STORAGE_BUFFER`，D3D12 读取 HLSL `RWStructuredBuffer` 时则仍需要原生 UAV 描述符，即使着色器实际上没有写入。

`DirectX12ShaderAccess` 通过 DXC 反汇编分析 DXIL 的资源句柄使用。对已经确认只有 load / 查询操作的结构化或原始缓冲区，Buffer SRV 可以生成并缓存相应的原生 UAV 描述符。实际存在 store、atomic、counter 操作，或者句柄用途无法确认时，仍要求调用方使用逻辑 UAV。

此路径始终校验 `DeclaredAccess` 包含 SRV。执行 Draw / Dispatch 前临时切换到原生 `UNORDERED_ACCESS`，执行后恢复原生状态，逻辑访问声明保持不变。默认堆上的 ShaderResource buffer 会预留原生 UAV 能力；该创建能力不授予 RHI 写权限。UPLOAD buffer 固定在 `GENERIC_READ`，不支持该原生 UAV 读取路径。

视图和命令录制持有资源使用计数，阻止瞬态池在仍有视图或未完成命令使用资源时复用它。

### 4.3 交换链

`DirectX12Swapchain` 基于窗口句柄创建双缓冲 `IDXGISwapChain3`，使用 flip-discard 呈现模式。交换链纹理包装 DXGI 的真实 back buffer，通过 `GetCurrentBackBufferIndex()` 选择当前图像。

交换链使用 HWND 的实际客户区尺寸。创建时将 DXGI 的 Width / Height 设为 0，让 DXGI 解析客户区，再通过 `GetDesc1()` 保存真实尺寸并填写 back-buffer 的 RHI 描述。不能直接把外部传入的窗口外框尺寸当成 framebuffer 尺寸，否则按客户区 Blit 时会留下右侧和底部未覆盖区域。调整尺寸时同样解析客户区；零尺寸事件保留原有图像。

调整尺寸前等待队列空闲并释放旧图像包装，再调用 `ResizeBuffers()`。呈现由 `DirectX12PresentExecutor` 等待依赖后调用交换链的 `Present()`。

交换链显式接收后端的图形 / 计算队列，不通过全局 `GRHIApi` 查找销毁依赖。析构同样等待队列空闲，保证 `Present()` 返回后仍被 GPU 使用的 back buffer 不会提前释放。

---

## 5. 管线与着色器参数

`DirectX12GraphicsPipelineState` 与 `DirectX12ComputePipelineState` 分别继承统一的图形 / 计算管线接口，持有 `ID3D12PipelineState`、Root Signature 和绑定信息。

着色器对象共用 `DirectX12ShaderBase`，解包 DXIL 并通过 DXC 反射取得资源寄存器、常量缓冲区大小和顶点输入语义。创建时还会保存每个缓冲区 UAV 是否已确认只读，避免在每次参数绑定时重新分析。

光追管线通过 `RHIRayTracingHitGroupDesc` 显式组合阶段 Shader。后端收集全部非空的 ClosestHit、AnyHit、Intersection，用于 DXIL 导出与全局 Root Signature 的反射合并；每个组合生成一个 `D3D12_HIT_GROUP_DESC`，分别填写三个入口。存在 Intersection 时使用程序化几何组，否则使用三角形组。SBT 保存命中组的 shader identifier，不为组内各阶段分别创建命中记录；此调整沿用现有逐阶段编译和 Shader 所有权约定。

访问分析支持 SM 6.0–6.5 的 `createHandle` 和 SM 6.6 起的 `createHandleFromBinding` / `annotateHandle`。它保守识别已知的句柄与只读指令，不是通用 LLVM 优化器；phi / select、句柄逃逸和未知指令会使只读映射不可用，调用方此时需要提供 UAV。指令语义参考 [Microsoft DXIL 规范](https://github.com/microsoft/DirectXShaderCompiler/blob/main/docs/DXIL.rst)。

创建管线时：

- 顶点输入语义取自反射，格式、偏移和步长取自 RHI 顶点描述。
- 常量缓冲区映射为 Root CBV。
- SRV、UAV 和 Sampler 映射为各自的 descriptor table。
- 光栅化、深度及附件格式通过 `DirectX12RHIUtils` 转换。

`SetBatchedShaderParameters()` 按寄存器索引和数组元素查找参数。显式 UniformBuffer 绑定 GPU 地址；离散参数根据 `BufferIndex`、`BaseIndex` 和数据偏移复制到临时常量缓冲区。

目前 RHI 资源参数没有 register-space 标识，因此非零 D3D12 register space 会被明确拒绝，避免相同寄存器编号绑定到错误资源。

---

## 6. 命令上下文与状态契约

```text
RHIContextBase
    -> DirectX12CommandContext
        -> DirectX12ComputeContext
        -> DirectX12GraphicContext
```

公共上下文管理 command allocator、command list、shader-visible 描述符堆、上传资源及完成回调。

资源分别保存两类状态：

| 状态 | 来源与用途 |
| --- | --- |
| `DeclaredAccess` | 来自上层 `RHITransition`，表达逻辑访问意图 |
| `States` | 原生 D3D12 子资源状态，用于生成实际屏障 |

`EndTransitions()` 校验非 Unknown 的 `AccessBefore`，按 mip / slice / plane 执行屏障，并更新逻辑访问声明。Unknown 表示使用后端已追踪的原生状态作为屏障起点，不表示允许任意资源绑定。

参数、顶点 / 索引、渲染附件及纹理复制绑定调用 `RequireAccess()` 校验声明和原生状态，不通过自动状态切换掩盖缺失或错误的访问声明。已确认只读的原生 UAV 缓冲区映射只在 Draw / Dispatch 的内部执行窗口改变原生状态，执行后恢复。合法只读状态可以组合；D3D12 不允许的读写组合会被拒绝。

上传辅助接口允许对尚未声明逻辑访问的资源执行初始化上传；资源已有访问声明时，上传必须符合 `TransferDest`。读回和图形实现的 Blit 可以临时调整原生状态，完成后恢复，不改变上层声明。

当前在 `EndTransitions()` 使用完整屏障，`None` 和 `NoSplit` 均采用这一保守实现；没有实现拆分屏障。Discard / MaintainCompression 转换标志会明确报错。

---

## 7. 队列、同步与资源保活

`DirectX12Queue` 对应 `RHIQueue`；`DirectX12SyncPoint` 使用 `ID3D12Fence` 提供时间轴同步。

图形和计算具有独立的逻辑队列、上下文池与 Fence 时间轴，但共用一条原生 DIRECT queue。这样计算上下文上的 `BlitTexture` 可以通过图形命令写入交换链图像，并满足 DXGI 对 back-buffer 写入队列的要求。当前没有独立异步计算能力。

提交时处理显式等待和资源上次提交产生的跨逻辑队列依赖。原生队列上的等待保守地覆盖整次提交，不提供精细的 stage-level 调度。

上下文在 Fence 完成前保留上传资源和 shader-visible 描述符堆。再次 `Begin()` 前等待上次提交完成，再重置 allocator 和描述符游标，防止覆盖 GPU 正在读取的数据。

---

## 8. 上传、读回与瞬态资源管理

### 8.1 数据传输

纹理上传按 D3D12 要求对齐 row pitch；读回时去除行填充，提供紧密排列的 CPU 数据。缓冲区上传 / 读回支持偏移和长度。

读回地址在提交并等待 Fence 后可用，调用方通过 `RHIApi::Unmap()` 释放暂存映射。每次 `UpdateTexture()` 处理一个 mip / array slice。

### 8.2 瞬态资源池

`DirectX12TransientResourceManager : RHITransientResourceManager` 校验并记录 `[beginIndex, endIndex]` 生命周期区间，管理纹理和缓冲区池。

当前采用保守复用策略：活跃生命周期之间不共享内存，即使区间不重叠也分别分配。Release 将资源标记为可回收；包装对象自然销毁也可进入回收流程。重复释放和向错误管理器释放会报错。

只有同时满足以下条件才可重新获取旧分配：

1. 资源已归还，且描述兼容。
2. 外部资源引用与视图已释放。
3. 没有录制中或等待完成的上下文使用它。
4. 上次提交的 Fence 已完成。

复用保留原生状态以生成正确屏障，同时清除上一生命周期的逻辑访问声明。该实现提供完整资源对象的安全复用，没有实现 Vulkan 后端那样的 heap 子分配或活跃区间之间的别名。缓存保留到管理器销毁，目前没有预算或淘汰机制。

---

## 9. 枚举转换：`DirectX12RHIUtils`

RHI 到原生 API 的枚举转换集中在 `Private/DirectX12RHIUtils.h/.cpp`：

| RHI 描述 | 转换函数示例 |
| --- | --- |
| 像素格式 | `ToDxgiFormat`、`ToDxgiSrvFormat` |
| 纹理类型 / 视图类型 | `ToResourceDimension`、`ToSrvDimension`、`ToUavDimension` |
| 资源用途 / 访问 | `ToTextureFlags`、`ToBufferFlags`、`ToResourceState` |
| 图元拓扑 | `ToPrimitiveTopology`、`ToPrimitiveTopologyType` |
| 光栅化与比较 | `ToFillMode`、`ToCullMode`、`ToFrontCounterClockwise`、`ToComparisonFunc` |
| 顶点步进与采样 | `ToInputClassification`、`ToFilter`、`ToAddressMode` |
| 逻辑 Buffer SRV 到原生存储视图 | `ToReadOnlyStorageUAV`，包含格式、步长、偏移和范围校验 |

资源和管线代码负责组装描述结构，避免在不同文件重复维护同一份映射。不支持的格式、TriangleFan、Point polygon mode 和 FrontAndBack culling 等不能静默映射成其他语义。

---

## 10. 能力边界与验证

当前已覆盖基础图形 / 计算、显式资源状态转换、上传读回和单层二维纹理缩放 / 格式转换。以下能力仍有限制：

- 光线追踪关闭，上层应检查 `bEnableRayTracing`。
- 无独立异步计算、拆分屏障或 heap aliasing。
- 不支持非零 register space、无界描述符数组及常量缓冲区数组。
- 单个录制上下文的描述符容量为 4096 个资源描述符和 256 个采样器。
- 纹理 UAV 每个视图覆盖一个 mip。

可通过 `Tests.exe RHIRenderTriangleTest` 和 `Tests.exe RHIShaderParameterTest` 选择现有测试。参数测试的 `InputBuffer` 保持 `RWStructuredBuffer<float4> ... register(u1)` 声明，RHI 端保持 SRV 绑定和访问声明。DXIL 实际只读取该资源，后端通过上述原生存储视图映射执行，无需修改测试中的着色器声明或主流程。

诊断开关：

- `RHI_DX12_GPU_VALIDATION=1`：启用 GPU-Based Validation。
- `RHI_DX12_DIAGNOSTICS=<目录>`：保存前四帧、读取前四次 Dispatch 的缓冲区结果并记录呈现计数。

诊断默认关闭。两个基础测试无法覆盖整个 RHI 契约，仍需结合访问冲突、子资源范围和瞬态生命周期等独立验证。

## 相关核心实现细节

本篇保留架构职责说明；执行时序、数学原理及源码限制见以下专题。本轮为源码整理，专题不等于运行验证通过。

- [原生 fence、queue 等待与 context 复用](../../key_implemetion/performance/02-rhi-queues-and-sync.md)
- [内存和完整 transient 资源复用](../../key_implemetion/performance/04-memory-and-transient-resources.md)
- [DXIL 只读 storage 映射与参数绑定](../../key_implemetion/performance/06-shader-compilation-and-binding.md)
- [DXR AS、shader identifier 与 table](../../key_implemetion/performance/07-ray-tracing-as-and-sbt.md)
