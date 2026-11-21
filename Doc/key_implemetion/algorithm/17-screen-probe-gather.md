# Screen Probe Gather 与一次反弹漫反射 GI

源码入口为 [ScreenProbeGather.h](../../../src/Framework/Renderer/Private/GI/ScreenProbeGather.h)、[ScreenProbeGather.cpp](../../../src/Framework/Renderer/Private/GI/ScreenProbeGather.cpp) 及三个 Compute Shader。Cache 数据语义见 [Surface Cache 算法](16-surface-cache-material.md)，持久缓存所有权及每帧 RDG 生命周期见 [Screen Probe 工程实现](../performance/13-screen-probe-gather.md)。此实现是固定 Tile、固定射线数的第一版，不等同于 UE Lumen 的完整 Screen Probe Gather。

## 坐标、输入与输出

输入为当前 View 的 GBuffer A/B/C、深度、Inverse View Projection、相机位置、Surface Cache Material/Direct Lighting 参数和可选 IBL 参数。屏幕 UV 原点在左上；重建时 `ndc.x=2u-1`、`ndc.y=1-2v`，深度直接使用 Renderer 的 `[0,1]` 投影深度，再通过 InvViewProj 做齐次除法。Probe、位置、射线方向和最大追踪距离均使用世界空间；本版的默认 Tile 为 8×8 屏幕像素、每 Probe 8 条射线、最长追踪距离 100 个世界单位。

最终 `IndirectDiffuse` 与 Debug 输出均为与视口等大的 RGBA32F RDG Texture。GI 输出存线性 RGB，Alpha 固定为 1。DebugMode 0 显示 Probe 法线及 Tile 边界，1 显示采样方向，2 显示有效辐射样本占比，3 显示 Probe Radiance 平均值，4 为默认/最终间接光结果。DebugMode 由 Viewer 的阶段选择映射到 Pass 输入；`EditorGIViewer` 提供各模式的选择入口。

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

该映射生成 cosine-weighted hemisphere 样本，方向 PDF 为 `p(ω)=max(dot(N,ω),0)/π`。hash 只改变方位角序列和每 Probe 的旋转，不做跨帧抖动、时域累积或随机数蓝噪声优化。每条有效射线的起点为 `P+0.03*N`，查询最大距离为 100。SurfaceCacheQuery 使用 Mode=1、InstanceID=`UINT32_MAX`；空 Probe 使用 Mode=2 和零距离，使已有 SurfaceCacheQuery Shader 不启动无效 SDF 追踪。

## SDF 命中、材质和出射辐射

`ScreenProbePlacementAndRayGeneration` 将 Query 写入 RDG StructuredBuffer。Surface Cache 的 GPU 输入重载把该 Buffer 直接交给既有 SDF Query Compute。它按实例局部 SDF 找最近命中，恢复 InstanceID、世界位置与几何法线，并用同一 Card Lookup 选择材质 Atlas 的实际 texel。随后 `AddSurfaceCacheLightingLookupPass` 依据该 Texel 取出缓存的直接出射 Radiance，启用时加上 [Radiosity](18-surface-cache-radiosity.md) 图集的间接出射 Radiance。全部查询和结果传递在 GPU 完成。

命中且材质与 Lighting 状态有效时，Probe Radiance 存入命中表面的直接出射 Radiance，加上启用时可用的 [Radiosity 间接出射 Radiance](18-surface-cache-radiosity.md)。命中但没有材质 Card/Lighting 覆盖时样本按黑色缺失处理。未命中时，从 IBL Specular Cube Map 的 mip 0 按射线方向采样，没有环境贴图时使用固定环境值 `(0.025,0.035,0.055)`。

## 屏幕空间重建与材质积分

对每个 Lit 屏幕像素，按像素相对 Tile 中心位置求周围四个 Probe 的双线性权重。另乘法线一致性和相机距离权重：法线点积低于 0.65 时拒绝；有效值在 0.65 至 0.9 用 smoothstep 淡入；距离容差为 `max(0.02, cameraDistance*0.025)`，权重为 `exp(-abs(pixelDistance-probeDistance)/tolerance)`。这是避免相邻深度层与物体边缘直接互相插值的启发式，不是严格的表面重投影或几何可见性证明。

对目标像素法线 `N_r` 和 Probe 法线 `N_p`，使用 Probe 的 cosine PDF 形成漫反射估计：

```text
E_r/π ≈ (1/n) * sum_i L_i(ω_i) * max(dot(N_r,ω_i),0) / max(dot(N_p,ω_i),1e-3)
GI_diffuse = BaseColor * (1-Metallic) * AO * (E_r/π)
```

`L_i` 是命中面的合计出射 Radiance 或未命中环境 Radiance。分母把 Probe 的 cosine 分布校正为目标表面的 cosine 权重。接收面 BaseColor/Metallic/AO 只乘一次。无效样本不加入分子，仍以固定 `RayCount` 作平均，因此命中无缓存导致的缺样会趋向变暗。

## GIViewer 中的 RenderGraph 顺序

`LumenViewerRenderer::AddPasses` 在 Viewer 自己的 Builder 中建立预览颜色、深度、深度金字塔和 GBuffer，并调用独立 `DefferedSceneRenderer` 生成本预览的场景缓冲。之后由该入口持有/注册 Surface Cache Material 与 Direct Lighting 缓存，并按选择追加对应调试 Pass 或 `AddScreenProbeGatherPasses`。Indirect Diffuse 模式将探针积分写入 Viewer 的预览颜色目标；其他 Probe 模式将选择的调试视图写入同一目标。该 Builder 不与 `RenderModule` 的帧 Builder 共享，也不将探针结果合成进主窗口场景颜色。

Pass 之间只传 RDG Texture/Buffer 引用。RDG 元数据声明 GBuffer/Atlas/查询结果 SRV、Probe/Query/Radiance Buffer UAV 与输出 Texture UAV；资源依赖和状态转换由现有 RenderGraph 编排。本实现没有修改 RenderGraphBuilder、RHI 访问意图或资源生命周期接口。ScreenProbe Gather 的可视化与输入附件来自 Viewer 的预览图，不等同于常规主渲染流程中的既有 ScreenSpaceGI/SSR。

## 复杂度与边界

设屏幕像素数为 `P=W*H`，Probe 数约 `P/T²`，每 Probe 射线数为 `R`。SDF 查询与 Lighting Lookup 处理 `O(P*R/T²)` 条射线；Radiance gather 处理相同数量，Integration 对每像素最多访问 4 个 Probe、每 Probe 重新遍历 R 条射线，复杂度近似 `O(P*R)`。以 1920×1080、T=8、R=8 估算，约 32,400 个 Probe、259,200 条射线；Structured Buffer 和两张 RGBA32F 输出的主体空间约 120 MiB/帧，未计 GBuffer、Cache 纹理、RDG 对齐和驱动额外分配。因此此配置主要验证数据流，性能不代表可投产默认值。

Probe 本身每帧重建。跨帧光照传播由 Surface Cache Radiosity 完成。GBuffer 法线和距离约束可能漏掉薄物体或突变的深度层，SDF 精度、Card 覆盖和 Lighting Cache 的限制继续适用。场景变更与显式重建的时序见 [工程专题](../performance/13-screen-probe-gather.md#radiosity-迭代与缓存失效)。
