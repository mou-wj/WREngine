# 射线、三角形求交与最近距离

核心源码：[DistanceFieldMgr.cpp](../../../src/Framework/Engine/Private/DistanceFieldMgr.cpp) 中 DistanceFieldTriangleBVHTraits::RayIntersect、ClosestPointOnTriangle、ClosestDistance；[BVH.hpp](../../../src/Framework/Core/PublicHeader/BVH.hpp) 中 Ray::RayIntersectAABB。GPU 距离版本见 [DistanceFieldVoxelizeCS.sf](../../../shaders/tools/DistanceFieldVoxelizeCS.sf)。

距离场的三角形函数用于网格距离场；场景拾取入口与差异见后文。两者均为 CPU 几何算法，不是 Vulkan/DXR 驱动内部的求交实现。

## 输入与参数的含义

射线 `P(t)=O+tD`；三角形顶点 A/B/C，边 E1=B-A、E2=C-A。若 D 为单位向量，t 才是空间距离；否则 t 只是沿 D 的参数。例如 D 扩大两倍，同一个交点的 t 减半。

三角形点可写为 `Q(u,v)=A+uE1+vE2`，重心权重为 `(1-u-v,u,v)`。在三角形内部须 `u>=0、v>=0、u+v<=1`，射线前方要求 t>=0。

## 项目使用的 Möller–Trumbore 形式

从 `O+tD=A+uE1+vE2` 得 `[-D,E1,E2]·(t,u,v)=O-A`。利用三重积求解，无需先计算三角形平面或保存其法线：

```text
p   = cross(D,E2)
det = dot(E1,p)
T   = O-A
u   = dot(T,p)/det
q   = cross(T,E1)
v   = dot(D,q)/det
t   = dot(E2,q)/det
```

源码中的变量 edge1/edge2/p/det/t/q/distance 对应上式；注意源码局部变量 t 是向量 T，最终标量射线参数叫 distance。

执行次序刻意在发现不可能命中时尽早退出：

1. `abs(det)<1e-6`：方向接近平面平行，或三角形退化，返回 false。
2. u 不在 [0,1]：返回 false。
3. v<0 或 u+v>1：返回 false。
4. distance<1e-6：交点过近或在后方，返回 false。
5. 写 outDistance，返回 true。

使用 abs(det) 而不是单向 det 判断，表示该函数没有做背面剔除。u/v 边界使用闭区间，射线穿过共享边时，两个相邻三角形可能都返回命中。

## 可手算的例子

A=(0,0,0)、B=(1,0,0)、C=(0,1,0)，O=(0.25,0.25,1)、D=(0,0,-1)：E1=(1,0,0)，E2=(0,1,0)，p=(1,0,0)，det=1，u=v=0.25，t=1。交点=(0.25,0.25,0)，权重=(0.5,0.25,0.25)。

若 O 改成 (0.8,0.8,1)，则 u+v=1.6，被拒绝；若 D=(0,0,1)，求得负 t，被拒绝；若三点共线，det 为零，不能得到唯一三角形面交点。

## epsilon 和精度边界

det 同时受边长和 D 长度影响，固定 1e-6 不是尺度无关容差；极小模型、极大坐标和接近平行情况需要独立测试。函数不是 watertight 算法，没有统一共享边归属，也没有显式 tMax；调用方若需要有限线段或最大距离还要自行检查。

BVH::RayIntersect 返回命中的**图元 ID 集合**，没有按 t 排序，也没有返回最近一次交点。不能把 set 中最小 ID 当成最近命中。SDF 奇偶判断更需要区分“命中三角形数”和“不同空间交点数”。

## 点到三角形：七个 Voronoi 区域

最近点问题是约束最小化 `min ||P-(A+uE1+vE2)||²`，约束 u>=0、v>=0、u+v<=1。若平面投影在面外，最近点落在边或顶点，因此不能只算点到平面的距离。

定义：AP=P-A、BP=P-B、CP=P-C；d1=E1·AP，d2=E2·AP，d3=E1·BP，d4=E2·BP，d5=E1·CP，d6=E2·CP。源码按下表逐步判断：

| 区域 | 判据 | 最近点 |
| --- | --- | --- |
| A | d1<=0 且 d2<=0 | A |
| B | d3>=0 且 d4<=d3 | B |
| AB | vc=d1d4-d3d2<=0，且 d1>=0、d3<=0 | A + E1·d1/(d1-d3) |
| C | d6>=0 且 d5<=d6 | C |
| AC | vb=d5d2-d1d6<=0，且 d2>=0、d6<=0 | A + E2·d2/(d2-d6) |
| BC | va=d3d6-d5d4<=0，且 d4-d3>=0、d5-d6>=0 | B+(C-B)·(d4-d3)/((d4-d3)+(d5-d6)) |
| 面内部 | 前述区域都不满足 | A+E1·vb/s+E2·vc/s，s=va+vb+vc |

ClosestDistance 用 ||P-Q|| 返回无符号距离，符号由外层距离场生成决定。CPU 最近点函数未独立防护所有退化分母，不能把平行求交的 epsilon 当作最近点的退化保护。

GPU 版本返回距离平方，避免每个候选都 sqrt；包含法线长度退化时回退到顶点距离的分支，但该分支不等价于严格的退化线段最短距离解，也不保证前面所有边分支的零分母均被覆盖。

## 射线与 AABB：slab 区间

每轴在 min/max 两平面之间限制 t：`t0=(min-O)/D、t1=(max-O)/D`，交换保证 t0<=t1；三个轴取区间交集，`tMin=max(0,t0x,t0y,t0z)`，`tMax=min(t1x,t1y,t1z)`，若 tMax<tMin 则不相交。这是 BVH 剪枝，不是最终三角形命中。

RayIntersectAABB 接受可选 maxParameter，默认 FLT_MAX，并拒绝空盒、非有限输入和负上界。D=0 时直接判断起点是否位于该轴 slab 内，边界包含在内，避免原先倒数写法的 0×∞/NaN；非零方向用除法计算区间。maxParameter 是射线参数 t 的上界，只有单位方向时才等于世界距离。边界接触使用 tMax<tMin，保留相等的接触点；没有修改 BVH 的全命中 ID 集合语义。

三角形求交/最近点与单个 AABB 检测均为 O(1)。加速查询复杂度见 [BVH 专题](02-bvh-octree-and-scene-query.md)。现有 MathTest 主要打印矩阵变换结果，未构成这些几何边界条件的自动断言集；最初专题整理没有新增或运行几何测试，后续编辑器拾取检查见下节。

## 统一场景拾取：屏幕射线、场景候选与最近三角形

入口：[SceneView::MakeRay](../../../src/Framework/Engine/Private/SceneView.cpp)、[SceneInterface / ScenePickResult](../../../src/Framework/Engine/PublicHeader/SceneInterface.h)、[Scene::Raycast](../../../src/Framework/Renderer/Private/Scene.cpp)、[EditorScene::SelectAt / ApplyMaterialAt](../../../src/Editor/SceneEditor/Private/EditorScene.cpp)。Editor 通过 Engine 接口完成查询，不链接具体 Renderer；接口查询不更新选择、材质或 GPU 状态。

### 屏幕位置与有限射线

MakeRay 的 position 是视口内部像素坐标，左上为 (0,0)，有效范围为 [0,Width)×[0,Height)。Window/Widget 的位置转换在上层完成，Viewport.X/Y 不再次扣除。材质投递的 UV 先乘最后绘制视图的 Width/Height。SceneView 保存当时的 View/Projection/InvVP 和深度约定；使用它完成拾取后才调用 Select，避免属性栏改变视口布局而影响本次射线。

```text
clipXY = (2*x/Width-1, 1-2*y/Height)
Hnear = InvVP * (clipXY, nearZ, 1)   // ZO: 0；NO: -1
Hfar  = InvVP * (clipXY, 1, 1)
N = Hnear.xyz / Hnear.w
F = Hfar.xyz  / Hfar.w
O = N; D = normalize(F-N); maxDistance = length(F-N)
```

输出从近面开始，到远面结束，透视/正交均使用同一反投影过程。无效视口、越界/非有限位置、齐次 w 绝对值小于 1e-7 或结果无效/长度过小返回 false，输出清空。bDepthRangeZeroToOne 默认 true，调用方仍必须与投影保持一致；未实现无限远投影专用射线。

### 场景空间树与三角形窄相位

Scene::Raycast 拒绝非有限起点/方向/最大距离、零或极小方向以及非正距离，将输入方向归一化。因此公共接口的 maxDistance 和结果 Distance 都使用世界长度，而不随输入方向长度缩放。Meshes 查询读取最后一次 FlushPendingUpdates 更新的 SceneAccelerationStructure；Lights 查询读取已注册 LightSceneProxy，沿用光源通知即时更新参数、注册/删除经 Flush 的既有流程。查询不自动 Flush 或等待 GPU。

树的射线 Query 用有限 slab 区间剪去未相交节点，叶子返回 PrimitiveSceneProxy 候选。随后只检查可见 StaticMeshProxy 的 CPU LOD0；同当前绘制路径使用 LOD0，不调用距离场的 BVH。代理的 LocalToWorld 用于顶点变换，避免组件尚未 Flush 的新位置与旧空间树混用。

每个 Section 检查 MaterialIndex、实际材质、索引区间和位置缓冲有效性；三角形索引为 `IndexBuffer[FirstIndex+offset+corner]+BaseVertexIndex`。按 NumComponents 读取局部位置前三个分量并变换到世界，拒绝非法索引和非有限顶点。继续使用上文 Möller–Trumbore 公式：

- `abs(determinant)<=1e-7*length(edge1)*length(edge2)` 排除平行/退化，区别于距离场函数的固定 epsilon。
- `0<=u<=1、v>=0、u+v<=1` 限定三角形内部。
- `0<=distance<=maxDistance` 限定线段，保留世界距离最近命中；同距离保留先遇到者，没有稳定的并列排序保证。

顶点在世界空间求交，父子变换、非均匀及负缩放下各对象的 t 可直接比较。WorldPosition=O+D*Distance；SectionIndex 为 LOD0 的 Section 下标，MaterialIndex 为该 Section 的材质槽，TriangleIndex 为 Section 内从零开始的三角形编号。失败返回 false 且将结果恢复默认值，避免复用上次命中。Component 是场景客户拥有的借用引用，不是持久对象 ID。

查询成本为访问的树节点数加候选三角形数，最坏仍为 O(N+T)；候选 vector 额外 O(K)，递归栈随树高增长。没有新建每网格三角形 BVH、持久查询缓存或异步拾取任务。既有资产 CPU 几何仍通过组件借用，查询、资产变更和 Flush 必须串行，不能把代理变换快照推广为任意几何修改/跨线程均安全。

### Editor 如何消费结果

普通点击先处理方向球和已启用变换工具；其余左键由 Window 捕获，松开时确认移动全程小于 4 像素且仍在视口内，再调用 SelectAt。光源图标是可见屏幕叠加，按相同图标几何优先命中；否则以 All 过滤调用 Raycast 返回最近网格或光源范围/箭头，映射到现有节点列表并 Select。无命中 Select(-1)。方向球/变换工具的拖动、失焦、resize、视角切换和其他鼠标操作不触发对象点击。

ApplyMaterialAt 共用同一个 MakeRay/Raycast，保留默认 Meshes 过滤，使用命中 MaterialIndex，等待原有具体访问完成点后修改组件材质，通知场景更新。无命中保持原选择与材质，不把投递退回当前选中对象；不创建材质实例、不修改共享网格默认材质。输入/生命周期见 [UI 实现](../performance/08-ui-rendering-and-window-lifetime.md)。

当前是双面几何拾取，不按 CullMode 模拟背面剔除，也不处理透明裁剪、骨骼变形或 GPU 顶点位移。零缩放的退化三角形拒绝，算法不是 watertight，共享边、极端尺度仍有浮点边界。All/Lights 额外检查点光球面、聚光有限圆锥及平行光箭头线段；几何索引保持 -1，天空光仍只通过列表选择。范围内起点取前方出口而非零距离，多个对象按最近表面/线段投影距离比较，网格与光源等距时保留先检查的网格；光源之间严格等距不保证稳定排序。解析公式、误差和线段阈值见 [光源提示与拾取](03-transforms-frustum-and-depth.md)。未保留 CPU 几何的资源需另行接入；此方案不是 GPU 像素 ID 拾取。

2026-10-31 的独立诊断覆盖 NO/ZO×透视/正交反投影、轴平行/边界 slab、最近命中、材质槽、非单位方向、有限距离、Flush 前后代理变换、负尺度/父变换和删除；公开 Window 输入链与完整 Editor 验证点击、拖动取消、光源图标和属性栏联动。原材质投递回归保留最近网格、非零槽位隔离、偏心 UV、六向 TRS 和 GPU 实色检查。入口、真实范围和未解决校验见 [当日记录](../../dev/bugfix/故障与修复记录-2026-10-31.md)。
