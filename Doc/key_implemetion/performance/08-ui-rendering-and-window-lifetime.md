# ImGui RHI 合成、纹理访问和窗口释放

架构：[SlateCore](../../architecture/slate/slatecore.md)、[ImGUISlate](../../architecture/slate/imguislate.md)。源码：[imgui_impl_rhi.cpp](../../../src/Framework/ImGUISlate/Private/imgui_impl_rhi.cpp)、[ImSlateRenderer.cpp](../../../src/Framework/ImGUISlate/Private/ImSlateRenderer.cpp)、[SlateRHIRenderer.cpp](../../../src/Framework/SlateRHIRenderer/Private/SlateRHIRenderer.cpp)。

## 一扇原生窗口里的合成

Widget 表示区域与事件目标，SceneViewport 渲染场景到 RenderTexture，ImGui 使用该纹理作为图像参与 GUI 绘制，WindowRenderer 统一提交到该 Window 的 swapchain。场景渲染目标不是另一个原生窗口或 OpenGL context。

ImGui 生成布局与 ImDrawData，项目后端消费绘制数据；没有拦截同名 GL 函数，也不依赖 imgui_impl_opengl3 发出 GL 命令。实际原生调用仍由当前 RHI 后端完成，OpenGL 仍须进入 context worker。

## 顶点、索引和剪裁

后端拼接所有 CmdList 的顶点与索引，使用 uint32 索引并烘焙 vertexBase + draw.VtxOffset；绘制时 FirstIndex=indexBase+IdxOffset，避免大 draw list 溢出 16 位索引。

UI 的 DisplayPos/DisplaySize 变换为裁剪坐标；屏幕剪裁先减 DisplayPos，再乘实际 framebuffer 与 DisplaySize 的比例，floor 左上、ceil 右下，限制在 target 尺寸内。OpenGL scissor 的 y 使用 `height-bottom`，其他后端使用 top。窗口 DPI、swapchain 尺寸钳制和 UI 尺寸不能假定始终相等。

UserCallback 之后恢复 pipeline、顶点绑定和投影状态；ResetRenderState callback 单独处理。回调不是允许任意库在错误线程直接调用图形 API 的通用互操作契约。

## 两类完成点

后端 frame slot 保存上传用的顶点/索引 CPU 数组和 RenderBuffer，重用该 slot 前检查其 Fence，防止覆盖上一提交仍读取的内容。

Window state 另外维护 LastSubmission：绘制提交后 Present，再提交空 completion marker，使其同时覆盖 OpenGL 这类把 Present 作为 worker 条目的后端。resize/ReleaseWindow 等待这个窗口记录的具体完成点，而非每帧 WaitIdle。

这两类记录服务于实际窗口与上传缓冲复用，不是材质 MaterialFrame，也不应扩展成无依据的全局对象保活层。

## 采样纹理在哪里更新 Fence

TexID 当前编码借用的 RenderTexture 指针，不是资产 ID。调用方必须维持 RenderTexture 有效，不能把 ID 放进 UI 后立即销毁所有者。

后端在 RenderPass 之前收集本次实际采样纹理，去重、读取 Tracker、建立读取状态和队列等待。若资源来自其他队列，先在源队列录制转换并更新该次源提交的 Tracker，再加入 graphics 等待。

WindowRenderer 提交实际 GUI draw 后，对 DrawResources.SampledTextures 写 SRV 状态和该 draw submission 的 Fence；顶点/索引缓冲也在这里更新。这些记录不能搬到未提交 GUI 访问的场景渲染器中，场景完成 Fence 也不能替代 GUI 后续采样的 Fence。

## 关闭与边界

Win32 事件、UI NewFrame、场景构建和图形提交有不同调用顺序。ApplicationBase/Window 负责平台消息；RHI 图形测试里的长循环并不一定处理同样的消息循环，因此“窗口无响应”需要先区分未泵消息和 GPU 卡死。

SceneViewport 的区域 resize 可能触发离屏纹理重建；Window resize 可能触发 swapchain resize，二者不同。释放字体纹理、自定义 UI 纹理、frame slots 和 swapchain 时，都要核查最后真实使用者；不根据裸 TexID 推断资源所有权。

当前三后端对 BeginRenderPass 是否覆盖先前 viewport/scissor 存在差异，OpenGL 部分显示路径方向也有已记录差异。上述坐标公式是本后端的实现，不表示全引擎坐标约定已经完全一致。

验证入口：默认 Launcher 的场景纹理与 ImGui 面板；应额外核查非零 DisplayPos、高 DPI、快速 resize、自定义纹理释放和回调恢复。本轮仅源码整理，没有重新进行这些交互测试。
