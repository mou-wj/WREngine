# Screen Probe GI 的时域累积与空间滤波

GIViewer 在 Screen Probe 重建后滤波。`ScreenProbeIntegration` 已将入射辐射转换为接收面的线性漫反射间接光，应用一次 `BaseColor × (1 − Metallic) × AO`。时空滤波只操作该线性 RGB，跨帧保存时域结果。方向性 Probe Radiance 仍供当帧积分和调试使用，Surface Cache 与 World Radiance Cache 不读取屏幕滤波结果。 [Screen Probe 重建](17-screen-probe-gather.md) · [工程时序与历史所有权](../performance/13-screen-probe-gather.md#屏幕历史与失效)

## 重投影与遮挡判断

当前像素中心坐标为 `q=(p+0.5)/(W,H)`，深度是 GBuffer 的设备深度 `d∈[0,1]`。当前世界位置从 `InverseViewProjection × (2*q.x-1, 1-2*q.y, d, 1)` 齐次归一化得到。将世界位置投影到上一帧 `PreviousViewProjection`，把结果换算为上一帧纹素坐标 `qPrev*(W,H)-0.5`。非正的齐次 `w`、超出视口或裁剪深度范围、归一化位移大于 `0.25` 时拒绝重投影。背景和非 Lit 像素也不累积。 [坐标实现](../../../shaders/GI/ScreenProbeGeometry.sf) · [时域实现](../../../shaders/GI/ScreenProbeTemporal.sf)

对上帧的四个双线性候选分别用保存的深度和上帧逆矩阵恢复世界位置。设两帧位置差为 `Δ`，当前法线与候选法线为 `N` 和 `Nprev`。候选需满足 `dot(N,Nprev)≥0.9`，双向平面距离 `max(abs(dot(Δ,N)),abs(dot(Δ,Nprev)))` 小于等于 `max(0.005, 0.8 * footprint)`，且 `length(Δ)` 小于等于 `max(0.015, 2.2 * footprint)`。`footprint` 是同深度沿屏幕 X/Y 偏移一个像素所得的较大世界空间距离。绝对下限使用引擎世界单位。候选还必须携带有效深度和累积帧数。仅对通过的候选归一化，剩余双线性权重至少为 `0.25`。这使历史候选在计算颜色前就能排除大部分遮挡变化。

## 历史范围约束与自适应权重

在当前 3×3 邻域中选取深度、法线和距离相容的有效像素，求线性 RGB 分量最小值与最大值。以当前亮度、邻域亮度跨度和 `0.015` 的下限确定容许范围，将历史颜色约束到扩展后的分量范围。瞬时亮度变化越大，历史响应系数越低。运动量也会降低响应系数，Probe 射线有效率低的像素则获得更小的历史权重。

```text
w = min(age / (age + 1), weightLimit) × response × lerp(0.65, 1, confidence)
L_temporal = lerp(L_current, clamp(L_history, max(0, lower - slack), upper + slack), w)
age_new = min(12, 1 + age × response)
```

其中 `age` 为有效历史长度，`confidence` 为积分输出 alpha，`slack` 用于保留 HDR 局部变化。有效历史不足、光照突变或场景重置时，输出迅速转向当前值。常态权重上限 `0.90`，当帧实际推进 Radiosity 或安排世界探针刷新时降至 `0.72`。历史有效性视图反映几何候选是否通过，权重视图反映光照响应与置信度作用后的最终比例。

## 空间滤波

空间 Pass 读取时域 RGB、历史长度、当帧深度和世界法线，以及 GBufferA 的 BaseColor 和 Metallic。只考虑 3×3 内的有效邻居，剔除法线点积低于 `0.85`、平面差超过 `max(0.005, 0.75 * footprint)` 或世界距离超过 `2.5 * footprint` 的候选。对剩余样本按像素距离、平面残差、法线、材质差和亮度差赋权。像素已有长历史时，空间结果混合比例从 `0.70` 逐渐降至 `0.35`，避免持续模糊。中心没有有效辐照样本时，只允许几何匹配的邻居补缺，结果仅写 Viewer 颜色。 [空间实现](../../../shaders/GI/ScreenProbeSpatial.sf)

每帧工作量随像素数线性增长。时域每像素至多取 4 个历史样本，并访问 8 个当前邻居；空间最多访问 8 个邻居。该方法以几何一致性估计可见性。相近且同法线的薄层、缺少探针覆盖的次像素几何以及帧间缓存突变仍需要更复杂的场景验证。现阶段使用相机矩阵处理运动，可见静态网格的变换或物料代理变化触发全图失效。 [GPU 测试](../../../src/Tests/ScreenProbeFilterTest.cpp) · [验证与范围](../../dev/bugfix/故障与修复记录-2026-11-21.md#gi-时空滤波验证)
