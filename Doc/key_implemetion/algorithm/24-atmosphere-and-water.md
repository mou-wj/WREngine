# 天空大气与水面渲染

## 大气光学模型

`Volume/AtmosphereRender` 通过视图扩展进入现有延迟光照后的 RenderGraph。模型使用公里和每公里光学系数，可用 `WorldToKm` 映射场景坐标。相机世界空间 Y 以海平面为基准，行星中心位于地面参考点下方一个半径处。顶部和地面通过射线与球体的交区间求得。真实积分只覆盖大气内向前的路段，并在地面或不透明深度处终止。 [C++ 入口](../../../src/Framework/Renderer/Private/Volume/AtmosphereRender.cpp) · [球壳和密度](../../../shaders/volume/AtmosphereModel.sf)

Rayleigh 与 Mie 的相对密度按高度指数下降，吸收层用大气高度 25% 附近的三角分布近似。RGB 消光 `σt(h)` 为密度乘以各自系数之和，Mie 消光含 10% 的对应散射系数作为额外吸收。Rayleigh 相函数为 `3(1+μ²)/(16π)`，Mie 用可调方向性的 Henyey–Greenstein 相函数。这里的 `μ` 是视线与向阳方向的夹角余弦。入射光照按最强启用的定向光快照获取，无定向光时使用参数中的太阳方向和强度。Viewer 将太阳角度同时写入它持有的真实定向光。 [单次散射原理](https://ebruneton.github.io/precomputed_atmospheric_scattering/atmosphere/functions.glsl.html)

## 透射查表与视线合成

`VolumeAtmosphereTransmittance` 生成 128×48 的 RGB 透射表。坐标分别以平方分布映射高度、以及从当地地平线到天顶的方向余弦。每个 texel 用 40 段中点积分计算光学厚度，存储 `exp(-∫σt ds)`。后续查表前显式判定太阳是否被行星遮挡。

`VolumeAtmosphereCompose` 以 24 段处理背景，以 10 段处理几何的大气透视。每段使用 `T × σs × phase × T_sun × (1-exp(-σt Δs))/σt` 累积单次散射。近零消光时使用段长的稳定极限。几何贡献为 `L + T × opaqueColor`。背景包含地面直射近似与太阳盘。合成读取旧颜色和深度，输出独立 UAV，随后通过声明了传输依赖的复制 Pass 写回。LUT 和输出颜色随本次 RenderGraph 释放。 [透射表](../../../shaders/volume/AtmosphereTransmittance.sf) · [视线散射](../../../shaders/volume/AtmosphereScattering.sf) · [合成](../../../shaders/volume/AtmosphereDraw.sf)

Viewer 的大气在本地体积雾之前合成。大气负责球壳中的空气，Froxel 负责另外指定的局部介质和雾。先后合成对两种介质的重叠只作近似。表面 IBL 与体积环境光仍来自场景原有环境贴图。当前模型只算单次散射，深暮光和地面反弹精度受限。 [Viewer 操作](../../specification/volumeviewer_spec.md)
