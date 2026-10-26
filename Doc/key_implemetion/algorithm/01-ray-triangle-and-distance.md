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

三角形求交/最近点与单个 AABB 检测均为 O(1)。加速查询复杂度见 [BVH 专题](02-bvh-octree-and-scene-query.md)。现有 MathTest 主要打印矩阵变换结果，未构成这些几何边界条件的自动断言集；本轮没有新增或运行几何测试。
