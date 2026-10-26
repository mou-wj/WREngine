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
