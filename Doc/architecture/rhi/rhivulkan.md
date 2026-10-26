# RHIVulkan 模块架构说明

## 1. 模块定位

`RHIVulkan` 是 `RHI` 抽象层的 Vulkan 实现，负责把统一的 RHI 接口具体落到 Vulkan API 上。它是一个平台后端模块，提供真实的 Vulkan 设备、队列、资源、管线和同步能力。

从源码结构看，`src/Framework/RHIVulkan/PublicHeader` 中的核心文件包括：

- `VulkanRHIApi.h`：Vulkan RHI 的主要实现类
- `VulkanResource.h`：Vulkan 纹理、缓冲区、命中结构等资源实现
- `VulkanPipelineState.h`：Vulkan 管线状态实现
- `VulkanQueue.h`：队列相关实现
- `VulkanDevice.h`：设备和资源管理实现
- `VulkanSync.h`：同步对象实现
- `VulkanSwapchain.h`：交换链实现
- `VulkanShader.h`：各类 Vulkan 着色器实现

简而言之，`RHIVulkan` 是 `RHI` 抽象层的具体落地层。

---

## 2. 功能入口：`VulkanRHIModule`

`VulkanRHIModule` 继承自 `RHI::RHIModule`：

```cpp
class RHIVULKAN_API VulkanRHIModule final : public RHI::RHIModule
```

它主要负责：

1. 模块注册：通过 `IMPLEMENT_SIMPLE_MODULE(VulkanRHIModule, "RHIVulkan")` 注册到模块管理器
2. `StartupModule()` 中创建 `VulkanRHIApi`
3. 调用 `GRHIApi->Init()` 初始化 Vulkan 设备和运行时
4. `ShutdownModule()` 中释放后端对象并清理状态

关键代码：

```cpp
void VulkanRHIModule::StartupModule()
{
    RHI::GRHIApi = CreateRHIApi();
    RHI::GRHIApi->Init();
    m_bLoaded = true;
}
```

这说明 Vulkan 模块仍然遵循统一的模块生命周期设计，而且只负责创建和销毁抽象 API 实现。

---

## 3. 具体实现：`VulkanRHIApi`

`VulkanRHIApi` 继承自 `RHI::RHIApi`：

```cpp
class RHIVULKAN_API VulkanRHIApi : public RHIApi
```

它负责：

- 创建 Vulkan 实例与设备
- 初始化物理设备与队列
- 创建纹理、缓冲区、视图、交换链和管线状态
- 覆盖 `RHIApi` 中定义的全部资源访问接口
- 承担具体的 Vulkan 调用桥接工作

它的核心成员包括：

- `VkInstance m_Instance`
- `VkPhysicalDevice m_PhysicalDevice`
- `VulkanDevice* m_Device`
- `m_MappedStagingBuffers`

这里可以看出，`VulkanRHIApi` 本身不是一个单独的设备对象，而是整个 Vulkan 后端实现的统一入口对象。

`VulkanDevice::Init()` 在创建逻辑设备后调用 `QueryDeviceProperties()`，一次性查询并缓存 `VkPhysicalDeviceRayTracingPipelinePropertiesKHR`。后续管线通过只读接口 `GetRayTracingPipelineProperties()` 获取设备限制；`VulkanRayTracingPipeline::CreateShaderBindingTable()` 直接使用该缓存计算 SBT 的步长和对齐，不再在每次创建管线时重复查询物理设备属性。

---

## 4. 资源实现体系

### 4.1 纹理与缓冲区

Vulkan 资源的具体实现结构如下：

```text
RHITexture
    └── VulkanTexture

RHIBuffer
    └── VulkanBuffer
```

#### `VulkanTexture`

`VulkanTexture` 继承自 `RHITexture`，负责映射到 Vulkan 的：

- `VkImage m_Image`
- `VkImageView m_DefaltView`
- `VkFormat m_Format`
- `VulkanAllocation m_Allocation`
- `m_TransientAllocation`

它还负责：

- 创建默认视图：`CreateDefaultView()`
- 管理视图绑定：`AttachView()` / `DetachView()`
- 处理底层图像布局与访问信息

#### `VulkanBuffer`

`VulkanBuffer` 继承自 `RHIBuffer`，负责映射到 Vulkan 的：

- `VkBuffer m_Buffer`
- `VkDeviceSize m_Size`
- `VulkanAllocation m_Allocation`

它也同样支持资源视图管理，并具备 transient allocation 处理能力。

---

### 4.2 光追资源

`RHIVulkan` 还实现了光追相关资源：

```text
RHIRayTracingAccelerationStructure
    ├── RHIRayTracingGeometry
    │    └── VulkanRayTracingGeometry
    └── RHIRayTracingInstance
         └── VulkanRayTracingInstance
```

这些类负责：

- `VkAccelerationStructureKHR`
- `VkDeviceAddress`
- Geometry / Instance 的构建参数
- 加速结构的创建与销毁管理

这对应于 Vulkan 的 `VK_KHR_acceleration_structure` 扩展。

---

### 4.3 视图对象

Vulkan 还提供了视图基类：

```text
VulkanViewBase
    ├── VulkanTextureView
    └── VulkanBufferView
```

这些对象负责底层 `VkImageView` / `VkBufferView` 的创建和销毁，并保存其唯一标识（`ViewId`），便于调试和缓存管理。

---

## 5. 管线与状态实现

### 5.1 管线基类

`VulkanPipelineBase` 是 Vulkan 管线实现的公共基类：

```cpp
class VulkanPipelineBase {
public:
    VkPipeline GetHandle() const { return _pipeline; }
    VulkanPipelineLayout* GetLayout() const { return _pipelineLayout; }
};
```

它持有：

- `VkPipeline _pipeline`
- `VulkanPipelineLayout* _pipelineLayout`
- `VulkanRenderPass* _renderPass`

其职责是统一管理 Vulkan 管线对象和对应的 pipeline layout。

### 5.2 图形管线

`VulkanGraphicsPipelineState` 继承自：

```cpp
class VulkanGraphicsPipelineState : public VulkanPipelineBase, public RHIGraphicsPipelineState
```

它负责创建和绑定图形渲染管线，通常会依赖：

- 顶点布局
- shader stages
- rasterizer state
- blend state
- depth stencil state
- render pass

### 5.3 计算 / 光追管线

同样地，项目中还有：

- `VulkanComputePipelineState : public VulkanPipelineBase, public RHIComputePipelineState`
- `VulkanRayTracingPipeline : public VulkanPipelineBase, public RHIRayTracingPipelineState`

这说明 Vulkan 后端在统一抽象接口下，支持把不同管线类型都映射到具体的 Vulkan 对象中。

光追管线遍历 `HitGroupTable` 中的 `RHIRayTracingHitGroupDesc`，将组合内各阶段展开到 `pStages`，并将阶段下标写入同一个 `VkRayTracingShaderGroupCreateInfoKHR`。Intersection 是否存在决定程序化几何或三角形组类型；未提供的阶段使用 `VK_SHADER_UNUSED_KHR`。布局收集包含 AnyHit 在内的全部非空阶段，同一 Shader 复用时只处理一次反射，相同 set/binding 合并阶段可见性并拒绝描述符类型或数量冲突。SBT 仍按 RayGen / Miss / HitGroup / Callable 顺序存放 group handle，记录数量取命中组数量，与组内阶段数量无关。

---

## 6. 着色器实现

`VulkanShader.h` 中定义了多种 Vulkan 着色器实现：

```text
RHIShader
    ├── RHIVertexShader   -> VulkanRHIVertexShader
    ├── RHIFragmentShader -> VulkanRHIFragmentShader
    ├── RHIGeometryShader -> VulkanRHIGeometryShader
    ├── RHIComputeShader  -> VulkanRHIComputeShader
    ├── RHITessControlShader -> VulkanRHITessControlShader
    ├── RHITessEvalShader    -> VulkanRHITessEvalShader
    ├── RHIRayGenShader      -> VulkanRHIRayGenShader
    └── ...
```

这些类都共享 `VulkanRHIShader` 这样的统一基础类型，最终通过 Vulkan shader module 进行编译和绑定。该层实现说明 Vulkan 后端对各类渲染阶段进行了逐类映射，而不是简单地使用单一 shader 对象。

---

## 7. 同步与队列

Vulkan 后端中，同步对象和队列能力也与 RHI 抽象接口相对应：

- `VulkanQueue`：对应 `RHIQueue`
- `VulkanSync`：对应 `RHISyncPoint` / 相关同步机制
- `VulkanSwapchain`：对应 `RHISwapchain`
- `VulkanPresentExecutor`：对应 `RHIPresentExecutor`

这些对象负责：

- 提交命令
- 管理 GPU 时间轴
- 处理等待与 fence
- 管理交换链图像的 acquire / present

这部分是 Vulkan 后端最靠近硬件的层次，负责把抽象逻辑转换为实际 GPU 执行流程。

---

## 8. 设备与内存管理

`VulkanDevice.h`、`VulkanMemory.h`、`VulkanTransientResource.h` 这几部分组成了 Vulkan 设备与内存抽象：

- `VulkanDevice`：统一管理 Vulkan 设备和资源生命周期
- `VulkanMemory`：管理分配、释放、staging buffer 等内存对象
- `VulkanTransientResource`：管理临时资源分配

这一层的设计目标是让上层 `RHIApi` 不需要关心 Vulkan 设备内部的细节，而是通过统一接口访问其能力。

---

## 9. 架构总结

`RHIVulkan` 模块的核心架构可以概括为：

1. `VulkanRHIModule` 负责模块注册与生命周期
2. `VulkanRHIApi` 实现 `RHI::RHIApi`，作为 Vulkan 后端统一入口
3. Vulkan 资源对象通过继承 `RHITexture`、`RHIBuffer`、`RHIRayTracingGeometry` 等接口实现具体能力
4. 管线对象通过 `VulkanPipelineBase` 统一管理 `VkPipeline` 与相应 layout
5. 视图、同步、队列、交换链和设备内存均被映射为 Vulkan 对应对象
6. 最终，RHI 上层代码只需要依赖抽象接口，而无需了解底层 Vulkan 细节

因此，`RHIVulkan` 的实现方式本质上是“抽象接口层 + Vulkan 具体实现层”的典型后端结构，它是整个渲染系统中最贴近真实 GPU API 的一个模块。

## 相关核心实现细节

本篇保留架构职责说明；执行时序、数学原理及源码限制见以下专题。本轮为源码整理，专题不等于运行验证通过。

- [队列和 timeline 完成](../../key_implemetion/performance/02-rhi-queues-and-sync.md)
- [分配、staging 与 transient 回收](../../key_implemetion/performance/04-memory-and-transient-resources.md)
- [反射、布局和 descriptor 寿命](../../key_implemetion/performance/06-shader-compilation-and-binding.md)
- [AS 构建/更新、scratch 和 SBT](../../key_implemetion/performance/07-ray-tracing-as-and-sbt.md)
