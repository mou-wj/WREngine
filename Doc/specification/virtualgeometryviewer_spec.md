# VirtualGeometryViewer 使用说明

## 模块和入口

模块位于 [src/Editor/VirtualGeometryViewer](../../src/Editor/VirtualGeometryViewer/CMakeLists.txt)，依赖 Engine、ImGuiSlate 和 Renderer。Editor 只组装窗口，SceneEditor 的 View Ribbon 通过回调打开窗口，不直接依赖 Viewer 或 Renderer。apps.json 显式声明 Viewer 对 Renderer 的启动依赖。

打开工程并创建或导入一个网格节点后，点击 View → Virtual geometry viewer，默认选择当前网格节点，Scene mesh 下拉框可切换到其他网格资产。与 MeshViewer 一样，这是主窗口内的浮动 ImGui 窗口，不创建独立原生窗口或图形上下文。

## 预览和信息

- 默认 Cluster 着色，使用真实 Visibility Buffer 和 Draw Metadata 映射颜色。
- 默认启用 Dynamic LOD，Target pixel error 控制目标误差，默认 1 像素，0 为完整叶 Cut。相机环绕、缩放、预览分辨率或预算变化后重新选择；列表与统计显示本帧实际渲染的混合层级 Cluster。
- 关闭 Dynamic LOD 后，Generated level 选择该层生成的 meshlet，0 为完整初始细节。较高层不包含所有停止简化的旧 Cluster，可能不能覆盖整个网格；该选项用于诊断生成数据，界面明确提示不是完整 DAG Cut。
- Material + deferred lighting 使用 Visibility → 属性重建 → 材质求值 → GBuffer A/B/C → 既有 DeferredShadingCS，预览场景提供一个不投影的方向光，没有 IBL 或主场景光源。
- Wireframe 默认关闭，开启后在 Cluster 或材质预览上叠加深灰色三角形边线。边线使用本帧实际选中的 Cluster，随动态 LOD 或生成层检查变化，不绘制原始 LOD0 的另一套网格。
- 显示 Cluster、Group、Root 总数和最大累计误差，以及当前层的 Cluster 和三角形数。表格显示各 Cluster 的顶点数、三角形数、材质槽、Group、GeneratingGroup 和包围球半径；行选择仅选择信息行，不改变光栅输出。
- 图片上右键拖动环绕，滚轮缩放。Rebuild 重新构建该预览副本。

材质槽使用 StaticMesh 资产的材质，缺失槽使用 Viewer 自有的默认 Lit 材质。第一版不读取场景组件的材质实例覆盖，仅支持当前 Resolve 支持的 Opaque Surface；没有新增自发光附件或独立材质系统。

## 所有权与时序

Editor 在窗口 Draw 前调用 Tick，从当前场景复制名称和资产引用。选择变化或点击 Rebuild 时，同步调用 VirtualGeometryResource::Build，并将结果写入 Viewer 自有 StaticMeshRenderData 的 VirtualGeometryData，再 InitializeResources。原资产的传统 LOD 和虚拟几何数据均不重建、不覆盖；大模型 CPU 构建会阻塞这次界面 Tick，当前没有后台构建任务。

Render 使用资源包围盒将网格归一化并放在原点，生成独立 SceneView；默认由 Raster 调用 CPU 选择器，误差投影同时考虑归一化实例矩阵、相机和预览分辨率。预览从本次 Draw Metadata 保存实际 ClusterID 列表用于界面显示，不重新推断一个统一 Mip。Cluster 模式调用 Debug Pass，材质模式调用 Resolve 和原光照封装。独立 Scene 只持有预览灯及其参数，未向主场景注册预览网格，也未修改主场景 Renderer 的编排。

预览拥有颜色目标、派生几何和默认材质，借由资产引用保持输入所有者。RDG 在实际提交处维护几何缓冲与颜色目标的访问 Fence，ImGui 后端在采样处继续更新颜色目标的 Fence。窗口关闭、项目重置、网格切换、重建或尺寸变化时，先按所用资源的最后 Fence 等待，再释放或替换；几何释放也检查上传 Fence，不使用 WaitIdle 或全局保活列表。

Compute 材质采样使用显式参数传递解析梯度，细节见 [Material Resolve](../key_implemetion/algorithm/12-virtual-geometry-material-resolve.md)。独立光照复用既有阴影访问表与级联参数准备函数，即使方向光关闭投影也创建绑定所需的合法参数；无 IBL 时跳过环境绑定，避免依赖未初始化的 LUT。

动态选择的 Group 替换、投影公式和边界条件见 [CPU LOD Selection](../key_implemetion/algorithm/13-virtual-geometry-lod-selection.md)。预算使用 Build 的累计采样误差，不能视为严格几何误差上界；没有几何渐变或切换滞回。

线框在预览颜色输出后追加一个 LineList Pass，复用 VirtualGeometryVertexFactory 的 PositionBuffer、局部到世界和视图矩阵；由选中三角形的局部索引加 VertexOffset 生成临时线段索引，每个三角形六个 uint32 索引，共享边可能重复提交。开启时 CPU 构造和上传成本为 O(选中三角形数)，索引数据为每面 24 字节；关闭时不生成索引或追加此 Pass。不使用原生 PolygonMode::Line，因而不依赖 Vulkan 的非实心多边形特性。

Pass 对颜色和现有 Visibility Depth 均为 Load/Store，声明顶点/索引访问并沿用 RDG 上传依赖。深度比较为 LessOrEqual、关闭深度写入，VS 仅对线框施加 0.00001 的 NDC 近向偏移来缓解重合深度闪烁；它不会改写原几何、Visibility 或 GBuffer。参数与上传向量属于本次 Builder，几何仍由 Preview 持有，访问 Fence 由实际 Pass 提交更新，沿用现有释放完成点。

## 验证范围

Material Resolve 和 Raster 的现有 GPU 测试在 Vulkan、DirectX12、OpenGL 通过，未修改测试输入或像素期望。另在忽略目录构建诊断程序，直接编译调用本模块 VirtualGeometryPreview.cpp，验证 Vulkan 第 0、1 层的 Cluster 与材质光照模式，并读回颜色和检查源网格未被写入。

后续使用工程快照中的 559 顶点、960 面球体补充检查，在 Vulkan 下读回六个生成层的 Cluster 和材质图像。Unlit 对照中，六层的 Cluster 与 Resolve 覆盖掩码逐像素一致，工程文件哈希保持不变。原 Lit 最粗层明显偏暗，且 34 面粗层本身已严重失去球形轮廓；这份输入不能据此声称最高生成层具有可接受的 LOD 质量，仍需完整切面与屏幕误差约束。该诊断只恢复保存的网格、材质数据，没有执行整个工程的 GUI 加载。

Launcher 已编译并启动，确认 Editor 首帧。由于本机 Computer Use 原生连接不可用，未完成真实按钮点击、拖动和浮窗画面检查，不将离屏读回说成 GUI 验证。复杂模型、多材质和全层级的完整画面对照，以及 DirectX12/OpenGL 下的 Viewer 光照预览，仍未验证。详细结果与故障原因见 [开发记录](../dev/bugfix/故障与修复记录-2026-11-14.md)。
