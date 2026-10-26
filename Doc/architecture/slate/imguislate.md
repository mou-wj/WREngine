# ImGUISlate 模块架构说明

## 1. 模块定位

`ImGUISlate` 在 SlateCore 上提供 ImGui 界面、布局、输入适配和一套项目自有的 RHI 渲染后端。业务层继续通过 ImWidget 绘制回调组织面板，不需要调用 OpenGL/Vulkan/D3D12。

关键实现：

| 文件 | 职责 |
| --- | --- |
| `ImMainWindow.h/.cpp` | Window、水平布局与窗口输入转发 |
| `ImSlateRenderer.h/.cpp` | 每窗口 ImGuiContext、帧生成、场景纹理合成 |
| `ImWidget.h/.cpp` | 逻辑区域与通用 DrawCallback |
| `LayoutMgr.h/.cpp` | 水平、垂直、网格布局工具 |
| `Private/imgui_impl_rhi.h/.cpp` | ImDrawData → RHI 命令、纹理、Shader、上传资源 |

CMake 链接 SlateCore、RenderCore，编译 ImGui 核心源文件，过滤官方 `imgui_impl_*` 图形后端；ImGUISlate 不再建立 WGL 环境或链接 OpenGL 绘制路径。新增的后端放在 Private，避免把 ImGui 细节扩散到 RHI 公共接口。

## 2. 窗口和控件

`ImMainWindow(width, height, title, renderer)` 接受由 SlateRHIRenderer 模块提供的非拥有渲染器指针。Window 负责唯一的原生表面，ImSlateRenderer 为该 Window 创建一个 ImGuiContext。

`ImWidget` 继承 Widget 和 ImWidgetBase，保存 `(x, y, width, height)` 绘制回调。`PopupImWidget` 继承 ImWidget，只表达弹层用途，不再拥有原生句柄。真正弹层的打开/关闭、模态和层级由回调中的 ImGui API 组织。

Window 与 LayoutManager 保存非拥有 Widget 指针；ImMainWindow::RemoveWidget 同步移除两处登记，并等待之前提交完成后允许调用方销毁资源。布局按客户区尺寸更新，不根据外框大小分配区域。

## 3. 单帧绘制

```text
Application.TickFrame
  → ImMainWindow.PollEvents
  → ImMainWindow.Draw
    → 切换该 Window 的 ImGuiContext、更新尺寸和 DeltaTime、NewFrame
    → 遍历可见 Widget，执行 Draw
        → SlateWidget 转发 SceneViewport.Draw，产生 RenderTexture
        → ImWidget 回调生成 GUI
    → 视口纹理加入 ImGui 背景 DrawList，GUI 窗口/弹层在其上方
    → ImGui.Render
    → SlateRenderer.Render(Window, callback)
        → ImGuiRHIBackend.RenderDrawData
        → 登记本批命令使用的纹理和缓冲
        → SlateRHIRenderer 提交后立即更新资源 Access 和 Fence
        → 一次窗口 Present，随后提交窗口完成标记
        → 返回窗口完成 Fence，ImGuiRHIBackend.EndFrame 仅记录帧存储的复用时机
```

场景保持离屏输出，避免改动 Renderer/Engine 的主渲染流程。GUI 可以覆盖场景区域；既不存在场景子 HWND，也不需要处理原生子窗口遮住 GUI 的问题。

## 4. 自有 ImGui RHI 后端

后端解析 ImDrawData，复用 RenderCore 的 ShaderCompiler、RenderBuffer、RenderTexture 和 RHI 状态缓存：

- 编译简单的 HLSL 顶点/片元 Shader，按当前 GShaderPlatform 输出对应后端数据。
- 顶点含二维坐标、UV、归一化 RGBA 颜色；顶点常量负责正交投影。
- 合并各 DrawList 的顶点，将索引转成 uint32 并折叠 VtxOffset，支持大网格和后端间的索引差异。三组帧资源按提交 Fence 轮转，顶点/索引上传和访问转换直接录入本帧命令列表。
- 按目标格式缓存图形管线，开启透明混合，关闭剔除和深度测试。RenderPass 使用实际后备缓冲尺寸，避免窗口调整期间客户区尺寸与交换链 extent 暂时不一致导致越界。
- 把 DisplayPos、FramebufferScale 和 ClipRect 换算为裁剪矩形；OpenGL 的 Scissor 使用底部原点。
- 为每条绘制命令绑定纹理 SRV、Sampler 并 DrawIndexed。
- 处理 `ImTextureData` 的 WantCreate/WantUpdates/WantDestroy，支持当前 ImGui 版本的动态字体图集。

VulkanRHI 已用负高度 Viewport 统一 Y 轴，所以 GUI 正交投影不再对 Vulkan 额外翻转。Vulkan 的 Shader 阶段使用独立 descriptor set；D3D12 当前资源参数使用 space0，编译定义分别匹配现有后端绑定约定。

## 5. 纹理与外部内容

本后端的 ImTextureID 表示 `RenderCore::RenderTexture*` 的整数编码，不是 GLuint、VkImage 或 D3D12 descriptor。视口纹理和字体图集均通过该约定进入普通 ImGui Image 绘制。

调用方使用外部 RenderTexture 时，必须保证对象及 RHI 资源在相关提交完成前有效，资源创建时应支持 ShaderResource。不能临时取裸句柄交给 ImGui 后立刻销毁其所有者。当前转换工具属于后端 Private 实现，尚未增加公共纹理注册/注销 API。

接入其他界面库时，可以复用 Window 和 SlateRenderer 的绘制回调，把其绘制列表翻译为 RHI 命令，或把受控的 RHI 离屏输出作为纹理合成。需要原生图形上下文的第三方 renderer 仍需专门互操作设计。

## 6. 输入与线程

平台事件先进入 ImMainWindow，由 ImSlateRenderer 在窗口坐标中向 ImGuiIO 投递一次。ImGui 未捕获的事件转给 SlateCore 的逻辑区域；场景已捕获拖动时仍接收后续移动/释放。

主线程负责 ImGui NewFrame、Widget 回调、Render，以及把 ImDrawData 同步翻译成 RHI 录制。异步后端执行的是已经录制好的命令和有效期受控的 RHI 资源，不跨线程持有下一帧会被改写的 ImDrawData。OpenGL 原生调用继续由 OpenGLRHI 的 worker 与 current context 执行，ImGUISlate 不迁移或借用它的上下文。

## 7. 首版限制

- 正常绘制不使用 WaitIdle，也不在每帧末尾等待完成。三组上传资源仅在复用未完成的那一组时等待对应 Fence；窗口释放/缩放等待该窗口最后的完成值。字体图集更新/销毁时单独等待其最后访问 Fence，字体上传继续沿用现有同步上传接口。
- 每窗口帧多提交一个空命令上下文，产生覆盖已排队 Present 的完成值；后续可用 RHI 的呈现完成接口替代该提交，但不能只等待 Draw Fence 就释放仍在 worker 使用的窗口。
- 字体图集更新当前上传整张图，不是只更新脏矩形。
- 支持 ResetRenderState 回调；其他 ImDrawCallback 在录制线程执行，不能假设存在 current GL context，不能直接调用官方原生后端函数。
- 未启用 ImGui 平台多视口。独立窗口由显式创建 Window 管理。
- 平台输入沿用现有 SlateCore 的有限 EKey 集合；完整文本/IME、全部快捷键和多 DPI 窗口仍需单独完善。

## 相关核心实现细节

本篇保留架构职责说明；执行时序、数学原理及源码限制见以下专题。本轮为源码整理，专题不等于运行验证通过。

- [ImDrawData 转换、纹理访问和上传缓冲复用](../../key_implemetion/performance/08-ui-rendering-and-window-lifetime.md)
