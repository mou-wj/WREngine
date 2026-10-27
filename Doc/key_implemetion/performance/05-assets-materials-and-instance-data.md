# 资产并发、材质快照和实例数据

架构：[Engine](../../architecture/engine.md)、[Material](../../architecture/material.md)。核心入口：[AssetManager.cpp](../../../src/Framework/Engine/Private/AssetManager.cpp)、[AssetManager.h](../../../src/Framework/Engine/PublicHeader/AssetManager.h)、[AssetRegistry.cpp](../../../src/Framework/Engine/Private/AssetRegistry.cpp)。

## 三张资产表分别解决什么

| 表 | 引用 | 用途 |
| --- | --- | --- |
| m_ResidentAssets | 强引用 | 管理器主动驻留资源 |
| m_LoadedAssets | 弱引用 | 查找仍被组件或其他资产持有的对象，避免重复加载 |
| m_Loading | LoadRequest 强引用 | 合并同路径的在途加载 |

Registry 只保存名字、规范化路径和类型，不拥有实际资源。NormalizePath 使用 absolute + lexically_normal + generic_string，不解析文件系统符号链接，也不是大小写无关的资源 GUID；不同文本是否对应同一实际文件，不能单凭“规范化”名称保证。

## LoadSync 的锁边界

1. 锁内检查 stopping，查 weak loaded；命中后验证动态类型并重新驻留。
2. 若已有 request，检查类型及 Owner 线程；同线程再次加载同一在途路径直接报依赖环。
3. 否则登记 request，保存当前 ResidencyGeneration，成为该路径的实际加载者。
4. **锁外**创建对象并执行完整 asset->Load，允许依赖资产继续递归加载。
5. 成功注册 Registry；锁内公布 loaded、按 generation/bKeepResident 决定驻留，完成 promise，移除在途项。
6. 其他请求者在锁外 Future.get，拿到同一对象或异常。

同线程循环依赖可检测；不同线程分别拥有 A、B 后互相等对方的 request，当前没有全局依赖环检测。类上有 mutex 不代表这一等待环已解决。

## 异步、清理和关闭

LoadAsync 经 std::async(launch::async) 执行 LoadSync，缓存命中回调也在异步 worker，且在 manager 锁外执行。它不是“后台只解码 CPU、主线程再创建 GPU”的分阶段导入器。具体 Load 可能同步调用 RHI；后端 context 限制仍然适用。

Clear 增加 generation 并移走驻留强引用，离开锁后析构，防止旧加载把清理前的请求重新驻留。Unload 标记该路径在途 request 的 bKeepResident=false，并移除驻留，但不会强制销毁其他对象仍持有的资源。

WaitForLoads 锁外等待 task/request，反复收集直至无任务；Shutdown 先拒绝新请求，再等待、清驻留。**控制线程调用，不可从加载回调调用**，否则可能等待包含自身的 future。已完成 task 的 get 才会观察并记录异常。

## 所有权和材质编辑

MaterialAsset 独占 Material；网格组件引用资产，并按需独占 MaterialInstance。材质实例不是 Asset，稀疏保存覆盖；Material 和 Instance 的纹理值强引用 TextureAsset。母材质对所属资产为弱引用，避免环。

[Material.cpp](../../../src/Framework/Engine/Private/Material.cpp) 发布不可变参数/状态快照。动态参数仅改变 Parameters revision；CullMode/TwoSided/BlendMode 改 RenderState；ShadingModel 改 Compilation 并替换 MaterialResource。批次捕获 shared_ptr，保证它仍被引用期间 CPU 参数不被编辑覆盖，不等于 GPU 自动安全退休。

MaterialResource 保存编译输入、请求过的 Shader 强引用与最后请求错误，查询复用全局 ShaderMap。当前程序固定 BuiltinSurface；前向/延迟 Surface Pass 显式拒绝未实现的 Masked/Translucent。不能根据 Opacity 参数存在就宣称透明流程已接通。

名称、语义、类型构成参数 key；同名 BaseColor 向量和纹理可并存。构建绑定见 MaterialShaderParameter.cpp，默认纹理取 RenderCore globals，不作为 TextureAsset 或序列化依赖。各值和 TextureMask 位定义仍必须与 Shader 一致。

## 实例数据的两次索引

[InstanceDataMgr.inl](../../../src/Framework/Engine/PublicHeader/InstanceDataMgr.inl) 每个 Block 保存 SubParameters 数组和 InstanceIds 数组。添加返回逻辑元素 offset，而不是 GPU 字节地址；CPU 用 sizeof(T) 换算上传字节。批次的 StartInstance/instance-id 流使 Shader 先取实例 ID，再索引参数结构，不能把 element offset 直接当 buffer offset。

BeginUpdateGPUResources 清空本次可见实例 ID 并置 dirty；参数数组可继续存在。UpdateGPUResources 在容量不足时扩容，dirty 时上传并显式转为读取状态；并非每帧复制整套静态资源。当前代码中实例参数读取使用 ShadingRateSource 访问标志，不能仅凭命名把它解释为标准 SRV；这是需要与后端映射核查的现状。

管理器锁可能覆盖 GPU 初始化、上传和等待，锁范围具有阻塞成本。没有统一的 GPU 完成后原地更新协议可从 CPU mutex 自动推导；并发编辑、渲染与释放依旧需要调用顺序约束。

验证入口：AssetOwnershipTest、MaterialContractTest、MaterialBindingTest。它们各自覆盖加载合并/所有权、参数/快照、实际绑定/默认像素；不是任意 GPU 资产后台加载或连续替换纹理压力测试。

2026-10-26 的 Editor 直接使用这些接口枚举/编辑参数，仅有效编辑时创建组件实例；复制网格为覆盖创建独立实例，母材质保存副本仍通过 MaterialAsset。界面不提供 MaterialInstanceAsset，也不新增资产管理器。场景资源重建前的具体 Fence 等待见 [UI/Editor 生命周期](08-ui-rendering-and-window-lifetime.md)，业务编辑和材质快照发布继续串行。

## 资源文件系统树与当前目录网格

[EditorAssetPanel](../../../src/Editor/AssetEditor/Private/EditorAssetPanel.cpp) 将文件显示与实际资产加载分开。FileEntry 只保存路径和 bDirectory；m_TreeEntries 按目录保存已访问的子项，目录树与右侧网格读取同一份 CPU 元数据。未展开的目录按需读取，文件树过滤时可能递归扫描其他分支；不在每个正常 UI 帧重新进行全盘枚举。Refresh 清除此元数据并重新读取，不清空 AssetManager，也不执行资产热重载。没有文件 watcher，外部变化要显式刷新。

调用链为目录树选择/卡片双击/路径栏/历史导航 → NavigateTo → 目录检查与读取 → 更新当前目录/选择/路径栏 → DrawResources 生成 ImGridItem。m_SelectedPath 是 UI 选择，不是另一个资产 Id；普通单击设置路径与卡片高亮，材质 JSON 的用户点击额外调用 OpenSelected，加载成功后通知宿主打开浮动材质编辑器。卡片的材质打开延至松开且未超过拖动阈值，目录树的原打开方式保留；Import/Refresh 的程序选中不触发弹窗。OpenSelected 调用原有 LoadSync：glb/gltf 加载后交给宿主的 MeshOpenedCallback，材质/纹理由面板保留当前强引用，网格/材质依赖仍由实际拥有者维持。Editor 宿主保存网格路径，在下一次 TickFrame 绘制前创建组件。关闭时面板自身销毁，文件路径/历史/元数据同步释放，不存在后台回调。

文件系统路径使用 filesystem::path，显示转为 UTF-8。Editor 将当前工程的 Content 设为 res://，无工程时根目录为空，不浏览引擎内置 resources；无参面板构造保留 resources 默认值供其他宿主使用。导航先 weakly_canonical 再检查位于内容根；跳过文件系统判为符号链接的条目，避免直接遍历这些链接。没有把该检查宣称为全文件系统身份/GUID 机制；Engine 既有窄字符串加载和中文字体覆盖仍有限制。

目录条目先按目录/文件分组排序。左侧过滤递归检查名字及子项，以保留匹配文件的父目录，并展开对应路径；右侧仅过滤当前目录的名字和扩展名类型。文件系统树可能重复检查子树，因此不能宣称过滤任意大型项目始终为 O(n)；首次过滤和大量图元提交仍可能阻塞 UI。m_TreeEntries 的内存随已访问目录增多，Refresh/面板销毁时释放；它只缓存文件元数据，不拥有 GPU 对象或重复缓存资产资源。

可拖动的内部树/网格边界只改变 ImGui 子区域宽度，不改变 SceneViewport，不触发 GPU 等待或纹理重建。底部操作条高度单独预留；树、网格分别滚动，窄区域隐藏卡片尺寸滑块。图标由公共 ImGridWidget 用图元绘制，浏览目录不加载整批贴图/网格，也没有新增缩略图 GPU 生命周期。

错误路径需要保持 UI 栈平衡：每个已展开 TreeNode 在目录读取失败后仍 TreePop；右侧目录元数据在进入资源网格 child 前准备，异常交给外层显示，避免跳过 EndChild。已删除文件选中失败时清空 UI 选择，后续双击不会错误打开之前选中的文件。目录被外部删除后的 Refresh 保留最后位置并显示错误，用户可用历史/路径栏恢复。

临时验证入口 out/diagnostics/editor-smoke/asset-main.cpp 使用实际 AssetEditor.dll、RHI/Engine 和现有 imgui_impl_rhi.cpp：投递自身 ImGui 输入，检查选择不加载、双击目录/资产、导航历史、名字/文件树筛选、内部分隔条、删除目录后的 UI 栈及窄布局；另渲染到离屏目标并读回检查图标与布局。此验证不等价于 OS 桌面鼠标操作，结果见 [同日记录](../../dev/bugfix/故障与修复记录-2026-10-26.md)。

## 工程资源导入与网格投放

入口为 EditorAssetPanel::QueueImport；Import 按钮复用既有 ImFileDialogWidget 的 MultipleFiles 模式，桌面文件事件也调用该入口。每项只保存源 filesystem::path 和入队时的目标目录，DrawContent 每帧最多消费一项。所有文件操作在 UI 线程同步执行，不建立后台加载器、额外资产管理器或线程队列；大文件仍会阻塞 UI。SetContentRoot 关闭文件对话框并清理未执行项、目录元数据、历史、筛选、选择及当前材质/纹理引用。

ImportFile 的执行顺序为：

1. 检查源为普通文件且扩展名受支持，规范化目的目录并确认位于当前内容根内。名字转换为最多 64 字符的 ASCII 文件名；用 create_directory 独占创建包目录，已有文件或目录用数字后缀避让，不覆盖源文件或已有包。
2. GLB 检查 2.0 文件头及 JSON chunk，buffers/images 的 URI 只接受 data URI 或无 URI 的内嵌数据；有外部依赖时明确报错，不复制一个依赖已丢失的 GLB。此检查不等价于完整 GLB 内容验证。
3. glTF JSON 的 buffers/images 本地 URI 经百分号解码后复制成 dependency_N.ext，data URI 保持原样，带远程协议的 URI 拒绝。材质 JSON 的 textureParameters 文件引用同样复制，依赖路径基于源文件目录解析，输出 JSON 改为包内相对路径。该通用资源入口不承担 MaterialLibrary 的完整材质参数校验，JSON 是否为可用材质仍由 MaterialAsset 加载确认。
4. 依赖复制完才写主 JSON，GLB/图片直接复制主文件；输出关闭并检查写入状态。失败只清理本次独占创建且仍位于内容根内的目标目录，源文件不变；这不是断电安全事务。单文件复制没有中途取消或内容去重。
5. 成功后清空文件元数据缓存并导航到新包，选中主文件；不自动加载资产、不生成场景节点、不自动打开材质编辑器。图片解码、网格/材质语义验证在用户实际打开时执行，失败的已发布文件保留供修复。

网格卡片只为普通 GLB/glTF 文件发布 EDITOR_ASSET_PATH，载荷为含结束符的 UTF-8 路径副本，不是 StaticMeshAsset/组件裸指针。SceneEditor 的 EditorSceneViewportWidget 仅在该载荷活动时显示透明逻辑投放区域；接受后调用宿主保存路径，下一 TickFrame 在当前工程 Content 内重新校验路径，通过 LoadSync<StaticMeshAsset> 和 EditorScene::AddMesh 创建世界原点的静态网格并选中。资源导入与场景实例化是两次显式操作，不把鼠标屏幕位置当成已实现的射线落点。

Project Close 先等待该窗口既有提交完成值，在下一帧绘制开始前清空面板引用/场景组件，消费场景移除命令后释放组件，最后 AssetManager::Clear 移除驻留引用；仍被预览天空资产/灯光持有的内置资源继续有效。没有更改 Engine 的强/弱资产表或资源拥有关系。工程描述与清理调用链见 [Editor 架构](../../architecture/app/editor.md)，CPU/GPU 时序见 [UI 生命周期](08-ui-rendering-and-window-lifetime.md)。

材质卡片使用 EDITOR_MATERIAL_PATH；路径与归一化落点由宿主保存，下一 TickFrame 在当前 Content 内重新核查并经 LoadSync<MaterialAsset> 加载。SceneEditor::ApplyMaterialAt 的最近几何命中决定组件及 Section.MaterialIndex，沿用 StaticMeshComponent::SetMaterial。组件强引用 MaterialAsset，旧槽位实例由原接口清理；网格默认材质、其他槽位和其他组件不变，不新建材质实例或另一套资产缓存。失败/背景投递不修改组件，错误显示于宿主菜单状态；工程关闭也清理尚未执行的材质路径。数学见 [视口拾取](../algorithm/01-ray-triangle-and-distance.md)。

## 材质文件库与显式导入

[MaterialLibrary](../../../src/Editor/MaterialEditor/Private/MaterialLibrary.cpp) 是 MaterialEditor 的文件操作对象，不替代 AssetManager/Registry。Refresh 扫描库根目录的 JSON，读取元数据，并用 Engine 的参数定义检查参数名/类型；目录形成左侧分类。条目只保存路径、名字、ShadingModel 和 BaseColor 等 CPU 数据，异常文件形成 warnings，不生成另一份运行时资产缓存。扫描为目录/文件数线性遍历加排序，当前不使用 watcher，外部更改需手动 Refresh；Refresh 不执行已有资产的原地热重载。

Import 的顺序：

1. 仅接收项目材质 JSON 或 PNG/JPG/JPEG/BMP/TGA 文件；图片构造 version 1、Lit、Opaque 的 BaseColor 纹理引用。JSON 复用当前参数定义校验，未知渲染枚举/启用的静态功能报错。图像实际解码在后续 TextureAsset::Load 中执行，不将文件扩展名检查视为解码成功。
2. 分类路径经 weakly_canonical 后必须位于库根目录内，拒绝 `../` 逃逸。以 ASCII 安全名字创建独占目标目录，重名目录/文件均跳过并加数字后缀；显示名称仍保留原 UTF-8 字符。desktop 源路径由 filesystem::path 保存，避免先用窄字符串破坏中文源文件名。
3. 解析 JSON 的 textureParameters，原相对路径基于源 JSON 所在目录。每个非 null 依赖复制到新目录的 texture_N.ext，再将 JSON 引用改为此相对路径。copy_file 不允许覆盖。图片导入直接使用收到的源路径，不重复拼接其父目录。
4. 依赖全部存在后才写 material.json，并 Refresh。异常只删除本次独占创建、且确认在库根目录内的目标目录，源文件保持原样；新建的空分类父目录可能保留。JSON 最后写入只是本应用同步调用中的发布顺序，不是磁盘断电事务，也不保证外部 watcher 看不到写入过程。
5. 面板再经 AssetManager::LoadSync<MaterialAsset> 选择导入结果。解码/GPU 加载失败会显示错误，但已写入的材质文件仍保留供用户修复，不擅自删除已发布文件。尚未实现取消单次复制、依赖去重或异步转换。

复制操作读取已保存的 JSON 与贴图，不隐式保存当前未落盘的参数。新建与保存副本保持原版序列化接口，Opaque 以外的材质可读取元数据，但 ApplySelectedMaterial 明确拒绝当前 Surface 未支持的模式。赋值调用组件 SetMaterial，只引用 MaterialAsset 的母材质，实际属性修改才按既有接口创建独占实例。

资源根目录包含中文路径时，Engine 的既有窄字符串文件加载仍有约束；此次使用 ASCII 导入文件名解决桌面中文源名，不宣称全项目 Unicode 路径已经改造。分类界面完整中文显示仍依赖字体覆盖。导入事件时序与资源拥有关系见 [UI 实现](08-ui-rendering-and-window-lifetime.md)，已执行检查见 [同日故障记录](../../dev/bugfix/故障与修复记录-2026-10-26.md)。
