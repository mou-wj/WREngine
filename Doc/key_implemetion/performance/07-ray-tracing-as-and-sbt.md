# 光追加速结构、scratch 与 Shader Binding Table

架构：[RHI](../../architecture/rhi/rhi.md)、[Vulkan](../../architecture/rhi/rhivulkan.md)、[D3D12](../../architecture/rhi/rhidirectx12.md)。CPU 自建 BVH 的算法另见 [空间索引](../algorithm/02-bvh-octree-and-scene-query.md)，不能把它当成原生 AS 的内存格式。

## 三种资源分工

| 资源 | 内容 | 生命周期 |
| --- | --- | --- |
| 输入 buffer | 顶点、索引或 instances | 构建/更新时可访问，TLAS 引用的 BLAS 仍需存活 |
| AS result storage | 驱动定义的不透明加速结构 | 后续 trace 或依赖它的构建期间保持有效 |
| scratch | 驱动在 build/update 时用的工作区 | 至少覆盖 GPU 构建执行期；不是 AS 持久结果 |

原生 AS device address 不等于底层 buffer 起始地址。查询尺寸不是构建；创建 AS 句柄也不是已经生成了可遍历的内容。

## Vulkan 创建与构建

源码：[VulkanResource.cpp](../../../src/Framework/RHIVulkan/Private/VulkanResource.cpp)、[VulkanPendingPipelineState.h](../../../src/Framework/RHIVulkan/PublicHeader/VulkanPendingPipelineState.h)。

BLAS 将三角形 vertex format/stride/index type/address、primitive 数等转成 GeometryTrianglesData；用 BuildGeometryInfo 和最大 primitive 数查询 AS 大小、buildScratchSize、updateScratchSize，再分配存储 buffer、创建 AS、查询 AS 地址。

TLAS 的 geometry 类型为 INSTANCES：输入是 VkAccelerationStructureInstanceKHR 数组，每个实例含 3×4 transform、mask、custom index、SBT offset 和 BLAS AS 地址。TLAS 同样需要 BuildGeometryInfo，因为该接口描述的是一次通用 AS 构建任务，不只描述三角形。本实现 instanceShaderBindingTableRecordOffset 固定为 0，尚不能据此表达每实例任意 SBT 记录选择。

PendingRayTracing::BuildAccelerationStructure 决定 BUILD 或 UPDATE、源/目标句柄及 scratch 大小，查询 scratch 地址对齐，申请 `requiredSize + alignment - 1` 后对 **device address** 对齐，防止只对 buffer 内 offset 对齐仍不满足基地址要求。

同一录制内连续构建可能复用 scratch，源码插入 AS build 写→读/写屏障；扩容时将旧 buffer 保留进 retired scratch 数组，Reset 时清理。调用方仍须正确安排 BLAS write→TLAS read、TLAS write→trace read 的显式访问转换，不能只靠 scratch 屏障替代它们。

## UPDATE 的条件

更新应基于已构建且允许更新的结构，保持原生 API 要求兼容的几何/实例布局。当前 Vulkan 代码检查 AllowUpdate，使用源=目标的原位更新；没有完整的“历史构建拓扑相同”验证，因此不能承诺改变索引拓扑或 primitive 数也能任意 update。允许修改内容和重新创建资源是两件事。

D3D12 [DirectX12Resource.cpp](../../../src/Framework/RHIDirectX12/Private/DirectX12Resource.cpp) 用 prebuild info 分配 Result 和 max(build,update) Scratch；[Context](../../../src/Framework/RHIDirectX12/Private/DirectX12Context.cpp) 检查 bBuilt/ALLOW_UPDATE，更新时加 PERFORM_UPDATE，设置 source address。scratch 转 UAV 并处理重复构建 barrier，context 暂存原生 COM 引用以覆盖提交期。

## Shader、group 与 SBT 是三层

RHIRayTracingPipelineStateDesc 有 RayGen、Miss、HitGroup、Callable 四张表。general group 只选择一个 general stage；hit group 组合 ClosestHit/AnyHit/Intersection，组内多个阶段对应一个 group handle。阶段可以复用，group 和 SBT record 数不等于 shader stage 数。

[VulkanPipelineState.cpp](../../../src/Framework/RHIVulkan/Private/VulkanPipelineState.cpp) 先合并反射布局，展开 stages/groups，创建 pipeline，再调用 CreateShaderBindingTable。当前只允许一个 RayGen。Ray-tracing pipeline properties 从 VulkanDevice 获取；scratch 的 AS properties 仍在构建处查询，不能误写成全部属性已集中缓存。

## Vulkan SBT 的对齐和内容

记 H=shaderGroupHandleSize，A=shaderGroupHandleAlignment，B=shaderGroupBaseAlignment：

`stride=alignUp(H,A)`，每张非空表的起点按 B 对齐；还要保证分配得到的 **整个 SBT device address** 对齐。region.size=count×stride，空表 region 全零，stride 不超过 maxShaderGroupStride。

```text
已对齐的 SBT 基地址
  RayGen: [handle | record padding]
  table padding 到下一 B 对齐位置
  Miss:   [handle | padding] ...
  table padding
  Hit:    [hit-group handle | padding] ...
  table padding
  Callable: 同上（可为空）
```

vkGetRayTracingShaderGroupHandlesKHR 返回按 group 编号紧密排列的 H 字节数据，拷到各表的 record stride 处。padding 不是另一个 shader；handle 是驱动定义的标识，不是 CPU 函数地址，也不是 SPIR-V 代码。本实现没有 local-record 参数负载，不能将空白填充解释为已经支持本地材质参数。

D3D12 [PipelineState](../../../src/Framework/RHIDirectX12/Private/DirectX12PipelineState.cpp) 创建 DXIL exports、hit-group exports、StateObject 和全局 Root Signature，用 shader identifier 填 table，再构造 DispatchRays 描述符。其原生对齐常量和 identifier 不应套用 Vulkan 查询值。

## 执行与验证

上传几何/实例 → build BLAS → 显式依赖 → build TLAS → 显式依赖 → 绑定 pipeline、TLAS、输出 UAV → TraceRays → 输出转 Present。RayGen 发射射线，miss 写背景，hit group 处理交点/命中逻辑。自定义 CPU 射线求交的推导见 [射线算法](../algorithm/01-ray-triangle-and-distance.md)。

RHIRayTracingTest 只覆盖简单三角形、单一实例及基本 shader tables，不是 UPDATE、程序化几何、多 hit-group 索引和 callable 全覆盖。OpenGL 没有此原生路径，明确不支持；既往不支持报错后的异常退出另有记录。本轮未运行新测试。
