# Virtual Geometry Page 组织与无损压缩

源码是 Engine 的 [VirtualGeometryPages.h](../../../src/Framework/Engine/PublicHeader/VirtualGeometryPages.h) 和 [cpp](../../../src/Framework/Engine/Private/VirtualGeometryPages.cpp)。它只处理 CPU 几何和文件，不调用 RHI、不创建线程。GPU 驻留、请求和资源完成点见 [流送工程实现](../performance/11-virtual-geometry-streaming.md)，原始 Cluster/DAG 输入见 [CPU Build](10-virtual-geometry-cpu-build.md)。

## 输入与输出

Build 接收已经构建的 VirtualGeometryResource 和明确的输出路径，创建独立 `.vgpages` 文件；返回 bool 和错误文本。Open 恢复目录，ReadPage 按页定位并独立解码，Matches 核对文件来源是否对应当前 CPU 几何。失败不将部分载荷交给 Renderer，文件写入失败也不能当成有效归档继续流送。该格式是可重新构建的派生缓存，不替代工程 `.geoms`，不保存 GPU 句柄。

原始 Cluster 保留 VertexOffset/IndexOffset 的逻辑含义及 Group、材质、Mip、Bounds。PageLocation 为每个 Cluster 保存 PageID、页内 VertexOffset/IndexOffset；索引值仍是 Cluster Local Index。Page 只决定存储/加载批次，不定义新的父子关系，不能按 Page 替换几何。

## 分页策略与边界

1. 找出每个终端 Root Group 的全部 Child，组成完整最粗 Cut。这些 Cluster 的页标记 bPinned，包含停滞叶和多个根。
2. 稳定排序：常驻粗 Cut 优先，其余按 MipLevel 降序；同层保留原 Cluster 顺序，因此仍继承 Build 的拓扑分簇局部性。
3. 在容量约束下贪心打包完整 Cluster，不拆面或顶点。每页最多 768 个独立顶点、1536 个局部索引。Pinned 与非 Pinned 绝不混装，避免把不必要的细层固定常驻。
4. 将各 Cluster 的完整属性串接为页内顶点数组，索引段另外串接；PageLocation 保存各自起点。不同 Cluster 的边界顶点可以重复，重复位置按原 float 位模式保存。

一个最大 128 面、最多 384 个局部顶点的 Cluster 必须能完整装入页。跨材质、UV Seam、硬法线等处理仍属于原 Build，压缩不焊接、不简化、不改材质槽，也不会重新量化共享边界。因此没有为压缩再引入几何误差或破坏原 Group 外边界的浮点重建。

## 文件布局

文件按显式小端整数写入，不直接 fwrite 含 vector、bool 或结构体填充的对象：

| 部分 | 字节布局 |
| --- | --- |
| Header，40 字节 | Magic `RVGP`、Version=1、PageCount、ClusterCount，各 uint32；RawBytes、SourceHash，各 uint64；DirectoryChecksum 和保留字，各 uint32 |
| Page 目录，每页 40 字节 | FileOffset uint64；StoredBytes、VertexCount、IndexCount、Codec、Checksum、Pinned、两个保留字，各 uint32 |
| Cluster 目录，每项 12 字节 | PageID、页内 VertexOffset、页内 IndexOffset，各 uint32 |
| Payload | 每页独立的 Raw 或 XOR/Varint 字节流 |

SourceHash 使用全部属性、索引、Cluster 前八个逻辑字段、Group Child/Parent 引用和 Root 索引的 FNV-1a 64 位摘要；当前实现基于项目主机上的基础类型字节，不宣称可跨不同端序/ABI 直接复用来源指纹。它用于拒绝同 Cluster 数但内容不同的缓存，不是安全认证。Page Checksum 使用解码 word 序列的 FNV-1a 32 位摘要，按每个 word 的低位字节到高位字节计算。

Open 检查 Magic/Version、非零计数、目录是否超文件范围、容量、Codec、Pinned 值及每页载荷范围，再核对目录/Cluster 位置表的 FNV-1a 校验和及总 RawBytes。这样页内偏移或 Pinned 标志的损坏不能仅因仍在范围内就被接受。ReadPage 再检查实际读取、Varint 终止/溢出、解码元素数和校验和。没有以校验失败的部分属性更新驻留表。Build/ReadPage 的错误由调用方显式处理，类内没有 try/catch 或静默返回假数据。

## XOR 预测与 Varint

每顶点依次是 Position.xyz、Normal.xyz、Tangent.xyzw、UV.xy、Color.xyzw，16 个 float32，共 64 字节。用 bit_cast 转为 uint32，逐顶点写入 word 数组，最后追加所有局部 uint32 索引。

编码器维护 16 个前值 predictor。第 i 个 word 使用通道 i mod 16：

`delta = currentBits XOR predictor[channel]; predictor[channel] = currentBits`。

将 delta 每次取低 7 位写入字节，高位 1 表示后续还有数据；最后一字节高位 0。解码重组 delta 后再次 XOR predictor，得到完全相同的 currentBits。它保留负零、切线 W、UV Seam 及颜色位模式，不使用可能让共享边界错位的独立页量化。索引段继续使用这 16 个预测槽；这不要求索引具有浮点语义，只是一种无损整数预测。

若编码结果不小于原 word 数×4，则 Codec=0，存原始小端 uint32 数据；因此 Page Payload 不因不利输入膨胀。目录与文件头仍有固定开销，StoredBytes 统计只包含载荷，不能称作整个文件字节数或 GPU Cache 占用。

## 解码与 GPU 地址映射

CPU worker 调 ReadPage，将位模式还原为 Core::Float3/Float4/Float2 数组及局部索引。Renderer 不需要压缩 VertexFactory 或 GPU 解码 Shader，继续使用现有传统顶点输入、软件 Raster 和 Material Resolve。

若 PageID 目前占 slot，某 Cluster 的物理地址为：

```text
physicalVertex = slot × 768 + PageLocation.VertexOffset
physicalIndex  = slot × 1536 + PageLocation.IndexOffset
triangleCorner = Positions[physicalVertex + Indices[physicalIndex + PrimitiveID×3 + corner]]
```

只有 GPU ClusterBuffer 的前两个相关 Offset 字段变成物理地址，CPU Clusters 保持构建时逻辑地址。原 IndirectArguments、软件 Raster 和 Resolve 都读取 GPU ClusterBuffer，因此不需要另一套 TriangleID 协议。硬件 VertexFactory 仍绑定完整物理位置池，BaseVertex 使用物理偏移。

## 成本与验证

分页排序 O(C log C)，打包及编码/解码 O(V+I)，编码只需要当前页的临时数据。目录常驻 CPU；实际异步读取只取所请求页的载荷。CPU 原始 Build 数组目前仍保留用于编辑、诊断和重建，尚未实现 CPU 几何淘汰、离线资产构建管线、GPU 压缩常驻或量化/熵编码。

[VirtualGeometryStreamingTest](../../../src/Tests/VirtualGeometryStreamingTest.cpp) 在 1152 面的真实 Build 上逐页重开文件，逐字节核对全部属性和局部索引，并验证压缩回退不膨胀；损坏载荷由异步读取路径检出，粗 Cut 仍可绘制。命令为 `Tests.exe -test=VirtualGeometryStreamingTest -rhi=Vulkan`，可换 DirectX12/OpenGL。实测压缩量、显存槽占用及测试限制见 [开发记录](../../dev/bugfix/故障与修复记录-2026-11-15.md#virtual-geometry-geometry-streaming--compression)，不能由一种平面输入推断任意模型压缩率。
