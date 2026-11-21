# Screen Probe Gather 的资源所有权与执行时序

Screen Probe 的算法与估计公式见 [算法专题](../algorithm/17-screen-probe-gather.md)。本文件说明目前仅由 Lumen GIViewer 启动的构图入口、缓存所有权、单次预览图的 RDG 生命周期与理论内存负载。Screen Probe 不属于 `RenderModule` 的常规主场景流程。

## 调用链与 Cache 所有权

Editor 的 View ribbon 按钮打开 `EditorGIViewer`。Viewer 每次绘制时从宿主回调借用当前 `SceneInterface` 和 `SceneView`，由 `GIViewerPreview::Render` 创建新的 `RenderGraphBuilder`，调用 `LumenViewerRenderer::AddPasses` 并执行该 Builder。预览有独立的持久颜色目标、深度、深度金字塔和 GBuffer；输入 Scene/View 仅在当前 UI 绘制和构图中借用，不转移所有权。

`LumenViewerRendererState` 持有材质、直接光照、间接光照缓存和当前 `Scene*`。首次在非空场景选择 Cache 模式时，入口收集静态网格实例快照并捕获材质。需要光照时建立或更新直接图集。之后预览图用 `Register` 绑定持久 Cache，并按 Viewer 所选模式添加对应 Pass。图内 Probe、Query、Radiance 等结构化缓冲和预览用 GBuffer/depth 均由当前 Builder 创建，缓存的 Atlas 与预览颜色目标分别由 Renderer 和 Preview 持有。

切换场景、关闭 Viewer 或工程时，Renderer Reset 沿用预览颜色目标最后访问的 Fence 后释放缓存。调整预览尺寸时，Preview 等待后只替换颜色目标。输出完成点代表与之存在图依赖的访问，新增异步路径时需重新核对完成点。

## Radiosity 迭代与缓存失效

[LumenViewerRenderer](../../../src/Framework/Renderer/Private/GI/LumenViewerRenderer.cpp) 在需要 Surface Cache 的模式下，以 InstanceID 对当前可见静态网格排序，对比标识、变换、RenderData 与不可变材质代理。任何一项变化都会退休旧图集并全量重捕获。需要光照时对比场景灯光快照，重新计算直接图集并将间接迭代数归零。重置按钮可单独清除间接历史，重建按钮可显式重捕获。相机移动与预览尺寸变化不改变世界空间 Card 的采样。

每个 Viewer 独占 [SurfaceCacheRadiosity](../../../src/Framework/Renderer/Private/GI/SurfaceCacheRadiosity.h) 的两张间接图集。首次迭代分配图集。每帧最多安排一次迭代，读上一轮的外部 SRV 并写另一张外部 UAV，根据当前迭代数公布写入的图集。第零轮返回直接光照。失效只清零迭代数，第一次重新写入完整图集，旧内容不会被采样。预览图在控制线程串行构建和提交，图集替换前沿用上一输出纹理的最后访问 Fence。材质采样、SDF 查询、积分和 Viewer 输出通过 RDG 传递依赖，覆盖该阶段图集的使用。旧灯光只触发重新构图，现有 RDG 跟踪持久图集写入的前序访问。

每迭代依次安排 `SurfaceCacheRadiosityRays`、`SurfaceCacheSDFQuery` 和 `SurfaceCacheRadiosityIntegrate`。Query 和 Hit 缓冲由当前 RenderGraph 负责。已有 GPU Query 结果缓存以输入缓冲名构造图内名称，使 Radiosity 与 Screen Probe 可各自追踪。之后根据模式追加 `SurfaceCacheRadiosityDebug` 或 Screen Probe 现有的 Lookup/Gather/Integration。连续两轮各自占据一张纹理，Delta 直接比较两者。黑色有效单元保留 alpha=1，空白处 alpha=0。

以 Card 数 C 计，持久图集共有 `2 × ceil(sqrt(C))² × 64 × 16` 字节。每轮查询与结果的逻辑空间为 `512 × C × (48 + 80)` 字节，实际分配还包含 RDG/RHI 对齐。默认最多自动执行 32 轮，可暂停或手动追加单轮。算法和近似边界见 [多次漫反射](../algorithm/18-surface-cache-radiosity.md)。

## 单图 RDG Pass 与同步

`LumenViewerRenderer::AddPasses` 每次为当前输出尺寸创建场景深度、深度金字塔及 GBuffer，再通过独立 `DefferedSceneRenderer` 构建预览场景颜色和几何。随后按 Viewer 模式安排 Surface Cache 捕获/注册、Lighting 更新/注册以及调试 Pass 或 `AddScreenProbeGatherPasses`。Screen Probe 阶段的 Pass 顺序为：

1. Placement/Ray Generation 写 Probe 与 SurfaceCacheQuery。
2. Surface Cache Query Compute 读取 Query、写命中结果。
3. Lighting Lookup 读取材质命中与直接光照 Atlas，加上启用时最新的间接图集，再写出射光查询结果。
4. Gather 读取 Query、Material Result、Lighting Result，写 Probe Radiance。
5. Integration 读取 Probe/Radiance/GBuffer，写 Viewer 目标或调试目标。

`GIViewerPreview::Render` 在所有 Pass 添加后执行该图。StructuredBuffer 与临时纹理通过 RDG 参数元数据声明 SRV/UAV/RenderTarget 访问，由 RenderGraph 安排依赖与状态转换；没有新增专用队列、Builder 生命周期扩展或 RHI 访问接口。Screen Probe 的 Integration 结果只写 Viewer 的预览颜色，不合入主窗口的 SceneColor。

## 分辨率对应的理论负载

以 1920×1080、8×8 Tile、8 Rays 估算：Probe 数为 32,400，Ray Query 数为 259,200。按 C++ Shader Parameter 布局估算：

| 资源 | 单元素大小 | 总量约值 |
|---|---:|---:|
| Probe Record | 64 B | 1.98 MiB |
| SurfaceCacheQuery | 48 B | 11.87 MiB |
| SurfaceCacheQueryResult | 80 B | 19.78 MiB |
| Lighting Query Result | 48 B | 11.87 MiB |
| Radiance Record | 32 B | 7.91 MiB |
| 两张 1920×1080 RGBA32F 输出 | 16 B/像素 | 63.28 MiB |
| 合计 | — | 约 116.7 MiB |

表内是逻辑 Buffer 负载，不包含 Viewer 的 GBuffer/深度、持久 Surface Cache/Lighting Atlas、RenderGraph/RHI 对齐和驱动额外分配。GPU 阶段结果可能在各自最后消费者完成前并存；实际峰值需要 GPU 资源统计测量。当前没有预算控制或分辨率自适应。打开 Viewer 时还会建立一套独立场景缓冲，因此该数值不是 Viewer 的全部显存占用。

## 限制与验证范围

Probe 每帧重建，跨帧迭代历史保存在 Surface Cache。当前使用固定探针间隔和固定样本数。GBuffer 法线与距离约束对薄物体和急剧深度变化的过滤存在局限，原有 SDF 精度和 Card 覆盖限制继续适用。Viewer 关闭时释放缓存。快照尚未跟踪 RenderData 对象内部原地修改的几何或 SDF，遇到这类变化需显式重建。

四色墙 GPU 图集与查询读回、`out/pro2` 的 Viewer 图形界面验证以及已知启动诊断见 [2026-11-20 开发记录](../../dev/bugfix/故障与修复记录-2026-11-20.md#surface-cache-radiosity-开发与验证)。帧耗时、显存峰值及更复杂输入仍待测量和验证。
