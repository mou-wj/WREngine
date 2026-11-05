# 核心实现阅读指南

2026-11-05 收窄 TextureViewer 来源：仅列出 Content 纹理资产和已生成的环境光输出，不再列举场景/快照及环境输入的运行时副本。路径身份、所有权和边界见 [来源与所有者](performance/08-ui-rendering-and-window-lifetime.md#来源与所有者)，实际复现与验证见 [修正记录](../dev/bugfix/故障与修复记录-2026-11-05.md#来源列表仅保留资产与环境光输出)。

2026-11-04 完成工程基础层快照和 IBL 初始化完成点续验：存储与所有权见 [工程快照](performance/10-project-snapshots.md)，缩图和色彩空间见 [Mip 重建算法](algorithm/05-pbr-ibl-and-sampling.md#工程纹理的普通-mip-重建)，实际通过范围与已知限制见 [续验记录](../dev/bugfix/故障与修复记录-2026-11-04.md)。

同日移除 SkyLightAsset，将环境光生成结果交给 Engine 的 [EnvironmentLightResourceManager](performance/05-assets-materials-and-instance-data.md#环境光计算产物管理)；默认环境无文件依赖，通用 TextureAsset 属性支持应用资源面板输入，工程保存输入基础层并在加载时重新生成。界面、所有权和实际验证见 [编辑器架构](../architecture/app/editor.md)、[开发记录](../dev/bugfix/故障与修复记录-2026-11-04.md#环境光计算产物改由-engine-管理)。

同日增加独立 TextureViewer：多来源二维画布、中心/拖动/滚轮锚点、红蓝主轴、中键平移和网格分级见 [画布算法](algorithm/09-texture-viewer-canvas.md)，来源引用、预览图与实际访问完成点见 [生命周期](performance/08-ui-rendering-and-window-lifetime.md#textureviewer-来源与资源生命周期)。来源覆盖 TextureAsset 及已生成环境光贴图，材质来源仅保留 TODO；实际验证范围见 [开发记录](../dev/bugfix/故障与修复记录-2026-11-04.md#textureviewer-多纹理画布与来源选择)。场景网格世界原点的红 X/绿 Z 轴和坐标重定位补偿见 [提示几何](algorithm/03-transforms-frustum-and-depth.md#xz-地面网格与光源提示几何)，中键/主轴续验见 [同日记录](../dev/bugfix/故障与修复记录-2026-11-04.md#纹理画布主轴中键平移与场景坐标主轴)。

同日补充资源目录创建/删除、文件与目录拖放、重命名、材质/glTF 本地依赖重写及路径缓存迁移；图片和自包含文件不再保留额外包目录。执行时序、所有权和失败边界见 [资源文件操作](performance/05-assets-materials-and-instance-data.md#资源文件移动重命名与删除)，实际 UI、资产复用和源删除后的 GPU 像素验证见 [开发记录](../dev/bugfix/故障与修复记录-2026-11-04.md#资源目录操作与单文件导入)。

本目录回答“具体怎样实现、依赖哪些条件、哪里尚不可靠”。模块职责与接口关系仍以 [architecture](../architecture/README.md) 的既有结构组织。目录名沿用需求首次指定的 `key_implemetion`。

2026-11-02 补充了组件类型化属性描述、通用 NodeEditor、网格可见性与光源照明/范围独立开关、对象右键删除的执行时序，见 [UI 工程实现](performance/08-ui-rendering-and-window-lifetime.md#组件属性参数与通用编辑界面)。

同日修正聚光锥角的弧度/余弦边界及阴影访问数据更新，见 [聚光算法](algorithm/05-pbr-ibl-and-sampling.md#聚光锥角的单位与衰减)、[阴影实现](algorithm/06-shadow-mapping-and-cascades.md#阴影访问数据必须随光源更新) 和 [故障记录](../dev/bugfix/故障与修复记录-2026-11-02.md#聚光灯启停无画面变化)。

2026-11-03 完成聚光修复后的通用节点回归、后端 Shader 复查和主目录 Launcher 重启，实际范围及未通过项见 [续验记录](../dev/bugfix/故障与修复记录-2026-11-03.md)。

同日增加独立 MeshViewer 并补充法线方向箭头：模块边界见 [编辑器架构](../architecture/app/editor.md#meshviewer-单网格检查)，几何/Phong 与箭头计算见 [预览算法](algorithm/05-pbr-ibl-and-sampling.md#meshviewer-的-phong-预览)，纹理合成、切换和资源完成点见 [工程实现](performance/08-ui-rendering-and-window-lifetime.md#meshviewer-预览与检查)。

2026-11-01 补充实体优先拾取、场景树右键创建、基础几何资产与 VF 切线组合；界面操作、所有权和后端 Shader 编译的实际范围见 [开发记录](../dev/bugfix/故障与修复记录-2026-11-01.md)。

首次整理完成于 2026-10-26，原始基线为 `04f910d`；随后按 `5bb415e` 及本次 Editor 工作区实现同步启动、UI、场景更新和顶点流缓存说明。首轮文档整理没有运行测试；Editor 调整的实际检查另见 [当日记录](../dev/bugfix/故障与修复记录-2026-10-26.md)。下文“已接入”来自实际调用方，“存在实现”不代表所有路径验证通过。

## 工程实现：performance

这里的 performance 按项目约定包含并发、内存和工程机制，并不表示已经做过性能优化或基准测试。

| 专题 | 要解决的阅读问题 |
| --- | --- |
| [任务池和渲染线程](performance/01-task-and-render-threads.md) | 谁执行 CPU 工作，等待什么，异常如何传播？ |
| [RHI 队列与同步](performance/02-rhi-queues-and-sync.md) | 录制、提交、worker 执行和 GPU 完成有什么区别？ |
| [RDG 执行与资源状态](performance/03-rdg-execution-and-state.md) | SceneView 扩展怎样添加 Pass，参数怎样变成依赖、屏障、物理资源和提交？ |
| [内存、资源池与瞬态资源](performance/04-memory-and-transient-resources.md) | 对齐、切分、复用、回收分别由谁负责？ |
| [资产、材质与实例数据](performance/05-assets-materials-and-instance-data.md) | 如何合并加载请求、维持文件/基础几何资产所有权、导入工程依赖和更新 GPU 数据？ |
| [Shader 编译与绑定](performance/06-shader-compilation-and-binding.md) | C++ 参数怎样进入 HLSL、反射和各后端绑定，布局缓存怎样保持 CPU 参数信息一致？ |
| [光追 AS 与 SBT](performance/07-ray-tracing-as-and-sbt.md) | AS 存储、scratch、group handle 和 SBT 怎样衔接？ |
| [UI 合成与窗口生命周期](performance/08-ui-rendering-and-window-lifetime.md) | 当前鼠标事件怎样分发，ImGui 怎样共用 RHI，纹理与窗口完成点怎样维护？ |
| [构建与模块启动](performance/09-module-build-and-startup.md) | 依赖准备、DLL 注册、拓扑启动和逆序关闭怎样串起来？ |
| [工程快照与二进制数据](performance/10-project-snapshots.md) | 如何保存运行时节点、层级及资源，脱离源文件恢复，并保持多文件一致和 GPU 状态？ |

## 原理与算法：algorithm

| 专题 | 要解决的阅读问题 |
| --- | --- |
| [射线求交与三角形距离](algorithm/01-ray-triangle-and-distance.md) | 屏幕射线怎样返回最近对象/材质槽，编辑器怎样优先拾取实体，t、重心坐标和最近点怎样求？ |
| [BVH、八叉树与场景查询](algorithm/02-bvh-octree-and-scene-query.md) | 如何分割、剪枝，候选集和精确命中有什么区别？ |
| [矩阵、视锥与深度](algorithm/03-transforms-frustum-and-depth.md) | NO/ZO 深度如何区分，方向球/变换工具怎样交互，网格怎样求可见地面 AABB、逐区细分并保持不透明 Alpha，光源球/锥/箭头怎样生成和拾取？ |
| [距离场与 Jump Flood](algorithm/04-distance-fields-and-jump-flood.md) | 有符号距离怎样生成、存储、合并和步进？ |
| [PBR、IBL 与采样](algorithm/05-pbr-ibl-and-sampling.md) | BRDF 项、采样 PDF、环境预滤波怎样对应？ |
| [阴影与级联](algorithm/06-shadow-mapping-and-cascades.md) | 光源投影、CSM 分割、Atlas 与 PCF 怎样衔接？ |
| [屏幕空间与抗锯齿](algorithm/07-screen-space-and-antialiasing.md) | HZB、SSR、SSGI、FXAA、TAA 当前做到哪一步？ |
| [网格导入与几何生成](algorithm/08-mesh-import-and-geometry.md) | 顶点访问、法线切线、实例索引和程序化网格怎样组织？ |
| [TextureViewer 画布与预览](algorithm/09-texture-viewer-canvas.md) | 多图片如何居中、选中、拖动和围绕鼠标缩放，网格如何分级，Cube/Mip/数组层如何显示？ |

## 如何阅读和核查

先读对应架构文档，再沿专题“源码入口 → 数据/公式 → 执行步骤 → 约束/限制 → 验证入口”查看。文档中的源码链接为相对仓库路径，关键函数名称供 IDE 搜索；不将易变行号当成永久定位方式。

[源码覆盖清单](source-coverage.md) 记录本次全量文件枚举和归类。范围包括自有 `src`、全部 `shaders`、构建脚本与现有文档；第三方 `deps`、生成文件、构建产物、二进制资产和 `temp` 中实验不作为自有实现逐项讲解。引用第三方的接口边界会记录，但不声称审计了 DXC、SPIRV-Cross、ImGui、tinygltf 或图形驱动内部算法。

清单表示检索覆盖，不等于对每行代码做了正确性审计。专题对核心调用链进行源码核对；普通知识、未接入的实验和已知缺口分别标注。此前运行结果仍查 [当日故障记录](../dev/bugfix/故障与修复记录-2026-10-25.md)，不能将其自动推广为本次文档中所有路径已通过测试。

文档检查包括：覆盖清单与实际文件集合一致、逐文件内容摘要核对、本文档集的本地链接解析、UTF-8 解码与差异空白检查。这些静态检查不替代编译、运行或算法数值验证。后续实现调整必须同步相关专题，当前 Editor 入口见 [架构说明](../architecture/app/editor.md)。

2026-10-29 补充了 Vulkan 管线布局缓存的 CPU 反射信息与复用条件；模型和方向轴共同绘制的故障、修复及实际验证范围见 [故障记录](../dev/bugfix/故障与修复记录-2026-10-29.md)。

2026-10-30 补充了局部缩放与旋转编辑工具、连续角度/扇形、Euler 转换和组件代理更新；模型独立读回、完整 Editor UI 与 Launcher 验证范围见 [故障记录](../dev/bugfix/故障与修复记录-2026-10-30.md)。

2026-10-31 补充了光源节点/代理更新、HintRender 的光源范围几何、XZ 网格可见区域 AABB 与单位网格细分、SceneSetting 控制提示绘制与编辑器依赖解耦、光源旋转/Direction 同步和单天空光注册约束，以及深度状态缓存字段抵消的复现与修复。随后将地面网格改为 100×100 控制顶点、逐块距离细分和远处透明度衰减，修复上传描述符提前 move 导致的依赖漏记和 BufferSRV 元数据分类，并验证 Immediate 只上传一次。后续又统一了 SceneView 屏幕射线、SceneInterface 最近几何查询与材质投放复用，补充光源图标命中和方向球黄色交互/中心标签。工程和算法细节分别见 [UI 生命周期](performance/08-ui-rendering-and-window-lifetime.md)、[射线拾取](algorithm/01-ray-triangle-and-distance.md)、[状态缓存](performance/06-shader-compilation-and-binding.md)、[提示几何](algorithm/03-transforms-frustum-and-depth.md)；真实 UI、三后端 Shader/缓存及 Launcher 的实际范围见 [故障记录](../dev/bugfix/故障与修复记录-2026-10-31.md)。

## 阅读时应特别留意的现状

以下是源码核对中需要开发者掌握的限制，本轮只记录，没有修改实现或新增运行验证：

- [队列同步](performance/02-rhi-queues-and-sync.md)：OpenGL 的逻辑完成值是 worker 执行完成，不能直接等同 GPU 完成；D3D12 对仅有 QueueType/Value 的等待有未兑现分支。
- [RDG 与资源回收](performance/03-rdg-execution-and-state.md)：Pass 析构已补齐，混合队列 Fence 更新和重叠子资源范围仍存在限制；[内存专题](performance/04-memory-and-transient-resources.md) 另外区分实际等待、延后删除与精确完成点回收。
- [几何与距离场](algorithm/04-distance-fields-and-jump-flood.md)：现有符号判断不严格等于奇偶规则，GPU mask/JFA 的数据契约也不能直接拼成已验证流程。
- [投影与屏幕算法](algorithm/03-transforms-frustum-and-depth.md)：NO/ZO 近面公式、深度金字塔 min/max 与实际深度约定需要联合核查；[屏幕效果](algorithm/07-screen-space-and-antialiasing.md) 中还标出了缺失 Shader 和未完整接入的路径。
- [网格导入](algorithm/08-mesh-import-and-geometry.md)：多 primitive、交错属性与切线 handedness 的支持存在边界，单模型显示正常不足以证明任意资产都可用。

维护时同步改专题和本索引：新增工程机制进 performance，新增数学/图形算法进 algorithm；总体职责改变则同时更新 architecture。不要为文档分类创造新的运行时概念或修改代码。

2026-10-31 后续补充光源范围/箭头拾取、查询过滤及平行光位置编辑，修正网格最后合成和不透明 Alpha。解析球/有限锥/有限射线与线段距离见 [矩阵和提示几何](algorithm/03-transforms-frustum-and-depth.md)，查询接口见 [射线拾取](algorithm/01-ray-triangle-and-distance.md)，混合状态与真实三后端读回、完整编辑器验证见 [当日记录](../dev/bugfix/故障与修复记录-2026-10-31.md)。
