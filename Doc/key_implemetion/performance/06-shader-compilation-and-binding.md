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

RDG Buffer SRV 的 BaseType 必须为 RDGBuffer_SRV。2026-10-31 发现 ShaderParameterTypeInfo<RenderGraphBufferSRV> 原先误填 RDGTexture_UAV：SHADER_PARAMETER_RDG_BUFFER_SRV 产生的 HLSL 仍可写成 StructuredBuffer 并编译，但 SetupPassInternal 会把 CPU BufferSRV 当 TextureUAV 解释，无法得到正确的缓冲 SRV 依赖；SetShaderParameters 也会选择 UAV 类型。当前已更正这个枚举，不通过更改网格的只读声明或新包装类迁就错误类型。受影响的自有调用为 HintGridParameters 的两个 uint 缓冲和 SceneLightShadowParameters::SplitBuffer；其他带嵌套元数据的 SHADER_PARAMETER_RDG_STRUCTURED_BUFFER 路径原本就使用 RDGBuffer_SRV。编译成功不证明元数据分类正确，依赖与转换的独立检查见 [RDG 执行](03-rdg-execution-and-state.md)。

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

## Vulkan pipeline layout：原生兼容与 CPU 元数据不能混为一谈

源码：[VulkanPipelineState.h](../../../src/Framework/RHIVulkan/PublicHeader/VulkanPipelineState.h)、[管线构建](../../../src/Framework/RHIVulkan/Private/VulkanPipelineState.cpp)、[VulkanPendingPipelineState.h](../../../src/Framework/RHIVulkan/PublicHeader/VulkanPendingPipelineState.h)。架构边界见 [RHIVulkan](../../architecture/rhi/rhivulkan.md)。

调用链为 Shader 反射 → BuildPipelineLayoutInfo → VulkanPipelineLayoutCache::GetOrCreateLayout → VulkanPipelineLayout::GetInfo → VulkanCommonPipelineDescriptorState::ParsePipelineLayout → SetPackedGlobalParameter。VulkanPipelineLayout 构造时复制整个 PipelineLayoutInfo；原生 VkPipelineLayout 由其中 Layouts/PushConstant 创建，CPU 侧则从 SetLayoutsByFrequency 生成资源映射和按阶段的 PackedUniformBuffer。缓存对象同时承担这两种信息，键必须覆盖两者的实际内容。

原生 descriptor set layout 描述 binding、descriptor 类型/数量和 stage 可见性，不记录 uniform buffer 内部的成员字节布局。本项目的方向轴 VS 反射出 96 字节 UBO，PositionOnlyVS 的两个矩阵反射出 128 字节；它们可以具有相同原生 descriptor 布局，却不能共用同一份 CPU UBO Size。旧键仅比较原生布局，会把后创建的模型管线关联到先前方向轴缓存的 96 字节元数据。实际写入 frequency=1、binding=100、byteOffset=64、numBytes=64 时，逻辑上限为 96，触发 64+64<=96 的断言失败。

这里的 96 是 PackedUniformBinding::Size，不等于 Data 总分配字节数。当前 PackedUniformBuffer 构造按 64 字节对齐各 binding，96 字节条目本身也会占 128 字节暂存区；额外 padding 不能替代正确的反射 Size，更不能作为取消断言或扩大某个 Shader 声明的理由。参数写入、上传/descriptor 范围都应使用当前管线真正对应的元数据。

当前 CalculateHash 的输入为：

- 原有原生 descriptor set layout 序列的哈希。
- 排序后的 Shader frequency 键及阶段数量；每阶段资源条目数量和 set/binding/descriptorCount。
- 每阶段 UBO 条目数量和 set/binding/Size。
- push constant 存在标记，以及存在时的 offset/size/stageFlags。

阶段 map 是 unordered_map，不能把迭代顺序当作身份；先复制并排序键，使相同内容不因插入/rehash 顺序不同而产生无意义的不同键。条目 vector 不做这种重排：尤其 UBO 的顺序会决定 ParsePipelineLayout 的 CPUOffset 打包顺序。所有新增组合继续使用 [Core/HashHelper.hpp](../../../src/Framework/Core/PublicHeader/HashHelper.hpp) 的 HashCombine，没有新增局部哈希算法。

新增 CPU 成本发生在后端管线创建时的 layout 查询：若有 F 个阶段、R 个资源映射和 U 个 UBO 条目，新增 O(F log F+R+U) 工作和 O(F) 临时键数组。命中已有上层 RHI 管线缓存时不需要重新执行这次 layout 构建；没有每次参数写入都重做排序。原生可兼容但 CPU 元数据不同的 layout 会保留为不同缓存对象，增加相应小量 CPU/Vulkan layout 存储。

缓存由 VulkanDevice 拥有，layout 对象中的元数据副本随其 unique_ptr 释放，管线只借用 layout 指针。设备关闭清空 map 后销毁 VkPipelineLayout，沿用原有管线/设备关闭顺序；键补全没有新增锁、Fence、WaitIdle 或跨帧保活机制。该缓存目前仍以 uint64_t hash 直接命中，不比较完整内容；哈希碰撞仍可能错误命中。共享 m_Layouts 未新增内部互斥，不能假定图形/计算上层各自的锁已形成统一并发协议。这些旧限制未在本次扩大修复。

本问题的颜色附件已由方向轴 Pass 参数声明给 RDG；修复位置是 Vulkan 布局缓存身份，不修改 RenderGraphBuilder。故障复现和修改后验证状态见 [2026-10-29 记录](../../dev/bugfix/故障与修复记录-2026-10-29.md)。

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

### 深度状态的有序组合

2026-10-31 的空场景/网格联调确认了旧 HashDepthStencilState 的确定性碰撞。方向轴请求 `(bDepthTestEnable=false,bDepthWriteEnable=false,DepthCompareOp=Less)`，模型请求 `(true,true,Less)`；Less 的枚举值是 1，其他默认布尔值和 mask 都为 0。旧实现逐字段 XOR，使两个键分别为 `0^0^1=1` 和 `1^1^1=1`。先绘制无光源空场景时，方向轴先占据键 1；之后模型命中该条目，实际 GetDesc 返回关闭测试/写入的状态。模型没有写入深度，后面的天空 Pass 覆盖整个背景，表现为“节点和 draw batch 已存在，但模型不可见”。先创建阴影/模型状态的场景会反向影响方向轴，顺序依赖不能当作正确行为。

当前按声明顺序使用 Core::HashCombine 组合测试、写入、比较操作、depth-bounds、stencil 开关与两个 mask，不新增哈希算法、缓存或同步。每次查找仍为固定 7 个字段的 O(1) 计算，缓存继续拥有各状态对象，上层管线借用它们，ClearAll 和设备关闭路径不变。这修正了已复现的字段抵消，未解决“仅以 hash 判等”的通用限制；其他缓存的完整描述符比较和遗漏字段仍须单独审查。验证覆盖 Vulkan/D3D12/OpenGL 各 32 组测试/写入/比较操作，以及真实编辑器导入模型后的画面，见 [故障记录](../../dev/bugfix/故障与修复记录-2026-10-31.md)。

Compiler 部分诊断输出写相同 test.spv/error.txt，重复编译会覆盖文件；它们不是磁盘 Shader 缓存，也不适合据此审计并行编译的每份产物。

验证入口：MaterialShaderCompilationTest、RHIShaderParameterTest。反射编译通过仍须核查输出图像、资源访问和 Validation；不是所有已注册 GlobalShader 都已具备缺失的生成依赖。
