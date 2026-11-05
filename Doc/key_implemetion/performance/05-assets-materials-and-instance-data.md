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

## 环境光计算产物管理

[EnvironmentLightResourceManager.h](../../../src/Framework/Engine/PublicHeader/EnvironmentLightResourceManager.h) / [EnvironmentLightResourceManager.cpp](../../../src/Framework/Engine/Private/EnvironmentLightResourceManager.cpp) 位于 Engine。环境输入是 TextureAsset；diffuse irradiance 和 specular prefilter 是运行时生成结果，由管理器持有，不继承 IAsset、不注册到文件资产系统。旧 SkyLightAsset 已移除。

```cpp
auto& environments = Engine::EnvironmentLightResourceManager::Get();
auto environment = textureAsset ? environments.Build(textureAsset) : environments.GetDefaultEnvironment();
environments.ApplyEnvironment(skyLight, environment);
// 解除应用：environments.ApplyEnvironment(skyLight, nullptr);
```

EnvironmentLightResource 是该头内的结果对象，私有成员保存输入 TextureAsset 及两张 RenderTexture，公开只读访问。管理器 m_Resources 按 TextureAsset 对象地址保存强引用，同一实际输入对象复用一套计算结果；同路径但独立导入的不同对象不会混同。结果强引用输入，所以表内键在该项存活期间不会悬空。SkyLightComponent 只持有结果使用引用；两个 IBL Getter 从该引用查询，直接写入裸纹理的旧 Setter 已移除。ApplyEnvironment 修改引用后沿 MarkRenderStateDirty 更新既有代理，不新建场景光照通道。

GetDefaultEnvironment 惰性生成中性灰默认环境。输入为管理器自己创建并上传的 1×1 RGBA8 像素 (128,128,128,255)，使用当前线性 UNorm 语义，随后复用同一 IBL 构建路径。默认输入不是 TextureAsset、没有文件路径，不显示为用户资源；结果的 GetInputTexture 返回空引用，属性显示 Default。Editor 新建天空光和原 Application 都应用这个默认值，不读取 DaySky 示例图片。m_DefaultInput/m_DefaultEnvironment 由管理器持有，在没有使用引用且调用 ReleaseUnused 时一并清理，Engine Clear 也清理它们。

Build 校验输入已加载、有 RHI、为单采样且可作为 SRV 的 Texture2D。它复用现有 Renderer::PreComputeIBL：创建 512×512、六面 cube，diffuse 单 Mip，specular 六 Mip，格式保持 BGRA8 UNorm、用途 SRV/UAV。这些配置是当前预计算接口的既有约束，没有本轮新增的可变质量设置。输入必须是现有着色器所期望的经纬环境图；类型校验不能判断图片本身是否具备这种语义。

预计算完成提交后，输入和两个输出的 Tracker 应记录同一个非零 Compute Fence；Build 核对并等待该实际值，再将结果加入表。未提交全部输出则报错，失败结果不入表。这个完成点允许刚恢复的独立对象因后续校验失败而立即清理，不用初始化后的全局保活列表或 WaitIdle。后续场景采样的状态和 Fence 仍由实际 Renderer/RDG 提交位置维护。

ReleaseUnused 只移除 use_count==1（仅管理器持有）的项，不等待 GPU。调用方必须先按原框架完成 GPU 使用；引用计数只表示 CPU 使用关系。EditorScene 在删除、ClearObjects 和 Shutdown 中沿既有 WaitForLastDraw/窗口完成点、Remove/Flush、销毁组件后调用它；替换新场景时，尚未搬入的新节点也持有引用，因此不会误清理。ProjectArchive::Load 使用函数异常处理，在局部恢复对象全部析构后清理本次未被使用的结果，初始化已由 Build 完成，原活动场景的引用继续有效。

Clear 由 EngineModule::ShutdownModule 在 AssetManager::Shutdown 之后、RHI 关闭之前调用，移除管理器拥有引用。宿主应已释放场景及工具使用引用；Clear 不强制销毁仍有外部 shared_ptr 的结果，也不把其释放推迟到 RHI 关闭以后。原 Application 的 ReleaseResources 同时补齐 SkyLightComponent 的 delete，并显式清理未使用结果，避免组件泄漏使结果跨越模块关闭。

这些方法与场景修改在控制线程串行，不为管理器增加 mutex、工作线程或异步请求合并。不能从 AssetManager 的加载 worker 任意调用 Build。当前不检测输入 GPU 内容的原地修改，也没有热重载失效机制；输入修改后的主动重建属于后续明确接口需求。缓存命中为平均 O(1)，ReleaseUnused 为 O(结果数)，首次 Build 同步阻塞于 GPU 预计算。工程只保存输入基础层，生成结果不写盘；格式与恢复见 [工程快照](10-project-snapshots.md)，IBL 数学见 [采样算法](../algorithm/05-pbr-ibl-and-sampling.md)。本轮实际验证与未通过项见 [开发记录](../../dev/bugfix/故障与修复记录-2026-11-04.md#环境光计算产物改由-engine-管理)。

天空光的 EnvironmentTexture 属性使用 ComponentProperty 的 TextureAsset 类型，值是 shared_ptr<TextureAsset>；空值表示恢复默认。属性提供者调用 Build/GetDefaultEnvironment 和 ApplyEnvironment。NodeEditor 根据类型统一绘制当前名称、Use selected texture 和 Reset，不按天空光类型定义专用面板；宿主用回调提供资源面板的选中纹理，两个功能模块不互相依赖。输入不支持或构建失败时显示属性错误，并保留原绑定。Editor 在原 NotifySelectedChanged 完成点之后 ReleaseUnused，保留仍在使用的结果。保存集合显式收集天空光的输入资产，使其立即进入 Project data；恢复的源纹理也在该列表中。资源面板单击图片会经 LoadSync 选中实际 TextureAsset，生成结果和默认输入均不作为文件卡片伪装出来。

GetResources 为检查工具提供控制线程上的已构建结果快照，返回强引用而非管理器容器引用，包含已存在的默认结果和输入缓存项，不自动创建默认值。TextureViewer 通过宿主回调枚举输入资产及 diffuse/specular 输出；其打开的来源页/显示项保有结果引用，按既有 Fence 完成点解除引用后才调用 ReleaseUnused。该接口不新增资产类型、全局预览缓存或并发读取保证，完整顺序见 [TextureViewer 生命周期](08-ui-rendering-and-window-lifetime.md#textureviewer-来源与资源生命周期)。

## 基础几何资产的创建入口

[StaticMeshAsset::CreateShape](../../../src/Framework/Engine/Private/Shape.cpp) 使用 EBasicMeshShape 选择既有生成器；每次创建一份 StaticMesh/LOD、顶点和索引资源、默认白色 MaterialAsset 依赖及局部距离场。生成算法不复制到 Editor，组件也不放宽“必须由资产拥有”的检查。返回的资产强引用网格，网格弱引用资产，组件在 SetStaticMesh 时保留资产。没有注册第二套缓存或常驻表；移除组件并释放最后一个资产引用后，网格包装随之销毁，相关 RHI 完成点继续遵循原资源约定。默认材质和距离场 Atlas 的全局生命周期仍属于原模块，不因网格资产析构而整体释放。

`builtin://shape/名称` 是内存资产身份，重复创建同类几何仍是不同资产实例，不通过 AssetManager 的文件路径归一化或加载合并。该标识不传给 LoadSync；工程快照保存实际几何并通过 StaticMeshAsset::Create 恢复。EditorApplication 在下一帧绘制前执行创建，再沿 AddMesh/Flush 注册；创建、距离场构建与上传同步执行，可能阻塞该帧。不能在任意后台线程调用，也没有以 CPU shared_ptr 替代 GPU 完成点。工程内容、共享关系和独占实例保存见 [快照实现](10-project-snapshots.md)，右键菜单时序见 [UI 实现](08-ui-rendering-and-window-lifetime.md)。

StaticMeshAsset 析构沿实际所有权释放各 LOD 的距离场 Atlas 槽位；原 Atlas 全局纹理仍由模块拥有，不随单个网格释放。DistanceFieldManager 的分配失败会回滚并报错，避免反复打开工程后继续上传零尺寸分配。调用方须先完成场景移除和原 GPU 完成点；固定 8 槽以及新旧场景同时准备的限制见 [工程快照](10-project-snapshots.md#代价和验证)。

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

1. 检查源为普通文件且扩展名受支持，规范化目的目录并确认位于当前内容根内。名字转换为最多 64 字符的 ASCII 文件名。图片直接 copy_file 到当前目录，使用 none 禁止覆盖；重名文件/目录用数字后缀避让，不再给每张图片额外建目录。其他格式仍用 create_directory 独占创建暂存目录。
2. GLB 检查 2.0 文件头及 JSON chunk，buffers/images 的 URI 只接受 data URI 或无 URI 的内嵌数据；有外部依赖时明确报错，不复制一个依赖已丢失的 GLB。此检查不等价于完整 GLB 内容验证。
3. glTF JSON 的 buffers/images 本地 URI 经百分号解码后复制成 dependency_N.ext，data URI 保持原样，带远程协议的 URI 拒绝。材质 JSON 的 textureParameters 文件引用同样复制，依赖路径基于源文件目录解析，输出 JSON 改为包内相对路径。该通用资源入口不承担 MaterialLibrary 的完整材质参数校验，JSON 是否为可用材质仍由 MaterialAsset 加载确认。
4. 依赖复制完才写主 JSON，GLB 直接复制主文件；输出关闭并检查写入状态。有外部依赖时该目录作为最终包；dependencyIndex 为零时把自包含 GLB/JSON 发布为当前目录的独立文件，并删除空暂存目录，重名同样加后缀。失败清理只针对本次独占创建且仍位于内容根内的目录，源文件不变；这不是断电安全事务。单文件复制没有中途取消或内容去重。
5. 成功后清空文件元数据缓存并导航到结果的父目录，选中主文件；不生成场景节点、不自动打开材质编辑器。图片沿 SelectPath 的既有逻辑立即加载为当前选中纹理，网格/材质在显式打开时加载。失败的已发布文件保留供修复。

网格卡片只为普通 GLB/glTF 文件发布 EDITOR_ASSET_PATH，载荷为含结束符的 UTF-8 路径副本，不是 StaticMeshAsset/组件裸指针。SceneEditor 的 EditorSceneViewportWidget 仅在该载荷活动时显示透明逻辑投放区域；接受后调用宿主保存路径，下一 TickFrame 在当前工程 Content 内重新校验路径，通过 LoadSync<StaticMeshAsset> 和 EditorScene::AddMesh 创建世界原点的静态网格并选中。资源导入与场景实例化是两次显式操作，不把鼠标屏幕位置当成已实现的射线落点。

Project Close 先等待该窗口既有提交完成值，在下一帧绘制开始前清空面板引用/场景组件，消费场景移除命令后释放组件，最后 AssetManager::Clear 移除驻留引用；仍被其他使用方引用的输入资产继续有效；环境光计算结果由管理器在组件清理后显式释放。没有更改 Engine 的强/弱资产表或资源拥有关系。工程描述与清理调用链见 [Editor 架构](../../architecture/app/editor.md)，CPU/GPU 时序见 [UI 生命周期](08-ui-rendering-and-window-lifetime.md)。

材质卡片使用 EDITOR_MATERIAL_PATH；路径与归一化落点由宿主保存，下一 TickFrame 在当前 Content 内重新核查并经 LoadSync<MaterialAsset> 加载。SceneEditor::ApplyMaterialAt 的最近几何命中决定组件及 Section.MaterialIndex，沿用 StaticMeshComponent::SetMaterial。组件强引用 MaterialAsset，旧槽位实例由原接口清理；网格默认材质、其他槽位和其他组件不变，不新建材质实例或另一套资产缓存。失败/背景投递不修改组件，错误显示于宿主菜单状态；工程关闭也清理尚未执行的材质路径。数学见 [视口拾取](../algorithm/01-ray-triangle-and-distance.md)。

## 资源文件移动、重命名与删除

入口：[EditorAssetFileOperations.cpp](../../../src/Editor/AssetEditor/Private/EditorAssetFileOperations.cpp)、[EditorAssetPanel.cpp](../../../src/Editor/AssetEditor/Private/EditorAssetPanel.cpp)、[AssetManager::UpdateSourcePaths](../../../src/Framework/Engine/Private/AssetManager.cpp)、[AssetRegistry::Unregister](../../../src/Framework/Engine/Private/AssetRegistry.cpp)。总体边界见 [Editor 架构](../../architecture/app/editor.md#资源文件系统与卡片视图)。文件操作作为面板成员的独立实现文件组织，没有新增资源管理器或公共 UI 框架。

### 操作与路径检查

目录树和网格条目共用 DrawFileItemActions。文件夹/图片/普通文件发布 EDITOR_CONTENT_PATH；网格、材质仍发布原 EDITOR_ASSET_PATH / EDITOR_MATERIAL_PATH，使原场景投放继续可用。文件夹条目和右侧当前目录区域接收三种路径载荷，按释放时目标目录与源文件名形成移动请求；载荷是 UTF-8 路径副本，不借用节点或资产。根只作为目标，不能拖出或删除。目录树的材质打开改为松开且未拖动时执行，避免拖放途中被材质弹窗抢走输入。

右键菜单保存重命名/创建目标，模态输入校验单个名字，拒绝分隔符、控制字符、Windows 保留设备名及尾部点/空格；删除模态明确提示会删除磁盘文件和目录内容。Draw 只记录 FileOperation，下一次 DrawContent 在遍历缓存前执行，避免遍历目录 vector 或 ImGridItem 时修改其来源。切换工程清除操作、模态和输入。创建/移动后刷新缓存、导航和选择；移动还重映射历史及未执行导入的路径，删除清理失效历史并退回有效父目录。

执行时重新 weakly_canonical 并检查所有目标仍位于 res://，禁止移动/删除根、直接符号链接、目录自嵌套及已有重名目标。错误保留文件并显示在原状态条中；没有实现覆盖、合并目录或大小写独有的重命名。文件 IO 在 UI/控制线程同步执行，大目录扫描/复制/删除会阻塞界面；没有 watcher、多选批量移动或跨工程迁移事务。

### 依赖维护与失败边界

MoveFilePath 在文件变动前 WaitForLoads，再 CollectEdits 扫描当前 Content 的有效材质 JSON textureParameters，以及 glTF 的 buffers/images URI。本地引用基于原文档目录解析为路径 T；源子树 S 移到 D 时，T 属于 S 则换为 D/(T 相对 S 的路径)，文档自身位置同样映射。新引用相对于新文档父目录计算，因此同时支持移被引用图片、移引用者和整体包；glTF 本地 URI 先百分号解码，再按 UTF-8 字节重新编码，data/远程 URI 不改写。移动单独 JSON 时，对子树外的依赖也会更新相对路径。

收集完所有改写后才 rename 文件/目录，再写回变化的文档。局部保存原文，普通写入异常会尝试恢复全部 JSON 和源目录，恢复也失败时把两项错误一起报告；不保证断电、磁盘损坏或外部程序并发修改时的原子性。解析失败或未知 JSON 不猜测其引用，外部手放且含外部 URI 的 GLB 不重写；通用导入入口仍拒绝这种 GLB。扫描成本 O(项目目录项数+支持文档字节数)，改写备份内存 O(变动文档大小)，没有新增持久依赖图或引用缓存。

DeleteFilePath 同样先等待加载，再检查支持的引用。源子树之外的文档仍引用源内文件时拒绝删除并显示引用者；一起删除的文档不阻止删除整个包。确认后直接 remove_all，不进入回收站，不提供撤销。实际删除失败可能已经移除部分目录项，不宣称递归删除是可回滚事务。

### 路径缓存与资源对象

文件成功移动后调用 UpdateSourcePaths(source,destination)，删除使用空 destination。接口要求控制线程串行、先完成 WaitForLoads，检测到尚未收尾的请求/任务则拒绝，不能从加载回调中调用；等待须在改文件前完成，避免异步 loader 正在读旧路径。管理器持有自身 mutex 时只更新已有强/弱路径表、资产源路径和 Registry 元数据；Registry 使用独立短锁，当前没有同时持有 Registry 锁再取 Manager 锁的反向调用链。被移除/替换的驻留引用收集在局部 vector，离开 Manager 锁后才析构，不在锁内执行资源析构或 RHI 操作。

移动覆盖 source 本身及目录后代的键，保留 shared_ptr、实际纹理/网格/Material 和用户显示名，后续从新路径 LoadSync 返回原对象。IAsset::SetSourcePath 为受保护虚接口，由管理器协调调用，三个文件资产只改 m_Path；新增文件资产需实现此能力。删除移除源路径查询/驻留/Registry，将仍存活对象的源路径置空；已有组件、材质、检查窗口或工程对象的强引用继续拥有内容。重新导入旧文件名会新建资产，不能通过旧弱缓存返回已删除文件的对象。文件来源名与运行时资源身份不混同，不为文件移动重新加载所有 GPU 资源，也不强制删除场景节点。

这些操作不改变工程快照保存格式，也不引入 GPU WaitIdle、Frame、全局保活表或 Renderer 依赖。已有资源的实际 GPU 使用仍须由原所有者和访问完成点控制；磁盘删除不是任意线程安全的 GPU 卸载 API。普通材质 Save copy 能使用移动后的 TextureAsset 路径；源已删除的纹理属于运行时对象，仍通过工程二进制快照保存，不能伪装成存在的独立纹理文件引用。验证与已知限制见 [2026-11-04 记录](../../dev/bugfix/故障与修复记录-2026-11-04.md#资源目录操作与单文件导入)。

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
