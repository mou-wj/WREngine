# Froxel 体积渲染

[AddVolumePasses](../../../src/Framework/Renderer/Private/Volume/VolumeRendering.cpp) 接收单个透视视图、常规 Z 场景深度和线性 RGBA32F 场景颜色。Viewer 将它追加到既有的延迟着色与天空绘制之后。输入及临时资源均属于本次 RenderGraph。 [预览操作](../../specification/volumeviewer_spec.md)

## 网格与介质

屏幕划分为 8×8 像素的 Tile，横纵方向向上取整。深度方向有 32 个对数层，边界为 `z_i = near × (far/near)^(i/32)`，其中 far 取视图远裁剪与介质范围的较小值。这里的 z 沿相机前轴，透视像素的实际世界长度为 `Δz / dot(ray, cameraForward)`。体素使用逻辑坐标 (tile.x, tile.y, slice)，在二维图集中每 8 个切片一行。三组切片分别保存 RGB 消光及标量密度、RGB 散射及各向异性参数、RGB 自发光。 [格点寻址](../../../shaders/volume/VolumeGrid.sf) · [介质注入](../../../shaders/volume/VolumeMedium.sf)

均匀介质的消光 `σ_t = σ_s + σ_a`，其中散射系数和吸收系数分别由数据乘以非负密度。自发光也乘以介质密度，单位为每世界长度的线性辐射。低频环境入射光的第一版固定为各方向均匀常量，因此归一化相函数的球面积分为 1，环境散射源是 `σ_s × AmbientRadiance`。

## 局部介质

调用方为每次构图传入 `LocalVolume` 列表，记录形状、位置、Euler 角、半轴尺度和介质参数。`Volume.LocalMedia` 上传世界到局部的逆矩阵。Froxel 中心落入局部坐标的 `[-1,1]³` 为盒内，平方长度不超过 1 为球内。非均匀缩放自然形成椭球。无效尺度或非正密度在上传前剔除。

在覆盖区域中，`σ_s`、`σ_a` 和自发光分别相加，故多个介质的光学厚度累加、透射率相乘。有效 g 取各介质散射系数亮度加权的均值，对同一相函数的介质是精确的，对多个方向性不同的相函数是近似。重叠计数存于自发光切片的 Alpha，供调试查看局部边界。中心采样会量化局部边缘，尺寸小于一个 Froxel 的区域可能遗漏。 [局部注入](../../../shaders/volume/VolumeMedium.sf)

## 按深度积分

`VolumeIntegrate` 解算每个像素的近面射线并把设备深度重建到世界位置。几何截断最后一段，清除值 1 则用配置的最大距离。对一段恒定消光 `σ_t`、长度 d、源项 q 的介质，按 RGB 分别计算：

```text
t = exp(-σ_t × d)
L += T × q × (1 - t) / σ_t
T *= t
```

光学厚度接近零时积分因子使用 `d × (1 - σ_t × d / 2)`，因此无消光的自发光也能正确累积。合成写 RGB `L + T × sceneColor`，保留背景 Alpha。 [积分](../../../shaders/volume/VolumeIntegrate.sf) · [合成](../../../shaders/volume/VolumeComposite.sf)

## 单次散射与光源注入

在 Scene 更新后，`BindVolumeLights` 读取已启用的场景代理，将光色、强度、范围和锥角余弦打包为四个四分量向量。数据保存在 RDG 上传 Pass 的值捕获中，经 RHI staging 在提交时上传。光源类型过滤只影响体积注入。方向与世界位置从 Froxel 的屏幕中心和对数层中心恢复。

`VolumeInjectLighting` 遍历光源，按平行光方向、点光距离或聚光范围和锥角计算入射量，并复用 `LocalLightAttenuation` 与 `SpotLightAttenuation`。令 `μ = dot(toLight, cameraRay)`。Henyey–Greenstein 相函数为 `(1-g²)/(4π(1+g²-2gμ)^(3/2))`。正 g 在光向相机传播时加强前向散射。g 为零时固定为 1/(4π)。光照图记录乘过相函数的 RGB，积分写源项 `σ_s × (direct + ambient) + emission`。 [单次散射 Shader](../../../shaders/volume/VolumeLighting.sf) · [通用衰减](../../../shaders/core/ShadingCommon.sf)

## 光源可见性

构图时从 Scene 已分配的阴影记录获取光源编号、切片变换及纹理。Volume 注册同一纹理为 RDG SRV，读取此前基础渲染生成的图。平行光按前向相机深度选 CSM，用同一张二维数组的指定 Layer。聚光使用灯光投影、Atlas UVScale/Offset 和半 texel 内缩，避免越界到相邻光源。点光复用现有 Cube 朝向转换与径向深度比较。无有效阴影、超投影范围和超现有槽容量时按可见处理。 [体积阴影采样](../../../shaders/volume/VolumeShadow.sf) · [场景阴影生成](../../../src/Framework/Renderer/Private/SceneRenderer.cpp)

可见性乘在入射光项上。积分的消光和透射率只由介质决定。**Light visibility** 图以入射光量加权展示该切片的可见比例。基础版本的 CSM 与 Spot 对 Froxel 使用单点深度比较，继承引擎已有的投影和深度偏移单位。

## 图资源与边界

`VolumeInjectMedium` 写图集 UAV。积分读取图集和不透明深度，写两张屏幕纹理。合成读取场景颜色及两张积分图，写独立结果。最后通过 `VolumeCopyComposite` 的 TransferSrc/Dest 声明复制回颜色目标，避免在同一个 Pass 对该颜色同时绑定 SRV 和 UAV。无效或禁用范围直接跳过。纹理生命周期由 Builder 管理，预览颜色由 Viewer 持有并沿最后访问 Fence 释放。

介质图集的逻辑空间为 `ceil(W/8) × ceil(H/8) × 32 × 3 × 16` 字节，光照图占同尺寸单组切片。另有两张积分图和一张合成图，各 16 B/像素。第一版每像素最多 32 次迭代，XY 用最近 Tile。要求输入为全尺寸透视视图与可写入复制目标的线性颜色，近平面之前和最大距离之外省略介质。
