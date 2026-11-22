# 天空大气与水面渲染

## 大气光学模型

`Volume/AtmosphereRender` 通过视图扩展进入现有延迟光照后的 RenderGraph。模型使用公里和每公里光学系数，可用 `WorldToKm` 映射场景坐标。相机世界空间 Y 以海平面为基准，行星中心位于地面参考点下方一个半径处。顶部和地面通过射线与球体的交区间求得。真实积分只覆盖大气内向前的路段，并在地面或不透明深度处终止。 [C++ 入口](../../../src/Framework/Renderer/Private/Volume/AtmosphereRender.cpp) · [球壳和密度](../../../shaders/volume/AtmosphereModel.sf)

Rayleigh 与 Mie 的相对密度按高度指数下降，吸收层用大气高度 25% 附近的三角分布近似。RGB 消光 `σt(h)` 为密度乘以各自系数之和，Mie 消光含 10% 的对应散射系数作为额外吸收。Rayleigh 相函数为 `3(1+μ²)/(16π)`，Mie 用可调方向性的 Henyey–Greenstein 相函数。这里的 `μ` 是视线与向阳方向的夹角余弦。入射光照按最强启用的定向光快照获取，无定向光时使用参数中的太阳方向和强度。Viewer 将太阳角度同时写入它持有的真实定向光。 [单次散射原理](https://ebruneton.github.io/precomputed_atmospheric_scattering/atmosphere/functions.glsl.html)

## 透射查表与视线合成

`VolumeAtmosphereTransmittance` 生成 128×48 的 RGB 透射表。坐标分别以平方分布映射高度、以及从当地地平线到天顶的方向余弦。每个 texel 用 40 段中点积分计算光学厚度，存储 `exp(-∫σt ds)`。后续查表前显式判定太阳是否被行星遮挡。

`VolumeAtmosphereCompose` 以 24 段处理背景，以 10 段处理几何的大气透视。每段使用 `T × σs × phase × T_sun × (1-exp(-σt Δs))/σt` 累积单次散射。近零消光时使用段长的稳定极限。几何贡献为 `L + T × opaqueColor`。背景包含地面直射近似与太阳盘。合成读取旧颜色和深度，输出独立 UAV，随后通过声明了传输依赖的复制 Pass 写回。LUT 和输出颜色随本次 RenderGraph 释放。 [透射表](../../../shaders/volume/AtmosphereTransmittance.sf) · [视线散射](../../../shaders/volume/AtmosphereScattering.sf) · [合成](../../../shaders/volume/AtmosphereDraw.sf)

Viewer 的大气在本地体积雾之前合成。大气负责球壳中的空气，Froxel 负责另外指定的局部介质和雾。先后合成对两种介质的重叠只作近似。表面 IBL 与体积环境光仍来自场景原有环境贴图。当前模型只算单次散射，深暮光和地面反弹精度受限。 [Viewer 操作](../../specification/volumeviewer_spec.md)

## 水面求交与动态法线

`AddWaterPass` 使用透视相机和现有不透明颜色及深度，输出附加水面的颜色和有效场景深度。水面是世界空间 Y 向上的有限高度场，位置、局部 XZ 半范围和绕 Y 轴旋转由 `WaterRenderSettings` 指定。`EvaluateWaterWave` 叠加三组正弦波，每项为 `A sin(k(d·x − vt) + φ)`，其中 `k = 2πf`，`f` 以每场景单位的周期数计，`v` 以场景单位每秒计。求得梯度 `Ak cos(k(d·x − vt) + φ)d` 后将梯度旋回世界 XZ，计算 `normalize((-∂h/∂x, 1, -∂h/∂z))`。法线强度只作用于着色梯度。 [水面 Shader](../../../shaders/volume/WaterSurface.sf) · [周期波原理](https://developer.nvidia.com/gpugems/gpugems/part-i-natural-effects/chapter-1-effective-water-simulation-physical-models)

`IntersectWater` 将视线裁剪到水面矩形、高度包络与近裁面到不透明深度之间。无波浪时直接求平面交点。有波浪时沿区间扫描 6 至 32 段，找到首个由空气进入水面的符号变化，再二分 7 次。极掠视、高频或高振幅时的多个交点可能落入同一采样段。当前入口处理俯看水面的透视视图，近乎水平的射线和位于静水面下方的视点退出求交。 [Pass 入口](../../../src/Framework/Renderer/Private/Volume/WaterRender.cpp)

## 水面的反射、折射与合成

折射率使用 1.333，法向入射反射率 `F0 = ((1.333 − 1) / (1.333 + 1))²` 约为 0.0204。`ShadeWaterLights` 复用项目的 GGX 分布、Smith 几何项、Schlick Fresnel 及点光和聚光衰减。每帧只上传已启用的平行光、点光和聚光快照，水面直接高光当前未采样场景阴影。反射优先对已启用的大气沿反射方向积分，太阳盘由直接 GGX 高光承担。大气停用时采样已有场景 IBL 的粗糙度 Mip，无环境图时使用天空色近似。

`RefractWater` 按 Snell 方向将原不透明深度估计的光程投回屏幕。候选样本须在屏幕内、处于同一水域且低于当地水面，并在交点后方。否则使用当前像素的不透明颜色。透射项为 `T = exp(-absorption × thickness)`，水色贡献为 `BodyColor × (1 − T)`。合成按 Fresnel 分配反射和透射权重，再叠加直接高光。只有不透明图中的样本可用于折射。 [水面光照与合成](../../../shaders/volume/WaterDraw.sf)

RenderGraph 的顺序为不透明场景 → 大气透射查表与水面光源上传 → 水面计算与颜色复制 → 大气合成 → 本地体积雾。`VolumeWaterSurface` 写独立颜色和深度，`VolumeWaterCopy` 再写回场景颜色。水面深度记录可见交点，未命中时保留原深度并将远平面天空深度归一到 1。为复用同一存储图像声明，有效深度纹理为 RGBA32F，后续只读取 R 通道。水面、大气和雾的资源状态由现有 RDG 参数声明驱动，临时资源随图管理。雾和几何大气透视都止于有效深度，折射先读取合成前的不透明图。 [Viewer 接入](../../../src/Editor/VolumeViewer/Private/EditorVolumeViewer.cpp) · [读回验证](../../../src/Tests/VolumeRenderTest.cpp)
