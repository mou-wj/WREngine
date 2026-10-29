# 矩阵、坐标变换、视锥与深度约定

源码：[Matrix.hpp](../../../src/Framework/Core/PublicHeader/Matrix.hpp)、[Transform.hpp](../../../src/Framework/Core/PublicHeader/Transform.hpp)、[BoxSphereBounds.h](../../../src/Framework/Core/PublicHeader/BoxSphereBounds.h)、[SceneView.cpp](../../../src/Framework/Engine/Private/SceneView.cpp)、[Camera.cpp](../../../src/Framework/Engine/Private/Camera.cpp)、[GBufferParametersHelper.sf](../../../shaders/core/GBufferParametersHelper.sf)。

## 内存顺序与乘法约定分开理解

Matrix 用 `(row,col)` 访问，行主存储只说明地址布局，不意味着向量一定放左边。当前 CPU 和 Shader 主链使用列向量意义：`world=M·local`，`clip=P·V·world`。SceneView::RebuildDerivedMatrices 显式计算 P×V。

点使用齐次 w=1，方向 w=0；TransformPoint 增加平移，TransformVector 不增加。TransformPoint 返回仿射结果，透视投影后的除 w 则需另做，不能用仿射函数替代投影/反投影。

矩阵 Inverse 使用伴随矩阵除 determinant，当前没有统一的奇异矩阵容差/错误结果协议。零缩放或退化投影可能产生非有限值，不能假设总能逆。

## TRS、父子层级和角度单位

MakeTransformMatrix 当前为 T×R×S，MakeRotationMatrix 接收角度制，rotation.X 为 pitch、Y 为 roll、Z 为 yaw，并返回 Rz×Ry×Rx。Perspective 的 fovy 则直接参与 tan(fovy/2)，使用弧度；不能把两个接口的角度单位混用。

[SceneComponent.cpp](../../../src/Framework/Engine/Private/SceneComponent.cpp) 按 parentWorld×local 递推世界矩阵，dirty 后再更新。AttachTo(keepWorldTransform=true) 计算 newLocal=inverse(parentWorld)×oldWorld，再提取平移、欧拉角和缩放，意图保持世界变换。

矩阵列长度提取缩放会丢失负尺度符号，一般带 shear 的矩阵也不能无损表示为该 TRS；欧拉角存在奇异姿态。SetWorldLocation 使用父逆矩阵转换，而当前 SetWorldRotation/SetWorldScale 直接写入 local 字段，不能根据函数名声称有父节点时也完成了严格世界空间赋值。AttachTo 对层级环的防护也应单独核查，否则递归 dirty/更新可能无法终止。

## LookAtRH 的推导

f=normalize(center-eye)，s=normalize(cross(f,up))，u=cross(s,f)。view 的前三行分别为 s、u、-f，平移列为 `(-dot(s,eye), -dot(u,eye), dot(f,eye))`。这使眼点到原点，朝向中心的方向位于相机 -Z。

eye=center 或 up 与 f 平行不能建立有效正交基，需要调用方排除；Normalize 的零长度处理不等价于修复退化相机。

### Editor 的六向视角

[EditorScene::SetViewDirection / UpdateCamera](../../../src/Editor/SceneEditor/Private/EditorScene.cpp) 用 EEditorViewDirection 表示六个固定方向及自由 Orbit。[EditorViewToolbar](../../../src/Editor/SceneEditor/Private/EditorScenePanel.cpp) 的 View 页调用它，不在 UI 内复制相机数学。

沿用世界 Y-up、右手 LookAtRH。输入为当前目标点 t、正距离 d，以及所选方向；输出为相机 eye、forward 和 up。统一 `eye=t-forward*d`，因此切换不改变目标点或缩放距离。表中的“相机侧”是相对目标的位置，不是世界绝对坐标。

| 视图 | 相机侧 | forward | up |
| --- | --- | --- | --- |
| Top | +Y | (0,-1,0) | (0,0,-1) |
| Bottom | -Y | (0,1,0) | (0,0,1) |
| Left | -X | (1,0,0) | (0,1,0) |
| Right | +X | (-1,0,0) | (0,1,0) |
| Front | +Z | (0,0,-1) | (0,1,0) |
| Back | -Z | (0,0,1) | (0,1,0) |

Top/Bottom 的 forward 与世界 Y 平行，若继续用 (0,1,0) 作为 up，`cross(forward,up)=0`。显式选用 ∓Z 后，屏幕 right 均为 +X，可构造有效正交基。固定方向使用表中的精确向量，不依赖 cos(π/2) 接近零的浮点残差来避免退化。

自由模式使用弧度制 yaw ψ、pitch θ：`forward=(cosψ*cosθ, sinθ, sinψ*cosθ)`，up=(0,1,0)。切换固定方向时同时设置与之对应的 yaw/pitch，右键拖动转回 Orbit 后从该方向继续；自由 pitch 限制到 [-1.5,1.5]，不会停在 ±π/2 的极点，首次离开极点会有约 0.071 弧度的夹角修正。Orbit 按钮恢复原斜向角度，保留当前目标与距离。

平移以 `right=normalize(cross(forward,up))` 和修正后的屏幕 up 建立偏移基，缩放只调整 d（既有 [0.2,1000] 范围）。Focus selected 更新 t，三者都保留所选方向。每次转换为常数复杂度、无额外堆分配。当前六向只调整朝向，Draw 仍使用 45°、真实视口宽高比、ZO 透视投影；没有把它宣称为正交视图，也未改造依赖透视的 Renderer/后处理。

2026-10-26 定向检查使用两颗不同位置的红/蓝 Unlit 球，六方向各渲染并读回 384×384 像素，核查可见性及蓝球位于预期屏幕象限，包含 Top/Bottom。另检查方向在平移/缩放时保持、在环绕时解除，以及 ribbon 绘制数据/布局。临时验证源位于 out/diagnostics/editor-smoke/view-main.cpp，未改动 src/Tests 流程；结果和桌面交互限制见 [同日记录](../../dev/bugfix/故障与修复记录-2026-10-26.md)。

## NO 与 ZO 透视

设 q=1/tan(fovy/2)，aspect=a，near=n，far=f，右手相机 z<0：

| 系数 | PerspectiveRhNo | PerspectiveRhZo |
| --- | --- | --- |
| M00/M11 | q/a、q | q/a、q |
| M22 | -(f+n)/(f-n) | f/(n-f) |
| M23 | -2fn/(f-n) | fn/(n-f) |
| M32 | -1 | -1 |
| 投影后的 z 范围 | [-1,1] | [0,1] |

名称 RH 指手性，NO/ZO 指 NDC 深度范围；它们都不自动表示 reversed-Z。当前正常 near/far 配合深度 clear=1、Less 比较，不能仅根据 Vulkan/D3D12 平台名推导是反向深度。

正交投影则以 x/y/z 的线性缩放和平移映射盒子，不产生透视 w。CSM 的 minZ/maxZ bounds helper 与以 near/far 表达的相机接口也不能交换参数意义。

## Frustum 六平面与 AABB

对于 clip 不等式 `-w<=x,y,z<=w`，可由 VP 行向量组合得到左右 `r3±r0`、上下 `r3±r1`、近远 `r3±r2`；SceneView::BuildFrustum 当前就是这个公式，再对 (A,B,C,D) 同比归一化。

若深度范围为 ZO，近面应来自 `z>=0`，即 r2；所以当前 CPU BuildFrustum 的近面公式与 ZO 路径需要核对。GPU FrustumCullCS 明确测试 ndc.z∈[0,1]，两路不能未经验证就声称等价。

IsBoxVisible 对每平面选择让 `n·p` 最大的 positive vertex：n 分量正则取 Max，负则取 Min。若连这个点都在外侧（n·p+d<0），整个 AABB 均在外侧；否则只是未排除，不保证盒内所有点都可见。每盒六面常数成本，无需八角点逐一测试。

GPU 分支把八角点投到 clip/NDC，累积共同越界位；穿越 w=0/近面的保守性仍需核查。CPU 场景默认使用前述平面测试。

## 反投影与世界位置恢复

给屏幕 uv 与深度 d，当前 GBuffer helper 使用 `x=2u-1、y=2(1-v)-1、z=d、w=1`，乘 InvViewProjection 再除 w。它假设深度已为 ZO；NO 需要相应 z 变换。深度是投影后的非线性量，不能直接当视空间米数。

SceneView::RebuildDerivedMatrices 只算 VP，注释明确未在该函数求 InvVP；必须核查具体调用方是否填入逆矩阵。错误的 InvVP 会影响 SSR、SSGI、阴影角点等所有下游。

GetFrustumCornersWS 使用 bDepthRangeZeroToOne 选择 NDC 近 z=0 或 -1，反投影整视锥后，按物理 near/far 比例插值得到级联角点。该 flag、实际投影矩阵与 Shader 重建公式必须一致。

## 深度金字塔与坐标验证

DepthPyramidGenerateCS 当前取四邻域 min。标准 Z 中，保守“最远深度”应是 max；reversed-Z 中最远才是 min。源码注释写 reversed-Z，不能作为上游确实使用反向深度的证明；每个使用者的比较式也要一起核查。

另有 viewport/scissor、窗口客户区与请求尺寸、GL 纹理原点等差异。既往三角形测试的尺寸差异来自 BeginRenderPass 覆盖显式视口的后端差别，不应通过改投影矩阵绕过；见 [RHI 回归记录](../../dev/bugfix/故障与修复记录-2026-10-25.md)。

验证入口：MathTest 打印 LookAt/Perspective/VP；不能代替 NO/ZO 近远平面、逆变换、负缩放、非均匀缩放和跨后端图像的数值断言。本轮仅源码核对。

## 编辑器方向轴：球面、屏幕命中与相对偏航

源码：[EditorViewAxes.cpp](../../../src/Editor/SceneEditor/Private/EditorViewAxes.cpp)、[ViewAxes.sf](../../../shaders/editor/ViewAxes.sf)、[EditorScene 输入](../../../src/Editor/SceneEditor/Private/EditorScene.cpp)。此处为叠加示意的正交投影，不修改场景相机已有的透视投影；架构见 [Editor](../../architecture/app/editor.md)。

令视口宽高为 W/H，S=min(144,min(W,H)-24)，S<48 时不绘制、不命中。局部区域左上为 (W-S-12,12)，中心 c=(W-S/2-12,12+S/2)。从 ViewMatrix 取旋转 R，以 w=0 忽略平移；对世界单位半轴 e=±X/±Y/±Z 得 q=R e，端点像素坐标为 p=c+(a q.x,-a q.y)，a=0.62S/2，屏幕 Y 向下。绘制加入 SceneView::Viewport.X/Y，输入已由 Window 减去 Widget 位置，使用同一局部坐标。

端点为以 e 为中心、半径 r=0.22 的球面：n=(sinθ cosφ,cosθ,sinθ sinφ)，v=e+r n；θ=πi/12，φ=2πj/16，每格两个三角形，由 SV_VertexID 生成顶点。背面剔除，法线同样乘 R，采用相机空间固定方向的简单明暗，不参与场景光照。负端点颜色为 C-=0.45 C+0.55，正端点保持 X 红/Y 蓝/Z 绿。细分数和 0.62 缩放由 C++ 向 Shader 注入宏，绘制与命中共用缩放来源。

先画轴线，再按 q.z 从小到大绘制球体；相机空间 +Z 更靠近观察者。图标不使用场景深度，重叠采用画家顺序。命中按相反顺序检查 |mouse-p|² <= (ar)²，优先选择画在上面的球。命中采用理想投影圆，不逐三角形求交，细分球边缘可能有不足一个像素的差异；轴正对相机时前后端点重合，选择前端点。

按下命中后保存初始偏移 v0=CameraPosition-Target、单位相机上方向 k 和鼠标位置。累计移动达到 4 像素前不改变视图；超过阈值后，水平差 Δx 控制偏航 α=-0.005Δx，垂直差 Δy 控制俯仰 β=-0.005Δy（弧度，屏幕 Y 向下）：

```text
Rotate(v,n,δ) = v cosδ + (n × v) sinδ + n (n·v) (1-cosδ)
yawOffset = Rotate(v0,k,α)
right = normalize(k × yawOffset)
v = Rotate(yawOffset,right,β)
CameraPosition = Target + v
CameraUp = Rotate(k,right,β)
```

这是两次 Rodrigues 旋转，先绕按下时相机上方向偏航，再绕偏航后的相机右方向俯仰。偏移和上方向随俯仰一同旋转，保留距离 |v0|、焦点以及二者原有夹角，避免只改偏移导致极点视图基底退化；两次角度均从按下位置累计计算，不重复累加已应用的旋转。Top/Bottom 视图同样可连续旋转。前向 -normalize(v) 回写 yaw=atan2(forward.z,forward.x)、pitch=asin(clamp(forward.y,-1,1))；m_OrbitUp 保留旋转后的上方向，后续平移/滚轮重建相机时继续使用该基底。既有右键环绕仍采用世界 Y 向上的常规控制。

未拖动且松开位置仍命中原球体才触发六向切换；拖动释放不切换。水平、垂直和斜向拖动都改变相机，失焦清除按下状态。规模固定为六条线、六个 384 三角形球、12 次 Draw；排序/命中最多六个元素。实际输入链、像素和退出检查见 [2026-10-28 记录](../../dev/bugfix/故障与修复记录-2026-10-28.md)，本项验证不推及整个 MathTest。
