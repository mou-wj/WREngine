# Renderer 模块架构说明

## 1. 模块定位

`Renderer` 模块位于 `RenderCore` 之上，是具体渲染功能的实现层。它不再关心 RHI 的底层细节，也不关心渲染图框架的最底层组织方式，而是基于 `RenderCore` 提供的资源、pass、shader 和线程能力，构建实际的渲染逻辑。

从源码结构看，`src/Framework/Renderer` 中的设计主要围绕以下类别展开：

- `MaterialShader.h`：材质相关的 shader 抽象
- `MeshMaterialShader.h`：网格材质 shader 抽象
- `RenderPass` / `SceneRenderer` 相关实现：负责真正的场景渲染过程
- `ViewFamily` / `RendererModule` / `SceneRenderer` 风格的更高层封装
- `Material`、`Mesh`、`Scene` 相关支持类

在整体架构上，`Renderer` 是“具体渲染能力实现层”，它是前端 `Engine` / `Application` 逻辑与底层 `RenderCore` / `RHI` 的桥接层。

---

## 2. 设计目标

`Renderer` 的目标是把渲染过程落到以下几个具体能力：

- 场景视图生成：根据相机、视口和场景构造渲染视图
- 材质渲染：让 `MaterialShader` / `MeshMaterialShader` 参与实际绘制
- 渲染 pass 执行：使用 `RenderCore` 的渲染图结构来执行 pass
- 资源绑定：关联 mesh、material、shader、buffer 与 texture
- 统一输出：将最终结果提交到交换链 / framebuffer / backbuffer

因此，`Renderer` 是“面向功能”的层，而 `RenderCore` 是“面向基础设施”的层。

---

## 3. MaterialShader 与 MeshMaterialShader

在这套代码中，`Renderer` 模块提供了两个重要的 shader 抽象类型：

```cpp
class RENDERER_API MaterialShader : public RenderCore::Shader
class RENDERER_API MeshMaterialShader : public RenderCore::Shader
```

### 3.1 `MaterialShader`

`MaterialShader` 代表“依赖 material 的 shader”，它的特点是：

- 可以根据 material 的属性编译 permutations
- 允许在 `ModifyShaderCompilerEnvironment` 中生成材质相关宏
- 可以通过 `GetShaderParameterMetadata()` 提供参数布局
- 适合处理材质相关的光照、颜色、贴图、透明度等功能

```cpp
static bool ShouldCompilePermutation(
    const RenderCore::ShaderPermutationParameters& parameters)
{
    return true;
}
```

这说明它提供的是一个非常轻量的扩展点，具体 shader 子类可以在其静态方法中控制是否编译某种 permutation。

### 3.2 `MeshMaterialShader`

`MeshMaterialShader` 是对 mesh / geometry 结合 material 的进一步封装。它的设计寓意在于：

- 材质 shader 负责“表面表现”
- mesh shader 负责“几何特性”
- 二者结合形成最终绘制 shader

因此，`Renderer` 模块可以在 shader 层面明确区分：

- 全局通用 shader：`GlobalShader`
- 材质相关 shader：`MaterialShader`
- 网格相关 material shader：`MeshMaterialShader`

这套层次与现代渲染架构比较接近：global pass、material pipeline、mesh pipeline。

---

## 4. `Renderer` 与 `RenderCore` 的关系

`Renderer` 模块依赖 `RenderCore` 的设计约定：

- `RenderCore::Shader` 提供 shader 基础类型
- `RenderCore::ShaderPermutationParameters` 提供编译条件
- `RenderCore::ShaderCompilerEnvironment` 提供编译环境定制
- `RenderCore::RenderGraphBuilder` 提供 pass 和资源组织能力
- `RenderCore::RenderThread` 提供线程基础设施

`Renderer` 的职责不是重新发明这些系统，而是：

1. 基于 shader 基础类定义特定渲染语言
2. 用 `RenderCore` 描述 pass 资源依赖
3. 在场景中收集 mesh/material/camera/view 信息
4. 调度渲染图执行

换句话说：

- `RenderCore` 提供“图形系统框架”
- `Renderer` 提供“场景渲染实现”

---

## 5. 场景渲染的抽象方向

从结构命名上看，`Renderer` 方向明显偏向“SceneRenderer”这一类分层：

```text
SceneRenderer
    ├── ForwardSceneRenderer
    ├── DeferredSceneRenderer
    ├── ViewFamily
    └── RendererModule
```

这类命名通常意味着：

- 每个 view/camera 由一个 `SceneRenderer` 处理
- 视图依赖于 scene、camera、viewport 和 output target
- 渲染过程中通过渲染图来组织 pass
- pass 最终调用 RHI 命令列表执行实际绘制

`Renderer` 模块在工程中很可能扮演以下职责：

- 每帧遍历场景中的可渲染对象
- 筛选参与当前相机的对象
- 生成渲染命令
- 把命令交给 RenderCore 的渲染图系统
- 提交到 RHI 后端渲染

---

### 5.1 按视图追加绘制 Pass

[RenderModule::BeginRender](../../../src/Framework/Renderer/Private/RenderModule.cpp) 开始时复制场景的 SceneSetting，在 `sceneRenderer->Build(builder)` 之后遍历 `views->GetViews()`，先按设置加入网格/目标光源 Hint，再遍历每个视图的 `DrawExtensions`。非空 `Callback` 接收当前 Builder、当前 SceneView、`SceneTextures.SceneColor` 和 `SceneTextures.SceneDepth`。调用顺序为视图顺序、列表顺序；GPU 执行顺序仍由 Pass 声明的资源依赖决定。Hint 在变换工具等上层叠加之前构图，延续原有显示顺序。

这是当前场景主体结束后的固定扩展点，扩展与场景使用同一个 Builder，并在统一的 `builder.Execute()` 前完成构图。扩展可通过公开的 RenderCore 接口声明参数、申请图内资源和添加 Pass。叠加颜色通常使用 Load_Store，深度是否测试、写入须与具体绘制及访问声明一致；回调本身不会自动声明附件访问。不增加额外队列提交、Present 或等待。

接口与列表定义在 Engine 的 [SceneView.h](../../../src/Framework/Engine/PublicHeader/SceneView.h)，具体工具的回调由上层填入。当前没有多个渲染阶段的 hook，也没有颜色替换返回值；将来添加后处理时需明确叠加点与颜色处理的先后。生命周期与注册示例见 [RDG 核心实现](../../key_implemetion/performance/03-rdg-execution-and-state.md)。

## 6. 渲染管线中的角色分工

可以把整个渲染系统分成 4 层：

### 6.1 底层图形 API 层：`RHI / RHIVulkan`

负责最底层的 device、swapchain、pipeline、buffer、texture、command list。

### 6.2 渲染基础设施层：`RenderCore`

负责：

- resource lifecycle
- render graph
- pass scheduling
- render thread
- shader cache

### 6.3 具体渲染实现层：`Renderer`

负责：

- material shader
- mesh shader
- scene rendering
- view rendering
- render pass orchestration

### 6.4 游戏 / 引擎运行时层：`Engine` / `Application`

负责：

- 资源管理
- actor / component
- camera / viewport
- frame update loop
- 业务逻辑与渲染交互

这个分层是工程设计上最重要的一点：每一层都只解决自己层级下的问题，避免 `Engine` 层直接依赖底层 `RHI` 细节。

---

## 7. Shader 与 Material 的协同关系

`Renderer` 模块中 material shader 的存在，说明最终绘制流程并不是简单的“mesh -> drawcall”，而是：

```text
Scene Objects
    -> Material
    -> ShaderPermutation
    -> PipelineState
    -> draw call
```

其中：

- `Material` 决定 surface 属性
- `MaterialShader` 决定 shader 的变量和编译组合
- `MeshMaterialShader` 把 mesh 和 material 结合起来
- 最终在 `Renderer` 中组装 pipeline state 并提交到 RHI

这意味着渲染器中的真正绘制逻辑不会只在一个 `Draw()` 函数里打完，而是会跨多个层次拆分：

- shader selection
- material parameter binding
- mesh data binding
- pass execution

---

## 8. 架构总结

`Renderer` 模块的本质可以概括为：

1. 依赖 `RenderCore` 提供的资源、图、线程和 shader 基础设施
2. 抽象 `MaterialShader` 与 `MeshMaterialShader`，把材质和几何体绑定到渲染逻辑中
3. 在较高层面组织 scene、camera、viewport 和 draw call
4. 把实际渲染逻辑组织成一个渲染器 pipeline，而不是直接写死在 `RHI` 层

因此，`Renderer` 不只是“一个绘制包”，而是整个渲染系统中真正实现可见效果的核心层。其最关键的价值在于：

- 把底层图形 API 封装成更高层次的渲染表达
- 把 material、mesh、shader、pass 统一组合起来
- 为 `Engine` 提供稳定的渲染能力抽象接口

在整个工程中，它是从 `RenderCore` 走向真实输出画面的关键桥接层。

## 相关核心实现细节

### HintRender 场景提示

[HintRender.h](../../../src/Framework/Renderer/PublicHeader/HintRender.h) / [HintRender.cpp](../../../src/Framework/Renderer/Private/HintRender.cpp) 提供 AddGridPass、AddLightPass 和 GetGridSpacing。Renderer::BeginRender 根据 Engine [SceneSetting](../../../src/Framework/Engine/PublicHeader/SceneInterface.h) 的 bDrawGrid/bDrawLightHints 和 LightHintTarget 决定调用，在模块内部传入本帧 Builder、SceneView、颜色及深度附件。上层无需包含 HintRender 或链接 Renderer。默认两类提示都关闭；光源目标为空时不绘制范围。实现使用 [HintRender.sf](../../../shaders/HintRender.sf) 的全局 Shader，光源和网格均使用 RHI LineList；参数在构图时复制到 Builder arena，不借用组件的异步存活，也不直接调用原生后端或 GUI。

网格为 Y=0 的 XZ 平面。CPU 用逆 VP 反投影视锥八角，检查十二条边与地面的交点并求 XZ AABB；向外对齐网格后再各扩一格。高度给出 0.1、0.2、0.5、1、2、5、10 米间距和两个方向的细分数量。VS 从固定 1×1 单位网格角点展开线段并映射到该区域，PS 按相邻平行线的投影间距淡出过密线。范围随真实视锥更新，远裁剪之外不绘制；不再用全屏逐像素求交或写 SV_Depth。按 10 米周期移除相机 X/Z 的大坐标，保持世界锚定。一个 Draw 提交 2(nx+nz) 个顶点，无动态顶点缓冲；目前采用 CPU 确定细分数量、VS 展开的路径，未接入 HS/DS 硬件细分，也未作性能基准。

光源提示读取既有组件的世界位置、方向、半径/锥角：点光 3 个正交大圆共 192 线段；聚光底圆 64 段、16 条母线和 1 条中心射线；平行光为箭杆及八条头部线，共 9 段。这些图形不替代实际光源数据更新。详细公式和限制见 [变换与提示几何](../../key_implemetion/algorithm/03-transforms-frustum-and-depth.md)。

两种提示 Pass 均 Load/Store 颜色和深度，开启深度测试但关闭深度值写入；当前 RHI RenderPass 没有只读深度附件标记，RDG 因而按深度附件访问保守排序。天空绘制也保存深度供扩展使用。工程时序、参数快照和光源代理更新见 [UI 与场景提交](../../key_implemetion/performance/08-ui-rendering-and-window-lifetime.md)。

本篇保留架构职责说明；执行时序、数学原理及源码限制见以下专题。本轮为源码整理，专题不等于运行验证通过。

- [场景空间索引](../../key_implemetion/algorithm/02-bvh-octree-and-scene-query.md)
- [距离场和体素 GI](../../key_implemetion/algorithm/04-distance-fields-and-jump-flood.md)
- [PBR 与环境预计算](../../key_implemetion/algorithm/05-pbr-ibl-and-sampling.md)
- [阴影与级联](../../key_implemetion/algorithm/06-shadow-mapping-and-cascades.md)
- [屏幕空间效果与抗锯齿](../../key_implemetion/algorithm/07-screen-space-and-antialiasing.md)
- [实例批次与几何数据](../../key_implemetion/algorithm/08-mesh-import-and-geometry.md)
