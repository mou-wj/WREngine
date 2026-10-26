# HZB、SSR、SSGI 与抗锯齿

入口：[DepthPyramidGenerate.cpp](../../../src/Framework/Renderer/Private/DepthPyramidGenerate.cpp)、[HZBOcclusionTest.cpp](../../../src/Framework/Renderer/Private/HZBOcclusionTest.cpp)、[ScreenSpaceReflection.cpp](../../../src/Framework/Renderer/Private/ScreenSpaceReflection.cpp)、[ScreenSpaceGI.cpp](../../../src/Framework/Renderer/Private/GI/ScreenSpaceGI.cpp)、[AntiAliasingPostProcess.cpp](../../../src/Framework/Renderer/Private/PostProcess/AntiAliasingPostProcess.cpp)。架构：[Renderer](../../architecture/render/renderer.md)。

## 先区分是否接入

当前 DefferedSceneRenderer 的尾部构建直接光、IBL、深度金字塔和 SSGI，最后按条件绘制天空。独立 SSR、GPU HZB 和 AA 类存在，不等于都在默认渲染链启用。

HZBOcclusionTestCS 引用 `/tools/HZBOcclusionTestCS.sf`，独立 SSR 的深度分支引用 `/tools/ScreenSpaceReflectionDepthPyramidCS.sf`；本次文件盘点未找到这两个 Shader。它们属于缺失实现/依赖，不能写成可用完整路径。taaTAA/FxaaCS 当前有注册及参数、Shader，未见默认主链中的完整历史帧 AA 调度。

## 深度金字塔的降采样

[DepthPyramidGenerateCS.sf](../../../shaders/tools/DepthPyramidGenerateCS.sf) mip0 从外部 depth 取值，后续 mip 对前一级 2×2 采样归约，边界 clamp。各级尺寸 max(1,width>>mip)，组数 ceil(size/8)。构造完整金字塔总 texel 数约 4/3 WH，计算和存储仍为 O(WH)。

源码固定 min 归约且注释 reversed-Z，而主图形路径含 clear=1/Less；必须核对实际深度。若把 min 当标准 Z 下保守最远深度，会破坏遮挡/层级步进依据。

CPU 一个 RDG Pass 内循环 dispatch 各 mip，分别绑定单 mip 输入/输出视图。Pass 内相邻 dispatch 的读写可见性需要后端屏障配合，RDG 的 Pass 间分析不能自动证明它们之间已同步。

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

## TAA：重投影最小原型

[taaTAA.sf](../../../shaders/tools/taaTAA.sf) 计算 `historyUV=(pixel+0.5)/resolution-velocity`，clamp 后取历史像素，`out=lerp(current,history,saturate(BlendFactor))`。因此 velocity 必须是同 UV 单位且方向与减法一致，不能传世界速度或像素位移而不换算。

当前没有完整的抖动序列、运动向量生成与历史纹理轮转入口，也没有反遮挡、深度验证、邻域 clipping、相机切换失效。它是 Shader 原型，不是已完成的稳定 TAA。历史 UV clamp 只防越界，不判断历史是否有效。

## 如何复查

先固定投影/深度与 normal 编码，再用已知平面和运动矢量检查每个中间结果；分别观察 depth mip、命中 UV、步数、diffuse/specular 项和 history。窗口有图不能证明算法正确，Shader 注册也不能证明缺失依赖已补齐。本轮没有执行这些新验证。
