# HZB、SSR、SSGI 与抗锯齿

入口：[DepthPyramidGenerate.cpp](../../../src/Framework/Renderer/Private/DepthPyramidGenerate.cpp)、[HZBOcclusionTest.cpp](../../../src/Framework/Renderer/Private/HZBOcclusionTest.cpp)、[ScreenSpaceReflection.cpp](../../../src/Framework/Renderer/Private/ScreenSpaceReflection.cpp)、[ScreenSpaceGI.cpp](../../../src/Framework/Renderer/Private/GI/ScreenSpaceGI.cpp)、[AntiAliasingPostProcess.cpp](../../../src/Framework/Renderer/Private/PostProcess/AntiAliasingPostProcess.cpp)。架构：[Renderer](../../architecture/render/renderer.md)。

## 先区分是否接入

当前 DefferedSceneRenderer 的尾部构建直接光、IBL、深度金字塔和 SSGI，最后按条件绘制天空。独立 SSR、GPU HZB 和 AA 类存在，不等于都在默认渲染链启用。

HZBOcclusionTestCS 引用 `/tools/HZBOcclusionTestCS.sf`，独立 SSR 的深度分支引用 `/tools/ScreenSpaceReflectionDepthPyramidCS.sf`；本次文件盘点未找到这两个 Shader。它们属于缺失实现/依赖。taaTAA 在打开时域超分时由主链调度，FxaaCS 保留独立 Shader。

## 深度金字塔的降采样

[DepthPyramidGenerateCS.sf](../../../shaders/tools/DepthPyramidGenerateCS.sf) mip0 从外部 depth 取值，后续 mip 对前一级 2×2 采样归约，边界 clamp。各级尺寸 max(1,width>>mip)，组数 ceil(size/8)。构造完整金字塔总 texel 数约 4/3 WH，计算和存储仍为 O(WH)。

当前主路径的窗口深度为 0 至 1，深度清理值为 1，比较函数为 Less。Min 归约选取邻域最前深度。需要保守最远深度的遮挡算法应使用与其判定匹配的归约方式。

深度金字塔使用 `R32_Float` 纹理，原场景深度仍为 `D32_Float` 深度附件。每个 Mip 调度一个 Compute Pass，同时声明整条 Mip 链的 UAV 访问，让 RDG 按写入顺序插入屏障。每个 Dispatch 分别绑定相邻的单 Mip 读写视图，Mip 0 从外部深度 SRV 读取。Shader 保留 SPIR-V 中的 `R32f` 图像格式，用于有类型存储读。

HZB 的基本原理是用物体投影矩形的保守深度范围与覆盖该矩形的层级深度比较；当前 ProjectAABBToScreenSpace 忽略某些 w<=0/屏幕外角点，穿越近面或包围屏幕的盒子可能不保守，不能当通用保守裁剪。GPU Shader 还缺失，因此其接口不构成可验证完整遮挡剔除器。

## SSR 的现有步进

[ScreenSpaceReflectionCS.sf](../../../shaders/tools/ScreenSpaceReflectionCS.sf) 从 GBuffer 法线、粗糙度和深度恢复世界位置，V 指向相机，反射方向 `reflect(-V,N)`；偏移 rayOrigin=P+0.03N 防自交。Unlit 或超过粗糙度阈值直接保留原 scene color。

TraceScreenSpaceRay 从固定 mip=6 向下搜索，步长 `MaxTraceDistance/MaxTraceSteps × 2^mip`，投影 samplePos 得 UV/depth，比较 `abs(rayDepth-sceneDepth)<TraceThickness`，最后 mip0 再精化。命中采 scene color，按 roughness/Fresnel 加到原色。

这不是严格 cell-boundary Hi-Z DDA：层级、步长和 thickness 是启发式；屏幕外、背面和未显示表面不可能靠这张 scene color 恢复。固定最高 mip 还要求资源确有足够层级。使用 abs(w) 或钳制 w 的投影处理不等价于正确裁剪 w<=0。

## SSGI 的现有估计

[ScreenSpaceGICS.sf](../../../shaders/tools/ScreenSpaceGICS.sf) 使用 hash 与余弦半球变换生成每像素 20 个方向，再过滤朝向观察者的部分方向；每方向用屏幕深度层级追踪，命中后取 scene color，以 NdotL 加权归一化，再乘 albedo×0.75。

源码的层级追踪最高 mip=6，每层最多 256 次，mip0 用 0<=rayDepth-sceneDepth<0.0005 判命中；UV 位移到 ray t 的换算仍为近似，不能称为严格透视正确射线遍历。

specular 分支计算了 roughness/Fresnel 权重，但当前返回的是原始 hitColor，未使用该 weight；因此不能把文档写成此路径已经按该公式做能量权重。最终输出 sceneColor+diffuseGI+specularGI，没有已完成的时域降噪、多帧累积或完整多次反弹。

大致成本为 WH×方向数×实际追踪步数，缓存和 early-exit 会影响运行时间；只是数量级分析，没有 GPU 计时数据。

## FXAA：当前是简化空间滤波

[FxaaCS.sf](../../../shaders/tools/FxaaCS.sf) 以 dot(rgb,(0.299,0.587,0.114)) 取亮度，读取十字四邻域；若最大最小差超过 max(0.0312,0.125×maxLuma)，按左右/上下梯度较强的一侧选邻居平均，再以 0.75 混合。

它没有标准完整 FXAA 的长边搜索/端点判定等步骤，应描述为当前以 FXAA 命名的简化边缘滤波。邻居在图边 clamp，输出透明度固定 1，是否允许丢弃 alpha 由使用者决定。

## 时域超分辨率

对输出像素中心计算归一化坐标 `uv=(p+0.5)/outputSize`。渲染投影的 NDC 偏移来自八相位 Halton(2,3) 序列，在采集源颜色时把相同偏移加回 UV。内部像素坐标为 `(uv+jitter)*renderSize-0.5`。以该坐标周围深度一致的纹素做双线性重建，并统计同表面 3×3 邻域的颜色上下界与均值。

选择内部像素邻域中贴近重建位置的最近深度，将其通过当帧抖动的逆 VP 恢复到世界空间，再经上帧无抖动 VP 投影。当前去抖动 UV 减去旧 UV 得到 UV 速度。历史以 `uv-velocity` 在输出尺度定位，四个颜色候选分别拒绝越界和设备深度差大于 0.0015 的样本。通过的权重乘以场景亮度响应、运动速度衰减和透明反应项。通过这一权重将历史颜色限制到当帧邻域范围后执行累积。

反应因子用透明绘制前后的颜色差除以当前颜色幅度，在没有透明绘制时取零。最终输出按设置对当前信号与邻域均值之差适量放大，再限制到邻域范围。历史保存锐化前的 HDR RGB，alpha 保存当前深度。以上步骤均在 [taaTAA.sf](../../../shaders/tools/taaTAA.sf) 完成，每个输出像素至多读 9 个本帧邻居和 4 个上帧候选。

[TemporalUpsampling](../../../src/Framework/Renderer/Private/PostProcess/AntiAliasingPostProcess.cpp) 由 Renderer Scene 持有。输出尺度两张 RGBA32F 历史共占每像素 32 字节，每图注册历史读槽和写槽，由 RDG 根据访问声明和上一提交 Fence 建立顺序。换场景、窗口尺寸变化、显示目标替换、内部尺寸变化、关停功能、相机突变或场景对象修改会使历史失效。逐帧构图没有额外 CPU 等待。释放或重新分配历史时等待相应纹理的最后队列 Fence。

主视口以内部尺寸绘制 GBuffer、SSGI、场景深度、HDR 颜色和透明表面。超分输出到原有 RGBA8 视口，彩色交互叠加与 ImGui 保持原尺寸。GI 和 Virtual Geometry Viewer 使用各自的独立图。设备深度阈值在远处的世界距离意义会变宽，透明面没有单独运动向量。 [开关与验证](../../specification/renderer_spec.md#场景视口超分辨率)

## 如何复查

先固定投影/深度与 normal 编码，再用已知平面和运动矢量检查每个中间结果；分别观察 depth mip、命中 UV、步数、diffuse/specular 项和 history。窗口有图不能证明算法正确，Shader 注册也不能证明缺失依赖已补齐。本轮没有执行这些新验证。
