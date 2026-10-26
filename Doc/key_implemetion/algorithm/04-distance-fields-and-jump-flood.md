# 局部/全局距离场、体素化与 Jump Flood

源码入口：[DistanceFieldMgr.cpp](../../../src/Framework/Engine/Private/DistanceFieldMgr.cpp)、[DistanceFieldData.cpp](../../../src/Framework/Engine/Private/DistanceFieldData.cpp)、[DistanceFieldProcess.cpp](../../../src/Framework/Engine/Private/DistanceFieldProcess.cpp)、[GlobalDistanceField.cpp](../../../src/Framework/Renderer/Private/GlobalDistanceField.cpp)、[Scene.cpp](../../../src/Framework/Renderer/Private/Scene.cpp)。几何基础见 [三角形距离](01-ray-triangle-and-distance.md)。

## 数据层次

`网格局部三角形 → 局部 64³ 距离数据 → Engine 距离场 Atlas → 场景世界 Block 页表 → 全局距离场 Atlas → Shader 采样/步进`。

标准有符号距离 φ(P) 在表面为零、外部为正、内部为负，绝对值是到表面的最短欧氏距离。存储成 3D 纹理后是离散近似，还受插值、量化、坐标尺度影响，不能无条件继承连续精确 SDF 的数学保证。

## 当前默认 CPU 路径

DistanceFieldManager::BuildMeshDistanceField 的 buildInCpu 固定为 true：每 LOD 从 Position/Indices 建三角形数组与 Core BVH，BuildCPUData 固定分辨率 64³。voxel size=(bounds.max-bounds.min)/64，GenerateDistanceSlice 当前采样位置是 `min+(x,y,z)*voxelSize`，并非体素中心。

每个采样点先查询 BVH 最近距离，再向包围盒中心附近带随机扰动的方向发射单位射线，按命中图元做符号判断。若 m_TaskPool 非空按 z 切片投递并等待；全局默认 manager 未注入任务池，因此不能把切片并行代码写成默认已并行运行。

64³=262144 个样点，R32_FLOAT 每份约 1 MiB，不含 BVH、三角形、上传和 Atlas。粗略成本是 O(R³×查询成本)，最坏接近 O(R³T)；固定分辨率改变空间精度，并不随模型尺寸自动保持单位体素大小。

## 奇偶符号的原理与现状差别

对封闭、无自交的表面，取不经过边/顶点的通往无穷远射线，独立表面穿越次数奇数表示内部，偶数表示外部。这里要数交点，不是三角形 ID；共享边可能让同一穿越被算两次。随机扰动只降低退化概率，不是确定性鲁棒性证明。

当前代码先按 intersectIds.size()%2 置负；若命中 ID 数大于 2，又按 1e-5 的距离阈值尝试去重。但该分支末尾无论去重结果奇偶都赋成负距离。**所以当前不是严格的奇偶符号实现。** 开放网格、自交、共享面与平行边也未被完整处理，不能将生成结果描述为保证正确的封闭体 SDF。

BuildTriangles 假设索引数是 3 的倍数、索引有效且 positions 为连续 xyz；最近点和空 BVH 的退化保护有限。导入器的约束见 [网格专题](08-mesh-import-and-geometry.md)。

## 局部 Atlas 的工程布局

Engine 初始化 Atlas 为 128×128×128、blockResolution=64，R32_FLOAT，等于 2×2×2=8 个 block，整图约 8 MiB。Allocate 线性扫描 allocation map，返回像素坐标 offset、固定块尺寸与 ID；Free 把对应 block 标为空，UploadData 上传该空间区域。

这是逻辑 3D 纹理块管理，不是 GPU 物理内存分页。容量不足、重复释放、初始化前使用等条件必须由调用链检查。已有静态网格测试未初始化 manager 就上传造成空纹理异常的记录，不能把 LoadMesh 成功读取 glTF 等同于完成全部资源前置初始化。

## GPU 体素化与 JFA：存在实现，默认入口未接通全套

[DistanceFieldVoxelizeCS.sf](../../../shaders/tools/DistanceFieldVoxelizeCS.sf) 每线程处理一个三角形：将顶点变到 voxel 坐标，在三角形包围盒内遍历体素，点到三角形平方距离<=0.75² 时写入表面标记。它是近似厚表面覆盖，不是精确 triangle-box SAT，也没有给出完整内部符号。

[DistanceFieldJumpFlood3DCS.sf](../../../shaders/tools/DistanceFieldJumpFlood3DCS.sf) 的 seed 保存 xyz 和有效位 w。初始化从 mask>0.5 得到 seed；每轮在 3×3×3 邻域、间隔 JumpStep 处比较候选 seed 到当前 voxel 的距离平方，写到另一张纹理；步长逐级缩小，最终 sqrt 得无符号距离。

```text
mask → seed A
step 大 → A 读 / B 写
step 减半 → B 读 / A 写
... → step=1 → 输出距离
```

若体素总数 V、最大边长 R，每轮约 27V 次候选，约 log₂R 轮，空间为 seed ping-pong 加输出纹理。它是近似最近 seed 传播，不是精确三角形最短距离；当前输出单位是 voxel，非自动世界单位，也未生成内外符号。

注意现有入口间的 mask 契约不同：voxelize 写 0 作为占据，JFA 初始化选择 >0.5，CPU BuildSurfaceMaskTexture 又使用 255 初值和图元编号。必须核对具体生产者/消费者及转换，不能把几个函数拼成一条已验证 GPU SDF 管线。

## 全局 Block 和合并

世界点先转 `(P-Origin)/BlockSize` 后 floor，得到有限网格 block 坐标；线性 ID=x+yNx+zNxNy。页表记录 Valid 和该 block 在 Atlas 内的 allocation offset，采样时将 block 内 UV 映射到 Atlas voxel 坐标再归一化。

GlobalDistanceField::Allocate 复用已存在的世界 block，否则分配 Atlas block 并更新页表；它不是虚拟内存 API，也没有由 ClipMap 名称自动提供无限范围、多层滚动和引用计数。中途分配失败与共享 block 释放的完整回滚/所有权仍需另查，不能根据结构名假设已有。

[DistanceFieldMergeCS.sf](../../../shaders/tools/DistanceFieldMergeCS.sf) 对输出 voxel 的世界中心遍历所有 source，乘 WorldToLocal 后在局部 Atlas 采样，再取 min，类似 SDF 并集。超出 source bounds 时使用“中心采样距离+到中心长度”的近似外推。非均匀缩放下局部距离并不等于世界欧氏距离，当前没有完整距离尺度修正，且输出复杂度约 O(输出体素×输出块×source 数)。

## 距离步进与全局 GI 的限制

标准 sphere tracing 用非负距离下界做步长、距离足够小判命中，同时约束最大路程/迭代数。当前 [GlobalDistanceFieldHelper.sf](../../../shaders/core/GlobalDistanceFieldHelper.sf) 最多 40 步，起点 0.1；无页返回 100000 后换成 BlockSize.x；条件是 **SDF>20 或 SDF<0 就返回**，不是标准 abs(SDF)<epsilon 命中。它不能被描述为已证明保守的 sphere tracer。

[GlobalDiffuseLightMap.cpp](../../../src/Framework/Renderer/Private/GI/GlobalDiffuseLightMap.cpp) 及 GI Shader 另有体素光照原型：按 mesh AABB 填体素，使用近似 normal/白 albedo，遮挡函数返回 false、环境函数返回程序化颜色；传播 Shader 按每体素 32 个方向再遍历所有体素，约 O(32V²)，不是已完成的距离场遮挡 GI。

本轮仅整理。应先用简单封闭盒/球验证距离和符号，再检查 Atlas 边界、尺度、初始化和上传，最后才验证全局步进与光照；不能用一张看似正常的场景图替代这些分层检查。
