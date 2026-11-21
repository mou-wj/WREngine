# Screen Probe Gather 与一次反弹漫反射 GI

源码入口为 [ScreenProbeGather.h](../../../src/Framework/Renderer/Private/GI/ScreenProbeGather.h)、[ScreenProbeGather.cpp](../../../src/Framework/Renderer/Private/GI/ScreenProbeGather.cpp) 及三个 Compute Shader。Cache 数据语义见 [Surface Cache 算法](16-surface-cache-material.md)，持久缓存所有权及每帧 RDG 生命周期见 [Screen Probe 工程实现](../performance/13-screen-probe-gather.md)。此实现是固定 Tile、固定射线数的第一版，不等同于 UE Lumen 的完整 Screen Probe Gather。

## 坐标、输入与输出

输入为当前 View 的 GBuffer A/B/C、深度、Inverse View Projection、相机位置、Surface Cache Material/Direct Lighting 参数和可选 IBL 参数。屏幕 UV 原点在左上；重建时 `ndc.x=2u-1`、`ndc.y=1-2v`，深度直接使用 Renderer 的 `[0,1]` 投影深度，再通过 InvViewProj 做齐次除法。Probe、位置、射线方向和最大追踪距离均使用世界空间；本版的默认 Tile 为 8×8 屏幕像素、每 Probe 8 条射线、最长追踪距离 100 个世界单位。

`IndirectDiffuse` 是与视口同尺寸的 RGBA32F RDG Texture，存线性漫反射间接光 RGB。调试输出启用时保留同尺寸纹理。普通探针调试的 GI Alpha 为 1，滤波模式用 Alpha 传递重建置信度。`DebugMode` 0 显示 Probe 法线及 Tile 边界，1 显示采样方向，2 显示有效辐射样本占比，3 显示 Probe Radiance 平均值，4 显示未滤波的重建结果，5 显示世界缓存命中。

## Probe 放置与半球射线

对宽高 `(W,H)` 和 Tile `T`，Probe 网格为 `ceil(W/T) × ceil(H/T)`。Probe `(x,y)` 选取像素 `min((x*T+T/2,y*T+T/2),(W-1,H-1))`。当深度小于 `0.999999` 且 GBuffer ShadingModelID 为 Lit(1) 时，Probe 有效；世界位置由像素中心和深度反投影，世界法线由 GBufferB.rgb 解码 `N=2*rgb-1` 后归一化。

每 Probe 构造与 N 正交的 `(t,b,N)` 基。对射线索引 `i` 与总数 `n`：

```text
u = (i + 0.5) / n
r = sqrt(u)
z = sqrt(1-u)
phi = 2*pi*frac(hash(probe,i) + rotation(probe))
localDir = (r*cos(phi), r*sin(phi), z)
worldDir = normalize(t*localDir.x + b*localDir.y + N*localDir.z)
```

该映射生成 cosine-weighted hemisphere 样本，方向 PDF 为 `p(ω)=max(dot(N,ω),0)/π`。普通调试模式的相位固定，滤波模式为每帧增加 `frac(SamplePhase × 0.61803398875)` 的方位偏移，使同一表面跨帧接收不同的方向估计。每条有效射线的起点为 `P+0.03*N`，查询最大距离为 100。SurfaceCacheQuery 使用 Mode=1、InstanceID=`UINT32_MAX`。空 Probe 使用 Mode=2 和零距离，使已有 SurfaceCacheQuery Shader 跳过无效 SDF 追踪。

## SDF 命中、材质和出射辐射

`ScreenProbePlacementAndRayGeneration` 将 Query 写入 RDG StructuredBuffer。Surface Cache 的 GPU 输入重载把该 Buffer 直接交给既有 SDF Query Compute。它按实例局部 SDF 找最近命中，恢复 InstanceID、世界位置与几何法线，并用同一 Card Lookup 选择材质 Atlas 的实际 texel。随后 `AddSurfaceCacheLightingLookupPass` 依据该 Texel 取出缓存的直接出射 Radiance，启用时加上 [Radiosity](18-surface-cache-radiosity.md) 图集的间接出射 Radiance。全部查询和结果传递在 GPU 完成。

启用世界探针时，在 SDF Pass 前对较长的射线尝试缓存查询。找到可靠远场候选后，只执行近场 SDF 区段。近场命中和无缓存时仍沿用 Surface Cache 路径。 [World Space Radiance Cache](19-world-radiance-cache.md)

命中且材质与 Lighting 状态有效时，Probe Radiance 存入命中表面的直接出射 Radiance，加上启用时可用的 [Radiosity 间接出射 Radiance](18-surface-cache-radiosity.md)。命中但没有材质 Card/Lighting 覆盖时样本按黑色缺失处理。未命中时，从 IBL Specular Cube Map 的 mip 0 按射线方向采样，没有环境贴图时使用固定环境值 `(0.025,0.035,0.055)`。

## 屏幕空间重建与材质积分

对每个 Lit 屏幕像素，用周围四个 Probe 实际采样的像素位置计算双线性权重，屏幕边缘避免重复取同一 Probe。`ProbeWeight` 根据点积 `dot(N,Nprobe)` 在 0.75 至 0.95 间淡入。两点之间的双向平面距离以 `max(0.005, 1.25 * footprint)` 为容差，超过 `1.75 * TileSize * footprint` 的世界距离也被拒绝。`footprint` 为同深度偏移一个像素的世界空间距离估计。四点无有效方向时，检查周围 3×3 Tile，使用空间与几何权重最高的有效 Probe，置信度乘 `0.6`。无相容 Probe 时保持零贡献。这些筛选减少深度断层和坡面间的混合，其精度受稀疏探针覆盖与几何分辨率限制。 [重建 Shader](../../../shaders/GI/ScreenProbeIntegration.sf)

对目标像素法线 `N_r` 和 Probe 法线 `N_p`，使用 Probe 的 cosine PDF 形成漫反射估计：

```text
E_r/π ≈ (1/n) * sum_i L_i(ω_i) * max(dot(N_r,ω_i),0) / max(dot(N_p,ω_i),1e-3)
GI_diffuse = BaseColor * (1-Metallic) * AO * (E_r/π)
```

`L_i` 是命中面的合计出射 Radiance、世界探针的方向样本或未命中环境 Radiance。分母把 Probe 的 cosine 分布校正为目标表面的 cosine 权重。接收面 BaseColor/Metallic/AO 只乘一次。无效样本不加入分子，仍以固定 `RayCount` 作平均，因此命中无缓存导致的缺样会趋向变暗。

## GIViewer 中的 RenderGraph 顺序

`LumenViewerRenderer::AddPasses` 在 Viewer 自己的 Builder 中建立预览颜色、深度、深度金字塔和 GBuffer，并调用独立 `DefferedSceneRenderer` 生成本预览的场景缓冲。入口持有并注册材质、光照和世界缓存，按显示模式追加调试 Pass 或 `AddScreenProbeGatherPasses`。GI 模式先输出原始屏幕间接光，再由 `ScreenProbeTemporal` 和可选的 `ScreenProbeSpatial` 写 Viewer 预览颜色。其他 Probe 模式直接写调试结果。 [GI 时空滤波](20-gi-temporal-spatial-filter.md) · [RDG 时序](../performance/13-screen-probe-gather.md#单图-rdg-pass-与同步)

Pass 之间只传 RDG Texture/Buffer 引用。RDG 元数据声明 GBuffer/Atlas/查询结果 SRV、Probe/Query/Radiance Buffer UAV 与输出 Texture UAV；资源依赖和状态转换由现有 RenderGraph 编排。本实现没有修改 RenderGraphBuilder、RHI 访问意图或资源生命周期接口。ScreenProbe Gather 的可视化与输入附件来自 Viewer 的预览图，不等同于常规主渲染流程中的既有 ScreenSpaceGI/SSR。

## 复杂度与边界

设屏幕像素数为 `P=W*H`，Probe 数约 `P/T²`，每 Probe 射线数为 `R`。SDF 查询、Lighting Lookup、Radiance Gather 处理 `O(P*R/T²)` 条射线。Integration 一般对每像素访问 4 个 Probe，必要时额外检查 3×3 邻域，对候选遍历 R 条射线，复杂度为 `O(P*R)`。以 1920×1080、T=8、R=8 估算，约 32,400 个 Probe、259,200 条射线。临时资源和持久历史的估算见 [工程专题](../performance/13-screen-probe-gather.md#分辨率对应的理论负载)。

Probe 每帧重建。跨帧光照传播由 Surface Cache Radiosity 完成，屏幕历史只用于滤除重建后的噪声。GBuffer 法线和距离约束可能漏掉薄物体或突变的深度层，SDF 精度、Card 覆盖和 Lighting Cache 的限制继续适用。 [缓存失效时序](../performance/13-screen-probe-gather.md#屏幕历史与失效)
