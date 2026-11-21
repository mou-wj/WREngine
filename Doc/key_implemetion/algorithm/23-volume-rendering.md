# Froxel 体积渲染

[AddVolumePasses](../../../src/Framework/Renderer/Private/Volume/VolumeRendering.cpp) 接收单个透视视图、常规 Z 场景深度和线性 RGBA32F 场景颜色。Viewer 将它追加到既有的延迟着色与天空绘制之后。输入及临时资源均属于本次 RenderGraph。 [预览操作](../../specification/volumeviewer_spec.md)

## 网格与介质

屏幕划分为 8×8 像素的 Tile，横纵方向向上取整。深度方向有 32 个对数层，边界为 `z_i = near × (far/near)^(i/32)`，其中 far 取视图远裁剪与介质范围的较小值。这里的 z 沿相机前轴，透视像素的实际世界长度为 `Δz / dot(ray, cameraForward)`。体素使用逻辑坐标 (tile.x, tile.y, slice)，在二维图集中每 8 个切片一行。三组切片分别保存 RGB 消光及标量密度、RGB 散射及各向异性参数、RGB 自发光。 [格点寻址](../../../shaders/volume/VolumeGrid.sf) · [介质注入](../../../shaders/volume/VolumeMedium.sf)

均匀介质的消光 `σ_t = σ_s + σ_a`，其中散射系数和吸收系数分别由数据乘以非负密度。自发光也乘以介质密度，单位为每世界长度的线性辐射。低频环境入射光的第一版固定为各方向均匀常量，因此归一化相函数的球面积分为 1，环境散射源是 `σ_s × AmbientRadiance`。

## 按深度积分

`VolumeIntegrate` 解算每个像素的近面射线并把设备深度重建到世界位置。几何截断最后一段，清除值 1 则用配置的最大距离。对一段恒定消光 `σ_t`、长度 d、源项 q 的介质，按 RGB 分别计算：

```text
t = exp(-σ_t × d)
L += T × q × (1 - t) / σ_t
T *= t
```

光学厚度接近零时积分因子使用 `d × (1 - σ_t × d / 2)`，因此无消光的自发光也能正确累积。合成写 RGB `L + T × sceneColor`，保留背景 Alpha。 [积分](../../../shaders/volume/VolumeIntegrate.sf) · [合成](../../../shaders/volume/VolumeComposite.sf)

## 图资源与边界

`VolumeInjectMedium` 写图集 UAV。积分读取图集和不透明深度，写两张屏幕纹理。合成读取场景颜色及两张积分图，写独立结果。最后通过 `VolumeCopyComposite` 的 TransferSrc/Dest 声明复制回颜色目标，避免在同一个 Pass 对该颜色同时绑定 SRV 和 UAV。无效或禁用范围直接跳过。纹理生命周期由 Builder 管理，预览颜色由 Viewer 持有并沿最后访问 Fence 释放。

介质图集的逻辑空间为 `ceil(W/8) × ceil(H/8) × 32 × 3 × 16` 字节。另有两张积分图和一张合成图，各 16 B/像素。第一版每像素最多 32 次迭代，XY 用最近 Tile。要求输入为全尺寸透视视图与可写入复制目标的线性颜色，近平面之前和最大距离之外省略介质。
