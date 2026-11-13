# CPU 虚拟几何：拓扑分簇、QEM 与替换 DAG

源码入口为 [VirtualGeometryResource.h](../../../src/Framework/Engine/PublicHeader/VirtualGeometryResource.h) 和 [VirtualGeometryResource.cpp](../../../src/Framework/Engine/Private/VirtualGeometryResource.cpp)，验证入口为 [VirtualGeometryBuildTest](../../../src/Tests/VirtualGeometryBuildTest.cpp)。模块边界见 [Engine 架构](../../architecture/engine.md#cpu-虚拟几何构建)。本实现是第一版 CPU 构建器，入口保持 `void VirtualGeometryResource::Build(const StaticMesh* staticMesh)`，没有另建公开 Builder。

## 数据、单位与执行顺序

所有位置、包围体和 Error 都在 Mesh Local Space，长度单位沿用输入网格。Build 同步读取 LOD0 的 CPU 数组，不调用 InitializeResources、不创建 RHI 对象、不读回 GPU，不修改 StaticMesh。调用方必须保证构建期间 CPU 网格不被其他线程修改；同一输出对象不能并发 Build/Clear。Engine 的既有模块依赖保持原样，算法没有后端分支或新的线程、锁、Fence。

顶点流统一使用 Core 的 `Float3`（位置、法线）、`Float4`（切线、颜色）和 `Float2`（UV）；内部几何运算使用 `Double2/Double3/Double4`，Quadric 保存为 `Core::Mat4d`。类型及公共运算来自 [Math.hpp](../../../src/Framework/Core/PublicHeader/Math.hpp) 和 [Matrix.hpp](../../../src/Framework/Core/PublicHeader/Matrix.hpp)，构建器及其测试不直接使用 GLM。Core 矩阵按行存储，QEM 使用 `Matrix(row, col)` 访问；平面归一化保留先求长度倒数再相乘的次序，四项乘积采用两两求和，避免类型迁移时浮点舍入改变近似同分候选的折叠顺序。

资源、Cluster 和 LOD Metric 的 `Bounds` 均使用 [Core::BoxSphereBounds](../../../src/Framework/Core/PublicHeader/BoxSphereBounds.h)，其中 `Box` 为 AABB，`Sphere.Center/Radius` 为包围球。不再额外保存 BoundsMin、BoundsMax 或用四维向量编码中心和半径；Clear 调用 Bounds.SetEmpty。

1. 入口先 Clear 当前资源，再由 ReadMesh 检查 StaticMesh/LOD0、位置流、可选属性流、索引及 Section 覆盖，应用有符号 BaseVertexIndex 和材质槽。
2. 预处理去掉退化面，Compact 只合并完整属性一致的顶点，拒绝重复几何面。
3. Cluster 建立几何边邻接，通过区域生长产生初始 Cluster，并写入 result 的独立顶点/局部索引区间及包围体。
4. Frontier 合并当前有效前沿，重新建立 Cluster 邻接与全局特征保护集合；Partition 将相邻 Cluster 分成一次整体简化的 Group。
5. Merge → Simplify → Cluster。Simplify 尝试约减半，第二次 Cluster 仅重新分簇，不进行额外简化。
6. 几何真正减少且重新分簇数不增加时，计算表面采样偏差，追加所有 Parent Cluster 和 Group，更新前沿。
7. 重新分组，直到一整轮没有有效替换；为剩余前沿建立 Root Group，验证全部索引、边界和误差，再整体移动到当前资源。

scratch 的 BuildMesh、Topology、Quadric、候选堆、BVH 和邻接图均为函数局部 CPU 数据。持久成员保存所有层的 Cluster、Group、关系索引、位置/法线/切线/UV/颜色及全局 AABB；临时数据不会挂到渲染系统。入口先清空当前资源，后续只写局部 result，完成全部验证和统计输出后才移动到当前资源；中途失败自然销毁局部容器，当前资源保持为空，既不需要 try/catch，也没有另加异常清理守卫。Require 的校验失败仍抛出带 VirtualGeometry 前缀的错误，标准库异常原样向调用方传播。已有成功结果也会在重建开始时清空，不是保留旧结果的强异常保证。Version 当前仍为 1，未接入磁盘序列化。

## 输入及异常拓扑策略

- 最高精度按 StaticMeshRenderData 的约定选 LOD0，不遍历其他传统 LOD。
- Position 必须为有效的三分量 CPU 流；有效 Normal 为 3、Tangent 为 3/4、UV 为 2、Color 为 3/4 分量，数量必须与位置一致，所有数值有限。输入切线的 w 必须为 ±1。缺失法线按索引邻接面积加权生成；缺失切线生成与法线正交的稳定基，**不是 UV 微分切线**。本阶段只有现有 UVBuffer 所代表的一套 UV。
- Section 必须覆盖索引流且不重叠，起点和数量均为三角形对齐；非负材质槽必须存在，-1 在输出用 INVALID 表示，不擅自映射其他材质。没有 Section 时将整个索引流视为一个默认槽/未指定槽。
- 对三角形令 L² 为最长边的平方，法向量为两边叉积；当叉积长度平方 ≤ L⁴×10^-24 时丢弃并统计。全部退化则失败。越界、非有限值、损坏属性、重复几何面（含反向重叠）明确失败，不尝试自动修复。
- MakeTopology 用**完全相同的 float 位置**建立几何顶点身份；它用于寻找跨 UV/法线接缝的几何邻接，并不焊接不同属性。没有 epsilon 近似焊接；接近但不完全一致的位置会被当成开边界。
- 有向边恰好两次且方向相反才建立流形边邻接。非流形边、同向边、开边界的端点全部锁定。顶点 link 不是单个闭合环时也锁定，包括蝴蝶结顶点。异常处原面保留，相邻安全区域仍可简化，不强行把异常扇面合并。

## Cluster 与 Group 的划分

Cluster 的图节点是三角形，边权为共享几何边长度；不同材质之间不连分簇边。Partition 以空间质心排序选择尚未使用的种子，只在当前区域的拓扑邻接前沿中选择下一节点。评分为：

`(候选周长 - 2 × 与区域共享边长) / 种子尺度 + 0.15 × 到区域质心的距离 / 种子尺度`。

第一项近似新增外边界长度，第二项限制长条区域。稳定的有序容器和 ID 次序用于打破同分；不是按原三角形索引切块，也不是 METIS 或最优图划分。每个 Cluster 最多 128 面，完整复制必要的边界属性顶点，保存单一 MaterialIndex。AABB 从实际顶点取得；BoundingSphere 以 AABB 中心及最远顶点距离构造，半径向上舍入，不保证最小球。

Group 使用相同的邻接生长机制，节点改为当前 Cluster，边权仍为跨 Cluster 的流形共享边长，目标上限为 4。没有邻接的岛不强合并。一个 Group 可以包含多个材质，但简化时锁住材质接口。每一轮都从当前前沿重建图，不沿用上轮 Group 的空间盒或孩子分区；新 Group 因而可以包含由多个旧 Group 生成的 Cluster。

## QEM、属性和边界合法性

Simplify 在 Group 局部中心/最大轴向跨度下做双精度归一化运算。对每个原三角形的单位平面 p=(nx,ny,nz,d)，构造 `K=p pᵀ`，顶点 Quadric 为关联平面的 K 之和。合并 u、v 时 `Q=Qu+Qv`，代价为 `[x,1]ᵀ Q [x,1]`；线性系统 `A x=-b` 用带主元的消元求解。奇异时使用端点及中点，非奇异时同时加入优化位置，所有候选进入按代价排序的优先队列。输出位置先舍入到实际 float，再用于合法性检查和代价计算。

代价仅决定尝试次序，不是 LOD Error。每次接受折叠后累加 Quadric，并重建这个小 Group 的拓扑与优先队列，避免旧候选成本失效；安全性优先于增量堆的性能。没有强迫达到 50%，没有顶点对任意收缩或跨岛收缩。

保护由两部分构成：当前全前沿的属性/材质/异常拓扑保护，加上当前 Group 自己的外边界保护。相同位置有多个 UV、法线、切线或颜色变体时，几何点锁定；材质交界也锁定。组内原 Cluster 边界不是保护条件，Merge 会重新合并完整属性一致的重复顶点，允许消除这些旧边界。

折叠的两个端点都必须未锁定，并且对应一条两面反向使用的内部流形边；切线 handedness 不同也拒绝。TryCollapse 还会：

1. 验证 link 的公共邻居等于该边两侧的对顶点。
2. 拒绝与其他几何点重合的新位置，拒绝重复面；这也覆盖只检查公共邻居数量会漏掉的四面体反例。
3. 对保留面检查相对面积和法线朝向，法向余弦不得降到 0.1 以下；原非零 UV 有向面积不能翻转或塌成零。
4. 比较折叠前后**有向几何外边界边的多重集合**，端点位置、边段、方向、重数必须完全一致。不是仅检查锁定顶点的位置。
5. 检查未锁定顶点的 link 仍为单闭环。没有合法候选就停止。

UV/Color 以新位置在旧边上的投影参数作插值；Normal 归一化，Tangent 对新法线正交化并保留 handedness。当前没有完整的高维属性 Quadric、纹理信号误差权重、重投影重参数化或全局三角形自相交检测。严格保护接缝会降低压缩率，这是第一版的明确取舍。

## 替换 DAG 与终止

Group 的 Child 集合是简化前的整个区域，Parent 集合是简化后重新分簇的整个区域；**替换必须按 Group 整体执行**，不能认为某个 Parent 只替换某个 Child。所有旧 Cluster 留在数组中，每个 Cluster 的 GroupIndex 指向消费它的 Group，GeneratingGroupIndex 指向生成它的 Group，叶 Cluster 的后者为 INVALID。

例如 A/B/C/D → Group G → E/F；下一轮 E 可与另一个旧 Group 的产物一起分组。数组不保存一个 Parent 指针来冒充普通树。新 Cluster 的 MipLevel 为孩子最大 MipLevel+1；前沿可能包含不同 Mip 的停滞区域，因此 frontier 统计和完整 Mip 数组统计不同。

接受的替换必须严格减少三角形且不增加 Cluster 数，避免第一次区域生长碎片化造成资源膨胀；这种保守条件可能拒绝仍有价值的简化。失败区域留在前沿，其他区域变粗后仍允许重新分组再尝试。整轮没有接受的替换就终止；前沿三角形整数总数每个成功轮次严格下降，保证有限终止，不靠固定距离或任意层数代替几何构建。

最终为前沿建立一个或多个 Root Group，Root 的 Child 非空，Parent 为空，ParentMetric=ChildMetric。每个 Cluster 恰好被一个 Group 消费，所有生成和消费关系都有合法索引；生成 Group 编号严格早于消费 Group，Parent Cluster 编号严格晚于所有 Child，这提供可验证的拓扑序。Root 的定义是终端替换集合，不是一个空占位 Group。后续屏幕 LOD 遍历必须理解此约定。

## 几何误差及 LOD 包围球

GeometricError 为简化前后分别建立归一化坐标的 Core::BVH，复用 DistanceFieldTriangleBVHTraits 的最近三角形距离。细长/归一化后退化的查询三角形使用线段距离回退，空三角形集合不会传给 BVH。每面取三个顶点、三条边的中点及重心，双向查询到另一表面最近距离，取最大值并还原网格单位，另加 float 查询舍入的数值余量。

这个值保存到 Group.SimplificationError：**它是双向采样估计，不是连续表面 Hausdorff 最大距离的严格上界**。数值余量不能覆盖未采样表面的误差。平均统计是各成功 Group 的误差算术平均，不是按面积加权的表面误差。

`ChildMetric.Error = max(孩子生成 Group 的 ParentMetric.Error)`，叶取零；`ParentMetric.Error = 向上舍入(ChildMetric.Error + SimplificationError)`。这样沿 DAG 单调、与 QEM Cost 分离，但因为局部量只是估计，累计量同样不保证相对原始网格的严格最大距离。不能直接宣称由此保证某个严格像素误差阈值。

两个 Metric 的 Bounds 使用相同的盒与球：包含整个 Group 的 Child/Parent Cluster，并合并孩子生成 Group 的历史 LOD 包围体，覆盖受替换影响的后代区域。UnionBounds 使用 Core::AABB::Merge 合并盒，球的并集在双精度求解，中心转 float 的误差计入半径，再向上舍入。这里没有直接调用 BoxSphereBounds::Merge，因为该方法会从合并后的盒重建球；本算法需要保留历史区域球的保守包含关系及既有舍入策略。同一 Group 的各 Cluster 通过 GroupIndex/GeneratingGroupIndex 读取同一组替换 Error/Bounds。资源总 Bounds 同时覆盖所有层顶点，不只覆盖原始叶面。

## 验证与当前边界

ValidateResource 检查属性长度/有限值、Cluster 容量/范围/局部索引、非退化与重复面、Group 引用计数、拓扑序、严格简化、各 Group 前后有向外边界、单调误差和 LOD 球包含关系。Tests 额外独立重建边集合、检查材质接口、UV 接缝、DFS 环检测、各级实际包围范围，并检查源 CPU 数组未变化及 RHI 全程未初始化。

正常用例运行方式为 `Tests.exe -test=VirtualGeometryBuildTest`，覆盖平面、硬法线立方体、球体、带孔曲面/UV 接缝、多材质、断开岛、非流形、正负 BaseVertexIndex、极小/极大尺度和退化面过滤。8 个无效输入用例分别用 `Tests.exe -test=VirtualGeometryInvalidInput0` 至 `VirtualGeometryInvalidInput7` 执行，依次验证索引越界、空指针、重复面、非有限位置、不完整 Section、损坏 UV、无效材质槽及无 LOD 网格。它们先建立有效结果，再进行无效重建，由既有测试 main 捕获错误并返回 1，测试对象析构时检查所有结果数组已清空且 RHI 仍未初始化，输出 VIRTUAL_GEOMETRY_FAILED_BUILD_STATE_EMPTY_OK；状态不符会直接使进程失败。外部诊断同时检查预期退出码、具体错误信息和清空标记，不把任意错误退出当成成功。测试文件不新增 try/catch，也没有改其他测试或 main 的异常处理。具体编译及运行结果见 [过程记录](../../dev/bugfix/故障与修复记录-2026-11-11.md)。

当前在有限 Group 内重建堆/拓扑、逐候选验证，代价高于增量 QEM；大量顶点的图/有序集合及保存所有层也会占用明显 CPU 内存。最坏情形并不保证每轮减半，层数和累计存储可能很大。本轮不承诺百万面构建性能，也未并行化或接入传统 StaticMesh 构建流程。

离线层次分簇和整体替换对应 [Nanite 概述](https://dev.epicgames.com/documentation/en-us/unreal-engine/nanite-virtualized-geometry-in-unreal-engine) 中的构建思路。QEM 依据 [Garland/Heckbert 的原始工作](https://www.cs.cmu.edu/~garland/quadrics/)，当前 CPU 构建器采用区域生长划分、保守属性接缝和采样误差估计。资源上传沿用 [显式 RHI 生命周期](../performance/05-assets-materials-and-instance-data.md#虚拟几何的-rhi-资源生命周期)，后续已实现的最细 Cluster 光栅与调试显示见 [Visibility 流程](11-virtual-geometry-visibility.md)。UE 构建器的高级图划分、属性优化及严格几何误差控制仍属于后续完善范围。
