# GIViewer 混合反射

GIViewer 的反射分支从 GBuffer 构造镜面射线，按屏幕深度、Mesh SDF、世界探针及环境贴图选择入射辐射。输出为线性镜面贡献或带该贡献的延迟光照预览。 [渲染入口](../../../src/Framework/Renderer/Private/GI/Reflections.cpp) · [资源与时序](../performance/13-screen-probe-gather.md#反射图与资源)

## 坐标与射线

像素中心由 `(pixel + 0.5) / size` 转为左上原点 UV，NDC 为 `(2u−1, 1−2v, depth)`，设备深度范围为 `[0,1]`。`ReconstructGIPosition` 用逆 ViewProjection 恢复世界位置 `P`。`N` 来自 GBuffer B 的世界法线，`V=normalize(CameraPosition−P)` 指向相机。Roughness 与 Metallic 分别来自 B.a 和 A.a。只有有效深度、Lit 材质及朝向相机的接收面生成参与追踪的射线。 [坐标代码](../../../shaders/GI/ScreenProbeGeometry.sf) · [射线生成与屏幕追踪](../../../shaders/GI/ReflectionTrace.sf)

粗糙度 `r≤0.03` 时方向为 `reflect(−V,N)`。更粗糙时以像素索引哈希生成两维均匀样本，在接收面的切线基中把视线按 `α=r²` 拉伸至单位半球，在可见投影上采样，再反变换得到微表面半角向量 `H`，最后使用 `reflect(−V,H)`。低于接收面切平面的方向回退。射线起点沿 `N` 和反射方向各偏移 `clamp(0.5 * footprint, 0.02, 0.10)`，最大长度 50 世界单位。GGX VNDF 决定采样方向分布。当前积分近似用一个方向的入射辐射乘分离和 BRDF 权重，未逐样本使用 `BRDF × cos / PDF` 进行无偏积分。 [Heitz 的 GGX VNDF 论文](https://jcgt.org/published/0007/04/01/)

## 屏幕交点

`TraceScreenReflection` 把世界线段投影到裁剪空间，按 `w>0`、`−w≤x,y≤w`、`0≤z≤w` 剪裁后，在屏幕主轴步进，最多 96 步。屏幕参数 `s` 沿投影线段变化，设备深度按该参数线性插值。遇到从可见深度前方走到后方的区间时二分四次，再将交点像素和深度重建到世界空间。由线段端点的齐次 `w` 对世界射线参数作透视校正。

接收像素附近的 3×3 区域被跳过。近距离、反面及世界空间残差超过 `clamp(2 * footprint, 0.025, 0.35)` 的候选被拒绝。有效交点只保存 UV 和置信度。接近视口边缘或厚度容差时，置信度平滑衰减，后续用 SDF 或 IBL 过渡。可信命中读取当前屏幕的延迟底色，其中含直接光和漫反射 IBL。屏幕颜色是在当前相机视角下着色这一事实限制了它对高镜面命中表面的准确度。

## 离屏与环境回退

1. 屏幕置信度约为 1 时，SDF Query 标为跳过。其余有效射线用已有 `AddSurfaceCacheQueryPass` 查找最近的 Mesh SDF 命中，再用 `AddSurfaceCacheLightingLookupPass` 查询对应 Card texel。极近的 SDF 命中排除为可能自相交。
2. Card 可用时，缓存的直接与可选 Radiosity 漫反射出射辐射沿反射射线抵达接收面。用缓存法线、BaseColor、Metallic 和 Roughness，再以 `−rayDir` 作为命中表面视线计算命中点的环境镜面近似。命中点的方向性直接镜面高光和递归镜面反射目前无数据来源。Card 未覆盖仍保留几何命中标识，并使用接收面的环境回退。
3. SDF 完整未命中、世界缓存启用且 `r>0.45` 时，按已有方向深度和空间可见性检查查询世界探针。查询点沿射线向前移动两个细层网格单位，查找剩余范围。已有 `LookupWorldRadiance` 负责避免穿过可能不相容的可见性范围。粗糙度从 `0.45` 增长到 `0.65` 时逐渐提升该低频近似的占比。确定存在离屏遮挡但缺少 Card 时保留环境回退，跳过世界探针。
4. 最终回退使用镜面方向与粗糙度对应 mip 的预滤波天空立方体。没有环境时，该接收面环境采样为零。有效的屏幕反射以置信度替换上述辐射；`r>0.8` 后逐渐向预滤波环境过渡。 [Surface Cache 语义](18-surface-cache-radiosity.md) · [世界辐射查询](19-world-radiance-cache.md) · [着色与回退代码](../../../shaders/GI/ReflectionResolve.sf)

## 反射能量与调试

接收面从 `F0=lerp(0.04,BaseColor,Metallic)` 求粗糙度版 Schlick Fresnel `F`。复用 BRDF LUT 的 `A,B`，输出 `L_spec = L_in × (F*A+B)`。`L_in` 是按上述置信度混合的入射镜面辐射。`L_spec` 只加到不含镜面 IBL 的延迟底色一次。直接光含直接镜面高光，沿用原来的光源 BRDF。SDF 命中点上使用的材质属于另一个表面，不改变接收面的 BRDF 权重。 [PBR 辅助函数](../../../shaders/core/EnvIBLParametersHelper.sf)

调试视图分别输出世界方向 RGB、屏幕交点 UV 与置信度、SDF 几何/有效覆盖/距离、混合来源、接收粗糙度、`L_in`、`L_spec` 和最终合成。对 `P` 个像素，屏幕最坏执行 `O(96P)` 步，Mesh SDF 开销与有效射线和实例遍历数相关，世界查找只用于符合条件的粗糙像素。固定每像素一条射线，无时域累积或粗糙反射降噪，粗糙、高对比区域会有采样噪声，薄物体可能被步进或 SDF 精度漏掉。 [验证和限制](../../dev/bugfix/故障与修复记录-2026-11-21.md#giviewer-反射验证)
