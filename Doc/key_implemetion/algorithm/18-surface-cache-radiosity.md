# Surface Cache Radiosity 多次漫反射

入口为 [SurfaceCacheRadiosity::AddIteration](../../../src/Framework/Renderer/Private/GI/SurfaceCacheRadiosity.cpp)。对应阶段为 [射线生成](../../../shaders/GI/SurfaceCacheRadiosityRays.sf)、[已有 SDF 查询](../../../shaders/GI/SurfaceCacheQuery.sf)、[间接出射光积分](../../../shaders/GI/SurfaceCacheRadiosityIntegrate.sf)。资源与帧时序见 [工程专题](../performance/13-screen-probe-gather.md#radiosity-迭代与缓存失效)。

## 图集与采样

每张材质 Card 对应间接图集的一块 8×8 texel。图集保持相同的 Card 行列顺序。采样在材质图集的对应单元内检查中心及四个象限，选取具有覆盖的世界位置和朝向 Card 外侧的世界法线。图集 alpha 表示单元是否有效，空 Card 和未覆盖区域写零。查询命中时用 Card 和材质 texel 映射到同一粗单元，跨 Card 边界不滤波。

每个单元固定发出 8 条半球射线。径向变量为 `u = (i + 0.5) / 8`，方位用等差低差异序列和由单元索引生成的固定旋转。局部方向为 `(sqrt(u) cos φ, sqrt(u) sin φ, sqrt(1-u))`，通过世界法线构造的正交基变换到世界坐标，方向的 PDF 为 `cos θ / π`。起点按 Card 单元大小和局部 SDF 容差作法线偏移，最大射程 100 个世界单位。源实例只跳过起点附近的命中，保留同一实例远处表面之间的反弹。

查询沿用实例 AABB、局部 SDF 和 Card Lookup，返回命中实例、Card 及精确材质 texel。积分拒绝背面、无材质覆盖和无有效直接光照缓存的命中。未命中项贡献零。该处理让开口可以损失能量，既有环境背景仍由 Screen Probe 的未命中分支处理。

## 跨轮反馈与能量

设 `D` 为已有直接漫反射出射 Radiance，`I_n` 为第 n 轮间接漫反射出射 Radiance。每条命中射线查询 `D(hit) + I_(n-1)(hit)`，第一轮使用 `I_0 = 0`。接收点按下式更新：

```text
ρ = min(saturate(BaseColor) × (1 - saturate(Metallic)), 0.98)
I_n(receiver) = ρ(receiver) × sum(D(hit_i) + I_(n-1)(hit_i)) / 8
```

分母包括无效射线。Lambert 的余弦与 `1/π` 在重要性采样中与 PDF 相消，接收面的漫反射率只乘一次。命中面的缓存值已经是出射 Radiance。固定场景和固定射线让每轮使用同一个离散算子，非负传输权重的行和上限为 0.98，在该模型下相邻迭代的无穷范数最多保留前一轮的 98%。实际收敛速度取决于材质、开口和采样命中率。调试 Delta 图是两轮 Texel 的绝对 RGB 差值，显示前放大 32 倍。

Screen Probe 对命中表面使用同一个 `D + I_n` 查询结果。屏幕像素按现有积分应用自己的材质参数和遮蔽，不复乘命中表面的反射率。

## 精度与代价

每轮产生 `CardCount × 512` 条查询。对象级追踪和 Card 搜索保留现有遍历复杂度。低分辨率的单点缓存会丢失一格内的薄几何、材质边界与高频光照，固定方向也可能漏掉窄小遮挡或反弹路径。此版本只传播由直接光照缓存种下的漫反射，材质覆盖、局部 SDF 和既有阴影误差都会进入迭代。

四色墙测试逐轮读回间接图集及由 SDF 命中得到的合计出射光，覆盖首次颜色传播、第二次回传、轮间收敛和光源移除。各后端数值见 [开发验证](../../dev/bugfix/故障与修复记录-2026-11-20.md#surface-cache-radiosity-开发与验证)。
