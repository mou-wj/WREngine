# 半透明、屏幕折射与吸收

[Renderer 使用约定](../../specification/renderer_spec.md#半透明绘制) · [RDG 资源生命周期](../performance/03-rdg-execution-and-state.md#半透明快照与附件)

## 排序与混合

`AddTransparencyPasses` 对当前视锥中的静态网格遍历 LOD0 区段。每个区段解析当前材质代理，Translucent 和 Additive 进入透明列表。排序键为世界空间包围球中心到相机的位移与相机前向的点积，并按原始 Primitive ID、区段索引解决等深度。实例分别排序，时间复杂度为 O(n log n)。同一像素中 A 为采样后的 Opacity，RGB 使用线性空间。两种混合公式如下。

| 模式 | 结果 |
| --- | --- |
| Alpha | A × 表面颜色 + (1 − A) × 已合成颜色 |
| Additive | A × 表面颜色 + 已合成颜色 |

两者均使用原场景深度测试、关闭 PSO 的深度写入并只写 RGB。水面深度额外通过背景快照的 A 通道约束像素，遮挡时输出零覆盖率。对象排序能正确处理从前到后关系一致的透明面。互相穿插的面、同一网格内的不同深度层和相近物体绕转时的排序跳变仍属于对象排序的局限。 [切片收集与排序](../../../src/Framework/Renderer/Private/TransparencyRender.cpp)

## 透射方向与出射光

`SampleRefraction` 以入射方向 I=−V 与朝向视点的世界法线 N 计算折射方向 R，折射率比取空气到介质的 1/IOR。厚度 t 是线性场景单位，内部路径长度近似为 d=t/max(|N·R|,0.1)。采用单一薄层近似，沿 I 与 R 的出射点差为 (R−I)·d，用 RefractionStrength 乘该偏移，再经当前 ViewProjection 投影到屏幕。

在投影点附近按 Roughness² 布置四个采样点。视口外或包含更近几何的位置回退到当前片元对应的背景样本。透射颜色逐通道乘 exp(−Absorption·d)。底层漫反射乘 (1−Transmission)，镜面反射继续走光照 BRDF，透射项按 (Transmission·(1−F)·(1−Metallic)) 进入表面颜色。F 由 IOR 的介电法向反射率 (((IOR−1)/(IOR+1))²)、材质 Specular 以及 Schlick 角度近似得到。清漆和布料层还会对其底层透射作相应能量衰减。已有 PBR 的直射光、阴影与 IBL 继续工作。

快照只包含不透明、天空及调用方已渲染的环境结果，因此后绘制的玻璃不会重新折射已绘制的透明层。厚度来自材质参数，模型背面和非屏幕几何不会参与求交。近似开销为每透射像素一次原色读取及四次偏移读取。 [透明像素着色器](../../../shaders/material/TransparencyMaterialPS.sf) · [折射和 Fresnel](https://www.pbr-book.org/4ed/Reflection_Models/Specular_Reflection_and_Transmission) · [介质透射](https://www.pbr-book.org/4ed/Volume_Scattering/Transmittance)
