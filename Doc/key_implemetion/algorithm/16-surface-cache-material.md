# Surface Card 材质捕获、直接光照与 SDF 命中查询

入口为 Renderer/Private/GI 的 [SurfaceCacheMaterial.h](../../../src/Framework/Renderer/Private/GI/SurfaceCacheMaterial.h) 与 [cpp](../../../src/Framework/Renderer/Private/GI/SurfaceCacheMaterial.cpp)。材质 Atlas 缓存 BaseColor、世界法线、Metallic、Roughness；独立的 [SurfaceCacheDirectLighting](../../../src/Framework/Renderer/Private/GI/SurfaceCacheDirectLighting.h) 缓存漫反射直接出射 Radiance。未实现间接光照、Emissive、Screen Probe、Radiance Cache、Radiosity 和反射。模块职责见 [Renderer 架构](../../architecture/render/renderer.md)，调用、所有权及完成点见 [工程专题](../performance/12-surface-cache-material.md)。

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
5. LookupSurfaceCacheSample 成功返回材质、CardID 及实际采样像素；原 LookupSurfaceCache 保留原接口包装。失败返回无覆盖，不能将同一个投影坐标的另一个深度层当作命中材质。

位置容差是离散采样的近似接受范围，不是严格的最近表面证明；过近的双层薄表面仍可能歧义。六向不能覆盖的内壁会明确显示无缓存。这一版没有多层深度、自适应 Card、跨 Card 连续过滤或误差上界保证。

## SDF Ray Hit 到 Instance 与 Card

现有 Global SDF 合并取最小距离，没有存 Instance ID；[通用 Helper](../../../shaders/core/GlobalDistanceFieldHelper.sf) 的历史命中条件也不等价于标准 sphere tracing，相关限制见 [距离场专题](04-distance-fields-and-jump-flood.md)。本次没有更改全局距离场结构、合并算法或原调用方。

[SurfaceCacheQuery.sf](../../../shaders/GI/SurfaceCacheQuery.sf) 复用 Engine 的局部网格 SDF Atlas 和每个实例的 WorldToLocal、体积范围、Allocation，逐实例追踪并保留最近命中的 InstanceID。它是对象级 SDF 查询链路，不是读取全局距离场中的对象编号，也没有用 CPU 三角形 Raycast 替代 SDF。

查询模式：

- Mode=0：已有世界命中；OriginDistance.xyz 为位置，Direction.xyz 为几何法线，Control.x 为 InstanceID。未知 ID 时按所有 Card 的覆盖位置匹配，重合对象可能歧义，优先传明确 ID。
- Mode=1：世界射线；方向在 Shader 中归一化，OriginDistance.w 为最大世界距离，Control.x 为指定实例或 UINT32_MAX。射线参数 t 和结果距离均为世界长度单位。

先做世界 AABB、再做变换后的局部体积 slab 测试。局部方向不重新归一化，因此其 t 仍对应世界射线距离。局部 CPU SDF 的采样位置是 `min+i*voxel`，访问纹理时使用 `allocation+i+0.5`，并在单个块内夹紧到 `[0,resolution-1]`，避免跨块插值。

令 A 为 LocalToWorld 的线性部分，局部距离为 φ。使用 `s=1/||A^-1||F`，将 `abs(φ)*s` 用于距离步进，再乘 0.8；这在连续精确 SDF 条件下给出保守尺度，但现有离散场与符号近似不自动继承严格保证。命中容差为用户容差和 `1.5*max(localVoxelSize)*||A||F` 的较大值。局部中心差分梯度乘逆转置得到世界几何法线；梯度接近零时退回射线反方向。容差、最大距离和迭代数均有限，超出迭代数有独立状态位。

最终对最近 SDF 命中执行同一个 Lookup。SurfaceCacheQueryResult 为 80 字节，新增 SampleCoordinate.xy 保存实际采样像素，未命中时为 UINT_MAX；其余字段语义不变。结果含实际位置/距离、InstanceID、CardID、材质、迭代数和状态：bit0=有 SDF/已提供命中，bit1=有缓存材质，bit2=至少一个候选追踪达到迭代上限。未驻局部 SDF 的实例不参与 Mode=1，不能将缓存捕获成功等同于其距离场可用。

## 漫反射直接光照的语义与计算

[SurfaceCacheDirect.sf](../../../shaders/GI/SurfaceCacheDirect.sf) 对 Material Atlas 的有效像素计算独立 RGBA32F Lighting Atlas。RGB 是世界表面向外的、与观察方向无关的 **Lambertian 漫反射直接出射 Radiance**；A=1 表示已计算且有 Card 覆盖。A=0 为无效，不能把黑色但 A=1 的阴影/背光表面当成缺失缓存。

设 `c=BaseColor`、`m=Metallic`、N 为已捕获的世界着色法线。每光源方向 L 指向灯，颜色乘强度为 `Li`，距离/锥角衰减为 a，阴影为 v：

```text
fd = c * (1-saturate(m)) / pi
Lo_diffuse = sum(fd * Li * a * max(dot(N,L),0) * v)
```

不保存 Irradiance，也不在 Lookup 后再乘一次 BaseColor；后续 GI 收集的是这个表面的出射值，接收表面的反射另行计算。材质缓存仍独立保存 Roughness，但当前引擎的 Lambert + GGX 模型中 Roughness 作用于镜面项；本阶段没有使用一个假定视线生成镜面，也没有将粗糙度误用为漫反射强度。没有缓存 view-dependent GGX、AO、Emission 或环境光。此处明确采用 Lambert 漫反射系数，不包含原完整 PBR 中依赖视线的 Fresnel `KD=(1-F)*(1-metallic)`，因此并非完整 Deferred 像素颜色的逐值副本。

复用 [ShadingCommon.sf](../../../shaders/core/ShadingCommon.sf) 的 LambertDiffuse、LocalLightAttenuation 和 SpotLightAttenuation。原 ShadePoint/Spot 也使用抽出的同一衰减函数，公式保持一致：

- Directional：`L=normalize(-Direction)`，a=1。
- Point：`L=(LightPosition-P)/distance`，`a=saturate(1-distance/radius)^2`；半径外及距离近零不贡献，避免除零。
- Spot：在 Point 衰减上乘 `smoothstep(outerCos,innerCos,dot(-Direction,L))`，内锥为 1、外锥外为 0；角度输入为弧度半角，CPU 归一化 Direction 并取余弦。

颜色/强度须有限且非负，方向非零、半径为正，Spot 半角范围经 CPU 验证。GatherSceneLights 使用 Scene::ForEachLight，沿用 Scene 更新后的 Enabled 过滤；保留各类型强度、颜色、位置、方向、半径、锥角和 CastShadow，不包含 SkyLight。

## 离屏表面的阴影覆盖

现有方向光 CSM 依赖主相机分割深度，且既有 ShadowParametersHelper 存在清空深度额外判定及越界风险，不能把其输出直接用于任意离屏 Card。本实现没有改旧 CSM 或原阴影访问表，而是为缓存生成完整的光源视图：

- Directional：所有快照实例的世界包围范围合并，取中心 C 和外接球半径 r。沿光方向 d 从 `C-2r*d` 看向 C；正交投影 XY 为 ±r，near=0.1r、far=4r。所有快照接收体与遮挡体都在覆盖内，不依赖主视图。
- Point：从灯位置向 ±X/±Y/±Z 六向做 90° 透视投影；near=0.005*Radius、far=Radius。
- Spot：从灯位置沿 Direction，FOV=2*OuterConeAngle，近远面同 Point。接收点在近面内时投影无覆盖，保守返回遮挡，不伪造无阴影结果。

资源申请复用 [ShadowMapAllocator](../../../src/Framework/Renderer/Private/ShadowMapAllocator.h) 的 2D Atlas 分配；所有投影视图使用 AllocateSpotShadow 的方形块，Point 六面也展开为六个二维块，不使用相机级联或原生 Cube 采样。每块留一像素边框。绘制复用现有 PositionOnlyVS、GetVertexOnlyState 和 [DepthShadowPassPS](../../../shaders/tools/DepthShadowPassPS.sf) 的 clip-depth 模式。新的 SurfaceCacheShadowPS 仅注册该已有文件的固定模式，避免依赖原 DepthShadowPassPS 全局类型中面向 mesh 的变体判断。所有快照 LOD0/Section 都参与遮挡，包括主相机不可见对象；传统 SceneRenderer 不变。

整张 Atlas/临时 D32 清空一次，块内各网格 Load_Store，使用无面剔除、Depth Less，BeginRenderPass 后设置矩形 viewport/scissor。阴影格式沿用分配器 RG16F，保存 clip z/w 及其平方，当前只读第一分量。Point 按 `P-LightPosition` 的绝对值最大轴选择对应面。

查找以同一 VP 投影世界点，ScreenYSign 对应各后端，执行受块边界约束的 3×3 PCF。比较 `receiverClipDepth-0.001 <= storedDepth`；清空值 1 正确表示没有遮挡。没有有效投影返回 0，不能在未覆盖区产生光漏。RG16F、固定 clip bias、单张场景正交图和 Point 面边缘夹紧仍有限制；这不是任意尺度下无误差的阴影保证，改进与验收记入 [优化项](../../dev/optimize/优化点-2026-11-17.md)。

## Lighting Lookup 与调试

[SurfaceCacheLightingLookup.sf](../../../shaders/GI/SurfaceCacheLightingLookup.sf) 消费第一阶段直接/SDF 查询的 SurfaceCacheQueryResult。先检查 material-valid、InstanceID、CardID、SampleCoordinate 是否在该 Card Tile 和 Atlas 内，再检查当前 PositionCoverage 与 Lighting A。读取**第一次材质查询实际选择的同一像素**，不使用已被 normal map 改变的着色法线再次选择另一张 Card。

结果 SurfaceCacheLightingQueryResult 为 48 字节：Radiance(float4)、PositionDistance(float4)、Status(uint4)。沿用 InstanceID、CardID、hit/iteration-limit；bit1 表示 Lighting 有效。命中无 Card 或没有有效光照时 A=0，不能将其视为有效黑色 Radiance。相应 CPU 参数必须由匹配快照的 Register/AddUpdatePasses 获得，失效检查及跨图约束见 [生命周期](../performance/12-surface-cache-material.md)。

AddSurfaceCacheLightingDebugPass 可单独显示整个光照 Atlas，或将查询输出为 32×32 色块。仅调试显示执行 `max(Lo*Exposure,0)/(1+max(Lo*Exposure,0))`，Atlas 原数据始终线性 HDR；命中无覆盖显示紫色，miss 黑色。

## 复杂度与验证边界

设实例 I、Card C=6I、Section 总数 S、射线 R、步数上限 K。构建遍历 CPU 顶点 O(V)，捕获约 6S 次绘制；已有世界命中逐 Card 查询 O(C)，射线最坏 O(RIK)，命中后另查 O(RC)，每候选最多 9 个点读取。尚无实例 BVH、对象剔除列表或捕获更新调度。

[SurfaceCacheMaterialTest](../../../src/Tests/SurfaceCacheMaterialTest.cpp) 使用真实 DistanceFieldManager 生成/上传两个盒网格，三个实例（其中两个共享资源）、18 Card、多 Section、UV 渐变纹理及法线贴图，覆盖镜像、旋转、非均匀缩放。每帧 41 个查询：18 个已知世界命中、18 个指定实例的 SDF 射线、3 个不带 ID 的射线、一条 miss、一个投影内但无有效表面覆盖的点。第二帧不重新捕获，继续查询持久缓存。

最终三后端每项两帧通过，材质与 UV 误差、世界法线、PBR 数值、Instance/Card ID、全部 Card 中心及无效边框有实际 GPU 读回断言；Atlas 与查询 Debug 有图像读回。命令、过程中故障、原测试回归及 Launcher 的范围见 [开发记录](../../dev/bugfix/故障与修复记录-2026-11-17.md)。该盒场景不构成开放/自交/薄壳/深凹模型的 SDF 和覆盖鲁棒性证明。

直接光照新增工作量：有效 Atlas 像素 T、灯光 L、阴影视图 H（Point 为六个）时，计算 O(TL)，每次遮挡最多 9 点读取，阴影绘制约 H*S 次 Section Draw。没有针对光源或遮挡体做空间剔除，也没有 GPU 阶段计时基线。[SurfaceCacheDirectLightingTest](../../../src/Tests/SurfaceCacheDirectLightingTest.cpp) 在三后端各运行 17 组、每组 8 查询，覆盖三类灯光/遮挡、聚光外锥及过渡、Point 两相反面、禁用、强度/颜色、多灯相加、持久注册、材质/粗糙度变化、物体变换、离屏背面及无覆盖拒绝；Radiance 与 CPU 解析参考的 RGB 向量误差小于 0.012。盒几何与有限灯位不证明所有 Cube 面接缝、极端尺度及复杂材质均无误差。
