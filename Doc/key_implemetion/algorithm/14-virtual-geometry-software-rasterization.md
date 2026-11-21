# Virtual Geometry GPU 软件光栅化

本专题说明项目第一版 Compute 软件光栅化。入口是 [VirtualGeometryRasterPass](../../../src/Framework/Renderer/Private/VirtualGeometry/VirtualGeometryRasterPass.cpp)，内部编排在 [VirtualGeometrySoftwareRaster](../../../src/Framework/Renderer/Private/VirtualGeometry/VirtualGeometrySoftwareRaster.cpp)，使用方式见 [Viewer](../../specification/virtualgeometryviewer_spec.md)。它消费既有 [GPU DAG 选择](13-virtual-geometry-lod-selection.md)，不改变 Engine 的构建数据、合法 Cut 或原主场景延迟流程。

## 输入、输出与执行顺序

输入是已上传的 VirtualGeometryResource、实例 LocalToWorld、SceneView、GPU selection 的间接参数和 VirtualGeometryRasterSettings。位置是网格局部坐标，屏幕尺寸和阈值单位为像素。最终仍输出 R32G32_UInt Visibility 的 DrawID/PrimitiveID 和单样本 D32_Float 深度；[Material Resolve](12-virtual-geometry-material-resolve.md) 不识别光栅来源，也没有新增 GBuffer 或材质分支。

```text
GPU 完整 DAG Cut → Cluster 视锥剔除 → 全部候选的固定间接参数
                                      ↓ GPU 投影尺寸分类
                         ┌────────────┴────────────┐
                   HW 参数、原硬件 Raster     SW Cluster 标记
                         ↓                        ↓
                   HW Visibility/D32       Clear 深度与 ID Buffer
                                                  ↓
                                           所有实例 DepthCS
                                                  ↓
                                           所有实例 VisibilityCS
                         └────────────┬────────────┘
                               全屏 RasterMerge
                                      ↓
                         最终 Visibility / SV_Depth
                                      ↓
                         原 Material Resolve → GBuffer
```

Hardware 模式完全沿原路径，不分配软光栅目标；Hybrid 根据 SoftwareClusterPixels 分配，默认阈值 32；SoftwarePreferred 对满足安全条件且投影直径不超过 128 像素的 Cluster 优先用软件。后者是诊断模式，仍明确保留超大、可能裁剪等 Cluster 的硬件回退，不声称任意三角形都由 Compute 绘制。软件模式要求 GPU selection 且不是 Generated level 检查；未报告或不在 4～8 范围内的子像素精度会明确报错。

## Cluster 路由与不丢失几何

[RasterRouting.sf](../../../shaders/virtualgeometry/RasterRouting.sf) 每线程处理一个候选 Cluster。FullArguments 的 IndexCount 非零才表示本帧可见。RasterPaths 保存 0 不可见、1 硬件、2 软件。HardwareArguments 复制原五个 uint 参数，仅对软件项将 IndexCount 置零。原 selection 参数保持不变，Viewer 的线框仍能覆盖两条路径。分类不重新做 LOD，因而不会将一个 Group 的替换拆成非法 Cut。

设局部包围球中心 c、半径 r，矩阵 M=ViewProjection×LocalToWorld，q=M(c,1)，第 i 行前三个分量长度为 lᵢ。球投影的最小齐次分母为 wmin=q.w−r·lw。wmin≤10⁻⁶ 时回退硬件；不将相机后方或穿过眼点的球送入除法。透视商的变化满足：

`|Δ(qᵢ/q.w)| ≤ r·(lᵢ+|qᵢ/q.w|·lw)/wmin`。

该式乘以视口宽/高就是实现中 diameter 的保守完整像素直径。这里使用矩阵行长度，也包含实例镜像、非均匀缩放和剪切，不用单一模型缩放值代替。

分类同时测试球是否完全位于六个原生裁剪半空间内：侧面为 rowW±rowX/Y，ZO 近面为 rowZ，NO 近面为 rowW+rowZ，远面为 rowW−rowZ。可能触及裁剪边界、w 不安全、超过 384 个局部顶点或 128 面时均回退硬件。软件 Shader 不实现齐次三角形裁剪，不能先丢弃越界顶点再声称没有缺面。球判断保守，可能让本可软件绘制的 Cluster 留在硬件路径。

RoutingStatistics 四个 uint 依次是 HW Cluster、SW Cluster、SoftwarePreferred 中的 HW 回退数、SW 三角形数。它们统计提交的几何，不是通过深度测试后的可见像素数。Hybrid 中超过用户阈值的硬件项不计入“强制回退”计数。

## Compute 三角形覆盖与深度

[SoftwareRaster.sf](../../../shaders/virtualgeometry/SoftwareRaster.sf) 一个 64 线程工作组处理一个候选 Cluster，非软件项整组返回。线程合作把最多 384 个顶点变换后放到 groupshared float3 ScreenVertices，屏障后各线程处理 primitive=thread、thread+64；其余工作组无需互相等待。

顶点先按与硬件 VS 相同的两次矩阵乘法计算 Clip，再透视除法。屏幕坐标包含 ViewOrigin 和 ViewSize。当前 Vulkan 在 VulkanPendingGfxState::SetViewport 内使用负高度，和 D3D12 一样采用 YSign=-1；OpenGL 采用 +1。当前 OpenGL 没有 glClipControl，原生深度从 NDC [-1,1] 映射到 [0,1]，因此软件深度为 ndc.z×0.5+0.5，其余为 ndc.z。此处没有全局更改项目的投影约定。

RHIPlatformInfo::RasterSubPixelBits 提供覆盖设置的子像素位数：Vulkan 在 VulkanDevice 原 QueryDeviceProperties 中缓存 subPixelPrecisionBits，D3D12 为 8，OpenGL 在自身 context worker 查询 GL_SUBPIXEL_BITS。令 s=2^bits，屏幕顶点量化为 round(screen.xy×s)。Edge(a,b,p) 是二维叉积；调整负绕序后，用整数边函数及 top-left 包含规则判定像素中心 `p=(pixel+0.5)×s`。包围盒与本视口取交集，只遍历可能覆盖的像素。128 像素的硬限制使本版 32 位有符号边函数的乘积保持在安全范围内。

深度平面也基于量化后的顶点 XY，使用像素中心的仿射重心权重插值三个 z/w；深度本身不是 UV 那样再做一次透视校正。使用未量化 XY 会使窄三角形的覆盖与深度平面不一致，实际边界测试曾出现约 6.3×10⁻⁶ 的差异。当前三后端测试误差最多一个 float ULP；这不是对所有驱动和任意退化输入的逐位等同保证。量化面积为零或不可求解的三角形不产生片元，深度不有限或达到远清除值 1 时不写入。

## 两阶段 32 位原子：保证深度和 ID 属于同一片元

本项目没有为此增加 RHI 64 位原子能力。软件深度是 uint Buffer，初始化为 asuint(1.0)，Visibility 是 uint Buffer，初始化为 0xFFFFFFFF。正的 [0,1] float 与其 uint 位表示保持相同排序；将负零规范为正零后，DepthCS 使用 InterlockedMin 完成最近深度归约。

必须完成**所有实例**的 DepthCS，之后才能运行任何实例的 VisibilityCS。第二阶段用相同变换、覆盖和深度表达式重新光栅化，只允许 bits==WinnerDepth[pixel] 的片元参与 ID 原子最小值。阶段编译为不同 Shader 变体，不能用运行时 bool 让反射中保留未绑定的另一阶段资源。

内部 ID 为 `(DrawID<<7)|PrimitiveID`，7 位足以容纳 Cluster 最多 128 面。候选 Draw 数限制为不超过 0x01FFFFFF，以保留全 1 无效值；越界明确报错，最终 uint2 接口不压缩。相等深度时按 DrawID、PrimitiveID 升序稳定选胜者，对应原硬件按候选顺序提交、Less 深度测试的规则。

如果一次 Dispatch 中先 atomicMin 深度、然后无条件写 ID，较远线程可能后写覆盖较近 ID；即使判断“我曾赢得原子操作”，也不能保证之后没有更近线程。因此本版宁可重复一次覆盖计算，也不交付深度与三角形不匹配的 Visibility。第二阶段只读 WinnerDepth，软件 ID 由一致的位级深度筛选后再归约，不依赖跨工作组自旋锁。

## 软硬件合并和资源完成点

[RasterMerge.sf](../../../shaders/virtualgeometry/RasterMerge.sf) 逐像素读取 HW Visibility、HW D32、软件 ID 和深度。软件有效且更近则选择软件；深度完全相等时按上述稳定 ID 顺序处理。输出的 uint2 与 SV_Depth 在同一个 Fragment 调用写入，深度比较 Always、深度写开启、颜色混合关闭。这样得到真正可被后续深度测试和原 Deferred 流程使用的 D32 附件，无需创建 D32 UAV 或重算 Resolve 深度。调用方传入 SceneDepth 时，最终引用保持为同一个对象；仍覆盖整个目标，不保留已有场景深度。

资源声明全部使用既有参数元数据：路由/软件 Compute 的 Buffer SRV/UAV、硬件间接参数的 RDG_BUFFER_ACCESS(IndirectArgs)、合并的 Texture SRV/Buffer SRV 和 RenderTargetBindingSlots。DepthCS 的 UAV→VisibilityCS 的 SRV 形成阶段依赖；所有 ID UAV 写入在合并 SRV 读取之前完成。由原 RDG 分析、屏障和队列提交维护最后访问 Fence，没有新增 Pass 访问接口、等待器或 WaitIdle。

中间资源属于当前 Builder 的 transient 资源；返回的 SoftwareTargets/RasterRouting 仅是图内调试引用，不是跨帧资源所有者。外部几何、实例与材质的所有权及最后访问 Fence 沿用原 Raster。CPU 读回仅在测试或用户显式 Capture 时发生，并在相应完成点后使用、随后 Unmap。线程/完成值限制见 [同步专题](../performance/02-rhi-queues-and-sync.md#软件光栅两阶段依赖与诊断计时)。

## 成本、精度与当前边界

设目标 P 个像素、C 个候选 Cluster，软件三角形 t 的像素包围盒面积为 Aₜ。分类工作 O(C)，两个软件阶段各为 O(C+软件顶点数+ΣAₜ)，清空/合并 O(P)。没有 compact software indirect dispatch，工作组仍覆盖全部候选，不可把实际软件 Cluster 数等同 Dispatch 数。每组共享顶点约 4.5KB。较大三角形的像素遍历可能明显低效，所以保留尺寸阈值和硬件路径。

软件模式额外需要约 20P 字节：HW Visibility/D32 为 12P，软件两个 uint Buffer 为 8P；每实例另约 24C+16 字节的路由和统计，未计资源池对齐、已有 selection 参数、最终目标和 Viewer 线框。双遍覆盖、额外全屏合并和 CPU 按候选提交仍有成本，本版不声称一定比硬件更快。

当前只支持双面、不透明、单样本、常规近 0 远 1 深度；不实现 MSAA、AlphaTest、位移、保守光栅、软件裁剪、反向 Z 或持久 GPU 光栅工作队列。可叠加 [Page Streaming](../performance/11-virtual-geometry-streaming.md)，软件光栅仍读取同一物理 ClusterBuffer/属性池，不负责 I/O。依赖既有 Build 的几何边界和合法 Cut；软件光栅不修补构建产生的边界缺口。浮点矩阵运算、深度插值和原生线光栅化仍可能存在边缘精度差异，不以深度容差改变遮挡比较规则。

## 可复查验证

[VirtualGeometrySoftwareRasterTest.cpp](../../../src/Tests/VirtualGeometrySoftwareRasterTest.cpp) 新增两个入口，原有测试不改主流程：

- `Tests.exe -test=VirtualGeometrySoftwareRasterTest -rhi=Vulkan`：512 面双材质平面，两个重叠的镜像/非均匀实例，0/1/1000 像素预算，每预算检查 Hardware/Hybrid/SoftwarePreferred，读回 Visibility、D32、GBuffer A/B/C 和软件中间结果。断言 ID 与硬件逐像素一致、深度差小于 10⁻⁶、软件深度必有对应 ID、局部三角形和属性恢复合法。
- `Tests.exe -test=VirtualGeometrySoftwareRasterEdgeTest -rhi=Vulkan`：非零视口原点和非对齐尺寸、相反绕序、等深重叠、近远裁剪、窄/亚像素三角形、超大 Cluster 和空场景。检查外部 SceneDepth 引用、背景及视口外不变、HW 回退与 SW 均实际发生。

将后端替换为 DirectX12/OpenGL 使用相同断言。球体 Viewer、线框及实测结果和未覆盖范围统一记录在 [当日记录](../../dev/bugfix/故障与修复记录-2026-11-15.md#virtual-geometry-gpu-software-rasterization)，不能用首帧启动代替图像正确性。
