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

2026-10-26 增加 [ImCommonWidgets](../../../src/Framework/ImGUISlate/PublicHeader/Widgets/ImCommonWidgets.h)：ImPanelWidget 管理逻辑区域的 Begin/End；ImListWidget 保存列表展示状态并通知选择；ImPropertyControls 提供即时文本、数值、向量、颜色、布尔和枚举输入。控件没有 Engine 依赖，数据仍由调用方拥有；Text 利用 ImGui resize callback 更新调用方 std::string 的 UTF-8 缓冲。

文本由 Win32 Unicode 窗口的 WM_CHAR 以 UTF-16 code unit 进入 EventHandler::OnTextInput → ImMainWindow → ImSlateRenderer → ImGuiIO::AddInputCharacterUTF16；输入法组合窗口定位和中文字体未完成。编辑键、Ctrl+A/C/V/X/Y/Z 与修饰键另走 KeyEvent，不能把 WM_CHAR 当成键状态。平台层不依赖 ImGui。

Editor 正常帧不调用场景 Flush；实际编辑才同步更新代理/场景 GPU 缓冲。更新前使用视口纹理最终采样的 Tracker 等待具体 queue/value，保证被替换的场景缓冲此前已完成；该纹理必须由当前窗口合成访问。此成本只发生于编辑/增删，仍可能让高频拖动等待 GPU，未实现多版本动态场景更新。没有新 WaitIdle、按帧保活容器或 RDG 改造。

编辑功能现由 SceneEditor、AssetEditor、MaterialEditor、NodeEditor 四个独立 DLL 提供，EditorApplication 仅拥有/组装它们的对象。场景的上述同步与回收代码位于 SceneEditor。AssetEditor 通过宿主回调请求打开网格/赋材质/打开材质窗口；MaterialEditor 的 MaterialEditorCallbacks 查询当前选择并同步通知变更，不引用具体场景类或资产面板。NodeEditor 同步查询当前组件，修改局部 TRS 后通知场景重建代理，不持有组件。回调捕获 EditorApplication，但没有异步投递；关闭时先释放窗口提交，再销毁对话框/菜单、节点及其他面板和回调、视口及场景。此顺序保证回调的被借用目标仍有效，不引入额外资源所有权。

Editor 由应用内 EditorLayout 组织三列和底部资产区，保存可交互调整的区域比例，底部初始占可用高度的 35%。ImMainWindow::SetLayout 仅允许在初始化后、登记控件前选择布局；不需要迁移已有控件。原生窗口尺寸变化仍由既有 OnResize 等待窗口提交再更新 Widget 几何。资产面板内部为可调整宽度的文件树与资源网格，并单独预留底部操作条；这些 ImGui 子区域没有原生句柄，也不创建额外呈现环境。目录元数据与资产加载边界见 [资产实现](05-assets-materials-and-instance-data.md)。

Editor 顶部菜单预留 24 像素，下面 ribbon 不超过剩余客户区的 112 像素。EditorLayout 扣除可见的竖向分隔条和一条横向 6 像素分隔条，按偏好比例及最小尺寸计算实际几何；右侧 Node Properties 无选择时折叠，Material Editor 浮窗跳过区域布局。它在 AddWidget/原生 Resize/UpdateLayout 的布局阶段执行。Layout 借用 menu、toolbar、splitter、节点及材质面板，Window 销毁其布局后 Application 才销毁这些对象；回调只同步借用布局器，不提交后台任务。三个 splitter 最先登记，菜单/ribbon 随后，均在 SceneViewport 之前处理；材质面板仍最后登记，保留其浮动窗口和展开模态库的文件投递优先级。

EditorViewToolbar 由 SceneEditor 实现，与 EditorScene 直接同步协作；没有新增模块、上下文或原生窗口。SetViewDirection 只改变 CPU 相机状态，保持目标点/距离，不重建场景资源、不触发场景 dirty，也不新增 GPU 等待。投影矩阵仍在 EditorScene::Draw 依据真实视口尺寸创建；工程时序与 [方向数学细节](../algorithm/03-transforms-frustum-and-depth.md) 分别记录。

Win32 事件、UI NewFrame、场景构建和图形提交有不同调用顺序。ApplicationBase/Window 负责平台消息；RHI 图形测试里的长循环并不一定处理同样的消息循环，因此“窗口无响应”需要先区分未泵消息和 GPU 卡死。

SceneViewport 的区域 resize 可能触发离屏纹理重建；Window resize 可能触发 swapchain resize，二者不同。释放字体纹理、自定义 UI 纹理、frame slots 和 swapchain 时，都要核查最后真实使用者；不根据裸 TexID 推断资源所有权。

当前三后端对 BeginRenderPass 是否覆盖先前 viewport/scissor 存在差异，OpenGL 部分显示路径方向也有已记录差异。上述坐标公式是本后端的实现，不表示全引擎坐标约定已经完全一致。

验证入口：默认 Editor 或 `-app=Application` 的场景纹理与 ImGui 面板；应额外核查非零 DisplayPos、高 DPI、快速 resize、自定义纹理释放和回调恢复。2026-10-26 的运行范围见 [故障记录](../../dev/bugfix/故障与修复记录-2026-10-26.md)，不能将接口/读回检查当成全部交互通过。

## 分隔条拖动与视口纹理重建

源码：[ImSplitterWidget](../../../src/Framework/ImGUISlate/Private/Widgets/ImCommonWidgets.cpp)、[EditorLayout 与回调](../../../src/Editor/Editor/Private/EditorApplication.cpp)、[ImMainWindow::UpdateLayout](../../../src/Framework/ImGUISlate/Private/ImMainWindow.cpp)、[ImSlateRenderer::WaitForLastDraw](../../../src/Framework/ImGUISlate/Private/ImSlateRenderer.cpp)。

每条 splitter 使用独立的逻辑 ImGui 窗口，WindowMinSize 降到 1×1、Padding/Border 为零，防止 ImGui 默认最小窗口宽度把 6 像素命中区扩大到相邻面板。InvisibleButton 的稳定 Id 保持拖动 active 状态，首次激活记录当前轴坐标，之后报告相对上次坐标的增量。窗口落点和控件位置变化不会把起始鼠标移动误算成拖动。松开/失焦按 ImGui 的输入状态取消，不在业务层再建一套捕获机制；Win32 原有鼠标捕获负责跨客户区接收释放。

EditorLayout 保存偏好比例及当前实际尺寸。设可用宽度 W=窗口宽度-可见竖向 gap，L/R 为两侧实际宽度，则中间宽度 C=W-L-R；左边拖动 δ 得 L'=L+δ，右边拖动 δ 得 R'=R-δ。无选中节点时右侧 R、rightGap、右栏最小宽度都为零，视口占满右侧；最小总宽度从 660 调整为 400 像素再按客户区缩放。右栏比例仍保存，重新展开时按原比例与尺寸下限恢复。横向分隔条上移 δ<0 时底部高度 B'=B-δ。每次用相邻区的最小尺寸钳制，然后仅在值改变时更新对应比例并调用 UpdateLayout。客户区不足时按可用尺寸/下限总和统一缩小各下限；缩放窗口不会覆盖偏好比例。每次重排遍历已登记区域，为 O(n)，常规 Editor 的 n 固定且很小，不新增对象树或重复缓存。

UpdateLayout 只在本帧视口绘制前执行：

1. ImSlateRenderer 保存前一帧 SlateRenderer::Render 返回的窗口完成 marker（与 EndFrame 使用的值相同，不创建新 Fence/提交）。它覆盖前一帧 GUI 对视口纹理的采样和 Present。
2. WaitForLastDraw 只等待该 Graphics 完成值，然后调用既有 Layout / Widget::SetGeometry / SlateWidget::OnResize / SceneViewport::Resize。旧视口纹理此时不再被前一窗口提交使用。
3. 本帧随后生成场景和 UI 绘制数据，引用新纹理并按既有路径提交；Access/Fence 仍在实际提交处更新。

不能将此等待放到生成了本帧旧纹理 TexID 之后，否则即使上一帧完成，当前 CPU 绘制列表仍可能保留将失效的指针。这里也不能直接用 ReleaseWindow 替代：该接口会移除 swapchain 状态，造成内部面板拖动时反复销毁/重建主交换链。现实现保留交换链，只有实际变化时等待；普通帧、纯悬停、碰到尺寸下限均不调用该等待。没有 WaitIdle、全局资源保活列表或 RDG 修改。连续拖动仍可能受 GPU 完成速度及纹理分配成本影响，并非无阻塞多版本 resize。

光标在 ImGui 完成当前帧后，经 SlateCore 的平台接口更新。Win32 维护共享 HCURSOR，不分配自有光标纹理；HTCLIENT 的 WM_SETCURSOR 使用该形态，非客户区仍走 DefWindowProc。主动 SetCursor 先检查实际鼠标所在客户区/捕获，避免后台窗口覆盖其他应用光标。ImGui 配置要求自行绘制光标时允许使用空 HCURSOR。

定向入口为临时 out/diagnostics/editor-smoke/splitter-main.cpp：实际 Loop 和 Editor 中投递 ImGui 输入事件，检查三条边界、光标请求、连续拖动/最小尺寸/失焦、RenderTexture 尺寸及渲染像素。它不是 OS 桌面输入自动化，系统鼠标形态和真实用户操作仍需按记录单独验收。

## 工程切换、材质弹窗和网格投放

### 文件对话框的目录创建与盘符导航

[ImFileDialogWidget](../../../src/Framework/ImGUISlate/Private/Widgets/ImFileDialogWidget.cpp) 的目录/文件/多文件模式共用导航、右键菜单和 New folder 模态框。Windows 的 RefreshRootDirectories 在打开或显式刷新时依次检查 A:/ 至 Z:/，只保存可访问的根路径；非 Windows 提供 `/`，未增加原生窗口/磁盘句柄或平台 API 依赖。检查是 UI 线程上的同步 filesystem 操作，不是后台磁盘扫描；断开的网络盘等文件系统响应仍可能阻塞打开/刷新，不把此列表宣称为包含未就绪介质的完整卷枚举。正常绘制不重复检查所有盘。

右键命中包含滚动表格的子窗口，打开目录操作菜单；菜单或工具栏仅提出打开创建弹窗的请求，输入与创建状态属于同一个 Widget。名称拒绝空值、`.`/`..`、路径分隔符/保留字符和结尾点/空格，filesystem::create_directory 只创建当前目录下的一个新目录，不递归建路径、不覆盖已有文件/目录。失败保留输入并显示错误；成功 NavigateTo 新目录，下一帧按原脏标志刷新，原 Select Folder/文件确认和宿主回调不变。取消创建不产生文件系统更改。界面使用 UTF-8 路径文本并以 u8path 还原输入；原生 filesystem::path 继续保存实际路径，不涉及资产资源拥有关系或 GPU 生命周期。

本控件的定向验证使用实际 ImGui 上下文与 ImGuiSlate.dll，覆盖右键创建、取消、冲突/非法名、中文目录确认、两个实际盘符切换及文件模式；这是应用内部输入检查，桌面操作与运行范围见 [2026-10-27 记录](../../dev/bugfix/故障与修复记录-2026-10-27.md)。

### 工程与场景操作

源码：[EditorApplication::TickFrame / CloseProject](../../../src/Editor/Editor/Private/EditorApplication.cpp)、[EditorScene::ClearObjects](../../../src/Editor/SceneEditor/Private/EditorScene.cpp)、[视口投放接收](../../../src/Editor/SceneEditor/Private/EditorScenePanel.cpp)、[EditorNodePanel](../../../src/Editor/NodeEditor/Private/EditorNodePanel.cpp)。

Project 的 New/Open 复用 Directory 模式的 ImFileDialogWidget。菜单与确认回调只保存 ProjectAction 和目录值；Cancel 不提交动作。实际调用链为 PollEvents → 消费待执行工程动作 → 消费待投放网格路径 → ImMainWindow::Draw / NewFrame。打开前先检查描述及 Content，非法工程不先清空原工作区。工程元数据只记录版本/名字/Content，不保存场景，没有新增全局 ProjectManager。

CloseProject 调用 ImMainWindow::WaitForLastDraw，复用 ImSlateRenderer 已保存的窗口 completion marker，覆盖旧 GUI 采样及 Present。此时上一帧已完成，本帧尚未生成 ImDrawData，才清空材质/资产面板的选择与待处理文件，再 ClearObjects：等待视口 Tracker 的实际访问值、RemovePrimitive、Flush 消费借用组件指针、销毁组件及重置选择/相机；最后 AssetManager::Clear 移除驻留引用。预览灯光/天空仍由 EditorScene 持有。没有新全局等待、额外命令提交、资源保活容器或 RDG 更改。等待仍可能阻塞切换操作；本轮未设计后台工程切换或场景序列化。

不能在菜单的 Draw 回调内直接清理工程：即使等待上一提交，本帧 CPU DrawList 也可能已经记录了旧纹理指针。路径/动作延后消费解决的是 UI 生命周期问题，不是一套资源自动退休机制。应用公开 CreateProject/OpenProject/CloseProject 供控制层使用，也须在绘制阶段之外调用。

材质用户单击经 AssetPanel::SelectPath → OpenSelected → MaterialOpened 回调 → ShowMaterial，只打开同一个 ImGuiContext 的浮动 Material Editor，默认 Properties 页。关闭按钮仅隐藏窗口，保留当前编辑选择；工程切换则清空选择和导入队列。HitTest 使用当前浮窗和展开库的可见几何，未显示时不接受桌面文件。没有 HWND/原生渲染环境，界面不再占右侧节点区域。

网格拖动载荷只含 UTF-8 路径。视口在载荷活动时增加透明逻辑窗口和标准 ImGui DragDropTarget，正常相机操作时不生成这层目标。投递接受只通知宿主记录路径；下一 TickFrame 在当前 Content 内重新检查并创建组件，因此不在当前 Draw 过程中加载后立即替换场景缓冲。最终 Access/Fence 仍由原场景与 GUI 实际提交维护。投放位置当前为世界原点，没有视口拾取/吸附。导入只保存文件、不会自动生成模型，详见 [资源实现](05-assets-materials-and-instance-data.md)。

EditorApplication 在 TickFrame 消费工程/网格操作后、Draw/NewFrame 前，根据现有场景选择将 NodePanel 设为 Visible 或 Collapsed，仅状态改变时调用原 UpdateLayout。初始状态为 Collapsed，布局器将右栏和右分隔条宽度归零，视口使用腾出的空间。视口纹理重建沿用上述窗口完成 marker 等待；普通帧不重排、不增加等待，没有为折叠改 RHI 或资源保活策略。选择若在 Scene 面板 Draw 中改变，布局在下一 TickFrame 生效，避免重建本帧已记录引用的纹理。

节点面板本身仍在 Draw 时查询当前选择；无选中组件时在 ImPanelWidget::Draw/ImGui::Begin 前返回，HitTest 同样拒绝命中，避免当帧删除后继续生成旧属性窗口。局部变换通过现有组件接口修改，场景通知负责具体 Fence 与代理更新；该面板没有跨帧持有裸组件指针或另存选择状态。用户可见的节点列表为空不代表没有预览灯光，空工程保持背景而无演示模型。实际创建/导入/投放及读回检查范围见 [2026-10-26 记录](../../dev/bugfix/故障与修复记录-2026-10-26.md)，整栏折叠的后续检查见 [2026-10-27 记录](../../dev/bugfix/故障与修复记录-2026-10-27.md)。

## 文件拖入材质库的执行顺序

调用链：[Win32Surface::WndProc](../../../src/Framework/SlateCore/Private/Win32Surface.cpp) 的 WM_DROPFILES → [Window::OnFilesDropped](../../../src/Framework/SlateCore/Private/Window.cpp) → [EditorMaterialPanel::OnFilesDropped / ProcessImport](../../../src/Editor/MaterialEditor/Private/EditorMaterialPanel.cpp)。

1. Surface 用 DragQueryPoint 获取客户区落点，用 DragQueryFileW 复制每个完整路径。HDROP 是系统消息资源，只在该次回调内借用，局部 RAII guard 调用 DragFinish；后续数据使用拥有内容的 filesystem::path。
2. Window 逆序命中注册的可见 Widget，只投递一个目标。事件副本落点减去 Widget 原点，接收者拿到局部坐标，不依赖当前鼠标位置。未命中则返回 false。
3. 材质面板保存每个路径和投递时的分类字符串，不加载资产、不复制文件。类别在入队时固定，因此后续 UI 切换分类不改变已排队文件的目标。队列属于面板，由 UI 线程访问，没有 worker、锁、原生句柄或额外资源保活列表。
4. 下一次 DrawContent 开始时 ProcessImport 最多消费一个条目，经 MaterialLibrary 导入，再经 Engine::AssetManager::LoadSync 选择新资产。失败逐项显示，后续文件继续；取消只丢弃未执行的路径，已导入文件保留。面板关闭时尚未执行的路径随对象释放，不发生后台回调。

原生消息处理先于 NewFrame，不能在 WndProc 中直接修改当前帧的 ImGui 数据或创建依赖图形上下文的对象。现实现将工作延到主线程既有 UI 绘制流程；这不是异步导入，单个大文件仍可能产生卡顿。它没有新增 GPU WaitIdle/Fence 逻辑；实际 TextureAsset 加载/上传和使用仍走原有 Engine/RHI 路径。

较大 Material Library 是同一 ImGuiContext 中的模态窗口，不创建 HWND/交换链。面板记录该模态可见矩形作为自身补充命中区域，依赖 Editor 现有最后登记材质面板的顺序。普通 Properties 或模态关闭后清除此扩展区域；没有实现任意第三方模态之间的全局遮挡排序。模态之外的桌面文件不会被广播给材质面板。

ImGridWidget 的颜色球由 UI 图元绘制，不采样材质纹理，所以扫描、分类、过滤不会给所有材质创建 GPU 资源。当前选择的 MaterialAsset / TextureAsset 是面板强引用，其余运行时驻留/弱引用仍由 AssetManager 管理。目录与文件事务细节见 [资产实现](05-assets-materials-and-instance-data.md)。
