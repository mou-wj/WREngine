# 阴影映射、级联划分与 Atlas

源码：[SceneRenderer.cpp](../../../src/Framework/Renderer/Private/SceneRenderer.cpp)、[SceneView.cpp](../../../src/Framework/Engine/Private/SceneView.cpp)、[ShadowMapAllocator.cpp](../../../src/Framework/Renderer/Private/ShadowMapAllocator.cpp)、[ShadowParametersHelper.sf](../../../shaders/core/ShadowParametersHelper.sf)、[DepthShadowPassPS.sf](../../../shaders/tools/DepthShadowPassPS.sf)。

## 从光源观察场景

Shadow mapping 先从光源视角存最近表面深度；正常着色时将世界点投影到同一光源空间，比较 receiverDepth 与 shadowDepth，较远者被遮挡。光源 VP、写入深度、采样坐标和 bias 的单位必须一致，不能把 clip 深度与世界距离比较。

项目为 spot/directional 存 clip.z/clip.w，为 point 的 raw 模式存到光源位置的世界距离。输出 RG=(depth,depth²)，但当前 Shader 主要采样 .r 做比较，不是已完整实现方差阴影或 Chebyshev 上界。

## 三种光源的资源

| 光源 | 分配与投影 |
| --- | --- |
| Spot | 一个 Atlas 子矩形，光源方向的透视投影 |
| Directional | 专用 Texture2DArray，按级联覆盖相机不同深度段，正交投影 |
| Point | 专用 cube 六面，从光源位置看 ±X/±Y/±Z，采样比较世界距离 |

ShadowMapAllocator 的专用纹理持 shared_ptr，lookup arrays 保存裸指针索引。释放后 compact lookup array 会改变后续索引，其他访问记录必须同步重建，不能把 TextureIndex 当永久 ID。

## CSM 分段为什么混合线性和对数

近处分配更多阴影精度，对数划分 `n·(f/n)^p`；线性划分 `n+(f-n)p` 提供更均匀的远处覆盖。混合 `split=(1-λ)linear+λlog`，当前 λ=0.7。

SceneView::BuildSplitDepths 当前写 SplitDepths[0]=near、[4]=far，p=i/4，仍有固定 4 的假设，不能声称 CascadeCount 任意可配。根据各分段反投影视锥角点，转 light-view 空间后取边界得到正交范围；实际 minZ/maxZ 与 RH 深度约定见 [变换专题](03-transforms-frustum-and-depth.md)。

着色时按 cameraDepth 在相邻 split 区间选择 cascade，再访问对应 VP 和纹理。深度是 view-space 距离还是投影深度必须沿调用方核对；源码选不到区间时可能 cascade==CascadeCount，也需要边界处理，不能假设总能落入合法段。

## Atlas 的行式分配

Spot Atlas 当前是 shelf 策略：CursorX 向右放，当前行不足就 X=0、Y+=RowHeight；超高度则失败。UVScale=(w/atlasW,h/atlasH)，UVOffset=(x/atlasW,y/atlasH)，采样先 light clip→[0,1]，再缩放偏移到子矩形。

ReleaseShadow 的 Spot 分支只有“后续实现 Atlas FreeList”的注释，未回收矩形；清空 allocation 不会让 shelf 游标后退或重用空洞。它不是带空闲矩形复用/合并的通用二维装箱。PCF taps 必须留在分配矩形内，只有 Atlas 整体 UV∈[0,1] 检查不足以避免相邻阴影串色；当前边界扩展/guard band 没有完整保证。

## PCF 实际计算

spot/directional 在 [-radius,radius]² 采样，默认 radius=1，9 次最近深度比较后取平均。比较项是 `(compareDepth-bias<=storedDepth) && storedDepth<1`，因此 clear 深度=1 被额外排除，不等价于所有常见的“未写入区域完全可见”定义。

cube 函数名含 PCF，但当前只取一次 cube 深度比较，不是多 tap soft shadow。CubeClipToUVW 根据主轴选面并施加 FaceFlips，需与渲染六面 view/up 组合校对，不能独立改某一个翻转。

bias 防止自遮挡 acne，但过大产生 peter-panning；clip bias 与世界距离 bias 必须分开。原生光栅 slope-scale/depth bias 还是另外一层设置，不能把 Shader 的常量 bias 当作它已经启用。

## 当前能力与成本

每个方向光成本乘 cascade 数，每个点光约六次投影绘制，spot 需要 Atlas 空间和对应 draw；PCF 每 shaded pixel 又增加 taps。视锥筛选、双面/CullMode 和各 Pass 的剔除覆盖也会影响阴影，而不只是主颜色 Pass。

本源码中有 PCF、固定级联和二次矩写入；不能仅凭旧结果图片名 PCSS 就称当前路径已实现 blocker search、penumbra estimation 或可变滤波核，也没有在此确认完整 VSM/EVSM。

验证应包括单光源平面、自阴影、级联边界、point 六面接缝、Atlas 邻接和越界；本轮没有运行这些专项画面或测量 draw/tap 性能。
