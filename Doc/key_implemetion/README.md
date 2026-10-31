# 核心实现阅读指南

本目录回答“具体怎样实现、依赖哪些条件、哪里尚不可靠”。模块职责与接口关系仍以 [architecture](../architecture/README.md) 的既有结构组织。目录名沿用需求首次指定的 `key_implemetion`。

首次整理完成于 2026-10-26，原始基线为 `04f910d`；随后按 `5bb415e` 及本次 Editor 工作区实现同步启动、UI、场景更新和顶点流缓存说明。首轮文档整理没有运行测试；Editor 调整的实际检查另见 [当日记录](../dev/bugfix/故障与修复记录-2026-10-26.md)。下文“已接入”来自实际调用方，“存在实现”不代表所有路径验证通过。

## 工程实现：performance

这里的 performance 按项目约定包含并发、内存和工程机制，并不表示已经做过性能优化或基准测试。

| 专题 | 要解决的阅读问题 |
| --- | --- |
| [任务池和渲染线程](performance/01-task-and-render-threads.md) | 谁执行 CPU 工作，等待什么，异常如何传播？ |
| [RHI 队列与同步](performance/02-rhi-queues-and-sync.md) | 录制、提交、worker 执行和 GPU 完成有什么区别？ |
| [RDG 执行与资源状态](performance/03-rdg-execution-and-state.md) | SceneView 扩展怎样添加 Pass，参数怎样变成依赖、屏障、物理资源和提交？ |
| [内存、资源池与瞬态资源](performance/04-memory-and-transient-resources.md) | 对齐、切分、复用、回收分别由谁负责？ |
| [资产、材质与实例数据](performance/05-assets-materials-and-instance-data.md) | 如何合并加载请求、维持所有权、导入工程依赖和更新 GPU 数据？ |
| [Shader 编译与绑定](performance/06-shader-compilation-and-binding.md) | C++ 参数怎样进入 HLSL、反射和各后端绑定，布局缓存怎样保持 CPU 参数信息一致？ |
| [光追 AS 与 SBT](performance/07-ray-tracing-as-and-sbt.md) | AS 存储、scratch、group handle 和 SBT 怎样衔接？ |
| [UI 合成与窗口生命周期](performance/08-ui-rendering-and-window-lifetime.md) | 当前鼠标事件怎样分发，ImGui 怎样共用 RHI，纹理与窗口完成点怎样维护？ |
| [构建与模块启动](performance/09-module-build-and-startup.md) | 依赖准备、DLL 注册、拓扑启动和逆序关闭怎样串起来？ |

## 原理与算法：algorithm

| 专题 | 要解决的阅读问题 |
| --- | --- |
| [射线求交与三角形距离](algorithm/01-ray-triangle-and-distance.md) | 怎样求 t、重心坐标和最近点，视口投放怎样选择最近网格与材质槽？ |
| [BVH、八叉树与场景查询](algorithm/02-bvh-octree-and-scene-query.md) | 如何分割、剪枝，候选集和精确命中有什么区别？ |
| [矩阵、视锥与深度](algorithm/03-transforms-frustum-and-depth.md) | NO/ZO 深度如何区分，方向球/变换工具怎样交互，网格怎样求可见地面 AABB 并细分，光源范围怎样生成？ |
| [距离场与 Jump Flood](algorithm/04-distance-fields-and-jump-flood.md) | 有符号距离怎样生成、存储、合并和步进？ |
| [PBR、IBL 与采样](algorithm/05-pbr-ibl-and-sampling.md) | BRDF 项、采样 PDF、环境预滤波怎样对应？ |
| [阴影与级联](algorithm/06-shadow-mapping-and-cascades.md) | 光源投影、CSM 分割、Atlas 与 PCF 怎样衔接？ |
| [屏幕空间与抗锯齿](algorithm/07-screen-space-and-antialiasing.md) | HZB、SSR、SSGI、FXAA、TAA 当前做到哪一步？ |
| [网格导入与几何生成](algorithm/08-mesh-import-and-geometry.md) | 顶点访问、法线切线、实例索引和程序化网格怎样组织？ |

## 如何阅读和核查

先读对应架构文档，再沿专题“源码入口 → 数据/公式 → 执行步骤 → 约束/限制 → 验证入口”查看。文档中的源码链接为相对仓库路径，关键函数名称供 IDE 搜索；不将易变行号当成永久定位方式。

[源码覆盖清单](source-coverage.md) 记录本次全量文件枚举和归类。范围包括自有 `src`、全部 `shaders`、构建脚本与现有文档；第三方 `deps`、生成文件、构建产物、二进制资产和 `temp` 中实验不作为自有实现逐项讲解。引用第三方的接口边界会记录，但不声称审计了 DXC、SPIRV-Cross、ImGui、tinygltf 或图形驱动内部算法。

清单表示检索覆盖，不等于对每行代码做了正确性审计。专题对核心调用链进行源码核对；普通知识、未接入的实验和已知缺口分别标注。此前运行结果仍查 [当日故障记录](../dev/bugfix/故障与修复记录-2026-10-25.md)，不能将其自动推广为本次文档中所有路径已通过测试。

文档检查包括：覆盖清单与实际文件集合一致、逐文件内容摘要核对、本文档集的本地链接解析、UTF-8 解码与差异空白检查。这些静态检查不替代编译、运行或算法数值验证。后续实现调整必须同步相关专题，当前 Editor 入口见 [架构说明](../architecture/app/editor.md)。

2026-10-29 补充了 Vulkan 管线布局缓存的 CPU 反射信息与复用条件；模型和方向轴共同绘制的故障、修复及实际验证范围见 [故障记录](../dev/bugfix/故障与修复记录-2026-10-29.md)。

2026-10-30 补充了局部缩放与旋转编辑工具、连续角度/扇形、Euler 转换和组件代理更新；模型独立读回、完整 Editor UI 与 Launcher 验证范围见 [故障记录](../dev/bugfix/故障与修复记录-2026-10-30.md)。

2026-10-31 补充了光源节点/代理更新、HintRender 的光源范围几何、XZ 网格可见区域 AABB 与单位网格细分、SceneSetting 控制提示绘制与编辑器依赖解耦、光源旋转/Direction 同步和单天空光注册约束，以及深度状态缓存字段抵消的复现与修复。工程和算法细节分别见 [UI 生命周期](performance/08-ui-rendering-and-window-lifetime.md)、[状态缓存](performance/06-shader-compilation-and-binding.md)、[提示几何](algorithm/03-transforms-frustum-and-depth.md)；真实 UI、三后端 Shader/缓存及 Launcher 的实际范围见 [故障记录](../dev/bugfix/故障与修复记录-2026-10-31.md)。

## 阅读时应特别留意的现状

以下是源码核对中需要开发者掌握的限制，本轮只记录，没有修改实现或新增运行验证：

- [队列同步](performance/02-rhi-queues-and-sync.md)：OpenGL 的逻辑完成值是 worker 执行完成，不能直接等同 GPU 完成；D3D12 对仅有 QueueType/Value 的等待有未兑现分支。
- [RDG 与资源回收](performance/03-rdg-execution-and-state.md)：Pass 裸指针释放、混合队列 Fence 更新和重叠子资源范围存在限制；[内存专题](performance/04-memory-and-transient-resources.md) 另外区分实际等待、延后删除与精确完成点回收。
- [几何与距离场](algorithm/04-distance-fields-and-jump-flood.md)：现有符号判断不严格等于奇偶规则，GPU mask/JFA 的数据契约也不能直接拼成已验证流程。
- [投影与屏幕算法](algorithm/03-transforms-frustum-and-depth.md)：NO/ZO 近面公式、深度金字塔 min/max 与实际深度约定需要联合核查；[屏幕效果](algorithm/07-screen-space-and-antialiasing.md) 中还标出了缺失 Shader 和未完整接入的路径。
- [网格导入](algorithm/08-mesh-import-and-geometry.md)：多 primitive、交错属性与切线 handedness 的支持存在边界，单模型显示正常不足以证明任意资产都可用。

维护时同步改专题和本索引：新增工程机制进 performance，新增数学/图形算法进 algorithm；总体职责改变则同时更新 architecture。不要为文档分类创造新的运行时概念或修改代码。
