# Froxel 体积渲染

[AddVolumePasses](../../../src/Framework/Renderer/Private/Volume/VolumeRendering.cpp) 接收单个透视视图、常规 Z 场景深度和线性 RGBA32F 场景颜色。Viewer 将它追加到既有的延迟着色与天空绘制之后。输入及临时资源均属于本次 RenderGraph。 [预览操作](../../specification/volumeviewer_spec.md)

## 网格与介质

屏幕默认划分为 8×8 像素的 Tile，横纵方向向上取整。深度默认 32 个对数层，边界为 `z_i = near × (far/near)^(i/N)`，其中 N 为层数，far 取视图远裁剪与介质范围的较小值。这里的 z 沿相机前轴，透视像素的实际世界长度为 `Δz / dot(ray, cameraForward)`。当前支持 4–16 像素 Tile 与 16–64 层，三组二维图集切片分别保存 RGB 消光及标量密度、RGB 散射及各向异性参数、RGB 自发光。 [格点寻址](../../../shaders/volume/VolumeGrid.sf) · [介质注入](../../../shaders/volume/VolumeMediumCommon.sf)

均匀介质的消光 `σ_t = σ_s + σ_a`，其中散射系数和吸收系数分别由数据乘以非负密度。自发光也乘以介质密度，单位为每世界长度的线性辐射。

## 局部介质

调用方为每次构图传入 `LocalVolume` 列表，记录形状、位置、Euler 角、半轴尺度和介质参数。`Volume.LocalMedia` 上传世界到局部的逆矩阵。Froxel 样本落入局部坐标的 `[-1,1]³` 为盒内，平方长度不超过 1 为球内。非均匀缩放自然形成椭球。无效尺度、非正密度或与积分深度区间无交集的包围球在上传前剔除。注入时先比较样本的轴向深度，再进行世界到局部变换和精确形状判定。

在覆盖区域中，`σ_s`、`σ_a` 和自发光分别相加，故多个介质的光学厚度累加、透射率相乘。有效 g 取各介质散射系数亮度加权的均值，对同一相函数的介质是精确的，对多个方向性不同的相函数是近似。重叠计数存于自发光切片的 Alpha，供调试查看局部边界。中心采样会量化局部边缘，尺寸小于一个 Froxel 的区域可能遗漏。 [局部注入](../../../shaders/volume/VolumeMediumCommon.sf)

### 非均匀密度

每个局部介质可独立选择均匀、程序噪声或 3D 纹理。通过 `WorldToLocal` 得到 `p_local` 后，采样坐标为 `0.5 × p_local + 0.5 + Offset + Velocity × FrameTimeSeconds`，速度采用每秒的归一化局部坐标单位。程序噪声用 `Frequency` 缩放该坐标，再对三个频带进行加权，单个频带用晶格整数哈希和三线性平滑插值。纹理采用重复寻址，从 Mip 0 取红通道。对两种非均匀场，最终密度为 `Medium.Density × max(0, sample × Scale + Bias - Threshold)`。密度分别乘入散射、吸收和自发光，阴影仍只改变入射光。

外部生产者可向 `VolumeDensityField::Texture` 提供已初始化、可采样的 3D 纹理。使用常规 RHI/RDG 写入并登记同步后可供后续注入读取。纹理借用到最后一次 GPU 访问结束，由调用方持有。Volume 按纹理对象去重，每视图最多八张不同纹理。绑定数组中的空槽复用第一个有效输入，HLSL 用常量描述符索引采样。无纹理的视图使用独立 Shader 入口，两条路径共用介质计算。只有 **Density sources** 调试模式会分配额外切片，RGB 分别展示程序噪声、均匀介质和纹理的合计密度。 [采样代码](../../../shaders/volume/VolumeDensity.sf) · [密度参数](../../../src/Framework/Renderer/Private/Volume/VolumeRendering.h)

## 按深度积分

`VolumeIntegrate` 解算每个像素的近面射线并把设备深度重建到世界位置。几何截断最后一段，清除值 1 则用配置的最大距离。对一段恒定消光 `σ_t`、长度 d、源项 q 的介质，按 RGB 分别计算：

```text
t = exp(-σ_t × d)
L += T × q × (1 - t) / σ_t
T *= t
```

光学厚度接近零时积分因子使用 `d × (1 - σ_t × d / 2)`，因此无消光的自发光也能正确累积。积分预先计算对数层间公比，通过递乘推进边界，最后一段仍以深度给出的终点截断。合成写 RGB `L + T × sceneColor`，保留背景 Alpha。 [积分](../../../shaders/volume/VolumeIntegrationCommon.sf) · [合成](../../../shaders/volume/VolumeDisplay.sf)

### 稀疏积分和深度感知重建

空间重建可选 stride 2，按每 2×2 像素区域选择一个主采样点，奇数尺寸的末边夹到实际像素。每个像素在重建时比较同类深度的四个邻居。积分图的 L Alpha 保存有符号终点深度，正数表示体积范围内有不透明几何，负数表示在积分范围内无遮挡。同类采样以双线性权重乘 `max(0, 1 - abs(Δz) / max(0.015, 0.015 × |z_end|))` 加权。权重和小于 0.25 时直接按该像素的深度重新积分，保留稀疏采样网格漏掉的细前景。其余像素一次写最终颜色。2×2 屏幕相邻样本存在遮挡和视线方向差异，因此重建是近似，深度不匹配时回退可增加工作量。 [重建与合成](../../../shaders/volume/VolumeReconstruct.sf)

### 抖动与历史重投影

启用抖动后，帧编号驱动长度 64 的三维序列，在一个 Froxel 各轴的 15%–85% 位置采样。局部介质、直接光和阴影使用同一个世界位置。积分边界与不透明截断保持固定。

时域混合并入主积分 Dispatch。前景锚点取不透明终点，无遮挡时取射线介质段中点。将锚点乘上一帧 VP 获得历史屏幕位置，拒绝屏幕外、相机背后和遮挡分类变化。前景另用上一帧的 clip.w 校验历史终点，容差为 `max(0.025, 0.025 × clip.w)`。通过校验的历史按当前与历史透射率及相对散射差衰减权重。T Alpha 保存实际混合权重，**History weight** 用灰度显示它。回退到精确积分的像素只用本帧结果。 [时域 Shader](../../../shaders/volume/VolumeTemporal.sf)

`VolumeRenderHistory` 每视图维护两组 L/T 纹理和上帧相机快照。非连续帧、尺寸和采样配置变化、相机切换或跳变、光源和介质参数变化、可见 Primitive 变换和环境贴图引用变化均使历史失效。原地更新外部几何、阴影或密度数据的调用方可递增 `SceneRevision`。连续密度动画以最大局部流速乘帧间隔估计活动程度，移动越快，历史权重越低。屏幕每条射线只有一个历史锚点，当前实现会在深度或光照突变处保守降权。 [C++ 历史管理](../../../src/Framework/Renderer/Private/Volume/VolumeRendering.cpp)

## 单次散射与光源注入

在 Scene 更新后，`BindVolumeLights` 读取已启用的场景代理，将光色、强度、范围和锥角余弦打包为四个四分量向量。数据保存在 RDG 上传 Pass 的值捕获中，经 RHI staging 在提交时上传。光源类型过滤只影响体积注入。方向与世界位置从 Froxel 的屏幕中心和对数层中心恢复。

`VolumeInjectLighting` 遍历光源，点光与聚光先用范围球轴向深度和平方距离排除覆盖外样本，再调用 `LocalLightAttenuation` 与 `SpotLightAttenuation`。令 `μ = dot(toLight, cameraRay)`。Henyey–Greenstein 相函数为 `(1-g²)/(4π(1+g²-2gμ)^(3/2))`。正 g 在光向相机传播时加强前向散射。g 为零时固定为 1/(4π)。光照图记录乘过相函数的 RGB，积分写源项 `σ_s × (direct + ambient) + emission`。 [单次散射 Shader](../../../shaders/volume/VolumeLighting.sf) · [通用衰减](../../../shaders/core/ShadingCommon.sf)

## 光源可见性

构图时从 Scene 已分配的阴影记录获取光源编号、切片变换及纹理。Volume 注册同一纹理为 RDG SRV，读取此前基础渲染生成的图。平行光按前向相机深度选 CSM，用同一张二维数组的指定 Layer。聚光使用灯光投影、Atlas UVScale/Offset 和半 texel 内缩，避免越界到相邻光源。点光复用现有 Cube 朝向转换与径向深度比较。无有效阴影、超投影范围和超现有槽容量时按可见处理。 [体积阴影采样](../../../shaders/volume/VolumeShadow.sf) · [场景阴影生成](../../../src/Framework/Renderer/Private/SceneRenderer.cpp)

可见性乘在入射光项上。积分的消光和透射率只由介质决定。**Light visibility** 图以入射光量加权展示该切片的可见比例。基础版本的 CSM 与 Spot 对 Froxel 使用单点深度比较，继承引擎已有的投影和深度偏移单位。

## 天空环境与合成位置

`VolumeGatherEnvironment` 沿六个世界轴采样场景已过滤环境图的最粗 Mip，求平均并乘环境强度，写入单个 RGB texel。若关闭天空输入或场景没有环境贴图，使用显式的常量 AmbientRadiance。积分将低频结果当作各方向均匀的入射辐射，其相函数球面积分为 1，因此环境源是 `σ_s × ambient`。这是空间均匀的 IBL 近似，未查询表面的 BRDF、Surface Cache 或屏幕 GI。它可以保留天空色调，但不表达室内间接遮挡或局部颜色反弹。 [环境注入](../../../shaders/volume/VolumeEnvironment.sf)

`MakeVolumeDrawExtension` 通过既有 SceneView 回调附加体积计算，Viewer 与主场景调用方都可显式注册。它应在场景的延迟着色和天空完成后运行。颜色里的表面光照只接受透射衰减，体积新增光照来自介质路径。SceneDepth 仍是最接近的不透明遮挡，当前只有一个积分终点。项目的延迟链路没有对每层透明片元公开独立的深度及雾积分，现有透明材质不能用本阶段重建介质与多层透明面交替的合成。 [调用入口](../../../src/Framework/Renderer/Private/Volume/VolumeRendering.h)

## 图资源与边界

`VolumeInjectMedium` 或 `VolumeInjectTexturedMedium` 写图集 UAV，随后注入光照。`VolumeIntegrate` 读取图集和不透明深度，写临时 L/T。启用时域时，`VolumeIntegrateTemporal` 直接读取上一组 L/T，写另一组历史纹理。`VolumeReconstructAndComposite` 或 `VolumeComposite` 读取场景颜色及积分结果，写独立颜色目标，然后 `VolumeCopyComposite` 用 TransferSrc/Dest 复制回目标。只看 Froxel 切片的模式跳过积分。临时纹理由 RenderGraph 管理。历史纹理由同一视图持有，每帧注册为外部资源，既有资源追踪记录跨帧访问 Fence。正常构图不等待历史，调整采样尺寸或释放时等待各纹理已登记的访问 Fence。Viewer 的颜色和演示密度资源同样遵循其预览提交的释放顺序。

介质图集三组切片，光照图集一组，每 texel 16 字节。启用 Density sources 时介质增加一组切片。合成占一张 16 B/像素的纹理。历史关闭时，临时积分占两张各 16 B/样本的纹理。开启时用四张持久纹理轮替，stride 2 时宽高均向上取半。统计基于纹理描述和图集行填充。实际峰值显存取决于 RenderGraph 的临时分配和复用。输入仍为全尺寸透视视图与可写入复制目标的线性颜色，近平面之前和最大距离之外省略介质。
