# 射线、三角形求交与最近距离

核心源码：[DistanceFieldMgr.cpp](../../../src/Framework/Engine/Private/DistanceFieldMgr.cpp) 中 DistanceFieldTriangleBVHTraits::RayIntersect、ClosestPointOnTriangle、ClosestDistance；[BVH.hpp](../../../src/Framework/Core/PublicHeader/BVH.hpp) 中 Ray::RayIntersectAABB。GPU 距离版本见 [DistanceFieldVoxelizeCS.sf](../../../shaders/tools/DistanceFieldVoxelizeCS.sf)。

这些 CPU 函数用于网格距离场，不是 Vulkan/DXR 驱动内部的求交实现。

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

当前 RayIntersectAABB 直接计算 1/D，没有显式处理 D=0；平行轴且恰好位于边界可能产生 0×∞/NaN，需要单独核查。边界接触使用 tMax<tMin，保留相等的接触点。

三角形求交/最近点与单个 AABB 检测均为 O(1)。加速查询复杂度见 [BVH 专题](02-bvh-octree-and-scene-query.md)。现有 MathTest 主要打印矩阵变换结果，未构成这些几何边界条件的自动断言集；最初专题整理没有新增或运行几何测试，后续编辑器拾取检查见下节。

## 编辑器材质投放：从视口落点到最近三角形

源码：[EditorScene::ApplyMaterialAt](../../../src/Editor/SceneEditor/Private/EditorScene.cpp)、[视口落点转换](../../../src/Editor/SceneEditor/Private/EditorScenePanel.cpp)。这是编辑器一次性 CPU 查询，不调用 Vulkan/DXR，不依赖网格距离场数据，也未把 GI 的未导出 TriangleBVHTraits 作为跨模块接口。

输入 viewportPosition=(u,v) 为当前显示区域的归一化坐标，左上为 (0,0)、右下接近 (1,1)。Widget 从 ImGui 窗口坐标减去 geometry.X/Y，再除以 Width/Height。无效/非有限 UV 不产生命中；确认的输出为组件下标、section.MaterialIndex，并触发该槽材质覆盖。失败返回 false，不改变选择/材质。

1. 构造裁剪点 C=(2u-1,1-2v,z,1)，使用相机 GetInverseViewProjectionMatrix 得齐次世界点 H，再以 H.xyz/H.w 作透视除法。W 接近零时明确报错。当前相机为右手、ZO 深度，近远 z 为 0/1；代码也按相机深度枚举保留 NO 的 -1/1 选择。
2. origin=Pnear，ray=Pfar-Pnear，direction=ray/length(ray)，nearest 初始为 length(ray)。因此所有命中的 distance 都是世界长度，并限定在近远裁剪之间；不从相机位置向无限远投射而误选近裁剪前物体。
3. 遍历场景组件与 LOD0 的 section，和当前 StaticMeshDrawBuild 的 LOD 选择一致。索引区间为 FirstIndex 至 FirstIndex+NumIndices，三角形索引加 BaseVertexIndex 后按 PositionBuffer.NumComponents 读取前三个分量；检查范围、负索引和至少三个位置分量，异常区间跳过。
4. 对三个局部顶点用组件完整 GetComponentTransform 转到世界，再执行上文的 Möller–Trumbore 公式，源码 determinant/u/v/distance 对应 det/u/v/t。世界空间计算使平移、旋转、层级变换、非均匀/镜像缩放下的 distance 仍可直接比较，无需对逆变换后的方向重新定标。
5. 此处平行/退化判断为 abs(determinant)<=1e-7×length(edge1)×length(edge2)，区别于距离场函数的固定 epsilon。重心条件使用 0<=u<=1、v>=0、u+v<=1；满足 0<=distance<nearest 才替换结果，因而返回最近命中而非列表首项/当前选择，同距离保留先遇到的三角形。

该查询为双面几何拾取，不根据材质 CullMode 模拟背面剔除；没有透明度/裁剪贴图检测。零缩放产生的退化三角形会被拒绝，但算法不是 watertight，共享边、极大坐标和很瘦的三角形仍有浮点边界。遍历成本为总三角形数 O(T)、额外空间 O(1)，只在投递时执行，没有新建 BVH/缓存或每帧悬停扫描；大型场景可有明显点击延迟。

查询只覆盖当前 Renderer 使用的静态 LOD0 与其 CPU 保留位置/索引，不包括骨骼、GPU 顶点位移或未保留 CPU 几何。既有 glTF 多 primitive 合并限制依旧存在，本次没有为拾取重写导入器。材质只支持已有 Opaque 绘制，其他模式报错。组件覆盖/提交时序见 [UI 生命周期](../performance/08-ui-rendering-and-window-lifetime.md)、[资产所有权](../performance/05-assets-materials-and-instance-data.md)。实际检查与限制见 [2026-10-27 记录](../../dev/bugfix/故障与修复记录-2026-10-27.md)。
