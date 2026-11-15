# VirtualGeometryViewer 使用说明

## 模块和入口

模块位于 [src/Editor/VirtualGeometryViewer](../../src/Editor/VirtualGeometryViewer/CMakeLists.txt)，依赖 Engine、ImGuiSlate 和 Renderer。Editor 只组装窗口，SceneEditor 的 View Ribbon 通过回调打开窗口，不直接依赖 Viewer 或 Renderer。apps.json 显式声明 Viewer 对 Renderer 的启动依赖。

打开工程并创建或导入一个网格节点后，点击 View → Virtual geometry viewer，默认选择当前网格节点，Scene mesh 下拉框可切换到其他网格资产。与 MeshViewer 一样，这是主窗口内的浮动 ImGui 窗口，不创建独立原生窗口或图形上下文。

## 预览和信息

- 默认 Cluster 着色，使用真实 Visibility Buffer 和 Draw Metadata 映射颜色。
- 默认启用 Dynamic LOD 和 GPU selection + indirect draw，Target pixel error 默认 1 像素，0 为完整叶 Cut，随后可按 GPU frustum culling 开关剔除不可见 Cluster。相机环绕、缩放、预览分辨率或预算变化后 GPU 重新选择；取消 GPU selection 后使用原 CPU 参考选择。
- Capture GPU / compare CPU 显式抓取当前完整 Cut 数、可见/剔除数量、三角形数量和 CPU Cut 差异数。GPU 模式的信息表显示该次抓取的可见 Cluster 快照，不自动每帧读回；相机或参数变化后需重新抓取。临界预算可能有 FP32/CPU double 舍入差异，界面如实显示，不静默回退。CPU 模式的信息表仍显示当前帧选择。
- 关闭 Dynamic LOD 后，Generated level 选择该层生成的 meshlet，0 为完整初始细节。较高层不包含所有停止简化的旧 Cluster，可能不能覆盖整个网格；该选项用于诊断生成数据，界面明确提示不是完整 DAG Cut。
- Material + deferred lighting 使用 Visibility → 属性重建 → 材质求值 → GBuffer A/B/C → 既有 DeferredShadingCS，预览场景提供一个不投影的方向光，没有 IBL 或主场景光源。
- Wireframe 默认关闭，开启后在 Cluster 或材质预览上叠加深灰色三角形边线。边线使用本帧实际选中的 Cluster，随动态 LOD 或生成层检查变化，不绘制原始 LOD0 的另一套网格。
- GPU 动态模式提供 Raster path：Hardware、Hybrid、Software preferred，默认 Hybrid，SW cluster pixels 默认 32，范围 1～128 像素。后者是 Cluster 包围球的投影直径上限；Software preferred 使用 128 的硬上限，触及裁剪边界或超出限制仍明确回退硬件。CPU 选择和 Generated level 检查强制使用原硬件路径。
- Capture 同时抓取 HW/SW Cluster、强制回退 Cluster 和 SW 三角形数。切换路径后需要重新 Capture；统计是当前快照，不是持续刷新或通过深度测试的像素数。Wireframe 使用原完整选择参数并对合并后的深度测试，两条路径都覆盖。
- Measure completed frame 按需测量一次完整预览的 CPU 构图、提交和队列完成等待；它不是 GPU timestamp 或单个 Raster Pass 时间。OpenGL 仅保证 worker 执行完成，界面明确标注不能当作 GPU 完成计时。Capture 和计时若同时请求，分别在两帧执行，正常帧不增加计时等待。
- Geometry streaming 默认开启，仅在 GPU Dynamic LOD 下生效；Cache budget MiB 默认 16。显示驻留/总页、Pinned、加载页数、Cache 申请字节/预算、唯一 Page 命中率、压缩载荷/原载荷、加载/淘汰/失败/预算阻塞和 I/O+解码累计时间。预算包含该资源的固定几何池和持久元数据，未计 GPU 附件、临时选择、驱动对齐和 staging。粗 Root 页必须装得下，否则明确报错。
- 缺页或预算不足时保留合法粗 Cut，达到目标精度可能要等待多帧；低预算不保证最终达到 Target pixel error。Capture 的 CPU 对比在流送开启时使用同一驻留约束下的合法 Cut 参考，而非要求与尚未驻留的理想细层相同。关闭流送、切到 CPU 或 Generated level 才恢复原全量 GPU 缓冲。
- 显示 Cluster、Group、Root 总数和最大累计误差，以及当前层的 Cluster 和三角形数。表格显示各 Cluster 的顶点数、三角形数、材质槽、Group、GeneratingGroup 和包围球半径；行选择仅选择信息行，不改变光栅输出。
- 图片上右键拖动环绕，滚轮缩放。Rebuild 重新构建该预览副本。

材质槽使用 StaticMesh 资产的材质，缺失槽使用 Viewer 自有的默认 Lit 材质。第一版不读取场景组件的材质实例覆盖，仅支持当前 Resolve 支持的 Opaque Surface；没有新增自发光附件或独立材质系统。

## 所有权与时序

Editor 在窗口 Draw 前调用 Tick，从当前场景复制名称和资产引用。选择变化或点击 Rebuild 时，同步调用 VirtualGeometryResource::Build，并将结果写入 Viewer 自有 StaticMeshRenderData 的 VirtualGeometryData，再 InitializeResources。原资产的传统 LOD 和虚拟几何数据均不重建、不覆盖；大模型 CPU 构建会阻塞这次界面 Tick，当前没有后台构建任务。

Render 使用资源包围盒将网格归一化并放在原点，生成独立 SceneView；默认由 Raster 编排 GPU Group 选择、Cluster 视锥剔除和间接绘制，误差投影同时考虑归一化实例矩阵、相机和预览分辨率。GPU Metadata 包含全部候选，不能直接当选中列表；只有显式 Capture 才读回 Flags/Statistics 并更新信息表。CPU 模式从 Draw Metadata 保存选中列表。Cluster 模式调用 Debug Pass，材质模式调用 Resolve 和原光照封装。独立 Scene 只持有预览灯及其参数，未向主场景注册预览网格，也未修改主场景 Renderer 的编排。

预览拥有颜色目标、派生几何和默认材质，借由资产引用保持输入所有者。RDG 在实际提交处维护几何缓冲与颜色目标的访问 Fence，ImGui 后端在采样处继续更新颜色目标的 Fence。窗口关闭、项目重置、网格切换、重建或尺寸变化时，先按所用资源的最后 Fence 等待，再释放或替换；几何释放也检查上传 Fence，不使用 WaitIdle 或全局保活列表。

Compute 材质采样使用显式参数传递解析梯度，细节见 [Material Resolve](../key_implemetion/algorithm/12-virtual-geometry-material-resolve.md)。软件/混合路径最终仍输出同一 Visibility 和 D32，原 Resolve 与光照无需识别来源；两阶段深度/ID 归约及合并见 [软件光栅化](../key_implemetion/algorithm/14-virtual-geometry-software-rasterization.md)。独立光照复用既有阴影访问表与级联参数准备函数，即使方向光关闭投影也创建绑定所需的合法参数；无 IBL 时跳过环境绑定，避免依赖未初始化的 LUT。

动态选择的 Group 替换、GPU 依赖调度、投影公式和边界条件见 [LOD Selection](../key_implemetion/algorithm/13-virtual-geometry-lod-selection.md)。预算使用 Build 的累计采样误差，不能视为严格几何误差上界；没有几何渐变或切换滞回。

线框在预览颜色输出后追加一个 LineList Pass，复用 VirtualGeometryVertexFactory 的 PositionBuffer、局部到世界和视图矩阵；由三角形局部索引加 VertexOffset 生成临时线段索引，每面六个 uint32，共享边可能重复提交。CPU 模式只生成选中三角形。GPU 模式为候选生成边索引，再用一个 Compute Pass 将 GPU 三角形间接参数的 IndexCount/FirstIndex 乘 2、VertexOffset 置零，按同一选择结果间接绘制线段；没有为线框另跑 CPU LOD。流送时只填充驻留页，线索引按物理 IndexOffset 放置，顶点加物理 VertexOffset，数组上限为 PageSlots×1536×2，不再按全量逻辑索引地址上传。未开启流送时仍按全部候选生成，CPU 构造与上传为 O(全部候选面数)，每面 24 字节，GPU 模式另有每 Cluster 20 字节参数。关闭线框不生成这些数据或 Pass，不依赖原生 PolygonMode::Line。

Pass 对颜色和现有 Visibility Depth 均为 Load/Store，声明顶点/索引访问并沿用 RDG 上传依赖。深度比较为 LessOrEqual、关闭深度写入，VS 仅对线框施加 0.00001 的 NDC 近向偏移来缓解重合深度闪烁；它不会改写原几何、Visibility 或 GBuffer。参数与上传向量属于本次 Builder，几何仍由 Preview 持有，访问 Fence 由实际 Pass 提交更新，沿用现有释放完成点。

## 验证范围

Material Resolve 和 Raster 的现有 GPU 测试在 Vulkan、DirectX12、OpenGL 通过，未修改测试输入或像素期望。另在忽略目录构建诊断程序，直接编译调用本模块 VirtualGeometryPreview.cpp，验证 Vulkan 第 0、1 层的 Cluster 与材质光照模式，并读回颜色和检查源网格未被写入。

后续使用工程快照中的 559 顶点、960 面球体补充检查，在 Vulkan 下读回六个生成层的 Cluster 和材质图像。Unlit 对照中，六层的 Cluster 与 Resolve 覆盖掩码逐像素一致，工程文件哈希保持不变。原 Lit 最粗层明显偏暗，且 34 面粗层本身已严重失去球形轮廓；这份输入不能据此声称最高生成层具有可接受的 LOD 质量，仍需完整切面与屏幕误差约束。该诊断只恢复保存的网格、材质数据，没有执行整个工程的 GUI 加载。

2026-11-15 补充 CPU/GPU 对照：使用同一份 559 顶点、960 面球体快照，在 0/0.5/1/4 像素预算下，Vulkan、DirectX12 的 Cluster/材质及线框开关、OpenGL 的 Cluster 及线框开关，共 40 对离屏图像逐像素一致；所有 GPU Capture 的完整 Cut 与 CPU 差异为零，原工程文件哈希未变。OpenGL 的独立光照预览仍未纳入这组检查，不能据此声称已验证；三后端的 GPU Material Resolve 像素测试另行通过。复杂模型和全部生成层的完整画面对照仍未完成。

Launcher 已编译并启动的历史首帧范围及 Computer Use 原生连接限制见 [2026-11-14 记录](../dev/bugfix/故障与修复记录-2026-11-14.md)，GPU 阶段的实际验证见 [2026-11-15 记录](../dev/bugfix/故障与修复记录-2026-11-15.md)。离屏读回不等同于真实按钮点击、拖动和浮窗交互验证。

同日软件光栅复测覆盖三后端三种 Raster 模式，默认 Hybrid 的 40 对球体图像与 Hardware 逐像素一致；SoftwarePreferred 的实心图像一致，但 Vulkan/D3D12 的原生线框叠加仍有 1～3 个边缘像素精度差异。完成帧测量在该小输入下没有显示软件路径速度收益，且没有 GPU 单阶段 timestamp；具体输入、计数、实测表格及未定位的偶发 Vulkan 诊断失败见 [软件光栅记录](../dev/bugfix/故障与修复记录-2026-11-15.md#virtual-geometry-gpu-software-rasterization)。
