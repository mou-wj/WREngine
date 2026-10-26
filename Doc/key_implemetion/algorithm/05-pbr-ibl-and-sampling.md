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
