# SlateCore 模块架构说明

## 1. 模块定位与边界

`SlateCore` 定义平台窗口、逻辑控件、输入、离屏视口和窗口绘制入口。它不依赖 ImGui，也不直接调用原生图形 API。

- `Window / PlatformSurface / PlatformSurfaceOwner`：实际 OS 窗口及生命周期。
- `Widget`：位置、大小、可见性、命中测试和输入处理。
- `SlateWidget`：承载一个 `SlateViewport` 的逻辑容器。
- `SlateViewport`：渲染到纹理的区域，Engine 的 `SceneViewport` 是其实现。
- `SlateRenderer`：按 Window 组织最终目标的绘制与提交。
- `ApplicationBase`：应用生命周期接口。

## 2. Window 与 Widget

只有 Window 持有平台 Surface 和原生句柄。Widget 是窗口中的逻辑区域，不创建子 HWND、交换链或独立图形上下文。原 `NativeWidget` 已移除；`SlateWidget` 保留为普通容器，继承 `Widget`。

一个 Window 通过 `AddWidget / RemoveWidget / GetWidgets` 登记多个区域，列表保存非拥有指针。业务层拥有 Widget，在销毁前移除，或先销毁使用它们的 Window。`SetRootWidget / GetRootWidgets` 仅为简单 Window 保留单区域兼容入口；ImMainWindow 应使用带布局参数的 AddWidget 和 RemoveWidget，避免绕过布局登记。

独立 OS 窗口应创建另一个 Window。普通菜单、提示、文件选择器等弹层继续属于逻辑 Widget，由 UI 框架在同一窗口内组织遮挡。

## 3. 几何、布局与输入

`WidgetGeometry` 保存相对所属窗口客户区的 X/Y/Width/Height。`SetGeometry` 触发位置、尺寸变化回调，`HitTest` 排除不可见区域。当前注册列表是扁平区域列表，不是完整的递归布局树。

窗口接收平台事件后：

1. GUI 适配层优先向自身输入系统传递窗口坐标。
2. GUI 未捕获的输入交给 Window，按注册逆序命中可见 Widget。
3. Window 将鼠标坐标减去区域原点，再交给目标 Widget；键盘发给焦点 Widget。
4. 被 Widget 处理的鼠标按下建立逻辑捕获，直到所有相关按键释放；拖动跨区域仍交给原控件。
5. Win32 Surface 同时使用 OS 鼠标捕获，允许拖出客户区后接收到释放。失焦或捕获取消时，Window 补发释放，防止视口交互卡住。

ImGui 的窗口、弹层内部排序与捕获由 ImGui 自己维护。当前 SlateCore 不提供通用弹层栈或跨框架遮挡排序器。

## 4. SlateWidget 与 SlateViewport

`SlateWidget::Draw()` 转发给所挂载的视口，尺寸变化转发给 `SlateViewport::Resize`，输入与焦点也转发给它。该包装器不拥有视口，保留它是为了复用既有 Engine/Application 的挂载方式。

`SlateViewport::GetViewportRenderTargetTexture()` 沿用既有 `void*` 接口。在当前 RHI 集成中，其值必须是有效的 `RenderCore::RenderTexture*`，不是原生纹理句柄。实际资源继续由视口或业务层拥有。

保留离屏场景目标，可以复用渲染器的后处理与尺寸管理；合并的是窗口呈现环境，并非要求场景和 UI 必须在一个 RenderPass 内完成。

## 5. 按 Window 提交的渲染接口

```cpp
struct DrawResources
{
    std::vector<RenderCore::RenderTexture*> SampledTextures;
    std::vector<RenderCore::RenderBuffer*> VertexOrIndexBuffers;
};
using DrawCallback = std::function<void(RHI::RHIGraphicCommandList&, RHI::RHITexture*, std::vector<RHI::RHIWaitInfo>&, DrawResources&)>;
virtual uint64_t Render(Window* window, const DrawCallback& draw) = 0;
virtual void ReleaseWindow(Window* window) = 0;
```

SlateCore 只前置声明 RHI 和 RenderCore 资源类型。`SlateRHIRenderer` 依赖 RHI/RenderCore，实现每个 Window 的交换链、Acquire、状态转换、提交、Present 和清理。DrawCallback 在调用期间同步录制，可向 waits 添加所采样资源的 GPU 依赖，并向 DrawResources 登记本批命令采样的纹理和使用的顶点/索引缓冲。SlateRHIRenderer 在 ExecuteContext 返回后立即使用该次绘制提交的 Fence 更新这些资源的 Tracker，同时回写 SRV 或 VertexOrIndexBuffer 访问状态。调用方不可保留传入的命令列表和目标指针供未来任意使用。Render 返回覆盖 Present 的窗口完成值，适配器仅用它管理帧上传存储的轮转。

每个有效窗口帧仅呈现一次：

```text
Window.Draw
  → 控件 Draw / 场景离屏纹理
  → UI 生成绘制数据
  → SlateRenderer.Render(Window, callback)
      → Acquire → RenderTargetView → callback
      → Present 状态 → Submit → Present
```

UI 适配器决定怎样把视口纹理与 UI 合成；交换链实现不认识 ImGui。这保留了其他 UI 框架通过 RHI 绘制回调接入的空间，但没有承诺任意原生渲染库可直接共用上下文。

## 6. 同步与释放

每个窗口的 GUI 后端轮转三组顶点/索引上传资源，复用时仅在该组 Fence 尚未完成的情况下等待对应值。正常提交不调用 WaitIdle，也不等待刚提交的帧完成；采样纹理的依赖放入 GPU WaitInfos，资源 Access 和 Fence 均在实际提交处手动回写，供后续场景绘制使用。若跨队列转换另行提交，也在该次提交后立即更新纹理 Tracker；最终绘制提交再记录 Graphics 队列的读取 Fence。

Present 后提交一个空命令上下文作为窗口完成标记，它覆盖同一队列上排队的绘制与 Present。这样 OpenGL worker 尚未执行 Present 时，窗口释放不会只等绘制 Fence 就提前销毁原生窗口。该标记不阻塞 CPU，代价是每窗口帧多一次队列提交。

窗口释放/缩放只等待本窗口最后的完成值，不清空整条队列。OpenGL 在 Acquire 时保留本帧索引，排队的 Present 按捕获的索引呈现，不依赖 worker 推进索引后主线程才能继续。

Window 析构会关闭其平台 Surface。`ReleaseWindow` 必须在原生窗口和 RHI 模块仍有效时调用。底层交换链销毁函数不自行添加 WaitIdle。
