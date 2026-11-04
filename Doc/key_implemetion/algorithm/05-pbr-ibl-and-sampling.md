# PBR、IBL 预计算与采样原理

源码：[ShadingCommon.sf](../../../shaders/core/ShadingCommon.sf)、[Sample.sf](../../../shaders/core/Sample.sf)、[IBLPrecomputeCS.sf](../../../shaders/tools/IBLPrecomputeCS.sf)、[EnvIBLParametersHelper.sf](../../../shaders/core/EnvIBLParametersHelper.sf)、[RenderModule.cpp](../../../src/Framework/Renderer/Private/RenderModule.cpp)。材质数据契约见 [Material](../../architecture/material.md)。

## 直接光照实际计算什么

N、V、L 为单位法线、指向观察者和指向光源的方向，H=normalize(V+L)。源码计算 Cook–Torrance 风格的微表面镜面和 Lambert 漫反射：

```text
F0 = lerp(0.04, BaseColor, Metallic)
F  = F0 + (1-F0)·(1-dot(H,V))^5
a  = Roughness²
D  = a² / [π·(NdotH²·(a²-1)+1)²]
G  = G1(NdotV)·G1(NdotL)
fs = DGF / max(4·NdotV·NdotL,1e-5)
fd = (1-F)·(1-Metallic)·BaseColor/π
Lo = (fd+fs)·LightColor·Intensity·NdotL
```

直接光 G1 使用 Schlick 近似，k=(roughness+1)²/8。D 分母也钳制以避免除零。dot 值 saturate，背光 NdotL<=0 提前退出。需要在线性颜色空间解释能量；图像显示、贴图色彩空间和 tone mapping 是额外问题，不能由公式正确自动保证。

当前 EvaluatePBRLighting 的 F0 固定从 0.04 起，虽然 MaterialData 有 Specular 字段，该函数没有用其调整介电 F0；文档不能声称该参数已影响所有 BRDF 项。法线字段名 NormalTS 也不能证明此时仍在切线空间，需沿 VertexFactory 的实际转换查看。

点光使用 `saturate(1-distance/radius)²` 范围衰减；聚光额外用 inner/outer cone 的 smoothstep。当前不是物理逆平方距离模型，不能将 intensity 直接解释为经过单位校准的流明/坎德拉。

### 聚光锥角的单位与衰减

[SpotLightComponent](../../../src/Framework/Engine/Private/LightComponent.cpp) 的角度 Setter 接受度并存为弧度，代理 Getter 仍返回弧度；阴影投影的 FOV 和范围示意的 tan 使用弧度。[Scene::UpdateGPUResourceIfNeeded](../../../src/Framework/Renderer/Private/Scene.cpp) 填充 SpotLightData 时必须转换为 `InnerConeCos=cos(innerRadians)`、`OuterConeCos=cos(outerRadians)`。不能改变代理角度语义来迁就 Shader，否则阴影投影和范围也会出错。

令 d 为光源向外照射的单位方向，L=normalize(lightPosition-worldPosition)，则 `Cone=dot(-d,L)=cos(theta)`，theta 是接收点偏离锥轴的夹角。有效半角满足 0≤inner≤outer<90°，因此 cos(inner)≥cos(outer)：Cone 小于外锥余弦时贡献为 0，大于内锥余弦时为 1，两者之间以 smoothstep 过渡，再乘距离衰减。每点成本 O(1)。内外角相等且 Cone 恰好等于阈值时仍可能进入零宽度 smoothstep，此退化边界未在本次修改；光源原点的方向退化及底层 Setter 的任意非法角度也不属于当前属性校验之外的保证。

历史实现曾将弧度直接写入上述 Cos 字段。20°/45° 错填为约 0.3491/0.7854，丢失正确的锥间渐变；外角 70° 错填 1.2217 后，单位向量点积永远不超过它，Shader 对所有接收点直接返回 0，Enabled 开关因而没有亮度差异。修正和实际 GPU 比较见 [2026-11-02 记录](../../dev/bugfix/故障与修复记录-2026-11-02.md#聚光灯启停无画面变化)。

## 采样与 PDF

对积分 I=∫f(ω)dω，Monte Carlo 估计为 `(1/N)Σ f(ωi)/p(ωi)`。因此方向分布和权重必须成对；样本方向看起来均匀，不代表估计无偏。

| 源码采样 | 变换思路 | 常见 PDF/含义 |
| --- | --- | --- |
| UniformSampleDisk | r=sqrt(u)、φ=2πv | 单位圆盘按面积均匀，1/π |
| UniformSampleSphere | z=1-2u、φ=2πv | 单位球面，1/(4π) |
| UniformSampleHemisphere | z=u、φ=2πv | 半球立体角均匀，1/(2π) |
| CosineSampleHemisphere | 圆盘采样提升 z=sqrt(1-r²) | cosθ/π |
| GGX/Beckmann/Phong | 通过各分布的反 CDF 改变 θ | 必须与对应 BRDF/法线分布和反射 Jacobian 配套 |

Hammersley 将 i/N 与 bit-reversal radical inverse 组合成二维低差异序列；固定 N 时便于稳定预计算，不是白噪声随机数。Shader 里的 sin/hash 也是确定性伪随机函数，不能当作密码学随机源或跨编译器逐位一致保证。

Sample.sf 的 GGXSampleHemisphere 把 roughness 直接当 a，而 ShadingCommon 的 ImportanceSampleGGX 内部使用 roughness²；调用方必须确认参数语义，不能只因同名 GGX 就交换两函数。

## 切线基与方向映射

构造 tangent=normalize(cross(up,N))、bitangent=cross(N,tangent)，根据 N 是否接近 up 选择备用轴，避免 cross 退化。local 方向按 T/B/N 展开到 world。法线和方向在同一坐标空间才能用于 BRDF。

环境原图是经纬图，方向到 UV 使用 atan2(z,x)/(2π)+0.5 与 acos(y)/π。cube 输出六面用单独的 face→direction 函数，其中翻转 st.y；这个转换是资产/Shader 约定，不能直接套到所有后端 present 的翻转上。

## IBL 的三份预计算结果

| 变体 | 输入/输出 | 当前采样 |
| --- | --- | --- |
| mode 0 | HDR 经向采样 → 各 roughness mip 的 specular cube | 256 个 GGX half-vector 样本，反射得到 L，以 NdotL 加权归一化 |
| mode 1 | HDR → diffuse cube | 256 个均匀半球样本，当前权重 cosθ·sinθ |
| mode 2 | NdotV、roughness → 二通道 BRDF LUT | 128 样本，积累 (1-Fc)Gvis 与 FcGvis |

specular 预滤波是当前加权近似，不是任意分布下通用无偏积分器。diffuse mode 的均匀立体角采样与额外 sinθ 权重/π 归一化不符合直接套用均匀半球积分的标准形式，需要独立能量核对；不能把注释“irradiance”当作已经做过正确 PDF 推导。

LUT 中 Fc=(1-VdotH)^5，Gvis=(G·VdotH)/(NdotH·NdotV)，预分离出 F0 的两项，运行时近似为 `prefiltered·(F·LUT.x+LUT.y)`；IBL 的 Schlick k=roughness²/2，与直接光 k 不同。

## 运行时取值与 GBuffer

roughness×(MipCount-1) 选 specular mip；法线方向查 diffuse cube，反射方向查 specular cube，NdotV/roughness 查 LUT，再用 kd=(1-F)(1-metallic) 混合。

当前 GBuffer A=BaseColor.rgb/Metallic，B=encoded normal.rgb/Roughness，C=Specular/AO/ShadingModelId/空余；法线用 N×0.5+0.5 编码，模型 ID 除 255 写入再还原。延迟重建把 Emissive 置零，没有独立 Emissive 通道，不能写成完整材质信息无损恢复。

计算预算约为输出 texel 数乘采样数；cube 六面和所有 mip 都要计入。GPU Dispatch 按线程组向上取整后必须边界检查，源码入口已有对应尺寸判断。CPU 上传的 SampleCount 不等于 Shader 实际使用数：这些分支当前有固定 256/128 常量。

建议核查：恒定环境的能量、roughness 两端、cube 接缝、黑色/金属端点与 NdotV≈0。这里列的是复查方法，本轮没有做数值积分或像素验证；已有材质 Shader 编译测试仅验证可编译与创建 RHI Shader。

## 工程纹理的普通 Mip 重建

入口：[ProjectArchive.cpp](../../../src/Framework/Engine/Private/ProjectArchive.cpp) 的 DownsampleTexture、ReadChannel、WriteChannel。工程只保存每个切片第 0 层像素；Load 按描述中的 MipLevels 逐级调用此函数并上传。持久化、队列状态和所有者见 [工程快照](../performance/10-project-snapshots.md)。这不是 IBL 粗糙度卷积，后者仍由前述 PreComputeIBL 重新执行。

输入为当前层宽 W、高 H、紧密排列的原格式数据；输出尺寸 W'=max(1,floor(W/2))、H'=max(1,floor(H/2))。坐标是图像 texel 边界，无物理长度。输出像素 (x,y) 覆盖输入矩形 `[xW/W',(x+1)W/W'] × [yH/H',(y+1)H/H']`，对应源码 x0/x1/y0/y1。将它与每个相交输入 texel 的单位正方形求重叠面积 weight，通道结果为 `sum(value*weight)/((x1-x0)*(y1-y0))`。

因此 3×1 → 1×1 会平均全部三个 texel，不会漏掉最右边；1×N 不会产生零宽，最后可到 1×1。每一级通常访问 2×2、奇数尺寸最多 3×3 的输入邻域，整条链为 O(W·H·通道数)，数组/立方体乘以切片数。加载只保留当前层和下一层两个 CPU 缓冲，上传沿用同步完成点；没有保留整条派生 Mip 链。

UNorm8 解码为 byte/255，结果钳制到 [0,1] 并按最近偶数舍入量化。sRGB 的前三通道先按 `c<=0.04045 ? c/12.92 : ((c+0.055)/1.055)^2.4` 解码；平均后按 `l<=0.0031308 ? 12.92*l : 1.055*l^(1/2.4)-0.055` 编码。Alpha 保持线性。黑白各半得到约 188，而非在编码空间直接平均得到的 128。

Float32 通过 memcpy 读写避免未对齐别名访问；累加使用 double。Float16 按 IEEE binary16 的符号、5 位指数和 10 位尾数解码，次正规数为 `fraction*2^-24`，规格化数为 `(1024+fraction)*2^(exponent-25)`，编码采用最近偶数舍入并处理次正规到规格化的进位。有限输入的凸平均不超出其范围；遇到 NaN/Inf 明确拒绝。基础层不经历解码重编码，像素字节原样恢复。深度、模板和不在当前格式表中的整数/压缩格式不在工程快照支持范围内。

当前滤波是普通颜色/数据缩图：不跨立方体面滤波、不对法线重新归一化，不执行预乘 Alpha 或透明裁剪覆盖率保持，也不保留人为编辑的高层内容。后续若引入特殊采样语义，需单独定义生成策略，不能把所有派生纹理都当成普通 Mip。现有 RenderTexture::GenerateMipMaps 仍是占位；加载实际使用本段 CPU 实现，不依赖三个后端不一致的 BlitTexture 支持。

2026-11-03 诊断覆盖独立切片、3×1 边界、sRGB 黑白、Float32 正负平均及 Float16 次正规数；数值结果和后端范围见 [故障记录](../../dev/bugfix/故障与修复记录-2026-11-03.md)。这不等于完成所有图像频率、格式和透明语义的质量评估。

## MeshViewer 的 Phong 预览

源码：[MeshViewerPreview.cpp](../../../src/Editor/MeshViewer/Private/MeshViewerPreview.cpp)、[MeshViewer.sf](../../../shaders/editor/MeshViewer.sf)。该工具用传统 Phong 显示几何及法线，与场景 PBR 材质无关；不改变前文的场景光照实现。

每个 LOD 按区域遍历索引：`vertex=Indices[FirstIndex+i]+BaseVertexIndex`，使用有符号 64 位中间结果验证 0≤vertex<位置数量。无区域时使用全索引区间，区间长度必须是 3 的倍数。位置流至少三个分量且有限；属性表保留所有原始分量，预览只读取前三个作为位置/法线。遍历 O(V+I)，派生 GPU 数据为每顶点两个 float4（位置、法线，共 32 字节）和每绘制索引一个 uint32；原始流不被覆盖。

所选 LOD 的位置 AABB 给出 `center=(min+max)/2`、`radius=max(length(max-min)/2,1e-5)`，Shader 使用 `(position-center)/radius`。这是均匀归一化，方向只需归一化而无需额外逆转置，原网格局部空间和属性表坐标不变。相机围绕原点：`eye=distance*(cos(pitch)*sin(yaw),sin(pitch),cos(pitch)*cos(yaw))`，RH LookAt 向原点，+Y 为 up，pitch 限于 ±1.5 弧度以避开极点，距离限 1.3～15 个归一化半径；右键拖动更新 yaw/pitch，滚轮指数缩放距离。投影为 45°、RH ZO、near=0.05、far=50，不受主场景相机影响。

面模式令 N 为插值单位法线，L=normalize(-LightDirection)，V=normalize(eye-position)，R=reflect(-L,N)。代码计算 `diffuse=max(dot(N,L),0)`，仅在受光面计算 `specular=max(dot(R,V),0)^32`，颜色为 `base*(0.12+0.8*diffuse)+0.25*specular`，再钳制并作 1/2.2 次幂显示，Alpha=1。固定基色为 (0.62,0.68,0.78)，定向光方向 (-0.4,-0.8,-0.5)。这不是能量守恒 BRDF，也没有阴影、IBL、材质贴图或多光源；其目的为检查形状和法线。

线框模式每个三角形按 `{0,1,1,2,2,0}` 展开六个端点，用 LineList 绘制，成本为 2I 个顶点，不依赖 PolygonMode::Line。它没有面填充，不作额外隐藏线消除，共享边可以重复绘制。法线起点 S 偏移 `0.002*N` 避免与表面重合，末端 `T=S+NormalLength*N`；长度是归一化半径的比例，默认 0.08。每条法线包含原线段及末端四根短边组成的小箭头，共五条 LineList 线段、10 个顶点，仍只追加一次 Draw。面模式下通过深度测试遮掉后方线段，关闭深度写入。

箭头先取不平行于 N 的参考轴：abs(N.y)<0.9 时使用 +Y，否则使用 +X；构造 `u=normalize(cross(N,reference))`、`v=cross(N,u)`，避免 ±Y 法线退化。四个后沿点为 `T-0.28*NormalLength*N+0.14*NormalLength*radial`，radial 分别取 ±u、±v，每点与 T 连线。箭头与法线长度同步缩放，轴向尖端始终为 T。Shader 用 vertexId/10 定位源顶点，vertexId%10 定位原线段及四根箭头边；CPU Draw 数量同步改为 10V，不新增几何缓冲、Pass 或资源访问。箭头是局部三维几何，缩远后可小于一个像素，不是固定屏幕大小的图标。

缺失、非有限或近零的源法线使用预览生成值：各三角形 `cross(b-a,c-a)` 累加到顶点后归一化，因此面积较大的面权重较大；未被有效三角形覆盖的零向量用 +Y。与源法线顶点拆分相同，不能恢复未记录的硬边/平滑组；界面明确区分生成值，顶点表仍读源数据。异常索引、无三角形、超出 uint32 上传大小的网格显示错误，不返回伪成功。渲染资源时序见 [工程实现](../performance/08-ui-rendering-and-window-lifetime.md#meshviewer-预览与检查)。
