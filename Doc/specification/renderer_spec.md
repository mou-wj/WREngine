# Renderer 功能与使用约定

## 材质着色模型

MaterialEditor 的 **Shading model** 修改母材质的模型。网格实例继承该模型，可独立修改对应分组的参数。**Default Lit** 在现有材质和工程文件中仍保存为 `Lit`。**Subsurface** 分组可调整散射色、散射强度与半径。把强度设为零可对照散射带来的变化。 [编码与算法](../key_implemetion/algorithm/05-pbr-ibl-and-sampling.md#运行时取值与-gbuffer)

**Clear Coat** 分组提供清漆强度和粗糙度。将强度设为零可检查基础层。粗糙度增加时，清漆高光展宽，基础层保留独立粗糙度。

Vulkan Debug 的清漆对照覆盖三类直接光源及非均匀 IBL，并分别读回调整粗糙度、关闭清漆、清除实例覆盖后的画面。四种光照均出现参数响应，清除覆盖复现初始画面。三个后端的材质 Shader 编译检查及 Vulkan 编辑器启动、正常退出通过。

`MaterialShadingModelTest` 使用同一球体和相机分别渲染四种光照，读回中心区域检查材质与实例覆盖，并输出 `MaterialShadingModels.ppm`。Vulkan Debug 下，散射对方向光、点光、聚光及非均匀环境光的最大线性 RGB 差值分别为 0.0874、0.2030、0.2922、0.0675。Vulkan、OpenGL 与 DirectX12 上用 `MaterialShaderCompilationTest` 检查前向、GBuffer 及两个延迟光照变体的原生 Shader 编译。

## Virtual Geometry 场景绘制入口

### 入口与职责

[VirtualGeomtryRender.h](../../src/Framework/Renderer/Private/VirtualGeometry/VirtualGeomtryRender.h) 声明 `Renderer::AddVirtualGeometryRender`，实现在同目录 cpp。该文件同时承担实例整理和 Pass 编排，没有额外的 Process 类、全局批次缓存或资源管理器。

```cpp
auto visibility = Renderer::AddVirtualGeometryRender(
    builder, scene, view, sceneDepth, gbufferA, gbufferB, gbufferC, 1.0f);
```

`scene` 是 Renderer 的 Scene，提供对象查询和现有实例 ID 映射。`view` 提供视口、视锥及相机矩阵，Scene 本身不保存这些信息。附件参数均为调用方当前 RenderGraph 中的引用。最后的 targetPixelError 可省略，默认 1 像素；0 请求完整最细 Cut，负数和非有限值报错。独立 Raster 调用可在每个 VirtualGeometryInstance 上配置 TargetPixelError。

targetPixelError 后可追加 `VirtualGeometryRasterSettings`，默认 Mode=Hardware 保持旧调用行为；Hybrid 按 SoftwareClusterPixels（默认 32，最大 128 像素）分流，SoftwarePreferred 将安全且不超过 128 像素的 Cluster 送入 Compute。软件模式要求 GPU selection，保留原 Metadata/Resolve，最终 Visibility 和传入 SceneDepth 的引用不变；内部额外建立硬件附件和两个软件 Buffer，Merge 后再执行 Resolve。低层输出的 RasterRouting/SoftwareTargets 仅供本次图内诊断，不能跨帧持有 transient 引用。回退、ID 限制、精度和成本见 [软件光栅化](../key_implemetion/algorithm/14-virtual-geometry-software-rasterization.md)。

此入口仅编排 Visibility Raster 和 Material Resolve，写入 SceneDepth 与 GBuffer A/B/C。它不提交图、不创建交换链、不执行光照，也不在调用时构建或上传虚拟几何。返回值复用 `VirtualGeometryRasterOutput`，供调试显示、Metadata 检查或其他后续消费者使用。

### 调用前提与附件语义

- 先完成场景 Flush 和视图更新，并与场景、组件和材质修改串行执行。入口不会自行刷新场景、申请另一套实例 ID 或等待 GPU。
- 需要参与绘制的网格应已对 `StaticMeshRenderData::VirtualGeometryData` 显式 Build 并 InitializeResources。当前视图中未构建虚拟几何的网格跳过，不自动切换传统网格绘制；构建有效但未上传的资源由 Raster 报错。
- SceneDepth 必须为单样本、单层的 Texture2D，格式为 D32_Float，包含 DepthStencil 与 ShaderResource 用途。Raster 直接写入传入的 RDG 纹理，返回的 VirtualGeometryDepthTexture 与其相同，不创建替代深度或复制深度。
- GBuffer A/B/C 必须为相同尺寸的单样本、单层 Texture2D，格式与既有 CreateGBufferInfo 的默认布局一致，并具有 UAV 用途。后续读取、读回等用途由调用方补充。
- 第一版仅支持从原点覆盖完整附件的一个视图。现有 Raster Clear 会清空整个深度，Resolve 会清空对应 GBuffer，不能将此入口作为保留既有场景内容的叠加绘制，也不能连续用于同一目标的多个子视口。
- 发生构图异常后，调用方应放弃该图，不能执行已添加的部分 Pass。入口不捕获异常或声称失败后仍有完整输出。

### 实例整理与执行顺序

`AddVirtualGeometryRender` 内部使用 `BuildVirtualGeometryInstances`。该函数调用 `Scene::GatherVisiblePrimitives(view)`，沿用场景现有的可见性和视锥查询，再筛选 StaticMeshProxy。网格数据有效时，从既有 StaticMeshProxyToInstanceId 映射取得 InstanceID，并保存 RenderData、LocalToWorld 和按槽排列的 MaterialRenderProxy 快照。缺失实例映射会报错，提示先完成场景更新。

收集结束后依次调用 `AddVirtualGeometryRasterPass(builder, view, instances, sceneDepth)` 和 `AddVirtualGeometryMaterialResolvePass`。场景入口为实例开启 GPU LOD 选择及视锥剔除：按 Group 依赖深度编排 Compute，先形成完整 DAG Cut，再生成可见 Cluster 和索引间接参数。Raster 清空 Visibility 和调用方深度，随后用稳定候选 DrawID 执行间接绘制；Resolve 读取同一深度及 Visibility，重建实际可见层的属性并写入 GBuffer。顶点/索引及 IndirectArgs 通过既有 RDG_BUFFER_ACCESS 声明，其余附件、SRV、UAV 沿用参数声明，由 RDG 生成依赖、屏障和提交访问记录。

返回值和原有 Pass 捕获保存录制期所需的资源及材质快照。GPU 完成前的释放和重建仍遵守资源最后访问 Fence，不增加 WaitIdle、全局保活或另一套生命周期控制。

后续若接入延迟光照，调用方应继续使用原有 DeferredShadingCS，并按既有流程准备灯光和阴影数据。此入口没有 SceneColor 参数，因而不调用独立的 AddVirtualGeometryDeferredShadingPass。RenderModule 当前保持传统渲染流程，尚未调用此入口。

属性恢复与材质数据布局见 [Material Resolve 实现](../key_implemetion/algorithm/12-virtual-geometry-material-resolve.md)，Cluster 绘制和 ID 定位见 [Visibility 实现](../key_implemetion/algorithm/11-virtual-geometry-visibility.md)。

CPU 参考和 GPU 选择的入口、屏幕误差公式、多对多依赖与裂缝条件见 [LOD Selection](../key_implemetion/algorithm/13-virtual-geometry-lod-selection.md)。独立 Raster 的 VirtualGeometryInstance::bGPUSelection 默认为 false，设置 true 开启 GPU 路径，bFrustumCulling 控制其剔除。GPU 输出的 Draws 是全部候选 Metadata，GPUSelections 才提供 Cut/可见标志、紧凑列表和统计。不启用流送或显式诊断时没有 CPU 读回；仍为每候选录制间接命令，Resolve 仍逐候选调度。选择只使用既有 Build 的累计采样误差与历史包围范围，不代表严格最大像素误差保证；没有滞回或遮挡剔除。可选流送及其反馈读回通过下一节的显式 Cache 接入。

### 显式 Page Streaming 调用

独立 Raster 的 VirtualGeometryInstance 可提供共享的 VirtualGeometryStreaming Cache，必须匹配同一个 RenderData，并开启 bGPUSelection，不能用于 Generated level 检查。调用方先 Build CPU 几何，再创建/打开 Engine 的 VirtualGeometryPageStorage，由 Cache 构造有限 GPU 池，无需全量 InitializeResources；每次 Execute 后、图销毁前调用 Cache::EndFrame，缺页反馈和上传由后续帧推进。同一资源的多个实例共用 Cache，其他资源可拥有独立预算；不在 Scene 封装中隐式创建全局缓存。完整调用及预算、释放前提见 [流送工程实现](../key_implemetion/performance/11-virtual-geometry-streaming.md)。

GPU 会保留理想误差选择，再计算因缺页必须粗化的 Group 依赖闭包，保证整个 Cut 完整且驻留，不要求所有中间层同时常驻；目标像素误差在缺页/受限预算期间可能超出。HW/SW Raster、Metadata、Material Resolve 均继续使用同一物理 ClusterBuffer 和原 Visibility/GBuffer 格式。

### 当前限制与验证入口

MaterialParametersHelper 现支持显式 UV 梯度，Compute 使用 SampleGrad，原像素 Shader 仍使用 Sample。此前回退后的错误属于历史状态，见 [故障记录](../dev/bugfix/故障与修复记录-2026-11-13.md)；2026-11-14 的三后端 Material Resolve 复测通过。独立场景入口仍未接入主渲染流程，完整传统路径与虚拟几何路径的全画面对照尚未完成。

[VirtualGeometryRenderTest](../../src/Tests/VirtualGeometryRenderTest.cpp) 通过 `Tests.exe -test=VirtualGeometryRenderTest -rhi=Vulkan` 运行，也可选择 DirectX12 或 OpenGL。它只验证空 Scene 经完整入口的组合行为，包括调用方深度复用、提交 Fence、无效 Visibility，以及非 8 倍数尺寸的深度与 GBuffer 清空读回，不覆盖非空场景实例收集和材质求值正确性。原 VirtualGeometryRasterTest 继续覆盖独立 Raster 的非空网格、多实例和 ID 映射。

2026-11-14 的空场景测试在 Vulkan、DirectX12 退出 0；当时 OpenGL D32 读回落到 GL_RGBA 并发生 GL_INVALID_OPERATION，未记为通过。2026-11-15 软件光栅深度对比需要实际读取 D32，因此仅在 OpenGLUtils 补齐 D32_Float→GL_DEPTH_COMPONENT 映射，保留原读取流程和断言；此次复测结果见 [当日记录](../dev/bugfix/故障与修复记录-2026-11-15.md#virtual-geometry-gpu-software-rasterization)。独立 Raster 的历史验证及早期测试自身问题见 [2026-11-14 记录](../dev/bugfix/故障与修复记录-2026-11-14.md)。

## VirtualGeometryViewer 消费方式

[VirtualGeometryViewer](virtualgeometryviewer_spec.md) 直接提供独立实例，调用选择、Raster、Resolve 和 AddVirtualGeometryDeferredShadingPass，不注册到主 Scene。默认 GPU Dynamic LOD，提供 CPU 参考切换、视锥开关和按需 GPU Capture 对比；关闭 Dynamic LOD 才按 MipLevel 检查生成层，界面明确标记其不是完整 Cut。线框同样由 GPU 选择参数驱动。光照封装复用 SceneRenderer 的阴影访问表和级联参数准备，再运行既有 DeferredShadingCS；无 IBL 时不绑定环境参数。主 RenderModule 与 DefferedSceneRenderer 的流程没有改变。

## Surface Cache Material 调用约定

[SurfaceCacheMaterial](../../src/Framework/Renderer/Private/GI/SurfaceCacheMaterial.h) 是 Private/GI 下可由 Renderer 消费者显式调用的材质缓存基础。Initialize 接收实例快照，或用 GatherSceneInstances 在 Scene Flush 后收集，不按主视图剔除；LOD0、材质和局部 SDF 仍由原有 Engine 路径创建。它不自动加入主场景、修改灯光或创建编辑器 UI。

首次调用 AddCapturePasses(builder) 捕获六向 Card，返回当前图的 SurfaceCacheMaterialParameters；之后可继续 AddSurfaceCacheQueryPass 和两个 Debug Pass。后续图调用 Register(builder) 读取持久 Atlas，不必重新捕获。图内参数/引用不能跨图保存；捕获失败须放弃图及其部分内容，调用方在退休实际 GPU 使用后重建/释放缓存。

SurfaceCacheQuery.Control.x 为 InstanceID 或 UINT32_MAX，Control.y 为模式：0 表示 OriginDistance.xyz 是已有命中点、Direction.xyz 是世界几何法线；1 表示世界射线，OriginDistance.w 是最大距离。模式 1 通过原局部 SDF Atlas 识别最近实例，未带有效 SDF 的对象不参与；模式 0 可以直接消费其他追踪器的已知命中。结果的 hit 与 material-valid 状态分开，不能把无覆盖结果当作黑色材质。

AddSurfaceCacheDebugPass 支持 BaseColor、世界法线、Card 覆盖、Roughness 和 Metallic；AddSurfaceCacheQueryDebugPass 显示查询材质、法线或 Card ID，黑色为 miss，紫色为命中无缓存，未命中且迭代耗尽显示橙色。输出目标须为 RGBA32F、带 UAV 用途的 RDG 纹理，由消费者显示或读回；这些 Pass 本身不创建 GUI，独立可视化入口由下文的 Lumen GI Viewer 提供。

材质/几何/变换变化、实例 ID 更新和来源 SDF 释放后，调用方须显式失效并重捕获。支持现有不透明材质，不提供复杂凹面完整覆盖、自适应 Card、动态 Atlas 淘汰；直接光照由下文独立缓存负责。资源所有权、调用示例和完成点见 [工程专题](../key_implemetion/performance/12-surface-cache-material.md)，投影、坐标、状态和验证见 [算法专题](../key_implemetion/algorithm/16-surface-cache-material.md)。

## Surface Cache Direct Lighting 调用约定

[SurfaceCacheDirectLighting](../../src/Framework/Renderer/Private/GI/SurfaceCacheDirectLighting.h) 接收已捕获的 SurfaceCacheMaterial 与灯快照，AddUpdatePasses 在同一个 Builder 内添加缓存阴影与直接光照 Compute，返回独立 Lighting 参数。支持 Directional、Point、Spot；Scene 灯光可用 GatherSceneLights 收集，须先完成场景更新。不会替主相机更换 CSM、改变主延迟绘制或自动开启 GI。

缓存为 Lambertian 漫反射出射 Radiance，未包含 view-dependent 镜面、环境光、Emission、AO、多次反弹或反射；Lookup 后不能重复乘源表面的 BaseColor。AddSurfaceCacheLightingLookupPass 接收第一阶段的查询结果，因此世界命中和 SDF 射线共用同一 Instance/Card/实际像素。未覆盖和未命中返回无效，已计算的全黑阴影仍是有效数据。

灯变化后调用全量 AddUpdatePasses；材质/物体变化先按原约定重建实例快照和捕获，再重算 Lighting。Register 检查匹配的材质 Revision；Invalidate 标记不可查询。只有调用方确认所有 GPU 用户退休后才可重建或 Release，不引入内部等待。完整调用顺序、内存、容量、失败和 Fence 条件见 [工程专题](../key_implemetion/performance/12-surface-cache-material.md#独立直接光照缓存与全量更新)，光照语义、离屏阴影、Lookup 和验证见 [算法专题](../key_implemetion/algorithm/16-surface-cache-material.md#漫反射直接光照的语义与计算)。

AddSurfaceCacheLightingDebugPass 不传 queryResults 时输出 Atlas；传入 Lighting 查询结果时输出 32×32 色块。输出须为有 UAV 用途的 RGBA32F RDG Texture，可传 exposure 控制调试映射，不改变缓存的线性数值。调用示例和专项入口为 Tests.exe -test=SurfaceCacheDirectLightingTest -rhi=Vulkan（也可 DirectX12、OpenGL）。GIViewer 可独立显示 Atlas；缓存仍没有主场景自动更新。

## Lumen GI Viewer

Lumen 的 Surface Cache Material、Direct Lighting、Radiosity 与 Screen Probe Gather 由独立的 [GIViewerRenderer](../../src/Framework/Renderer/PublicHeader/GIViewerRenderer.h) 编排。通过 Editor 的 [GIViewer](../../src/Editor/GIViewer/PublicHeader/EditorGIViewer.h) 模块从 View 页打开。Viewer 使用当前场景与相机创建独立 RenderGraph、预览颜色目标、深度和 GBuffer，按所选阶段运行缓存和探针 Pass。

Viewer 默认显示 Scene 预览。选择 Surface Cache、Direct Lighting、Radiosity 或 Screen Probe 阶段会按需初始化缓存。`Rebuild Surface Cache` 按钮切换到 Card Coverage 视图并重建。

Scene 视图提供 `Draw Mesh Cards in scene` 和 `Selected Card only` 开关。Mesh Card 列表按 Scene Instance 和六个轴向投影列出 Card；选择 Card 后显示其世界中心、法线、范围和 Atlas Tile。场景叠加把 Card 的世界投影矩形经当前 ViewProjection 投到预览图片上，以轴向颜色区分并以黄色强调选中项。该叠加由 ImGui 绘制，按 X-ray 方式显示，不受场景深度遮挡；它表示空间覆盖范围，不代表捕获到的表面三角形。选中 Card 后可切换并放大查看其 Atlas Tile 的 Base Color、World Normal、Coverage、Roughness 或 Metallic。

Surface Cache 的 Base Color、World Normal、Card Coverage、Roughness、Metallic 和 Direct Lighting Atlas 显示对应图集。Card 投影示意表达 U/V 投影基与法线方向。Screen Probe 的 Placement、Ray Directions、Hit Ratio 和 Radiance 显示探针阶段结果。

`Radiosity / Indirect Atlas` 显示累计间接出射光，`Radiosity / Iteration Delta` 显示相邻两轮的变化。启用 `Use radiosity in Screen Probes` 时，探针命中会读取直接与间接出射光之和。`Pause` 停止自动迭代，`Step` 暂停并推进一轮，`Restart` 清除间接历史，可配合暂停逐轮查看。实例快照和灯光变化会触发相应缓存重建或重算。具体失效条件见 [工程说明](../key_implemetion/performance/13-screen-probe-gather.md#radiosity-迭代与缓存失效)，反弹算法见 [Radiosity 算法](../key_implemetion/algorithm/18-surface-cache-radiosity.md)。

`World Probes / Radiance Atlas` 左右显示细层和粗层，每层按 3×2 摆放六个 XZ 切片。每个格点包含八面体方向图，黄色边框表示当帧刷新，暗边框表示等待更新，粉色 texel 表示该方向无效。用 `Use world cache` 切换参考路径。`World probes per frame` 同时控制世界探针与体素的更新来源，设为 0 可冻结刷新并检查移动后的重叠槽复用。 [世界探针算法](../key_implemetion/algorithm/19-world-radiance-cache.md)

`Voxel Lighting / Axis Slice` 通过 `Y slice` 选择 XZ 切片。三列依次为 X、Y、Z，上下分别为正向与负向。黄色格边表示本帧更新，粉色格表示该方向不可用，暗灰格表示未填充。Screen Probe 视图中同时勾选 `Use world cache` 和 `Use voxel lighting` 可使用体素查询。`Screen Probes / Cache Source` 显示各探针的射线路径比例，金色为体素，青色与紫色为世界探针细层与粗层，灰色为几何追踪或环境。 [体素光照算法](../key_implemetion/algorithm/22-voxel-lighting.md)

`GI / Raw`、`GI / Temporal` 与 `GI / Spatial Filtered` 分别显示当前探针重建、时域累积和边缘感知空间滤波后的漫反射间接光。`GI / History Validity` 用绿色表示通过重投影检查，红色表示历史被拒绝或重置。`GI / History Weight` 用灰度表示最终历史权重。`GI / Reprojection Error` 的红绿通道显示归一化的几何残差，蓝色显示屏幕运动幅度。六种 GI 视图间切换保留历史，离开这些视图后重新进入会开始新一轮累积。使用宿主视口相机改变视角可观察历史重建。 [滤波与调试含义](../key_implemetion/algorithm/20-gi-temporal-spatial-filter.md)

### 反射预览

在 `Reflections / Composite` 查看延迟底色与混合反射的合成。`Ray Direction` 用 RGB 表示世界方向，`Screen Hit` 用 RG 表示屏幕 UV、B 表示置信度。`Geometry Hit` 显示硬件或 SDF 几何命中与距离，`Hit Instance` 按场景实例 ID 着色。`Source` 的青色、品红表示有着色数据和缺少着色数据的硬件命中，绿色为屏幕，黄和橙为 SDF，紫色为世界探针，蓝色为环境。另有 `Roughness`、`Radiance` 和 `Contribution` 查看接收面粗糙度、入射辐射与镜面项。

`Use radiosity in reflections` 控制离屏 Card 光照是否加入反弹项，`Use world cache` 控制粗糙反射的世界缓存回退。当前主场景仍使用 `DefferedSceneRenderer` 的常规 ScreenSpaceGI/SSR。预览反射合成复用同一延迟 GBuffer 与直接光，在延迟漫反射 IBL 之后写入镜面项。 [算法与回退](../key_implemetion/algorithm/21-hybrid-reflections.md)

### 硬件追踪预览

在 Screen Probes、GI 或 Reflections 视图勾选 `Vulkan hardware tracing`。默认使用 SDF，未开启 Vulkan 原生光追管线时使用同一 SDF 路径。状态行给出实际路径、BLAS 数、TLAS 实例数与版本。切换追踪后端会使现有 GI 累积历史失效。 `Screen Probes / Trace Source` 按 Tile 显示几何命中比例，黄色为 SDF，青色为有 Card 的硬件命中，品红为硬件命中但缺少 Card。未命中的方向按既有世界查询或环境策略取光。 [实现与验证](../key_implemetion/performance/07-ray-tracing-as-and-sbt.md#giviewer-场景快照与-rdg)
