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

悬停端点由同一个 HitTest 得到，按住时优先显示按下端点；对应轴线和球体底色设为黄色，保留球面明暗。MainVS 以球心投影为原点、球半径为单位生成屏幕正向 Label.xy；LabelInfo.x 保存端点编号 0～5，顺序为 +X/-X/+Y/-Y/+Z/-Z。MainPS 采用固定 3×5 字形：前三列画正负号、一列间隔、后三列画字母，`cell=floor((Label.x+0.7,0.5-Label.y)*5)`。每格宽高为半径的 0.2，完整文字宽 1.4 倍半径、高 1 倍半径，中心落在球心，墨色为深色；标签不随球体旋转，不另分配字体纹理。极小视口下标签随球缩小，是简洁像素字形，不声称具备通用文本排版能力。

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

## 节点位移编辑轴：投影、约束命中与世界空间拖动

源码：[EditorTranslationGizmo.h](../../../src/Editor/SceneEditor/PublicHeader/EditorTranslationGizmo.h)、[实现](../../../src/Editor/SceneEditor/Private/EditorTranslationGizmo.cpp)、[TranslationGizmo.sf](../../../shaders/editor/TranslationGizmo.sf)。`SetTarget` 借用 SceneComponent，`HitTest`/`BeginDrag` 接收当前 SceneView 和视口内部鼠标像素位置，`Drag` 只输出候选世界位置，由宿主在原有资源更新时序中应用。绘制通过 SceneViewDrawExtension 加入当前 RDG，架构和场景变更时序见 [Editor](../../architecture/app/editor.md) 与 [UI 工程实现](../performance/08-ui-rendering-and-window-lifetime.md)。本节描述世界空间位移；局部缩放和旋转见后续两节，当前工具不包含吸附和撤销。

坐标基固定为世界单位轴 eX/eY/eZ，不随组件自身或父节点旋转。X 红、Y 蓝、Z 绿，与右上方向示意一致；`ETranslationHandle` 包含 X/Y/Z 单轴和 XY/XZ/YZ 双轴平面。原点 O 取 `target->GetWorldLocation()`。设 `c=ViewProjectionMatrix·(O,1)`，视口尺寸为 W/H，`Project` 计算局部屏幕像素 `sx=(c.x/c.w+1)W/2`、`sy=(1-c.y/c.w)H/2`；屏幕 Y 向下。鼠标输入已经相对于视口，不再叠加窗口坐标或 Viewport.X/Y；实际 RHI viewport/scissor 才使用视图在附件中的 X/Y。

`BuildGeometry` 按 `AxisPixels=90` 估算世界长度：

```text
L = 2 × AxisPixels × c.w / (abs(ProjectionMatrix(1,1)) × H)
axisEnd[i] = O + L × e[i]
```

由屏幕纵向的投影导数 `pixelsPerWorldUnit=abs(P11)H/(2c.w)` 得此逆比例。90 像素是与像平面平行的参考长度，世界轴仍会因朝向和透视产生缩短，并非三条轴的实际投影都固定为 90 像素。常规正交矩阵的 c.w=1 时此式也成立；当前 Editor 仍使用透视场景相机，不将公式适用性当作 Renderer 正交流程已验证。`Project` 拒绝非有限值、c.w<=1e-5 及近远裁剪范围之外的点；NO 使用 -c.w<=c.z<=c.w，ZO 使用 0<=c.z<=c.w。Origin 不可投影时整个工具不绘制/命中，某个端点不可投影时跳过对应轴；屏幕边界外的几何由视口裁剪。

三个平面四边形分别为 `{O, O+0.30L·a, O+0.30L·(a+b), O+0.30L·b}`，其中 `(a,b)` 为 `(eX,eY)`、`(eX,eZ)`、`(eY,eZ)`。O 确实是四边形的一个顶点。平面使用缺失轴的颜色并半透明显示；悬停或按住的句柄为黄色。平面先按中心的视线距离从远到近绘制，再绘制箭头轴；没有场景深度测试，因此作为可操作的编辑叠加显示在模型上方。

`MakeRay` 以鼠标 `(x,y)` 计算 NDC `(2x/W-1, 1-2y/H)`，将近 z=0（ZO）或 -1（NO）及远 z=1 分别乘 InvVP、除 w，得到近点 R 和单位方向 d。源视图的深度约定必须与投影对应。局部坐标与视口尺寸非有限/非法、齐次 w 过小或近远点重合时返回失败。

`HitTest` 先检查三条轴，再检查平面：

- 轴使用投影端点与原点形成屏幕线段，只允许外侧 `t∈[0.38,1]`，与原点附近平面区域分开；取鼠标到此段的距离，最近且小于 `HitPixels=7` 像素者命中。投影长度小于 `MinAxisPixels=12` 的轴不参与交互，避免正对视线的轴被误选。
- 轴还要求 `GetAxisPlaneNormal` 能建立稳定拖动平面，即 `|d-e(d·e)|²>=0.01`。这样命中结果本身已经排除了近乎沿轴观察的退化射线。
- 平面使用射线与世界平面求交，法线 n=a×b。`IntersectPlane` 计算 `t=dot(O-R,n)/dot(d,n)`、`Q=R+t·d`；要求 `abs(dot(d,n))>=MinPlaneCosine=0.06`、t>=0 且结果有限。再要求 `dot(Q-O,a)/L` 与 `dot(Q-O,b)/L` 均在 `[0,0.30]`，多平面覆盖时取最近交点。绘制/命中都按原点中心射线排除近乎侧视的平面，鼠标自身射线仍须通过相同分母检查。

`BeginDrag` 保存 `m_StartOrigin`、首次交点 `m_StartHit`、`m_DragNormal`，以及按下时的 InvVP、Viewport 和 NO/ZO 标记。单轴句柄以按下射线 d0 构造法线 `n=normalize(d0-e·dot(d0,e))`：n 与 e 垂直，故平面包含选定轴；同时它是视线在轴正交平面上的分量，使平面尽量面向观察方向。平面句柄直接使用 a×b。由首次鼠标交点作为锚点，按下不会把对象中心吸到鼠标射线上。

后续 `Drag` 始终用按下的相机与固定世界平面求当前交点 Q，令 Δ=Q-m_StartHit，输出位置：

```text
单轴：   candidate = m_StartOrigin + e × dot(Δ,e)
双轴：   candidate = m_StartOrigin + a × dot(Δ,a) + b × dot(Δ,b)
```

式中的乘号表示标量乘向量，不是叉积。实现利用世界单位基，直接将 candidate 初始化为 m_StartOrigin，再只累加选中的一个或两个数组分量；未选分量不会被浮点投影计算改写。位移从按下位置累计，未重复累加已经应用的量，也不随着移动中的对象重新设置拖动平面。Viewport 的 X/Y/Width/Height 改变会拒绝候选输出；宿主在 resize、切换工具/目标、失焦等取消边界中结束拖动。平面交点退化或输出非有限时保持原位置；不能以任意大倍率放大鼠标移动掩盖退化。

世界约束与节点属性中的局部 Position 需要区分。宿主调用既有 [SceneComponent::SetWorldLocation](../../../src/Framework/Engine/Private/SceneComponent.cpp)，无父时 local=candidate，有父时 `local=inverse(parentWorld)·(candidate,1)`；只改变位置，保留局部旋转和缩放。父节点有旋转时，沿一个世界轴移动可能改变多个局部 Position 分量，世界空间仍只沿选定方向移动。非均匀/负缩放同样经过完整父逆矩阵，而非只除以一个缩放数。

`CanSetWorldLocation` 检查父矩阵元素有限，设其前三列长度之积为 V，要求 V>0、det 有限且 `abs(det)>1e-6·V`，按相对体积排除零缩放和近奇异基底，同时允许合法的小尺度。Core::Inverse 本身仍没有奇异保护；本检查属于该编辑工具的输入约束，没有改写公共数学接口。固定三个轴与三个平面使几何、命中和拖动均为常数规模；箭头/平面最多形成 15 个屏幕三角形，既有 Shader 参数和 Builder arena 承载本帧快照，不需要为命中建立场景加速结构。

独立公式检查入口为忽略目录 `out/diagnostics/translation-gizmo-math.py`，结果保存在同名 JSON。2026-10-29 的 float64 检查覆盖 NO/ZO × Orbit/Front/Top/Right 共 8 组视图，逐轴/逐平面验证约束、父旋转加非均匀负缩放的世界/局部往返，以及 90 像素参考尺度；正对视线轴和近乎侧视平面明确拒绝。最大拖动误差 2.44e-14、父变换往返误差 3.02e-16、参考像素长度误差 2.84e-14。它验证独立公式，不代替实际 C++ float、命中所有分支、GPU 画面或完整 UI 流程验收；后者的真实检查范围见 [2026-10-29 位移工具记录](../../dev/bugfix/故障与修复记录-2026-10-29.md)。

## 局部缩放编辑轴：单位标记与轴向拖动倍率

新增实现：[EditorScaleGizmo.cpp](../../../src/Editor/SceneEditor/Private/EditorScaleGizmo.cpp)。声明与共享绘制函数继续位于 [EditorTranslationGizmo.h](../../../src/Editor/SceneEditor/PublicHeader/EditorTranslationGizmo.h)，三个具体工具复用同一个 Shader 和 `GizmoDrawing::AddPass`，没有为缩放另建 GPU 几何或 Shader 文件。

缩放和旋转使用局部编辑语义。`GizmoDrawing::RotationBasis` 从目标向父链逐层左乘 `MakeRotationMatrix(LocalRotation)`，得到 `B=R_parent0·...·R_target`，其列为编辑工具的三个正交、右手单位方向。父子所有缩放均不参与 B，因此负缩放不会反转工具颜色轴，非均匀缩放也不会把圆环拉成椭圆。原点仍取目标真实世界位置。这个 B 表示层级旋转形成的编辑方向，不是对可能带 shear 的完整世界矩阵做分解；有非均匀缩放父级时，实际模型几何仍保留其仿射变形。

`BuildGeometry` 使用 `UnitAxisPixels=55` 计算单位参考长度，再按对应局部缩放分量的绝对值伸缩：

```text
unitLength = GizmoDrawing::WorldLength(view, O, 55)
end[i] = O + B[i] × unitLength × abs(LocalScale[i])
midpoint[i] = O + B[i] × unitLength × abs(LocalScale[i]) × 0.5
```

缩放绝对值为 1 时采用 55 像素参考尺度，比原先的 90 像素标尺更接近原点；这不是 1 世界单位，轴的实际屏幕长度仍受朝向和透视影响。各端点随组件当前 LocalScale 改变，负值只影响长度的绝对值，不反转 B 的颜色轴方向；父级缩放不参与该长度计算。每条轴端放半径 5 像素的圆点，连接三条轴各自世界长度 50% 处的中点形成三角形，端点圆点仍留在完整轴长处。中点先在世界空间求出，再独立调用 `Project`，不能直接平均屏幕原点和端点，因为透视除 w 后的屏幕中点通常不是世界中点。连接边只作视觉辅助，不提供额外的均匀缩放或双轴缩放句柄。绘制和命中都调用同一个 `BuildGeometry`，不会出现画面端点已移动但仍命中旧位置的两套几何。

命中先检查端点圆点（中心距离小于 8 像素），再检查当前轴段的 `t∈[0.15,1]`（距离小于 7 像素），选择最近项；这里 t 为当前整条轴段的比例，共享原点不单独命中。轴投影长度至少为 12 像素，所选 LocalScale 必须有限且绝对值在 `[0.01,10000]` 内，否则对应轴淡化且不接受拖动。悬停和按住项以黄色绘制；零或超出范围的输入不会被工具在按下时偷偷改写。没有另加最短/最长显示长度钳制：极小但仍在数值范围内的缩放也可能因 12 像素下限无法重新命中，极大缩放可能使端点越过近远裁剪面而不显示，或投影到视口外而被裁剪；可通过属性数值恢复，不保证整个允许数值区间内各轴始终可拖。

`BeginDrag` 保存完整 m_StartScale、鼠标 m_PressPosition、所选 m_Active 和视口，并保存按下时从投影原点指向所选轴投影端点的单位屏幕方向 m_DragDirection。由至少 12 像素的可操作轴建立该方向，后续不随端点伸缩重新计算。鼠标坐标和方向都采用屏幕 Y 向下的同一约定：

```text
dragDirection = normalize(screenEndAtPress - screenOriginAtPress)
distance = dot(currentMouse - pressMouse, dragDirection)
exponent = clamp(0.01 × distance, -20, 20)
magnitude = clamp(abs(startScale[axis]) × exp(exponent), 0.01, 10000)
candidate = startScale
candidate[axis] = copysign(magnitude, startScale[axis])
```

沿按下轴的屏幕方向向外拖时 distance 为正，绝对缩放增大；向原点拖时为负，绝对缩放减小。垂直于该方向的位移不参与倍率，不能再将“水平移动不变”用于任意朝向的轴；distance 为 0 时原样返回按下值。以按下值和固定方向重新计算，既避免重复累乘已经生效的比例，也避免移动端点改变倍率方向或灵敏度；未碰上下限时反向等距离拖动具有相反的对数倍率。仅替换选定数组分量，其他缩放分量逐值保留，位置和旋转由宿主保持；负尺度保留符号且不会跨过零，因此向外拖会让负值的绝对值增大。输出为 LocalScale，宿主使用 `SetLocalScale`，不调用当前尚未实现严格父空间转换的 SetWorldScale。视口变动或非有限输入拒绝输出，具体取消捕获与同步沿已有宿主路径进行。

规模固定为三条轴、三个 12 段圆点和三条连接线，最多 48 个屏幕三角形。当前共享路径每个三角形提交一次 Draw，便于复用已有参数/Shader，但并不等于做过绘制调用批处理优化。

2026-10-30 的 C++ 检查覆盖 24 组相机方向、组件旋转及正负尺度组合，验证向外增大绝对值、向内缩小、垂直方向不变、回到按下位置和重复事件幂等。Vulkan 实际窗口输入及 GPU 读回验证了三轴方向、动态端点/命中和中点连线；完整 Editor UI 与 Launcher 正常关闭范围见 [验证记录](../../dev/bugfix/故障与修复记录-2026-10-30.md)。

## XZ 地面网格与光源提示几何

源码：[HintRender.cpp](../../../src/Framework/Renderer/Private/HintRender.cpp)、[接口](../../../src/Framework/Renderer/PublicHeader/HintRender.h)、[Shader](../../../shaders/HintRender.sf)。输入为本帧 SceneView、颜色/深度 RDG 附件和可选光源组件；输出均为线列表 Pass。一世界单位按一米解释，深度测试开启、深度写入关闭。以下为当前可见区域几何方案；此前全屏求交版本的实现与问题保留在 [历史修复记录](../../dev/bugfix/故障与修复记录-2026-10-31.md)。

### 可见地面交面与 XZ AABB

提示的启用入口是 SceneInterface 保存的 SceneSetting；Renderer::BeginRender 在场景主体之后按设置添加目标光源 Pass，执行全部视图扩展后再添加网格，Editor 不直接调用 HintRender。设置、借用目标及参数快照的线程和生命周期约束见 [场景提交](../performance/08-ui-rendering-and-window-lifetime.md)。下述几何算法不因调用入口调整而改变。

网格平面是 XZ，即 Y=0。相机到平面的垂直距离 h=abs(CameraWorldPos.Y)，不是到世界原点的距离。GetGridSpacing 先算 desired=clamp(0.1*h,0.1,10)，再从 S={0.1,0.2,0.5,1,2,5,10} 取不小于 desired 的最小值；AddGridPass 在此基础上加入每块区域到相机的距离，分别选择间距。相机在平面下方采用同样规则。

为避免远离原点后的矩阵精度损失，CPU 从相机 X/Z 移除 floor(C/10)*10 的整数周期，Y 不变，并用原 ViewMatrix 的旋转行及相对相机位置重建 Vrel[row,3]=-dot(V[row].xyz,Crel)。计算 VP=Proj*Vrel 及其逆矩阵。S 中每个间距都整除 10 米，故此平移不改变网格相位；场景和其他 Pass 的矩阵未改变。后续 bounds 和 Shader 中的 WorldPosition 实际是此相对世界坐标，不是绝对大坐标。

`GetGroundBounds` 将 NDC 八角 `(±1,±1,z,1)` 乘逆 VP 并除 w，其中近 z=0（ZO）或 -1（NO）、远 z=1。拒绝非有限结果及 abs(w)<1e-7。对视锥的近面四边、远面四边和连接近远面的四边逐一检查：端点 a/b 的 Y 异号时，交点为 `p=a+(b-a)*[-a.y/(b.y-a.y)]`；abs(Y)<=1e-5 的角点也加入，并将结果 Y 设零。

视锥是凸体，因此平面截面的极点位于这些边上；对所有交点取 min/max X/Z 就能包住可见地面。截面可能为三角形、四边形或更多顶点，跨地平线或带滚转时不能只取四条角点射线的交点，也不能假定始终恰好存在最近两个点和最远两个点。没有交点或 X/Z 范围退化时不添加网格 Pass。该方法同时适用于有限远面的透视和正交投影；没有实现无限远投影的专用处理。

### 100×100 控制顶点与分块距离细分

CPU 保留固定的 100×100 整数控制顶点 `(x,z)`，x/z=0…99，共形成 99×99 个基础区域。为避免没有必要的浮点存储，每个顶点按 `x | (z<<16)` 存成一个 uint，GPU 解码低/高 16 位；总数据 40000 字节。该缓冲使用 Shader SRV 读取，LineList 本身不绑定传统顶点描述或索引缓冲，不意味着只有原先四个角点。

对交面 AABB 的每个 X/Z 分量向外对齐到 10 米，再求所有基础块统一的方形边长 c：

```text
origin   = (floor(bounds.Min/10)-1)*10
required = (ceil(bounds.Max/10)+1)*10-origin
c        = ceil(max(required.x,required.z)/(99*10))*10
worldXZ  = origin + controlVertexXZ*c
```

c 至少为 10 米，且是 10 米整数倍；99c 覆盖所需范围。选择共同方形边长可使每个档位都整除基础块，避免长宽各取任意比例后局部格子成为非正方形。该范围包住实际交面，不是固定相机周围方块；窄长交面会多覆盖一些可见区外面积。整个网格仍受真实近远裁剪，所谓延伸不是数学意义的无限远。

CPU 枚举 99² 块。`IsGridPatchVisible` 将一块四角乘 VP，只有四角都在同一齐次裁剪平面之外时才剔除；深度边界使用当前 NO/ZO 约定。这是保守剔除，不是只看块中心，跨近面的区域仍可保留。对保留区域的 [minX,maxX]×[minZ,maxZ]，求相机到区域上最近点的距离：

```text
dx = eye.x-clamp(eye.x,minX,maxX)
dz = eye.z-clamp(eye.z,minZ,maxZ)
d  = sqrt(dx²+h²+dz²)
q  = max(h,0.2*d)
s  = GetGridSpacing(q) // ceil_S(clamp(max(0.1*h,0.02*d),0.1,10))
```

使用区域最近点而非中心避免相机所在的大块因中心较远而丢失近处细节。近处仍由高度约束，远处逐步选取更大的米制间距；最小 0.1 米、最大 10 米。相同间距的基础块索引压到一起，每档一个实例 Draw，最多七个 Draw，未保留区不提交。每个索引是基础块左下控制顶点在 100×100 缓冲中的下标；右上顶点下标再加 101。

### VS 展开、边界归属与远处淡出

`PatchSettings=(本档索引起始位置,c/s,100)`。MainGridVS 通过 `SV_InstanceID` 读取块索引，解码两个控制顶点并映射到世界范围；`SV_VertexID/2` 选择线段，最低位选择端点。一块按 X/Z 两方向各生成 n=c/s 条线，总计 4n 个顶点。固定坐标使用 `(round(cellMin/s)+index)*s`，沿线坐标使用块的两端；不通过相反的大浮点数相加重建原点附近网格。

每块只包含自己的低边界，不重复生成高边界；相邻块拥有该共享边，外部缺少的最外高边界已经在可见范围之外。块与世界相位都是 10 米整数周期，所有细分档位整除它，因此主线位置一致。不保证不同档位的每条次线都连续：0.2/0.5 等档位不完全嵌套，会在分界形成次线终点；当前没有硬件曲面细分或连接带，离散密度切换仍可能观察到。主线按每 10 米着较亮颜色，次线较暗，都是原生约 1 像素宽 LineList。

MainGridPS 保留投影密度抑制：当前点 p、垂直该线移动 s 的点 a、沿线移动 s 的点 d 分别投影并除 w，乘 `(viewportWidth,viewportHeight)/2` 得像素差 A/D；局部平行线垂直间隔估计 `spacing=abs(A.x*D.y-A.y*D.x)/max(length(D),1e-5)`。透明度乘 `saturate((spacing-0.65)/1.85)`，主线的估计步长使用 10 米。这处理掠射角压缩；它是有限差分，跨 w=0 样本的稳定分母并不等同精确距离场。

再按逐像素到相机的真实距离 r 计算 `fade=exp2(-r/max(40,10*h))`，最终 alpha=原 alpha×密度因子×fade。使用透视正确插值的 WorldPosition，而不是插值每顶点距离，使一条很长线中间不会因端点都远而错误消失。透明混合的线条贡献随距离连续趋近零，露出原场景颜色；直接把 RGB 乘小却保持 alpha，会产生更黑的遮挡，不能达到远处消隐。该效果是透明度衰减，未实现模糊卷积、雾或后处理。固定功能混合继续使用 SrcAlpha/OneMinusSrcAlpha，但写掩码为 RGB（0x7），不把该衰减权重写回场景 Alpha。空场景背景清为 (0,0,0,1)，GizmoDrawing 也仅写 RGB；GUI 图像采样因此不再将网格区域的 Alpha 当作第二次透明度。网格在所有视图扩展构图后加入，颜色 Load_Store 及深度依赖使其叠加在完整场景之后；并未复制场景颜色或增加读回/等待。

相机高度 20 米时基础间距为 2 米；区域最近距离达到 100/250 米之外后分别可进入 5/10 米档。40 米是近地相机的最小衰减半距，高处则按 10h 调整，避免俯瞰时整个网格消失。

### 复杂度、资源与退化条件

反投影视锥和求交为常数规模；分块枚举 O(99²)，筛选索引占用最多 9801×4=39204 字节，基础顶点 40000 字节。最多七个 Draw，GPU 顶点数为 `sum(4*c/s_i)`（对所有提交块求和），PS 成本由实际线覆盖和过绘制决定。远处增大 s 同时减少真实生成的顶点和线条，不只是把细线在 PS 中涂透明。固定控制数据和每视图索引沿用 RDG transient 缓冲/上传，未建立常驻网格缓存或缓冲池；[工程代价与完成点](../performance/08-ui-rendering-and-window-lifetime.md)另述。

这是 CPU 按区域选细分数量、VS 通过实例展开线段的实现，没有将未接通的 HS/DS 声称为已实现。极大远裁剪会令 c 变大，靠近相机的大块仍可能生成大量细线；超过单 Draw 的 uint32 顶点数时明确报错。有限远面限制、世界输入本身的浮点精度、档位跳变、近零高度侧视退化、未做性能基准均保留。

### 光源提示

点光球心为 GetWorldLocation，半径为 GetAttenuationRadius。在 XY、XZ、YZ 三平面各采样 64 段大圆，P(a)=O+r*(u*cos(a)+v*sin(a))，颜色固定蓝色。这是以三个大圆表达球形范围，未做带更多经纬线的球面细分；半径不额外乘节点缩放。

方向/聚光先检查 Direction 有限且非零并归一化为 d，再选与 d 不近平行的参考轴，构造 u=normalize(cross(d,reference))、v=cross(d,u)。聚光沿方向的长度 L 使用衰减范围，底圆中心 C=O+L*d、半径 r=L*tan(outerAngle)；底圆 64 段，均匀采 16 个底圆点与 O 相连构成母线，中心射线连接 O/C。外角存储为弧度，面板读出转度，setter 再转回弧度；内角限制为 [0,外角]，外角为 [1,89] 度。该示意沿用当前范围作为锥的轴向长度，不额外绘制衰减球与圆锥的交线，也不显示内锥。

平行光无有限衰减范围，使用从 O 沿 d 长 1.5 米的箭杆，末端均匀八个方向生成箭头线：end → end-0.3*d+0.15*(u*cos(a)+v*sin(a))，a=2π*i/8。共九段，避免只有平面内两根箭翼时侧看接近不可辨。天空光没有局部范围，不生成范围 Pass。非有限位置、方向、半径或退化外角不生成非法几何。提示大小使用世界单位，不保证极近、极远视角下保持固定像素大小。

### 光源范围与箭头的解析拾取

入口是 SceneInterface::Raycast 的 Lights/All 过滤，实际数学位于 HintRender::RaycastLight；Meshes 默认查询不包含这些编辑范围。Scene 先将射线方向归一化，再传入有限世界距离上界。提示绘制及拾取均采用一世界单位一米，局部光范围不乘组件缩放；未显示范围提示也可按范围查询注册光源，当前绘制开关仍只控制原有目标光源的范围显示。天空光无空间拾取体。

点光以代理位置 C 和衰减半径 r 表示球面。对 P(t)=O+tD，令 V=O-C，求 `(D·D)t²+2(V·D)t+(V·V-r²)=0`。QuadraticRoots 使用 double 中间值和 `q=-0.5*(b+copysign(sqrt(b²-4ac),b))`，两个根为 q/a、c/q，q=0 时用重根，避免一大一小两项相减的消减误差。判别式仅在相对 1e-12 容差内钳为零。取 0<=t<=上界的最小根；内部起点使用正向出口，而不因位于体积内就返回零，避免大范围光源以零距离覆盖近处网格。起点恰在表面仍可为零。

聚光以代理位置为锥顶 C、归一化方向 A、轴向长度 L=衰减范围、外半角 θ 构造闭合有限圆锥，与提示底圆半径 L*tan(θ) 一致。令 V=O-C、h=V·A、s=D·A、k=cos²θ，其侧面二次式为：

```text
a = s²-k*(D·D)
b = 2*(h*s-k*(V·D))
c = h²-k*(V·V)
```

每个根须满足轴向高度 `0<=h+s*t<=L`，排除无限双锥的另一半及越界部分。近平行母线使 |a|<=1e-12 时按线性式求解。另测高度 0/L 的顶点与底面：`t=(cap-h)/s`，交点到轴心的径向距离不超过 cap*tan(θ)。边界容差为 `1e-7*max(1,L)`；即使 s 非零，也先检查起点是否已处于底面/顶点容差内，避免组件 Euler 产生的极小方向误差把零距离误判成下一次侧面交点。内外角均遵循现有组件 getter 的弧度值，内角不影响外范围拾取。非法半径、外角、方向或射线不命中。

平行光没有有限照射体，拾取调用与绘制共用的 ForEachDirectionalLine：箭杆 O→O+1.5A，八条头部线长向回退 0.3 米、径向 0.15 米。对一条线段 P+uE 和有限射线 O+tD，最小化 `|W+tD-uE|²`，W=O-P，约束 t∈[0,T]、u∈[0,1]。设 dd=D·D、ee=E·E、de=D·E、dw=D·W、ew=E·W，内部驻点为：

```text
determinant = dd*ee-de²
t = (de*ew-ee*dw)/determinant
u = (dd*ew-de*dw)/determinant
```

同时检查四条约束边 t=0、t=T、u=0、u=1 各自的一维最小值，平行情形跳过内部驻点，退化线段按点处理。最短距离<=0.05 米即命中，返回相应射线 t，WorldPosition 仍在射线上，可能不恰在线段上。该阈值为世界长度，不是固定像素宽度；远处箭头可继续通过已有图标选择。箭头共享一套线段生成，避免显示和拾取分别维护不同端点。

Scene::Raycast 在网格与光源间比较世界距离，光源结果的 SectionIndex/MaterialIndex/TriangleIndex 均为 -1。图标仍有编辑器叠加优先级；其他范围按数学边界选择，不模拟线框像素覆盖或透明材质。选中范围较大的光源可能使背景点击命中光源；重叠范围按最近边界/线段距离判定，严格重合光源没有稳定排序保证。数学成本每灯为常数，注册表遍历 O(L)，无新 BVH、持久缓存或 GPU 查询。

### 光源方向与局部 Euler 的对应

[LightComponent.cpp](../../../src/Framework/Engine/Private/LightComponent.cpp) 为方向/聚光复用 SceneComponent 的世界 +X 前向。组件变换变化时 OnTransformChanged 通过 GetForwardVector 刷新原 direction 缓存，因此数值旋转和既有旋转圈输出会同时改变代理与范围图形。构造时将原默认 -Y 方向转为对应 Euler，保留默认照射方向。

SetDirection 的输入是世界方向 d；存在父节点时先用父世界矩阵的逆把向量变换为局部方向，再归一化。Core::MakeRotationMatrix=Rz(z)*Ry(y)*Rx(x)，其 +X 列为 (cos(z)cos(y),sin(z)cos(y),-sin(y))，因此可选择 x=0、y=-asin(clamp(dLocal.z,-1,1))、z=atan2(dLocal.y,dLocal.x)，转为度后调用 SetLocalRotation。单个方向向量无法决定绕前向轴的滚转，直接编辑 Direction 会按此约定将 x 置零；旋转工具本身仍保存完整 Euler。零/非有限输入和奇异父变换产生的无效局部向量明确报错，未改写通用 SceneComponent 的父变换接口。光源没有缩放编辑，常规局部尺度保持 1；通过脚本施加负尺度/剪切父变换仍遵循现有 GetForwardVector 的完整世界矩阵语义。

## 局部旋转编辑轴：圆环、连续角度与旋转扇形

新增实现：[EditorRotationGizmo.cpp](../../../src/Editor/SceneEditor/Private/EditorRotationGizmo.cpp)。三个颜色圆环的法线分别为上述 B 的 X/Y/Z 列，参考半径使用 `WorldLength(...,75)`；每环 64 段。对轴 i，选择循环基 `u=B[(i+1)%3]`、`v=B[(i+2)%3]`，因 `u×v=B[i]`，正角方向与所选局部轴的右手方向一致：

```text
direction(φ) = u cosφ + v sinφ
point(φ) = O + radius × direction(φ)
```

`PickRing` 对可投影线段求鼠标到线段的最近点，7 像素内可命中；近似重合时以视图深度选择较近的圆弧。`DragReference` 同时供 HitTest 与 BeginDrag 使用，只有能够建立稳定拖动参考的部位才显示可交互高亮。普通视角将反投影射线与选定圆环平面相交，要求 `abs(dot(rayDirection,normal))>=0.08`、交点在射线前方且径向长度至少为 `0.1×radius`；投影到平面后归一化为径向方向。

按下保存 O、半径、起始 B、局部 Euler/旋转矩阵、起始径向、InvVP 和视口。普通拖动用按下平面及相机求当前径向 vc，将上次有效方向 vp 到 vc 的有符号小角累加：

```text
increment = atan2(dot(normal,cross(vp,vc)), clamp(dot(vp,vc),-1,1))
angle = previousAngle + increment
R_local_candidate = R_local_press × R_axis(angle)
```

小角累加允许跨越 ±π 和多圈，不能每帧只比较起始方向的 atan2。失效交点不推进 m_LastDirection 或 m_Angle。每次相邻采样仍需要足够连续：只凭两个采样方向无法判断一次事件是否跳过了半圈以上，当前没有推断缺失的鼠标轨迹。m_Angle 使用弧度，`MakeRotationMatrix` 接收的增量为角度制。

近乎侧对视线的圆环不能稳定求平面交点时，按下阶段固定选择屏幕切线方式。取命中圆弧方向，在正向旋转 0.01 弧度前后投影，令 `T=(screenNext-screenStart)/0.01`（像素/弧度），后续按 `angle=dot(mouse-pressMouse,T)/dot(T,T)` 求角。T 必须有限且长度至少 4 像素/弧度，否则拒绝命中/激活。此方式接受左右或上下位移在圆弧切线上的分量，径向移动不旋转；选择后的方式在同一次拖动内不切换，避免平面/切线之间发生角度跳变。

右乘增量意味着围绕按下时的组件局部轴旋转；位置和 LocalScale 不变。有旋转父节点时通过父链 B 显示该局部轴在世界中的方向，没有使用只把输入写入 local 的 SetWorldRotation。局部旋转矩阵写回 Euler 时，一个轴的物理旋转可能改变多个 Euler 字段，这是 `Rz(Z)·Ry(Y)·Rx(X)` 参数化的结果，不等同于多个轴同时自由旋转。

当前通用 `GetEulerRotation` 返回的 Y/Z 次序与 `MakeRotationMatrix` 不一致：例如输入 `(0,30,0)` 构造的矩阵会被解成 `(0,0,30)`。本次未修改公共数学接口，`NearEuler` 在新旋转实现中明确提取与 RzRyRx 配套的结果。普通姿态使用 `x=atan2(R21,R22)`、`y=atan2(-R20,hypot(R00,R10))`、`z=atan2(R10,R00)`；当 `abs(cos(y))<=1e-5` 时保留参考 X，解 `z=atan2(-R01,R11)+sign(y)·x`。再比较标准解与等价解 `(x+180,180-y,z+180)`，各分量按 360 度整数倍调整到当前属性附近，选距离较小者。极点附近的等价 Euler 不唯一；阈值分支允许约 1e-5 量级的矩阵近似，不能当作任意角度下绝无属性跳变的保证。

拖动时以按下 O、起始径向和所选法线生成半透明黄色扇形，并绘制圆心到起始/当前方向的两条径向线。扇形可视扫角限制在 `[-2π,2π]`，最多 64 片；实际 m_Angle 继续累计，多圈后填满一盘，当前径向线仍随最终角度变化。扇形本身不区分已转过几整圈，当前接口可查询未包裹的 `GetAngleRadians`。圆环随后以当前组件基底绘制；所选轴法线在自身绕轴旋转时保持不变，起始扇形不会随对象反复重设参考。

三环线条最多 384 个屏幕三角形，扇形与两条径向线再增加最多 68 个，总计不超过 452 个三角形/Draw。CPU 命中扫描固定 192 段，属于常量规模；现有逐三角形参数上传和 Draw 有真实 CPU/驱动成本，尚未批处理。Pass 仍只声明颜色附件 Load_Store，不测试或更新场景深度。屏幕投影、完成点、参数快照和组件所有权沿用前述公共路径。

2026-10-30 独立公式检查入口为忽略目录 `out/diagnostics/rotation-scale-math.py`，结果保存在同名 JSON。2943 组局部旋转组合覆盖任意起始 Euler、Y=±90 及其邻域、三个局部轴、正负和超过一圈的增量；父旋转组合、63 组有符号缩放与上下限、±200/±450/±1080 度跨界累计均通过。最大旋转矩阵误差约 1.68e-5、乘父旋转后约 1.71e-5，主要来自极点阈值近似；旧 Euler 顺序问题也有独立反例。该脚本使用 NumPy float64 验证公式，不替代实际 C++ float、GPU 扇形和完整编辑器 UI 检查；真实运行、模型读回与 Launcher 正常关闭结果见 [验证记录](../../dev/bugfix/故障与修复记录-2026-10-30.md)。
