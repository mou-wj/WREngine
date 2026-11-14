# Virtual Geometry CPU / GPU 动态 LOD 与合法 DAG Cut

入口为 [SelectVirtualGeometryClusters](../../../src/Framework/Renderer/VirtualGeometry/VirtualGeometryLODSelection.h)，实现在[同目录 cpp](../../../src/Framework/Renderer/VirtualGeometry/VirtualGeometryLODSelection.cpp)。它消费 [CPU Build](10-virtual-geometry-cpu-build.md) 的原有数组，不改变 Engine 资源结构。Renderer 总体职责见[架构文档](../../architecture/render/renderer.md)，调用配置见 [Renderer 使用约定](../../specification/renderer_spec.md)。

GPU 入口为 [AddVirtualGeometryGPUSelectionPass](../../../src/Framework/Renderer/VirtualGeometry/VirtualGeometryGPUSelection.cpp)，计算程序为 [GPUSelection.sf](../../../shaders/virtualgeometry/GPUSelection.sf)。下文先解释 CPU 参考算法和共同投影公式，再说明 GPU 的依赖求值、视锥剔除及间接绘制。两条路径消费相同的 Build 数组。

## CPU 输入、输出与适用范围

输入为只读 VirtualGeometryResource、实例 LocalToWorld、SceneView 和非负有限的 TargetPixelError。相机矩阵、投影和分辨率取自本帧 SceneView：使用与 Raster 完全相同的 ViewProjectionMatrix、Viewport.Width/Height 和 bDepthRangeZeroToOne；调用方须先更新视图矩阵快照。输出是该实例本次要绘制的唯一 ClusterID 列表，按资源索引递增排列，不要求具有统一 MipLevel。

误差与包围球均来自 Group.ParentMetric，长度单位为 Mesh Local Space。实例矩阵只接受非奇异仿射变换，允许负缩放、非均匀缩放及剪切；运算使用 Core 的矩阵和向量，组合矩阵与误差投影使用双精度。TargetPixelError=0 明确要求完整叶 Cut；默认 Raster 实例和场景入口均为 1 像素。输入非法时抛出带 Virtual geometry LOD 前缀的错误，不改写资源或返回部分结果。

选择器不创建 RHI 对象、不读取 GPU、不增加 Fence、锁、线程或常驻缓存，也不做视锥/遮挡剔除。即使包围范围在相机后方，仍保留完整表示，由既有 Raster 裁剪。每次从叶集合重新计算，不借用上一帧 Cut；相机或阈值变化可以直接细化或粗化，但当前没有滞回或几何渐变，切换可能产生可见跳变。

## Group 的替换语义

Child 是简化前的整个区域，Parent 是简化后整个区域重新分簇的结果。Parent 与 Child 不存在逐 Cluster 的一一对应；一个 Group 的多个 Parent 后续可以被不同 Group 消费，新的 Group 又可以跨越多个旧 Group 的边界。

现有 Build 保证：

- 每个 Cluster 恰有一个消费它的 GroupIndex；叶 Cluster 的 GeneratingGroupIndex 为 INVALID，其余恰由一个 Group 生成。
- 生成 Group 的数组索引严格小于消费 Group，数组本身就是拓扑序。
- RootGroupIndices 指向 Parent 为空、Child 非空的终端 Group。Root 不执行“替换为空”，多个根和停滞叶区域均须保留。
- ParentMetric.Error 沿生成与消费关系单调；Metric.Bounds 包含本次 Child/Parent 及历史后代范围。

因此不能从每个 Cluster 独立选择一个 Mip，也不能用普通树递归随意挑 Parent。将每个 Parent 的所有图论叶祖先当作独占覆盖集合也不正确：同组多个 Parent 共享整组 Child 祖先，却共同构成有效表示。

## 构造 Cut 与正确性

1. ValidateDAG 检查 Group 范围、Child/Parent 双向引用、生成/消费唯一性、根集合、有限 Metric 和误差单调性。严格递增的生成到消费索引拒绝环；本阶段不重复执行 Build 的几何合法性验证。
2. selected 初始只包含所有 GeneratingGroupIndex=INVALID 的叶 Cluster，形成完整最细表示。applied 初始为零。
3. 按 Group 数组拓扑序遍历。Root、零预算或 Parent 屏幕误差超预算的 Group 不替换。
4. 候选 Group 必须满足 Child 集合的每个成员当前都在 selected 中。前置生成未发生时保留当前表示，不为了取得 Parent 强行粗化不达标的依赖。
5. 一次完整操作移除全部 Child、加入全部 Parent，并记录 applied。代码中的两个循环是同一次同步 CPU 操作，不存在中间结果被绘制或并发读取的时点。
6. 最终对每个 Cluster 检查 `selected == (叶或生成Group已应用) && 消费Group未应用`，然后输出有序列表。

合法性以完整叶 Cut 为归纳起点：每次只将当前表示中完整的一个 Child 区域替换为其完整 Parent 区域，因此不会同时保留被替换的 Child，不会遗漏 Group 的部分 Parent，也不会重复消费 Cluster。跨旧 Group 的替换仍遵守相同规则。停滞区域一直在 Cut 中；多个根不需要被拼成一个虚构根节点。每个 Group 只处理一次，生成关系严格前向，不存在反复切换或递归栈溢出。

这是一种保守的、通过可达替换序列构造的 DAG Cut，不承诺全局最少三角形。如果保守投影或依赖条件阻止某次替换，会保留更细表示。

## 从局部几何误差到像素误差

令 `M = ViewProjection × LocalToWorld`，`m_i` 为 M 第 i 行前三项，`a_i=|m_i|`；`q=M×[c,1]` 是 Group 包围球中心 c 的齐次坐标，r 为半径，e 为累计 ParentMetric.Error。任意球内位置 p 的坐标满足：

```text
wMin = q.w - r*a3
wPerturbedMin = wMin - e*a3
s_i = (abs(q_i) + r*a_i) / wMin, i∈{x,y}
```

对长度不超过 e 的局部位置扰动，有 `|δclip_i|<=e*a_i`、`|δw|<=e*a3`。对齐次除法展开：

```text
((clip_i+δclip_i)/(w+δw)) - clip_i/w
    = (δclip_i - (clip_i/w)*δw)/(w+δw)

ndcError_i <= e*(a_i+s_i*a3)/wPerturbedMin
pixelX = ndcError_x * Viewport.Width/2
pixelY = ndcError_y * Viewport.Height/2
pixelError = hypot(pixelX, pixelY)
```

源码对应 PixelError 的 minimumW、perturbedW、maximumNDC、ndcError 和 pixels。球半径使用整个历史替换范围，避免只用中心距离低估靠近相机的部分。透视项随相机距离与投影参数变化；水平、垂直分辨率分别参与，包含偏轴观察时的除法变化。LocalToWorld 直接进入 M，因而不依赖可能低估剪切的“最大单轴缩放”近似。

正交投影时 a3=0、w 为正的常数，该式自然化为固定投影比例下的线性误差，不随相机的正交前后移动而缩放。投影翻转屏幕 Y 只改变符号，不改变用于界定误差的范数。

若 wMin 或 wPerturbedMin 非正，误差返回无穷大，保留细节。另按当前普通深度约定构造近面：ZO 为 M 第 2 行，NO 为第 2、3 行之和；当近面与半径 r+e 的范围相交或整个范围在近面后，亦拒绝粗化。这样不会在眼点或近裁剪面穿越包围体时除以接近零的深度。这里不处理反向 Z 的另一套近面定义，当前 Visibility Raster 使用清深度 1、Less 比较。

上述齐次投影估算对“给定 e 的位置扰动”采用保守界定，但 Build 的 e 仍是双向表面采样估计，**不是严格 Hausdorff 上界**。因此 TargetPixelError 是控制几何精度的预算参数，不能据此宣称最终连续表面具有严格的最大像素误差保证。它也不衡量 UV、法线或材质信号的屏幕误差。

## GPU 依赖求值与完整 Cut

输入仍是资源、LocalToWorld、SceneView、TargetPixelError，另有 frustumCulling 开关。ValidateVirtualGeometryLODInput 复用已有 DAG、Metric 和投影合法性检查；随后 CPU 只构造静态依赖调度，不决定任何 Group 是否满足误差预算：

```text
depth(g) = 0                                      若全部 Child 都是叶
depth(g) = 1 + max(depth(generator(child)))        否则
```

代码中的 depths、offsets、order 按深度组织 GroupID。每层一条 Compute Pass，每线程处理一个 Group，线程组大小 64；同一层不存在生成依赖。相邻 Pass 对 AppliedGroups 声明 UAV 访问，由现有 RDG 产生依赖和屏障。这里没有用线程组内 barrier 冒充全 GPU 同步，也没有跨工作组自旋等待。每帧仍重新验证和整理调度，未增加常驻缓存或 Engine 字段。

令 E(g) 表示非 Root、非零预算且 ParentMetric 的像素误差达标；GPU 的 SelectGroupsCS 计算：

```text
A(g) = E(g) AND 每个非叶 Child 的 A(generator(child))
A(root) = false
```

这个递推等价于 CPU 的“所有 Child 都在当前 frontier”条件：Child 的消费 Group 唯一，因此在求值 g 之前，它不可能已被另一个 Group 消费；叶从开始就存在，非叶只取决于其生成 Group 是否应用。所有生成 Group 都在前一批 Dispatch 中完成，同层线程只写自己的 A(g)。不同旧 Group 的 Parent 可以在新 Group 中共同被消费，仍是整体替换，不是单 Cluster 独立挑选。

所有 Group 求值完成后，SelectClustersCS 每线程计算一个 Cluster 的完整 Cut 标志：

```text
cut(c) = (c 是叶 OR A(generator(c))) AND NOT A(consumer(c))
```

由 CPU 部分的替换归纳证明，这个集合保留多根和停止简化的区域，不包含同一替换关系的细、粗表示重叠。没有按 MipLevel 筛选，也没有把多对多关系改为树。GPU 只读取现有 ClusterBuffer、GroupBuffer、GroupChildClusterIndexBuffer；不需要另存一份 GPU Hierarchy 或重新设计资源。

当前上传是原始 C++ 数据：Cluster 72 字节，Group 112 字节，通过 StructuredBuffer<uint> 标量读取；Cluster 球位于字节 56，ParentMetric 球位于字节 84、误差位于字节 100。C++ static_assert 对照真实 sizeof/offsetof，编译环境传递结构步长；不让 HLSL float3 对齐规则改变原始布局。输入的组合矩阵和行范数超出 FP32 范围时明确拒绝；标量数组寻址范围也显式检查。

## GPU 视锥、输出与精度

GPU 沿用上述齐次投影公式，但使用 FP32；CPU 参考使用 double。临界阈值附近可能因舍入而得到不同的合法 Cut，调试对比会报告差异，不静默回退或改写 CPU 结果。Build 的采样误差不是严格上界，这个限制同样存在于 GPU。视口原点不参与误差缩放，宽高参与；反向 Z 仍未支持。

**先形成完整 Cut，再剔除其不可见 Cluster。** GPU 的 ClusterFlags bit 0 保存完整 Cut，bit 1 保存可见子集；可见子集因剔除而不覆盖全网格，不能把它当成完整 Cut 检查。剔除不反过来改变 Group 资格。

对 `M=VP×LocalToWorld`，局部球 c、r 在任一裁剪半空间 `n·p+d>=0` 内的最大平面值为 `n·c+d+r|n|`。六个平面直接取 M 的行组合：左/右为 row3±row0，上/下为 row3±row1，远为 row3-row2，近为 ZO 的 row2 或 NO 的 row3+row2。因此平面法向与半径同时保留实例的非均匀缩放、镜像与剪切，不需要把球近似成一个世界缩放因子。只有球严格位于某平面外才拒绝，并加向外浮点容差。当前没有背面锥、HZB 遮挡剔除。

输出 VirtualGeometryGPUSelection 保存本次图中的 RDG 引用：

| 数据 | 含义 |
| --- | --- |
| AppliedGroups | 每 Group 一个 uint，终端 Root 为零 |
| ClusterFlags | 每 Cluster 一个 uint，区分完整 Cut 和可见性 |
| VisibleClusters | 原子追加的紧凑可见 ID 列表，无固定顺序；只有 Statistics[1] 个元素有效 |
| Statistics | 完整 Cut 数、可见数、剔除数、可见三角形数 |
| IndirectArguments | 按原 ClusterID 固定槽位排列的 20 字节索引绘制参数 |

最终 Dispatch 为每个候选写入 IndexCount、InstanceCount=1、FirstIndex、VertexOffset、FirstInstance=0；未选择或已剔除项的 IndexCount=0。计数及紧凑列表用 InterlockedAdd 生成，多工作组的追加顺序不保证一致，正确性比较按集合进行。资源均属于本次 Builder，状态和 Fence 由既有 RDG 管理；正常路径无 CPU 等待读回。

## 裂缝条件与当前构建数据

Build 的 TryCollapse 比较折叠前后有向几何外边界边的多重集合，Build 接受简化及最终 ValidateResource 又检查 Child 与 Parent 合并区域的外边界一致。共享端点使用相同 float 位置；UV/硬法线变体、材质边界和异常拓扑均保护。它保护的不只是边界顶点，还包括边段及重数。

在此契约下，一次完整 Group 替换不会改变它与当前 Cut 其余部分的接口；对合法替换序列归纳，混合层级仍保持这些接口。选择器保留全部依赖、整组替换和停滞部分，避免固定 Mip 筛选导致缺失区域。CPU 测试另外用实际 Build 的曲面，对不同预算的整个 Cut 独立重建有向几何边界并与叶 Cut 比较。

这不等于所有视觉接缝都消失：输入已有的不焊接近似位置、非流形缺口、属性接缝仍会保留；没有额外的裙边、边缘缝合或 T-junction 修补。独立 Draw 的有限精度、粗化后的形状变化和法线变化也可能影响画面。当前不改变构建数据格式或重做简化算法。

## Raster、Metadata 与 Resolve

[Raster](../../../src/Framework/Renderer/VirtualGeometry/VirtualGeometryRasterPass.cpp) 在构图时按实例调用选择器，将列表保存在本次 RasterInstance。CPU Draw Metadata 和录制 lambda 均遍历同一列表，保证 DrawID 与 ClusterID 对齐；绘制时不重新选择或按 MipLevel 二次过滤。列表及实例快照按值保留到本次图录制结束。

VertexFactory 不承担选择职责；仍绑定完整 PositionBuffer，每个选中 Cluster 使用自己的 VertexOffset、IndexOffset、TriangleCount。Visibility 保留 DrawID/PrimitiveID。[Material Resolve](12-virtual-geometry-material-resolve.md) 通过本次 Metadata 查找对应层的 Cluster，再使用局部索引和属性缓冲恢复三角形；材质仍取 Cluster.MaterialIndex 和实例快照。该链路不需要改变 Shader、GBuffer 编码、资源状态或 RHI 后端。

上述列表与 Draw Metadata 一一对应的描述适用于 CPU 模式。GPU 模式为每个实例的**全部候选 Cluster** 建立稳定 Metadata，DrawID 不随选择改变；录制时遍历相同候选表，用 ClusterID×20 定位间接参数。参数为零的候选不产生像素，有效像素仍通过原 DrawID/PrimitiveID 找到正确层级的局部索引、VertexOffset 和 MaterialIndex。现有 Resolve 不需要改材质算法或 GBuffer 编码，但仍为每个候选编排 Compute Pass；不可用 Draws.size() 代替 GPU 选中数量。

AddVirtualGeometryRender 的 targetPixelError 默认 1，现为所整理实例开启 GPU 选择和视锥剔除。独立 Raster 的 bGPUSelection 默认 false，保留显式 CPU 参考用法；设置 true 即使用 Compute+间接绘制，bFrustumCulling 控制其剔除。VirtualGeometryViewer 默认 GPU 动态模式，统计与列表需通过 Capture GPU 获取一次快照；CPU 模式直接显示当前选择。Generated level 只在关闭 Dynamic LOD 时检查生成产物，bInspectGeneratedLevel 优先于 GPU 模式，不是合法 Cut 模式。

GPU 使用相同的完整替换序列和 Build 边界契约，因而不额外破坏混合层边界。它没有新增边界修复算法，现有输入缺口和采样误差限制仍然适用。后续可选的 [软件光栅化](14-virtual-geometry-software-rasterization.md) 只对可见 Cut 分类，保持 selection 的原始全量间接参数不变，另外生成硬件参数与软件标记，不改变 DAG 的替换关系。

## 复杂度、验证与限制

设 C 为全部 Cluster 数、G 为 Group 数、E 为 Child/Parent 引用总数。CPU 参考每实例验证和选择时间 O(C+G+E)，临时内存 O(C+G)。GPU 路径 CPU 验证/调度仍为 O(C+G+E)；GPU 工作量 O(C+G+Child引用数)，Dispatch 数为依赖深度+1。Selection 数据为约 28C+8G+16 字节，另有既有资源、候选 Metadata 和图参数，不包含 Viewer 线框索引。CPU 仍录制 O(C) 间接绘制；Resolve 为每个候选扫描视口，实际成本可能高于仅选择后提交的 CPU 模式，未做性能收益保证。没有 Indirect Count、批量 MultiDraw、遮挡剔除、滞回或 Streaming；软件光栅的独立后续成本见上述专题。

- [VirtualGeometryLODSelectionTest](../../../src/Tests/VirtualGeometryLODSelectionTest.cpp)：手工跨组 DAG 的精确预期 Cut、多根和停滞叶、正交/透视、分辨率/投影放大、镜像/非均匀缩放/剪切、近面处理；真实 576 面曲面的阈值序列、逆向整组还原为全部叶的独立合法性检查，以及全 Cut 有向边界一致性。全程不初始化 RHI。
- 同文件注册 VirtualGeometryLODInvalidInput0～7，分别检查负预算、无效根、重复 Child、越界范围、循环关系、非法 Metric、非仿射变换和零尺寸视口。通过原有 main 捕获异常，不能将任意失败退出视为预期通过。
- [VirtualGeometryLODRenderTest](../../../src/Tests/VirtualGeometryLODRenderTest.cpp)：512 面、两个材质区域、UV 渐变纹理，同屏一个细实例和一个可粗化的镜像/非均匀缩放实例。比较实际 Metadata 与选择结果，读回 Visibility、GBuffer A/B/C，逐像素检查 ID、UV、平面法线和材质参数。

执行入口为 `Tests.exe -test=VirtualGeometryLODSelectionTest` 与 `Tests.exe -test=VirtualGeometryLODRenderTest -rhi=Vulkan`；GPU 测试也支持 DirectX12/OpenGL。CPU 阶段结果见 [2026-11-14 记录](../../dev/bugfix/故障与修复记录-2026-11-14.md#cpu-动态-lod-选择与渲染接入)。

新增 [VirtualGeometryGPUSelectionTest](../../../src/Tests/VirtualGeometryGPUSelectionTest.cpp) 在 20 个跨组替换岛组成的 240 Cluster、140 Group、60 Root 图上，对 9 种视图/变换、7 个阈值和剔除开关共 126 组用例逐项对比 CPU Cut、独立平面可见性、紧凑列表、统计和 20 字节间接参数；Group 与 Cluster Dispatch 均跨工作组。它是图逻辑夹具，不冒充真实 Build 的边界验证。[VirtualGeometryGPULODRenderTest](../../../src/Tests/VirtualGeometryGPULODRenderTest.cpp) 对真实 Build 的双材质平面、两个变换实例及三个预算，验证 GPU 间接绘制后的 Visibility/UV/法线/GBuffer，并确认可见 Cluster 属于 CPU 参考 Cut。测试命令只需替换 -test 名称。实际三后端、球体像素对照和 Launcher 范围见 [2026-11-15 记录](../../dev/bugfix/故障与修复记录-2026-11-15.md)。这些验证不替代任意复杂网格的严格像素误差与裂缝证明。
