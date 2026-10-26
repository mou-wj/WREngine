# Shader 编译、参数布局与绑定

架构：[RenderCore](../../architecture/render/rendercore.md)、[Material](../../architecture/material.md)。入口：[ShaderCompiler.cpp](../../../src/Framework/RenderCore/Private/ShaderCompiler.cpp)、[ShaderParameter.h](../../../src/Framework/RenderCore/PublicHeader/ShaderParameter.h)、[ShaderParameter.cpp](../../../src/Framework/RenderCore/Private/ShaderParameter.cpp)。

## 从 C++ 声明到实际资源槽

```text
参数宏和 Metadata（类型、成员、偏移、数组、嵌套结构）
  → 生成 /Generated/ShaderParameters/*.sf 虚拟头
  → 展开 Shader 文件与 VirtualIncludes，注入宏/排列
  → DXC 编译与反射
  → PackedBinaryData + ParameterMap
  → 后端 Shader / pipeline layout
  → SetShaderParameters 构造常量字节与资源绑定
```

三种 offset 不能混用：CPU 结构成员 offset、反射常量缓冲 offset、RHI batched data offset。元数据用于遍历 CPU 内容和 RDG 访问，ParameterMap 用 Shader 实际使用后的反射结果匹配绑定；被优化掉的声明不一定仍有 allocation。

结构 include/reference/nested 并非同一种内存关系。引用、数组、资源视图和 RenderTargetSlots 必须按元数据类型遍历，不能把整个 C++ struct 直接 memcpy 当作所有平台的 cbuffer。

## 三个编译出口

| 目标 | 当前路径 | 运行时产物 |
| --- | --- | --- |
| Vulkan | HLSL → DXC SPIR-V → SPIRV-Cross 反射 | SPIR-V 代码与 set/binding/UB 信息 |
| DirectX12 | HLSL → DXC DXIL → D3D 反射 | DXIL 与 shader stage/entry/hash |
| OpenGL | HLSL → SPIR-V → SPIRV-Cross GLSL | GLSL、UBO、storage、采样器组合映射 |

CompileToMetal 当前是未实现分支，不能把枚举存在当成 Metal 后端。GL 编译时有按阶段的 binding offset（stage 区段宽度 500）及 combined image sampler 映射，原始 HLSL tN/sN 不能直接当 GLSL 最终槽。

GenerateOrGetShaderPrameterMetaDataSF 必须在需要的生成头被 include 前注册内容。已有测试出现“生成头不存在”，属于注册/编译时序问题，不能通过虚构空头或删除 Shader 参数绕过。

## 打包格式不是跨版本资产格式

[ShaderCompiledDataPacker.h](../../../src/Framework/RenderCore/PublicHeader/ShaderCompiledDataPacker.h) 保存阶段、定长 entry 字符串、hash，以及平台特有反射和代码。部分 Vulkan 字段使用 uint16_t，字符串也有长度上限；超大数量/偏移不应假设无损。它是当前进程和后端的传递格式，不是带版本迁移、跨 ABI 保证的长期 Shader 库。

ShaderType/GlobalShader/MaterialShaderMap 通过静态注册构造排列。MaterialKey 包含平台、类型、VF/flags、排列和材质编译参数；动态 Roughness 等数值不应进入编译 key。材质缓存和全局元数据并未提供任意线程同时编译/修改的完整协议。

## 各后端参数落地

- Vulkan 收集 set layouts、descriptor 类型/数量和 stage 可见性，创建布局；UBO/descriptor 的临时分配必须覆盖提交期。光追同一 binding 的跨阶段反射需合并并检查冲突。
- D3D12 用反射生成 Root Signature 与描述符绑定，context 管理上传常量及 shader-visible heap 的寿命；原生寄存器类别不等于 RHI 逻辑访问权限。
- OpenGL 将常量写入 UBO/相关 uniform，纹理与 sampler 根据 combined mapping 绑定；所有原生 GL 操作必须进入 worker 的 context。

## Vulkan descriptor set 的分配与回收

[VulkanDescriptorSets.cpp](../../../src/Framework/RHIVulkan/Private/VulkanDescriptorSets.cpp) 按 layout hash 找 TypedDescriptorPool，每个原生 pool 初始以 256 sets 为容量单位；优先取该 layout 的 FreeSets，否则从 pool 分配，登记到 InUseSets。BindDescriptorSets 将 set 集合与当前 command buffer 关联到 pending 列表。

GarbageCollect 仅当关联 command buffer 的状态为 NeedRecycle 才将集合从 InUse 移回 Free；该状态又依赖队列完成记录推进。不能在 descriptor 写完或 bind 返回时立即复用 set。descriptor set、layout、其引用的 image/buffer/view 和临时 uniform 数据是几种独立寿命，回收某一层不意味着其他层自动安全。

manager 由 Device 持有，当前所读分配/绑定/GC 路径未见内部互斥；RDG 有多 context 并行录制，因此不能把 per-context 原生 command buffer 的线程隔离推广到共享 descriptor/cache 数据结构。这里记录审查边界，没有新增锁或宣称已复现竞争。

## D3D12 只读 storage-buffer 映射的关键逻辑

[DirectX12ShaderAccess.cpp](../../../src/Framework/RHIDirectX12/Private/DirectX12ShaderAccess.cpp) 解决“Shader 声明 RWStructuredBuffer，但实际只读；调用方按 RHI SRV 使用”的情形。它分析 DXIL assembly 中的 handle 起点和使用，而不是按变量名猜测权限。

1. 识别 createHandle 或 createHandleFromBinding 对应的资源范围。
2. 沿 annotateHandle 扩展句柄集合，迭代到不再变化。
3. 所有使用必须属于已知只读指令；store、atomic、counter、phi/select、逃逸或未知使用都拒绝只读映射。
4. 证据充分时用 native UAV 描述符/状态实现读取，但仍校验调用方声明了 RHI SRV，并在操作后恢复原生状态。

这是一种保守的特定 DXIL 文本分析，不是完整 SSA 数据流证明器。UPLOAD buffer 固定 GENERIC_READ，不支持该映射；分析不能证明只读时必须明确报错，不能要求上层把正确 SRV 改成 UAV 掩盖问题。

## 缓存和并发边界

[RHIPipelineStateCache.cpp](../../../src/Framework/RHI/Private/RHIPipelineStateCache.cpp) 对各缓存使用独立 mutex，锁内查询并创建，避免同一缓存并发重复创建，但原生创建可能长时间持锁。缓存只用 size_t hash 作为 key，未见命中后完整描述符碰撞核对；hash 碰撞正确性不能当作已有保证。

Compiler 部分诊断输出写相同 test.spv/error.txt，重复编译会覆盖文件；它们不是磁盘 Shader 缓存，也不适合据此审计并行编译的每份产物。

验证入口：MaterialShaderCompilationTest、RHIShaderParameterTest。反射编译通过仍须核查输出图像、资源访问和 Validation；不是所有已注册 GlobalShader 都已具备缺失的生成依赖。
