# 网格导入、几何生成与切线空间

源码：[StaticMeshAsset.cpp](../../../src/Framework/Engine/Private/StaticMeshAsset.cpp)、[Shape.cpp](../../../src/Framework/Engine/Private/Shape.cpp)、[StaticMeshResources.h](../../../src/Framework/Engine/PublicHeader/StaticMeshResources.h)、[LocalVertexFactory.cpp](../../../src/Framework/Engine/Private/LocalVertexFactory.cpp)、[LocalVertexFactory.sf](../../../shaders/vertexfactory/LocalVertexFactory.sf)、[StaticMeshProcess.cpp](../../../src/Framework/Renderer/Private/StaticMeshProcess.cpp)。所有权见 [资产专题](../performance/05-assets-materials-and-instance-data.md)。

## glTF 字节寻址与当前支持范围

基本寻址为 `buffer.data + bufferView.byteOffset + accessor.byteOffset + i*stride`，componentType/type 决定解码方式，normalized 和 sparse 还有独立语义。

当前 GetBufferData/ReadVec* 及主读取分支主要按连续 float2/float3 直接 reinterpret_cast；索引支持 unsigned byte/short/int。它们没有完整处理交错 byteStride、稀疏 accessor、归一化整数属性等情况，不能仅因使用 tinygltf 就宣称整个 glTF 格式已被支持。

主路径把 model.meshes 遍历结果组织为 LODResource；这不是从 glTF 场景节点、层级变换和 LOD 扩展完整恢复场景。具体每 primitive 的顶点数组有 assign 覆盖，Section.FirstIndex 固定为 0、NumIndices 使用累积 size；多 primitive 组合的基顶点/section offset 仍有待完善，不能将单球模型成功推广为任意网格。

导入后会 InitializeResources，并同步构建局部距离场，因此 LoadMesh 不仅是文件解析。距离场 Atlas 和图形设备必须先初始化，见 [距离场专题](04-distance-fields-and-jump-flood.md)。

## 程序化网格

Shape.cpp 当前包含 Cube、Plane、Sphere、Cylinder 的构造。统一产物是 position、UV、normal、tangent 和三角索引数组，再构造 LOD/包围体及资源；不是在 Shader 中即时生成网格。

球面采样经纬：θ=lat·π/latSegments，φ=lon·2π/lonSegments，位置 `(sinθ cosφ, cosθ, sinθ sinφ)`。单位球法线可直接使用位置，UV=(lon/lonSegments,1-lat/latSegments)。每经纬单元拆成两个三角形，约 O(latSegments×lonSegments) 顶点/索引。

经度 seam 重复首尾点以允许 UV 断开，极点会生成重复位置及退化三角形；这是固定参数网格的边界，后续距离/切线计算必须考虑。segments=0 不能直接代入除法，当前调用依赖有效默认分段值。

cube 按面组织顶点才能同时表示硬法线和每面 UV，不能为了“去重位置”合并所有角点而丢失这些属性。圆柱侧面与端盖也需要分离法线和 UV；它们的细分增加顶点与三角数，不自动增加材质或资产对象数量。

## 切线空间到世界空间

法线贴图解码通常为 nTS=texture.rgb×2-1，按 T、B、N 基变换到目标空间后归一化。对标准切线约定，B=cross(N,T)×handedness，其中 handedness 通常为 ±1。UV 翻转、镜像和负缩放会影响该符号。

LocalVertexFactory.sf 当前用 LocalToWorld 的 3×3 变换 normal/tangent，再计算 bitangent。一般非均匀缩放下，正确法线方向应由 inverse-transpose 变换，不能直接从“normalize 了”推出与非均匀缩放兼容。

球体生成的 tangent.xyz=(-sinφ,0,cosφ)，但 tangent.w 当前写 0；而 Shader 某些分支把 w 当 handedness 乘到 bitangent。这个契约需要专项核查，不能在文档里擅自替源码改写成 ±1 或声称 TBN 始终正交有效。

## 包围体与实例绘制

CPU AABB 由 position 扩张得到；对象变换后用于场景查询的 bounds 必须是世界空间保守界，负缩放/旋转不能只变换 min/max 两点。SceneComponent/PrimitiveComponent 的更新路径与 proxy bounds 需要一起核查，算法判断的空间不能混用。

2026-10-26 的编辑器接入修正 StaticMeshProxy：此前只构造局部平移矩阵，现在从 const GetComponentTransform 取得完整世界矩阵，再求其逆；场景变换通知在 Flush 时替换代理缓存。矩阵/包围体应随编辑一起更新，但这不修复 Shader 中非均匀缩放法线仍未用逆转置的问题。

MeshBatch key 包含 VF、材质快照、索引资源、FrontFace 等状态，同一批次还按 section/element 等信息组合实例 ID。索引缓冲的 FirstIndex、VertexOffset/BaseVertex 和 StartInstance 各控制不同寻址阶段；不能用单个“offset”概念替代。

Shader 通过实例 ID 流查结构化参数 buffer，模型矩阵、材质快照和 draw element 必须对应同一实例序列。GPU 数据的更新与完成点另见 [实例数据工程实现](../performance/05-assets-materials-and-instance-data.md)。

验证应覆盖多 primitive、交错属性、不同索引宽度、UV seam、镜像法线、非均匀缩放和 section 材质；现有单模型 Launcher 目视验证不足以证明这些情况。以上未完成项均为源码整理结果，本轮未修改导入或几何生成实现。
