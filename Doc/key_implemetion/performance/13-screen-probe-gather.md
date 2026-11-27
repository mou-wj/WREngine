# Screen Probe Gather 的资源所有权与执行时序

Screen Probe 的算法与估计公式见 [算法专题](../algorithm/17-screen-probe-gather.md)。本文件说明目前仅由 Lumen GIViewer 启动的构图入口、缓存所有权、单次预览图的 RDG 生命周期与理论内存负载。Screen Probe 不属于 `RenderModule` 的常规主场景流程。

## 调用链与 Cache 所有权

Editor 的 View ribbon 按钮打开 `EditorGIViewer`。Viewer 每次绘制时从宿主回调借用当前 `SceneInterface` 和 `SceneView`，由 `GIViewerPreview::Render` 创建新的 `RenderGraphBuilder`，调用 `GIViewerRenderer::AddPasses` 并执行该 Builder。预览有独立的持久颜色目标、深度、深度金字塔和 GBuffer；输入 Scene/View 仅在当前 UI 绘制和构图中借用，不转移所有权。

`GIViewerRendererState` 持有材质、直接光照、间接光照、世界探针缓存、屏幕历史和当前 `Scene*`。首次在非空场景选择 Cache 模式时，入口收集静态网格实例快照并捕获材质。需要光照时建立或更新直接图集。之后预览图用 `Register` 绑定持久 Cache，并按 Viewer 所选模式添加对应 Pass。图内 Probe、Query、Radiance 等结构化缓冲和预览用 GBuffer/depth 均由当前 Builder 创建，缓存的 Atlas 与预览颜色目标分别由 Renderer 和 Preview 持有。

切换场景、关闭 Viewer 或工程时，Renderer Reset 沿用预览颜色目标最后访问的 Fence 后释放缓存。调整预览尺寸时，Preview 等待后只替换颜色目标。输出完成点代表与之存在图依赖的访问，新增异步路径时需重新核对完成点。

## Radiosity 迭代与缓存失效

[GIViewerRenderer](../../../src/Framework/Renderer/Private/GI/GIViewerRenderer.cpp) 在需要 Surface Cache 的模式下，以 InstanceID 对当前可见静态网格排序，对比标识、变换、RenderData 与不可变材质代理。任何一项变化都会退休旧图集并全量重捕获。需要光照时对比场景灯光快照，重新计算直接图集并将间接迭代数归零。重置按钮可单独清除间接历史，重建按钮可显式重捕获。相机移动与预览尺寸变化不改变世界空间 Card 的采样。

每个 Viewer 独占 [SurfaceCacheRadiosity](../../../src/Framework/Renderer/Private/GI/SurfaceCacheRadiosity.h) 的两张间接图集。首次迭代分配图集。每帧最多安排一次迭代，读上一轮的外部 SRV 并写另一张外部 UAV，根据当前迭代数公布写入的图集。第零轮返回直接光照。失效只清零迭代数，第一次重新写入完整图集，旧内容不会被采样。预览图在控制线程串行构建和提交，图集替换前沿用上一输出纹理的最后访问 Fence。材质采样、SDF 查询、积分和 Viewer 输出通过 RDG 传递依赖，覆盖该阶段图集的使用。旧灯光只触发重新构图，现有 RDG 跟踪持久图集写入的前序访问。

每迭代依次安排 `SurfaceCacheRadiosityRays`、`SurfaceCacheSDFQuery` 和 `SurfaceCacheRadiosityIntegrate`。Query 和 Hit 缓冲由当前 RenderGraph 负责。已有 GPU Query 结果缓存以输入缓冲名构造图内名称，使 Radiosity 与 Screen Probe 可各自追踪。之后根据模式追加 `SurfaceCacheRadiosityDebug` 或 Screen Probe 现有的 Lookup/Gather/Integration。连续两轮各自占据一张纹理，Delta 直接比较两者。黑色有效单元保留 alpha=1，空白处 alpha=0。

以 Card 数 C 计，持久图集共有 `2 × ceil(sqrt(C))² × 64 × 16` 字节。每轮查询与结果的逻辑空间为 `512 × C × (48 + 80)` 字节，实际分配还包含 RDG/RHI 对齐。默认最多自动执行 32 轮，可暂停或手动追加单轮。算法和近似边界见 [多次漫反射](../algorithm/18-surface-cache-radiosity.md)。

## 世界探针缓存

`WorldRadianceCache` 持有两级环绕索引的 432 个 CPU 槽键和最后更新帧，以及一张 36×432 的 RGBA32F 持久图集。每次进入使用缓存的预览帧，先依据世界坐标校验槽身份。新槽在方向样本写入前保持不可见，已有槽优先按最后刷新时间排序，未更新槽按距相机的距离排序。默认预算为每帧 24 个节点，节点至少相隔 32 个缓存更新帧才会周期刷新。预算设成 0 时只复用有效的重叠格点。 [缓存实现](../../../src/Framework/Renderer/Private/GI/WorldRadianceCache.cpp)

控制线程生成本次节点状态与任务列表，上传到当前图的结构化缓冲。依次运行 `WorldRadianceCacheRays`、`SurfaceCacheSDFQuery`、`SurfaceCacheLightingLookup`、`WorldRadianceCacheStore`，光照查询结果用各自的输入名区分。状态里的有效位表示方向行已安排写入，当前图的 Atlas UAV→SRV 依赖保证消费者排在写入之后。未初始化槽的旧行不会被查询。上一图的资源状态和完成点沿用持久纹理的访问 Tracker。图集 Store 完成后，世界图集可视化和屏幕探针查表才读取数据。

当前图的 Radiosity 先于依赖该图集的 Surface Cache 出射光查询。世界探针不作为 Radiosity 的输入。场景变更重建时退休旧资源，灯光、环境贴图、Radiosity 开关或重启变化时清除有效槽并重新分帧填充。常规反弹迭代通过周期刷新渐进传播。GIViewer 关闭时按预览输出的完成点退休资源。

持久 GPU 图集逻辑占用约 243 KiB。状态上传每帧约 13.5 KiB，预算为 `B` 时刷新 `36 × B` 条世界射线。启用缓存的屏幕路径额外存放逐射线的范围修改副本（48 字节）与候选（16 字节）。 [算法说明](../algorithm/19-world-radiance-cache.md)

## 体素光照缓存

`GIViewerRenderer` 按需持有 `VoxelLighting`，后者分配一张持久 `RGBA32F` 3D 纹理。逻辑空间为 `6 × 6 × 6 × 12 × 16 = 41,472` 字节（40.5 KiB），包含六组 Radiance 和六组深度/净空数据。`RegisterExternalTexture` 继承上一图对纹理的访问状态和 Fence，沿用 GIViewer 的预览输出 Fence 释放或退休资源。该路径无独立队列、阻塞等待或每帧 CPU 读回。 [资源实现](../../../src/Framework/Renderer/Private/GI/VoxelLighting.cpp) · [预览接入](../../../src/Framework/Renderer/Private/GI/GIViewerRenderer.cpp)

`VoxelLightingInject` 在 World Atlas Store 后读取其 SRV 和节点状态并写 Volume UAV。它遍历粗层槽位，仅重算本帧刷新的有效节点，已更新的重叠槽保持原内容。无效节点同帧清空，可防止移动或失效以后查询旧世界坐标的数据。初次使用和暂停消费后重新启用时，从全部有效世界槽重注入。几何、灯光、环境或 Radiosity 来源变化沿用上游的失效和预算。常规 Radiosity 反弹变化随世界节点周期刷新逐步传递。

注入后 `WorldRadianceCacheRayLookup` 或 `VoxelLightingDebug` 从同一纹理的 SRV 读取结果。Screen Probe 的原始查询到 Trace/Gather 的顺序依赖不变，缓存候选沿用原有缓冲。只有启用体素时才绑定对应的 Shader 排列。时域滤波在来源开关变化后重置历史。每刷新一个粗层格点读 36 个方向、写 12 个 texel。首次刷新最多读取 7,776 个方向样本，平常由粗层预算决定。 [方向及遮挡算法](../algorithm/22-voxel-lighting.md)

## 单图 RDG Pass 与同步

`GIViewerRenderer::AddPasses` 每次为当前输出尺寸创建场景深度、深度金字塔及 GBuffer，再通过独立 `DefferedSceneRenderer` 构建预览场景颜色和几何。随后按 Viewer 模式安排 Surface Cache 捕获/注册、Lighting 更新/注册以及调试 Pass 或 `AddScreenProbeGatherPasses`。Screen Probe 阶段的 Pass 顺序为：

1. Placement/Ray Generation 写 Probe 与 SurfaceCacheQuery。
2. 启用时 `WorldRadianceCacheRayLookup` 按体素、细层世界探针、粗层世界探针的顺序尝试远场查询，为有效候选缩短追踪范围。体素查表需要勾选对应选项。
3. Surface Cache Query Compute 按当前 Query 范围写命中结果。
4. Lighting Lookup 读取材质命中与直接光照 Atlas，加上启用时最新的间接图集，再写出射光查询结果。
5. Gather 读取 Query、Material Result、Lighting Result 和可选世界探针候选，写 Probe Radiance。
6. Integration 读取 Probe/Radiance/GBuffer，写原始间接光和按模式启用的调试目标。
7. GI 滤波模式追加 `ScreenProbeTemporal`。该 Pass 读取原始间接光、当前 GBuffer 和历史 SRV，写入本轮历史 UAV。Raw、Temporal、History Validity、History Weight 和 Reprojection Error 模式在此写 Viewer 颜色。
8. Spatial Filtered 模式追加 `ScreenProbeSpatial`，从本轮历史 SRV 和 GBufferA 空间滤波，写 Viewer 颜色。

`GIViewerPreview::Render` 在所有 Pass 添加后执行该图。StructuredBuffer 与纹理通过 RDG 参数元数据声明 SRV/UAV/RenderTarget 访问，由 RenderGraph 安排依赖与状态转换。屏幕间接光的消费者是 Viewer 自己的预览颜色。

## 屏幕历史与失效

[ScreenProbeFilter](../../../src/Framework/Renderer/Private/GI/ScreenProbeFilter.cpp) 由预览 Renderer 按需持有，每个 Viewer 独立。两组 Ping-Pong 槽各有 `Lighting`、`Geometry` 和 `Moments` 三张 RGBA32F `RenderTexture`，共占每信号像素 96 字节。`Lighting` 保存线性漫反射间接光 RGB 和历史长度，`Geometry` 保存世界法线 RGB 和设备深度，负深度表示无效。`Moments` 的前三个分量存亮度的一阶矩、二阶矩和方差。CPU 保留前一帧的 VP、逆 VP、相机位置及读写槽状态。图在预览控制线程串行构建并提交，历史通过 `RegisterExternalTexture` 让 RDG 接续上次访问 Fence 和资源状态。同一帧读上一槽，写当前槽，空间 Pass 等待当前槽 UAV 并以 SRV 读取。预览输出的完成点覆盖这一依赖链。

多轮空间滤波的中间颜色是 RDG 瞬时纹理，每轮分别声明当前输入 SRV 和下一输出 UAV。切换方差路径或变化信号、GBuffer 尺寸会使旧历史失效。信号尺寸变化时重建历史槽，GIViewer 在覆盖预览纹理前等待旧颜色关联的提交 Fence。关闭 Viewer 复用既有的预览资源退休流程。

分辨率变化时，Preview 沿用上一输出的完成点替换颜色目标，下一次滤波按新尺寸重新分配历史。关闭 Viewer、工程或替换缓存实例时，`GIViewerRenderer::Reset` 沿用预览输出 Fence 退休历史。失效只标记旧历史不可读，首帧绑定已有白纹理作为未访问占位，并完整覆盖当前槽。未完成滤波构图时不提交新槽状态。

场景实例或材质代理快照变化、灯光快照变化、环境贴图对象变化、重建 Surface Cache、Radiosity 重启及来源开关变化会重置历史。跳过滤波模式、预览尺寸变化以及帧间相机位置变化超过 8 个世界单位也会使上一结果失效。普通 Radiosity 更新和当帧有预算的世界探针刷新降低权重上限，几何与亮度条件继续逐像素生效。未单独为屏幕历史增加 GPU 等待或回收接口。 [重投影算法](../algorithm/20-gi-temporal-spatial-filter.md)

## 反射图与资源

GIViewer 反射模式调用 [DefferedSceneRenderer::BuildWithDeferredSpecular](../../../src/Framework/Renderer/Private/DefferedSceneRenderer.cpp)。此单视图入口在已有 GBuffer 和直接光计算后，只保留漫反射 IBL 到 `DeferredColor`。后续 Pass 为 `ReflectionScreenTrace`、`SurfaceCacheSDFQuery`、`SurfaceCacheLightingLookup`、`ReflectionResolve`，合成视图最后绘制天空。原有主场景和其他预览模式仍由普通 `Build` 编排。

`ReflectionScreenTrace` 从 GBuffer 写屏幕交点和 `SurfaceCacheQuery`。剩余射线交给 `AddGITracePasses`，按当前配置走 Mesh SDF 或 Vulkan 硬件命中，再由 Lighting Lookup 读取持久 Card 图集。`ReflectionResolve` 写 Viewer 预览颜色。硬件排列重新计算光源直接 PBR，复用 Surface Cache 阴影图和 Radiosity 间接漫反射。可选世界探针和环境片段沿用原来的回退。追踪、解析与 Resolve 通过 SRV/UAV 跟踪，无 CPU 读回依赖。Viewer 关闭和快照替换沿用预览颜色的最后访问 Fence。 [硬件追踪资源与 Pass](07-ray-tracing-as-and-sbt.md#giviewer-场景快照与-rdg)

每像素的反射中间资源包括 16 字节屏幕交点、48 字节查询、80 字节通用命中和 48 字节光照记录，共 192 字节。硬件分支的原始命中额外占 32 字节。1080p 的逻辑总量分别约 379.7 MiB 和 443.0 MiB，另有已有延迟底色、GBuffer 和持久资源。实际峰值由 RDG 生命周期及对齐决定。GIViewer 对反射分辨率按比例设 1920×1080 上限。 [反射算法](../algorithm/21-hybrid-reflections.md)

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

表内是保留完整 Probe Debug 输出时的逻辑负载。GI 滤波模式的 Integration 用一张全分辨率 RawGI 和 1×1 Debug 占位，空间结果写既有 Viewer 颜色目标。屏幕历史的四张 RGBA32F 纹理以 `4 × 16 × W × H` 字节常驻，1080p 约 126.56 MiB。以上不包含 Viewer 的 GBuffer/深度、持久 Surface Cache/Lighting Atlas、RenderGraph/RHI 对齐和驱动额外分配。当前没有历史精度预算或分辨率自适应，实际 GPU 峰值与 Pass 耗时待测。

## 限制与验证范围

Probe 每帧重建，方向性 Radiance 没有独立的跨帧滤波。当前使用固定探针间隔和固定样本数，滤波模式逐帧旋转射线方位。GIViewer 的 GBuffer 没有物体级 Motion Vector。法线和距离判断对薄物体、共面不同物体以及急剧深度变化存在局限，原有 SDF 精度和 Card 覆盖限制继续适用。快照尚未跟踪 RenderData 对象内部原地修改的几何或 SDF，遇到这类变化需显式重建。

多后端的滤波像素读回和 `out/pro2` 调试视图检查见 [时空滤波验证](../../dev/bugfix/故障与修复记录-2026-11-21.md#gi-时空滤波验证)。四色墙 GPU 图集与查询读回见 [Radiosity 验证](../../dev/bugfix/故障与修复记录-2026-11-20.md#surface-cache-radiosity-开发与验证)。
