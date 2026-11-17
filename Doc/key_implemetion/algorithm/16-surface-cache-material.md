# Surface Card 材质捕获与 SDF 命中查询

入口为 Renderer/Private/GI 的 [SurfaceCacheMaterial.h](../../../src/Framework/Renderer/Private/GI/SurfaceCacheMaterial.h) 与 [cpp](../../../src/Framework/Renderer/Private/GI/SurfaceCacheMaterial.cpp)。本阶段只缓存 BaseColor、世界法线、Metallic、Roughness，不缓存直接/间接光照或 Emissive，也不实现 Screen Probe、Radiance Cache、Radiosity 和反射。模块职责见 [Renderer 架构](../../architecture/render/renderer.md)，调用、所有权及完成点见 [工程专题](../performance/12-surface-cache-material.md)。

## 输入、坐标与 Card 生成

输入是一组显式的 SurfaceCacheMeshInstance 快照：InstanceID、LocalToWorld、共享 StaticMeshRenderData 和按材质槽排列的不可变 MaterialRenderProxy。也可在场景 Flush 后调用 GatherSceneInstances；它使用 ForEachPrimitive 及原 LocalVF 实例 ID 表，不使用主相机视锥，因此当前相机外的可见静态对象也能参与。隐藏对象不收集。

每个实例遍历 LOD0 的 CPU Position，乘仿射变换得到世界 AABB。创建世界坐标的 ±X、±Y、±Z 六张 Card；这不是沿模型主方向的自适应分布。记录：

- Center：世界 AABB 中心。
- AxisU/AxisV/AxisN：正交基；N 指向捕获相机，U/V 覆盖二维面。
- Extent.xyz：AABB 在 U/V/N 上的投影半长，最小 0.001、再乘 1.02 留边；Extent.w 是世界空间像素尺度，用于有限分辨率的位置核验。
- Tile：Atlas 内有效像素矩形 x/y/width/height，四周保留一像素无效边框。
- Owner：InstanceID、快照实例索引、六向局部编号及保留位。公开 InstanceID 可以不连续，不能将其直接当 Instances 数组下标。

六向法线任意单位方向至少有一轴投影较大，但这只保证候选方向存在，**不保证六张外部 Card 能覆盖所有凹槽、内壁或遮挡层**。

## 正交投影与捕获

设 P 为世界点，C 为 Card 中心，U/V/N 为单位正交轴，a/b/d 为三个半长。捕获矩阵直接实现：

```text
clip.x = dot(P-C,U)/a
clip.y = dot(P-C,V)/b
clip.z = 0.5 - dot(P-C,N)/(2d)
clip.w = 1
```

N 一侧的外表面深度更小，Depth Compare=Less。整个 Atlas 只清空一次，之后各 Card/Section 使用 Load_Store，在 BeginRenderPass 之后设置 Tile 的 viewport/scissor；部分后端在 BeginRenderPass 中会初始化动态状态，顺序不能反过来。

材质捕获复用 LocalVertexFactory 的顶点流及 MeshMaterialShader 编译路径，[VS](../../../shaders/GI/SurfaceCacheCaptureVS.sf) 与 [PS](../../../shaders/GI/SurfaceCacheCapturePS.sf) 共用完整 varying 签名。VS 用逆转置变换法线；切线用 LocalToWorld 后对法线正交化，bitangent 符号包含实例行列式符号和 tangent.w。法线贴图要求有效切线流；普通无贴图法线可不带切线。

旧式基础几何可能携带 tangent.w=0；捕获将其按 +1 手性处理，正交化后切线接近零时使用稳定正交基，避免没有法线贴图也因零 bitangent 产生 NaN。它不能还原本来就缺失的作者 UV 手性，复杂法线贴图仍依赖正确切线数据；测试的第二个网格覆盖零 w、另一个镜像实例使用有效切线与法线贴图。

PS 通过现有 MaterialParametersHelper 的 BuildMaterialData 求值，保留纹理、UVTiling/Offset、ORM 与 NormalIntensity 的现有语义。像素着色器使用正常隐式纹理导数，不存在 Compute 阶段直接使用隐式导数的问题。输出三个 RGBA32F 附件：

| 附件 | RGB | A |
| --- | --- | --- |
| BaseColorMetallic | BaseColor | Metallic |
| NormalRoughness | 归一化世界法线，含法线贴图 | Roughness |
| PositionCoverage | 捕获位置（世界空间） | 是否有效覆盖 |

另有 D32 深度附件，仅用于捕获最近层。每个 Tile 只绘制所属实例，因此相邻对象不会妨碍建立这个对象自己的缓存。最近层的几何法线与 Card N 点积大于 0.05 时 Coverage=1，否则为 0；背向最近层仍写深度，不能用更深层冒充该位置的有效外表面。这里不依赖片元 discard 或可选的 Vulkan fragment-demotion 能力。

## 世界命中的材质 Lookup

可复用函数见 [SurfaceCacheLookup.sf](../../../shaders/GI/SurfaceCacheLookup.sf)。输入为 InstanceID、世界命中位置、**几何**法线和位置容差。查找步骤：

1. 筛选所属 Instance 的 Card，检查点在 Card 三轴覆盖范围内，几何法线与 N 点积大于 0.2。
2. 计算 `uv = (dot(P-C,U)/a, dot(P-C,V)/b * ScreenYSign) * 0.5 + 0.5`。ScreenYSign 在 Vulkan/D3D12 为 -1、OpenGL 为 +1，已用非中心 UV 渐变读回核对。
3. 对像素周围 3×3 做 Tile 内受限的点读取，不跨 Tile 采样，避免 Atlas 边框和相邻实例的材质渗漏。
4. 只有 Coverage 有效，且捕获位置与输入点距离不大于 `max(查询容差, 2*Extent.w)` 的样点才参与。以 `法线方向匹配度 - 0.25*位置误差/允许误差` 排序，选最优样点。
5. 成功返回材质及 CardID；失败返回无覆盖，不能将同一个投影坐标的另一个深度层当作命中材质。

位置容差是离散采样的近似接受范围，不是严格的最近表面证明；过近的双层薄表面仍可能歧义。六向不能覆盖的内壁会明确显示无缓存。这一版没有多层深度、自适应 Card、跨 Card 连续过滤或误差上界保证。

## SDF Ray Hit 到 Instance 与 Card

现有 Global SDF 合并取最小距离，没有存 Instance ID；[通用 Helper](../../../shaders/core/GlobalDistanceFieldHelper.sf) 的历史命中条件也不等价于标准 sphere tracing，相关限制见 [距离场专题](04-distance-fields-and-jump-flood.md)。本次没有更改全局距离场结构、合并算法或原调用方。

[SurfaceCacheQuery.sf](../../../shaders/GI/SurfaceCacheQuery.sf) 复用 Engine 的局部网格 SDF Atlas 和每个实例的 WorldToLocal、体积范围、Allocation，逐实例追踪并保留最近命中的 InstanceID。它是对象级 SDF 查询链路，不是读取全局距离场中的对象编号，也没有用 CPU 三角形 Raycast 替代 SDF。

查询模式：

- Mode=0：已有世界命中；OriginDistance.xyz 为位置，Direction.xyz 为几何法线，Control.x 为 InstanceID。未知 ID 时按所有 Card 的覆盖位置匹配，重合对象可能歧义，优先传明确 ID。
- Mode=1：世界射线；方向在 Shader 中归一化，OriginDistance.w 为最大世界距离，Control.x 为指定实例或 UINT32_MAX。射线参数 t 和结果距离均为世界长度单位。

先做世界 AABB、再做变换后的局部体积 slab 测试。局部方向不重新归一化，因此其 t 仍对应世界射线距离。局部 CPU SDF 的采样位置是 `min+i*voxel`，访问纹理时使用 `allocation+i+0.5`，并在单个块内夹紧到 `[0,resolution-1]`，避免跨块插值。

令 A 为 LocalToWorld 的线性部分，局部距离为 φ。使用 `s=1/||A^-1||F`，将 `abs(φ)*s` 用于距离步进，再乘 0.8；这在连续精确 SDF 条件下给出保守尺度，但现有离散场与符号近似不自动继承严格保证。命中容差为用户容差和 `1.5*max(localVoxelSize)*||A||F` 的较大值。局部中心差分梯度乘逆转置得到世界几何法线；梯度接近零时退回射线反方向。容差、最大距离和迭代数均有限，超出迭代数有独立状态位。

最终对最近 SDF 命中执行同一个 Lookup。结果含实际位置/距离、InstanceID、CardID、材质、迭代数和状态：bit0=有 SDF/已提供命中，bit1=有缓存材质，bit2=至少一个候选追踪达到迭代上限。未驻局部 SDF 的实例不参与 Mode=1，不能将缓存捕获成功等同于其距离场可用。

## 复杂度与验证边界

设实例 I、Card C=6I、Section 总数 S、射线 R、步数上限 K。构建遍历 CPU 顶点 O(V)，捕获约 6S 次绘制；已有世界命中逐 Card 查询 O(C)，射线最坏 O(RIK)，命中后另查 O(RC)，每候选最多 9 个点读取。尚无实例 BVH、对象剔除列表或捕获更新调度。

[SurfaceCacheMaterialTest](../../../src/Tests/SurfaceCacheMaterialTest.cpp) 使用真实 DistanceFieldManager 生成/上传两个盒网格，三个实例（其中两个共享资源）、18 Card、多 Section、UV 渐变纹理及法线贴图，覆盖镜像、旋转、非均匀缩放。每帧 41 个查询：18 个已知世界命中、18 个指定实例的 SDF 射线、3 个不带 ID 的射线、一条 miss、一个投影内但无有效表面覆盖的点。第二帧不重新捕获，继续查询持久缓存。

最终三后端每项两帧通过，材质与 UV 误差、世界法线、PBR 数值、Instance/Card ID、全部 Card 中心及无效边框有实际 GPU 读回断言；Atlas 与查询 Debug 有图像读回。命令、过程中故障、原测试回归及 Launcher 的范围见 [开发记录](../../dev/bugfix/故障与修复记录-2026-11-17.md)。该盒场景不构成开放/自交/薄壳/深凹模型的 SDF 和覆盖鲁棒性证明。
