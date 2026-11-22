# VolumeViewer

在编辑器的 View 栏选择 **Volume viewer**。窗口内置地面、球体、两个遮挡物以及平行光、点光和聚光，可以在未打开工程时使用。视图支持右键环绕、中键平移、滚轮缩放和 **Reset camera**。关闭窗口后可从 View 栏重新打开。同一应用的主视口和 GIViewer 各自拥有独立的渲染输入。

预览场景使用 Engine 的基础网格和灯光组件。VolumeViewer 持有组件、资产引用、相机及颜色目标，经既有 RenderInterface 构建延迟渲染图。左侧设置栏独立滚动，右侧预览使用窗口分配给它的空间，展开设置只改变左栏内容。颜色目标按右侧预览尺寸分配，按比例限制在 1280×720 以内。ImGui 在图形提交后采样这个纹理。关闭帧先停止提交预览图片，关闭或调整尺寸时才按纹理记录的最后访问 Fence 等待，再释放或替换目标。

基础预览使用既有的 GBuffer、场景深度、表面光照和颜色 Pass。测试场景及参数由界面模块持有，介质参数的类型由 Renderer 的 Volume 目录定义。 [预览入口](../../src/Editor/VolumeViewer/Private/EditorVolumeViewer.cpp) · [介质参数](../../src/Framework/Renderer/Private/Volume/VolumeRendering.h)

## 体积调试

勾选 **Enable volume** 以显示参与介质，取消勾选可查看原始场景。**Composite** 显示透射衰减后的背景与散射、自发光的合成。**Scattering** 和 **Transmittance** 分别显示积分辐射和 RGB 透射率。**Density** 与 **Froxels** 使用 **Slice** 查看指定深度层。**Medium** 可编辑介质范围、散射、吸收和自发光。 [积分与深度](../key_implemetion/algorithm/23-volume-rendering.md)

**Direct lighting** 显示直接体积散射。`Lights` 中可以单独启用各类光源对雾的贡献，并调整测试光强。`Medium` 中的 **Anisotropy** 控制相函数。 [单次散射](../key_implemetion/algorithm/23-volume-rendering.md#单次散射与光源注入)

**Light visibility** 显示所选深度层内光源的可见程度，黑色表示被遮挡。**Volume shadows** 只控制体积光是否读取现有阴影贴图，适合与 **Direct lighting** 配合对照。 [阴影坐标与比较](../key_implemetion/algorithm/23-volume-rendering.md#光源可见性)

**Local volumes** 初始包含一个盒体和一个椭球，可新增、删除及编辑变换与介质。**Local overlap** 按覆盖数量着色，白线标记该切片中数量变化的边界。**Density** 显示局部与全局密度之和。 [覆盖与叠加规则](../key_implemetion/algorithm/23-volume-rendering.md#局部介质)

初始盒体使用程序噪声，椭球使用内置 16³ 密度纹理。每个局部介质可通过 **Density source** 切换来源，编辑坐标偏移、速度、密度缩放和阈值。密度场初始静止，勾选 **Animate density** 后按设置的速度移动。**Global density** 只控制全局介质。选择 **Density sources** 并移动 **Slice**，红、绿、蓝分别表示噪声、均匀介质和纹理的密度。多种源覆盖同一个 Froxel 时会混合显示。 [密度定义](../key_implemetion/algorithm/23-volume-rendering.md#非均匀密度)

**Indirect lighting** 显示体积中的低频环境散射。可切换 **Use sky environment** 和 **Sky intensity**，天空输入来自预览场景的现有 IBL 环境。主场景需要显式将 `MakeVolumeDrawExtension` 注册到符合输入要求的 SceneView。 [合成约定](../key_implemetion/algorithm/23-volume-rendering.md#天空环境与合成位置)

### 画质与耗时对照

**Quality and timing** 可独立设置 Tile 像素宽度、深度层数、稀疏重建、Froxel 抖动及最大历史权重。两个预设会恢复相同的相机、积分配置和密度动画时间，并固定 Froxel 采样位置，以便对比。**Reference preset** 使用逐像素积分。**Optimized preset** 使用稀疏积分和时域重投影。需要观察逐帧采样效果时可勾选 **Froxel jitter**。**History weight** 以灰度显示混合权重。黑色可表示重置、遮挡变化，或该像素回退到本帧精确积分。

面板列出 Froxel 维度、主积分射线网格及体积纹理逻辑容量。**Measure completed frame** 测量一次预览的 CPU 提交和队列等待，可能包含场景着色、同步及驱动开销。OpenGL 的队列完成点表示工作线程处理完成。Vulkan 的独立基准提供 GPU 时间戳采样。 [历史与重建实现](../key_implemetion/algorithm/23-volume-rendering.md#稀疏积分和深度感知重建)

## 大气预览

在左侧展开 **Atmosphere** 并勾选 **Enable atmosphere**。**Sun elevation** 和 **Sun azimuth** 同时改变天空采样和预览场景的主定向光。**Aerial perspective** 控制几何上的雾化和透射。场景太小时可调大 **Km / scene unit** 来观察距离差异。**In-scattering** 和 **Transmittance** 分别查看大气自身的辐射和透射率。 [大气计算](../key_implemetion/algorithm/24-atmosphere-and-water.md#大气光学模型)

## 验证范围

隔离 Debug 构建验证了模块链接、Launcher 启动及 VolumeViewer。展开画质和局部介质控件时，右侧边界和主积分尺寸保持一致。固定相机时，多次截图中的默认雾形和光斑位置一致。以下像素测试另外验证了预览使用的实际渲染链路。Launcher 日志仍有已有的 Vulkan Shader 模块诊断。

`Tests.exe -test=VolumeRenderTest -rhi=<Vulkan|DirectX12|OpenGL>` 读回 2 和 10 世界单位深度的透射率分别约为 0.683861、0.138070，天空到 20 单位的透射率约为 0.018686。三个后端正常退出，并通过 RGB 消光、合成、清除密度和零消光自发光检查。

同一入口的三光源用例读回平行光各向同性红通道 0.330751，正向散射参数 0.55 时升至 1.77591。点光范围外为零。聚光锥内蓝通道 0.49275，锥外为零。

阴影用例注入已知深度的 CSM 数组层、点光 Cube 和聚光 Atlas。三后端均在三类光源同时存在时读回 RGB `(0, 0, 0)`，并验证关闭阴影恢复原光照以及消光保持一致。

局部介质用例的盒体、椭球与全局雾叠加透射率为 0.094419。三后端均验证重叠计数、局部直接光和阴影响应、删除椭球及将盒体移出视野。

非均匀密度用例在三个后端均读回常量 3D 密度纹理的透射率 0.464403，与 `exp(-0.13 × 5.9)` 一致。均匀、噪声及两张纹理同时参与时，噪声与纹理调试通道的最大时间差约为 0.1454、0.0759，均匀通道保持稳定。中心像素透射率从 0.0350 变到 0.0271，并验证非均匀介质会散射直接光、受其阴影遮挡。

时空用例中，三个后端均以稀疏重建保留落在采样点之间的细前景，读回透射率约 0.68386。相机平移时保留历史，新遮挡拒绝历史，改变介质参数、实际光源强度和场景版本均重置历史。静止噪声介质以相同抖动序列测量，17×9 像素用例的帧间透射率变化 RMS 从 0.03334 降为 0.00381。

`Tests.exe -test=VolumeAtmosphereTest -rhi=<Vulkan|DirectX12|OpenGL>` 在三个后端均正常退出。白天高空蓝通道约为 0.247，太阳降至地平线附近时红通道约为 1.783、蓝通道约为 0.015。测试还对照了行星遮住太阳、禁用大气及关闭几何大气透视时的输出。几何位置从 2 个场景单位移到 8 个时，蓝光透射率由约 0.970 降为 0.887。

`VolumePipelineTest -rhi=Vulkan` 通过实际延迟渲染、网格、三种灯光、场景阴影、天空和 3D 局部密度纹理验证离屏合成。参考合成中心红通道约 0.470699，优化合成约 0.471276。稳定的第二帧历史权重为 0.85，释放历史后的帧为 0。关闭 IBL 后间接散射降为零，未注册扩展时的前景读回保持一致。测试正常退出，完整 Renderer 初始化仍输出已有 SPIR-V 校验及旧 Shader 编译诊断。

### 固定场景性能

在 Debug 构建目录中运行 `Tests.exe -test=VolumeBenchmarkReference -rhi=Vulkan` 与 `Tests.exe -test=VolumeBenchmarkOptimized -rhi=Vulkan`。在名称末尾加 `1080p` 可以选择 1920×1080。两路径使用相同的相机、介质、遮挡区、Tile 大小和积分层数。独立优化基准启用 Froxel 抖动。预热 8 帧后取 72 帧均值，重复运行三次，下表取中位数。Vulkan 的图形和计算指向同一原生队列时，从整个体积图提交的前后采样硬件时间戳。跨度包含提交间的空闲，适合对照这两种完整路径。

| 分辨率 | Vulkan 队列跨度（参考 → 优化） | 主积分射线（参考 → 优化） | 临时与历史纹理合计（参考 → 优化） |
| --- | --- | --- | --- |
| 960×540 | 4.69 → 4.65 ms，差异处于本轮波动内 | 518,400 → 129,600 | 39.7 → 31.8 MiB |
| 1920×1080 | 5.45 → 4.96 ms，约减少 9% | 2,073,600 → 518,400 | 158.2 → 126.6 MiB |

射线计数只包含主积分，部分轮廓像素还会精确积分。容量为体积图的逻辑纹理总和，实际峰值受资源复用和驱动分配影响。 [基准代码](../../src/Tests/VolumeRenderTest.cpp)
