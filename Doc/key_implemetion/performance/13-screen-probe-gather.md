# Screen Probe Gather 的资源所有权与执行时序

Screen Probe 的算法与估计公式见 [算法专题](../algorithm/17-screen-probe-gather.md)。本文件说明目前仅由 Lumen GIViewer 启动的构图入口、缓存所有权、单次预览图的 RDG 生命周期与理论内存负载。Screen Probe 不属于 `RenderModule` 的常规主场景流程。

## 调用链与 Cache 所有权

Editor 的 View ribbon 按钮打开 `EditorGIViewer`。Viewer 每次绘制时从宿主回调借用当前 `SceneInterface` 和 `SceneView`，由 `GIViewerPreview::Render` 创建新的 `RenderGraphBuilder`，调用 `LumenViewerRenderer::AddPasses` 并执行该 Builder。预览有独立的持久颜色目标、深度、深度金字塔和 GBuffer；输入 Scene/View 仅在当前 UI 绘制和构图中借用，不转移所有权。

`LumenViewerRendererState` 持有 `SurfaceCacheMaterial`、`SurfaceCacheDirectLighting` 和当前 `Scene*`。首次在非空场景打开 Viewer 时，入口收集静态网格实例快照、初始化两种 Cache，并在本帧图中加入 Material Capture。需要直接光照或 Probe 模式时加入/更新 Lighting Cache。之后同一场景的预览图用 `Register` 绑定持久 Cache，并按 Viewer 所选模式添加对应的 Cache 调试或 Screen Probe Pass。图内 Probe、Query、Radiance 等结构化缓冲和预览用 GBuffer/depth 均由该次 Builder 创建，不跨 Builder 保存；Cache 的 Atlas 与 Preview 颜色目标由相应 Renderer/Preview 对象持有。

同一 Scene 的内容变更不会自动让缓存失效。用户可按 `Rebuild Surface Cache` 先重置，再由后续预览图重新捕获；切换到不同 Scene 时入口重置旧 Cache。关闭 Viewer、关闭工程或调整预览尺寸也会释放/替换预览颜色目标。释放前沿用资源访问 Fence：Preview 等待其颜色目标的最后访问，Renderer Reset 再释放 Cache；不使用 `WaitIdle` 或全局保活列表。关闭或切换时序若有新的跨队列访问，应继续依据真实最后访问 Fence 审核，不能仅凭输出画面已提交推断全部资源已退休。

## 单图 RDG Pass 与同步

`LumenViewerRenderer::AddPasses` 每次为当前输出尺寸创建场景深度、深度金字塔及 GBuffer，再通过独立 `DefferedSceneRenderer` 构建预览场景颜色和几何。随后按 Viewer 模式安排 Surface Cache 捕获/注册、Lighting 更新/注册以及调试 Pass 或 `AddScreenProbeGatherPasses`。Screen Probe 阶段的 Pass 顺序为：

1. Placement/Ray Generation 写 Probe 与 SurfaceCacheQuery。
2. Surface Cache Query Compute 读取 Query、写命中结果。
3. Direct Lighting Lookup 读取材质命中与 Lighting Atlas，写 Lighting 查询结果。
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

固定 Probe 不做跨帧历史、去噪、重要性采样、自适应放置、反射/屏幕命中补充或多次反弹；GBuffer 法线/距离约束可能漏掉薄物体和急剧深度变化，也可能保留局部漏光。SDF 精度、Card 覆盖和 Lighting Cache 的限制继续适用。Viewer 关闭后不自动保留缓存；同一场景发生编辑后必须显式重建，尚无 Scene 变更通知或增量捕获。

`cmake --build out/build/screen-probe-vs --target Renderer GIViewer Editor Langcher --config Debug` 编译通过。使用该隔离目录启动 `Langcher.exe -app=Editor` 后，日志出现 `[Editor] First scene and GUI frame submitted`，进程仍在运行；同时 Vulkan validation 报告 `StorageImageReadWithoutFormat` 和重复 `OpTypeImage` 错误，并记录 GlobalDiffuseLightMap、ScreenSpaceReflectionDepthPyramid、HZBOcclusionTest Shader 编译失败。这些发生在空场景启动路径，不能当作 GIViewer 非空场景验证，也尚未确认与新 Lumen Pass 有关。未在 Viewer 中加载非空场景、打开各模式或进行画面/读回核验，因此 SDF Query、Cache Lookup、Probe Integration 及颜色反弹仍待验证。相关缓存生命周期和功能验收建议见 [优化项](../../dev/optimize/优化点-2026-11-17.md#sc-opt-08-screen-probe-缓存生命周期和实际场景验证)。
