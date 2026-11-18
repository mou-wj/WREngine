# Surface Cache 材质、直接光照资源与 RDG 时序

源码为 [SurfaceCacheMaterial](../../../src/Framework/Renderer/Private/GI/SurfaceCacheMaterial.h)，实现位于同目录 cpp；职责见 [Renderer 架构](../../architecture/render/renderer.md)，投影、SDF 与查询公式见 [算法专题](../algorithm/16-surface-cache-material.md)。这是 Renderer 的材质缓存基础，不是新的资产类型或全局 GI 管理器。

## 所有权与输入前提

调用方持有一个不可复制的 SurfaceCacheMaterial。它保存实例变换和材质代理快照、Card/Instance 元数据，并拥有四张 RenderTexture：BaseColorMetallic、NormalRoughness、PositionCoverage、Depth。实例使用共享 StaticMeshRenderData 和不可变材质代理，沿用现有资源所有者关系；缓存不负责初始化传统 LOD、生成局部 SDF 或替代 AssetManager。

Initialize 要求 LOD0 已初始化顶点/索引和 LocalVertexFactory，至少含 Position/Normal/UV；Section 和 BaseVertex 范围合法、材质槽存在且为现有不透明表面，法线贴图另需有效切线流。变换须有限、可逆且为仿射。先验证并创建临时纹理，成功后发布成员，失败不会发布一半的新快照。已初始化对象要在退休旧 GPU 使用后 Release，再 Initialize。

局部 SDF Allocation 和 CPU Volume 只作快照描述，不转移其所有权。调用方必须让来源资产及 Engine 距离场 Atlas/分配在查询期间保持有效；不能让资产删除/释放 SDF 后继续使用旧 Allocation。相应场景对象、材质、变换或几何修改后需显式重建快照和重新捕获，不能仅修改组件就假设缓存自动更新。GatherSceneInstances 使用原场景实例 ID 表，须先 Flush，并与场景修改串行执行。

## Atlas 与内存

固定方形 Tile 的有效分辨率默认 64，可配置 8～512；每个 Tile 外有一像素无效边框。每实例六张，列数 `ceil(sqrt(CardCount))`，至少一列；宽高均为列数乘 `(resolution+2)`，上限 8192，实例数上限 1024。边界检查不代表已有动态显存预算或任意设备均能承受最大配置。

三个 RGBA32F 加 D32 的申请量约 `52*atlasWidth*atlasHeight` 字节，不含驱动对齐、读回 staging、上传及图内元数据。三实例、64 分辨率的测试为 330×330，纹理申请约 5,662,800 字节。它优先保留精度和调试位置数据，没有压缩格式、Atlas 淘汰或分级分辨率。

## 显式调用与持久缓存

```cpp
Renderer::SurfaceCacheMaterial cache;
cache.Initialize(Renderer::SurfaceCacheMaterial::GatherSceneInstances(scene), 64);

// 第一次图：捕获后可以在同图内查询、调试。
auto parameters = cache.AddCapturePasses(builder);
auto results = Renderer::AddSurfaceCacheQueryPass(builder, parameters, queries);
Renderer::AddSurfaceCacheDebugPass(builder, parameters, atlasDebug, 0);
Renderer::AddSurfaceCacheQueryDebugPass(builder, results, uint32_t(queries.size()), queryDebug, 0);
builder.Execute();

// 其他图只需 cache.Register(otherBuilder)，不重新捕获。
// 等待实际 GPU 使用的完成点后才可 cache.Release() 或销毁/重建来源资源。
```

AddCapturePasses 返回的是当前图的参数绑定；Register 每图注册四张中参与查询的三张外部纹理，并将当前 Card/Instance 元数据上传到 transient StructuredBuffer。不得跨 Builder 保存这些图内引用。CPU vector 对象由图 arena 保存，大载荷仍在 vector 堆存储，通过带 TransferDest 参数的原 RDG Pass 调用 UpdateBuffer，避免原始字节 arena 单页 64 KiB 的限制。

CPU 发起批量查询时，`AddSurfaceCacheQueryPass(builder, cache, queries)` 会创建 Query Buffer 并由 RDG 上传；Screen Probe 这种 GPU 生产者可调用接收 `RenderGraphBufferRef` 和 queryCount 的重载，直接把前序 UAV 输出作为 SRV 输入给同一 Query Shader。两种入口共用 `SurfaceCacheQuery` 布局和结果格式，后者不会读回 CPU；Query/Result 均为图内临时数据，不能跨 Builder 缓存。

本阶段不自动挂入 RenderModule 主场景，也不在每帧隐式全场景捕获；独立消费者显式编排，后续 Lighting/Radiosity 可以复用同一缓存与 Lookup，不需要重新从屏幕恢复材质。捕获编排完成标志只表示已经添加 Pass，不表示 GPU 已执行；图构建/执行失败时必须放弃该图和缓存内容，重新捕获或 Release，不能用部分结果继续查询。

## 独立直接光照缓存与全量更新

[SurfaceCacheDirectLighting.h/.cpp](../../../src/Framework/Renderer/Private/GI/SurfaceCacheDirectLighting.h) 增加与材质 Atlas 分离的所有者。它只持有 RGBA32F Radiance、现有 ShadowMapAllocator 及预分配的阴影矩形，不建立全局 GI Manager，也不修改 SceneRenderer/RDG/RHI 架构。材质缓存由外部持有，须在 Lighting 对象使用期间保持存活。其 Shade/Shadow/Lookup 公式见 [算法](../algorithm/16-surface-cache-material.md#漫反射直接光照的语义与计算)。

```cpp
Renderer::SurfaceCacheMaterial material;
material.Initialize(Renderer::SurfaceCacheMaterial::GatherSceneInstances(scene), 64);
Renderer::SurfaceCacheDirectLighting lighting;
lighting.Initialize(material, 128, 64); // 每个 Point 消耗六个阴影视图。
auto cacheParams = material.AddCapturePasses(builder);
auto lightParams = lighting.AddUpdatePasses(builder, material,
    Renderer::SurfaceCacheDirectLighting::GatherSceneLights(scene));
auto hits = Renderer::AddSurfaceCacheQueryPass(builder, cacheParams, queries);
auto radiance = Renderer::AddSurfaceCacheLightingLookupPass(builder, cacheParams, lightParams, hits, uint32_t(queries.size()));
Renderer::AddSurfaceCacheLightingDebugPass(builder, lightParams, atlasDebug);
Renderer::AddSurfaceCacheLightingDebugPass(builder, lightParams, queryDebug, radiance, uint32_t(queries.size()));
builder.Execute();
```

AddUpdatePasses 每次完整清空/重绘缓存阴影并重算 Lighting Atlas；不会清空 Material Atlas。灯颜色、强度、开关、位置/旋转/半径/锥角/投影变化后，在场景更新后再次收集并调用该接口即可；不变时可 Register(builder, material) 复用持久结果。调用方可先 Invalidate；禁止用旧参数继续查询。运行时修改 Scene 不会自动驱动本独立缓存，未接入主场景 GI。

Material 的 Initialize、Release、AddCapturePasses 更新 Revision。Lighting 的 IsValidFor/Register 检查对象身份与 Revision，重新捕获后旧 Lighting 会被拒绝，AddUpdatePasses 完成构图后发布新版本。物体变换、几何、材质代理、SDF 分配变化仍按第一阶段约定：退休旧用户，Release/Initialize 新实例快照，AddCapturePasses，再 AddUpdatePasses。相同 Atlas 尺寸可复用 Lighting 分配；尺寸改变须在退休后 Release/Initialize Lighting。不能仅改组件而继续使用旧快照；失效机制是显式的全量重建流程，没有引入 Scene 监听或增量脏卡系统。

资源申请完成后才发布 Initialize 成员。AddUpdatePasses 开始先失效，容量/光源参数检查失败不发布可用状态；完成构图才标记可注册。该标记只代表已编排，不代表 GPU 已完成，图失败或被放弃后应 Invalidate 并重新计算，不执行半张失败的图。

ShadowResolution 默认 128，可取 32～1024；MaxShadowSlices 默认 64，可取 6～256，`columns=ceil(sqrt(maxSlices))`，阴影宽高为 `columns*(resolution+2)`，不能超过 8192。容量不足明确报错，不能静默取消某灯阴影。RGBA32F Lighting 申请量约 `16*N*N` 字节，阴影 Atlas RG16F 约 `4*M*M`；D32 为图内临时纹理，另约 `4*M*M`。不含对齐、staging 和读回。预分配块避免逐次更新累加分配器的 shelf 游标，Release 仍只由调用方在实际 GPU 使用退休后执行。

LightData 80 字节、ShadowSlice 80 字节，使用正确反射的 StructuredBuffer 与显式 TransferDest 上传；SurfaceCacheQueryResult 从 64 扩为 80 字节，SampleCoordinate 保存实际材质像素。现有查询/Debug 按相同 C++/Shader 类型自动适配，原测试流程未更改；不得使用旧硬编码 stride 解析结果。

## 访问声明、线程和完成点

清空和逐 Card 捕获通过 RenderTargetBindingSlots 声明三个颜色附件与深度；顶点、法线、UV、切线和索引通过 RDG_BUFFER_ACCESS(VertexOrIndexBuffer) 声明。材质输入使用现有 BuildShaderParameters 注册纹理。Capture VS/PS 共用 SurfaceCacheCaptureViewParameters，复制同一份 Card 视图数据，避免同一视图的矩阵和方向分散成互不相关的阶段常量块。

查询 Compute 读取三个 Atlas SRV、Card/Instance/Query StructuredBuffer 和局部 SDF Atlas；结果为 StructuredBuffer UAV。Debug 读取 Atlas 或查询结果，输出 RGBA32F UAV。RDG 负责附件写→SRV、UAV 写→SRV/TransferSrc、跨队列依赖及最后访问 Fence，没有修改 Builder 或新增 Pass 访问接口。

直接光照的 Shadow Pass 通过原 RDG_BUFFER_ACCESS 声明 Position/Index，通过 Targets 声明 Shadow Atlas 与深度附件；Compute 读取三张 Material Atlas、Lights/Shadows 表及 Shadow SRV，写独立 Radiance UAV。Lookup/Debug 显式读取 Radiance 和查询结果，再写结果 UAV。附件写→SRV、Lighting UAV→Lookup/Debug SRV 和读回依赖由现有 RDG 推导；没有改访问意图接口、状态管理、增加锁或 WaitIdle。

创建、Initialize、构图、Execute 及编辑由调用方串行安排；实际图录制沿用原并行机制。图的 lambda 保留录制所需实例/材质快照，局部 SDF 资源仍由既有 Engine 管理。GPU 完成前不能释放缓存、来源 LOD 或 SDF Allocation。Release 和析构不加入 WaitIdle，也不替调用方等待；调用方应依据缓存及来源资源最后访问 Fence 退休 GPU 用户，再释放对象，且在 RHI Shutdown 前完成。

测试在图中声明 TransferSrc，并等待实际外部 Debug 纹理的提交完成点，再检查映射结果；OpenGL 使用其已有同步 MapRead 接口，在提交工作完成后读取。专项测试的 CPU 等待属于验证，不在正常捕获/查询函数中执行。

## 调试与边界

- AddSurfaceCacheDebugPass：0 BaseColor、1 世界法线、2 Card 范围/覆盖、3 Roughness、4 Metallic。无效覆盖为紫色；观察输出 RDG Texture，不创建额外原生窗口。
- AddSurfaceCacheQueryDebugPass：每个结果占 32×32 像素；0 材质色、1 法线、2 Card ID 色。未命中黑色，命中无覆盖紫色，未命中但触发迭代上限橙色。也可读回原结果查看实例、Card、距离和状态。
- AddSurfaceCacheLightingDebugPass：单独显示 Direct Lighting Atlas 或 SDF/直接命中的 Radiance 色块；外部自行决定显示/读回位置。
- 本阶段未新增编辑器按钮或可视化面板；专项测试验证上述真实 GPU 图像及数值，Launcher 只检查正常应用启动回归，不代替 Surface Cache 场景验收。

ShaderParameter.h 仅按已有 UInt3 特化补齐 UInt2/UInt4 的类型信息；没有新 BaseType、绑定规则或 RHI 接口。阶段签名、std140 排布及共享视图常量块的问题见 [故障记录](../../dev/bugfix/故障与修复记录-2026-11-17.md)。

当前仍需手工重建快照，元数据每图上传，Card/实例查询线性；局部 SDF 也受生成符号、体素精度和有限容量限制。待完善项统一归档 [优化清单](../../dev/optimize/优化点-2026-11-17.md)，不将这些限制写成已完成的完整 Lumen。
