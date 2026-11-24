# 半透明、屏幕折射与吸收

[Renderer 使用约定](../../specification/renderer_spec.md#半透明绘制) · [RDG 资源生命周期](../performance/03-rdg-execution-and-state.md#半透明快照与附件)

## 排序与混合

`AddTransparencyPasses` 对当前视锥中的静态网格遍历 LOD0 区段。每个区段解析当前材质代理，Translucent 和 Additive 进入透明列表。排序键为世界空间包围球中心到相机的位移与相机前向的点积，并按原始 Primitive ID、区段索引解决等深度。实例分别排序，时间复杂度为 O(n log n)。同一像素中 A 为采样后的 Opacity，RGB 使用线性空间。两种混合公式如下。

| 模式 | 结果 |
| --- | --- |
| Alpha | A × 表面颜色 + (1 − A) × 已合成颜色 |
| Additive | A × 表面颜色 + 已合成颜色 |

三种合成方式均使用原场景深度测试、关闭 PSO 的深度写入，并保留场景 Alpha。水面深度通过背景快照的 A 通道约束像素，遮挡时输出零覆盖率。对象排序适合从前到后关系一致的透明面，相交区段和同一网格内的多层面可能存在局部错序。 [切片收集与排序](../../../src/Framework/Renderer/Private/TransparencyRender.cpp)

## Alpha Mask

Masked 区段参加传统不透明绘制和阴影，保留深度测试及写入。覆盖率由材质的 OpacityMask 标量乘经过材质 UV 变换后的 Opacity 采样得到。像素值严格低于 AlphaCutoff 时由 clip 丢弃，颜色、深度及阴影矩均不写入。阈值为零时全覆盖，原有 Opaque 材质也沿用全覆盖。模式不改变 GBuffer 布局。点光阴影沿用世界空间径向距离，其他光源沿用裁剪深度除以 w。 [覆盖率函数](../../../shaders/core/MaterialParametersHelper.sf) · [遮罩深度 Shader](../../../shaders/material/MaskedDepthShaderPS.sf) · [标签与阴影](../../../src/Framework/Renderer/Private/SceneRenderer.cpp)

## 加权混合

Weighted 模式对当前视图中普通 Alpha 区段关闭对象排序。片元使用窗口深度 z∈[0,1]、覆盖率 aᵢ 和权重 wᵢ = 0.25 + 4(1−zᵢ)²。第一个目标累积线性 RGB S = Σ(Cᵢ aᵢ wᵢ)，第二个目标累积 W = Σ(aᵢ wᵢ) 和光学深度 τ = Σ[−log(max(1−aᵢ, 10⁻⁴))]。由于透过率相乘等价于光学深度相加，合成以 S/W 为颜色、以 A = 1−exp(−τ) 为 Alpha。W=0 时保留原颜色。

两个目标复用现有 Additive 混合，Shader 使用 1 作为源 Alpha，累积量存于 RGB。相交面的交换提交顺序满足同一公式，半精度累加允许舍入差异。加权平均偏向窗口深度较小的片元，仅近似相交面逐像素前后关系。含可见 Additive 或透射区段的视图回到有序混合。 [累积 Shader](../../../shaders/material/TransparencyMaterialPS.sf) · [合成 Shader](../../../shaders/material/TransparencyCompositePS.sf) · [Weighted Blended OIT](https://jcgt.org/published/0002/02/09/)

## 透射方向与出射光

`SampleRefraction` 以入射方向 I=−V 与朝向视点的世界法线 N 计算折射方向 R，折射率比取空气到介质的 1/IOR。厚度 t 是线性场景单位，内部路径长度近似为 d=t/max(|N·R|,0.1)。采用单一薄层近似，沿 I 与 R 的出射点差为 (R−I)·d，用 RefractionStrength 乘该偏移，把起点和偏移点分别经 ViewProjection 投影，相减其 NDC 坐标。按视口尺寸和后端窗口 Y 方向转换为像素偏移，再加到当前 SV_Position。强度为零时，两次投影相同，像素偏移为零。

在投影点附近按 Roughness² 布置四个采样点。视口外或包含更近几何的位置回退到当前片元对应的背景样本。透射颜色逐通道乘 exp(−Absorption·d)。底层漫反射乘 (1−Transmission)，镜面反射继续走光照 BRDF，透射项按 (Transmission·(1−F)·(1−Metallic)) 进入表面颜色。F 由 IOR 的介电法向反射率 (((IOR−1)/(IOR+1))²)、材质 Specular 以及 Schlick 角度近似得到。清漆和布料层还会对其底层透射作相应能量衰减。已有 PBR 的直射光、阴影与 IBL 继续工作。

Sorted 共享最初的背景快照，包含不透明、天空及调用方已渲染的环境结果。LayeredRefraction 按同一排序规则，在每个透射区段绘制前复制当前场景颜色和原深度。后绘制的前层玻璃因此可以采到已混合的后层透明颜色。透射层之间仍依赖对象排序，使用不透明或水面深度拒绝无效采样。厚度来自材质参数。模型背面和非屏幕几何没有参与求交。每个透射像素需要一次原色读取和四次偏移读取，分层模式每个透射区段另外增加一次全目标快照。 [透明像素着色器](../../../shaders/material/TransparencyMaterialPS.sf) · [折射和 Fresnel](https://www.pbr-book.org/4ed/Reflection_Models/Specular_Reflection_and_Transmission) · [介质透射](https://www.pbr-book.org/4ed/Volume_Scattering/Transmittance)
