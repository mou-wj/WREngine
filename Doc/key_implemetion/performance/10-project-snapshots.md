# 工程快照、对象关系与二进制数据

## 入口和边界

工程保存的是当前运行时对象的可恢复数据，不是源模型路径，也不是 C++ 内存镜像。入口为 Engine 的 [ProjectArchive.h](../../../src/Framework/Engine/PublicHeader/ProjectArchive.h) / [ProjectArchive.cpp](../../../src/Framework/Engine/Private/ProjectArchive.cpp)，宿主为 [EditorApplication](../../../src/Editor/Editor/Private/EditorApplication.cpp)，场景所有者为 [EditorScene](../../../src/Editor/SceneEditor/Private/EditorScene.cpp)。模块关系见 [Engine 架构](../../architecture/engine.md) 和 [编辑器架构](../../architecture/app/editor.md)。

ProjectNode 保存 Name、Id 和 unique_ptr<SceneComponent>。ProjectSnapshot 包含 Nodes 与 Assets：后者是工程内容对象，包括尚未实例化的资产，不是 GPU 保活列表或第二套 AssetManager。组件继续强引用 StaticMeshAsset/MaterialAsset；资产拥有实际资源，组件独占 MaterialInstance。序列化过程中的编号表只在单次读写期间存在，不缓存原文件路径。

当前支持基础 SceneComponent、StaticMeshComponent、点光、聚光、平行光、天空光，以及网格、材质和普通颜色纹理。新增组件必须明确增加格式处理；不能把未知类型静默转换为基础节点。编辑相机、布局比例、选择、编辑轴状态、GPU 句柄、代理、Fence、RDG、任务和窗口均不属于工程内容。

## 文件与数据格式

```text
RenderingLearn.project.json
Content/                         导入候选文件；恢复场景不读取这些源文件
Data/snapshot-<generation>/
  Scene.json                     完整节点集合、资源表和内部引用
  Geometry.geoms                 CPU 几何数据
  Texture.texs                   纹理像素数据
```

工程入口 version=2，保留 name/contentDirectory，并用 snapshot 指向某一代 Scene.json。旧 version=1 工程仍打开为空场景，首次 Save 升级；未知版本报错。Scene.json 自身 version=2，包含 nodes/meshes/materials/textures 四个数组。早期未提交原型的 Scene version=1 保存全部 Mip，现已移除该读写路径，加载明确报版本不支持。

两个二进制文件的 8 字节头分别为 ASCII `RLGEOM01`、`RLTEXS02`。几何数据使用小端 IEEE 754 float32 与 uint32；代码在编译时要求小端和 4 字节 float，不写含 padding 的结构体。JSON 记录数据块的 offset/bytes，偏移相对文件起点。加载检查文件头、偏移、长度、元素大小、文件边界与 RHI 单次上传大小上限；当前没有压缩、内容校验和或跨字节序转换。

网格的每个 LOD 保存 Position、Normal、UV、Tangent、Color 五个流，各含原分量数、bValid 和数据块；另存索引与 Section 的 FirstIndex/NumIndices/BaseVertexIndex/MaterialIndex。Bounds 从恢复的位置数据重算，GPU 缓冲、VertexFactory 和局部距离场重建，不序列化这些派生对象。当前数据必须符合已有 Renderer 的三角形及顶点流约定，畸形 Section/索引会报错。

纹理记录格式名称、2D/2DArray/Cube 类型、尺寸、目标 Mip 数和切片数；data 数组按 slice 排序，每个切片只有第 0 层的一个数据块，不写任何高层 Mip 像素。目标 Mip 数是重建配置，加载逐级生成，不是持久化派生数据。每块是紧密排列的原格式像素，不是驱动专有平铺数据。当前支持格式表中非深度/模板、非 MSAA 的这些纹理，维度和数组范围显式校验。未知类型、未初始化基础层或未声明 TransferSrc 的纹理明确报错。

普通 Mip 在 ProjectArchive 加载过程中用 CPU 面积滤波重建，随后沿用 UploadData 和具体子资源 SRV 转换。使用 UNorm8、Float16/32 的原格式；sRGB 的 RGB 在线性空间平均，Alpha 按线性值处理。立方体各面和数组各层独立生成，未实现跨面接缝修正、法线专用滤波或透明覆盖率保持。算法、奇数尺寸和精度见 [纹理 Mip 重建](../algorithm/05-pbr-ibl-and-sampling.md#工程纹理的普通-mip-重建)。现有 RenderTexture::GenerateMipMaps 仍为空实现；本流程没有调用它，也没有为快照增加后端 API。

材质记录共享母材质的参数、BlendMode、ShadingModel、CullMode、TwoSided；参数按名称和类型区分，纹理参数指向工程纹理编号。组件材质槽单独记录覆盖资产编号、是否存在实例及实际覆盖项。没有实例时不创建实例；没有覆盖的参数继续继承母材质，不把解析后的有效参数全部固化。

## 节点与资源身份

节点 Id 在当前场景创建时分配，保存、重开、删除其他节点后保持；加载后下一编号从最大已有值递增。parent 记录父节点 ID，0 表示根。记录局部位置、Euler 角度（度）、缩放和 Visible。光源另外记录 Enabled、颜色、强度、阴影、适用范围以及聚光锥角（显式为度）；照射方向由恢复后的变换派生，不与 Rotation 重复存储。天空光的 environment 指向工程纹理表中的环境输入基础层，不保存 diffuse/specular IBL 输出，也不保存环境源图片路径。镜面 IBL 的各层代表不同粗糙度，加载必须重新预计算，不能用普通缩图代替。

资源编号按当前实际资产对象身份分配：相同路径但已经分离的数据不会因为路径相同而错误合并。多个节点共享同一个资产，加载时仍共享；相同 RenderTexture 被材质和天空光输入引用时，只写一次基础层像素。资源编号只在该快照内有效；当前不承诺其跨代保持不变。

保存集合包括当前节点及其递归资源依赖、工程已恢复的内容、Content 内已加载的驻留资产及材质编辑器当前持有的材质。未加载的导入文件仍是候选源文件，不会为保存而全部加载。源文件浏览与工程对象列表分开；保存/打开后 Assets 的 Project data 可查看恢复的网格、材质和纹理，网格双击可创建实例，材质可打开和赋给组件。

删除节点后，下一次保存只写剩余节点，不记录对源模型的删除补丁。删除父节点沿当前组件关系移除其整个子树，先解除工具借用、Remove/Flush，再销毁所有者；共享网格资产可以继续作为工程内容存在。场景文件不会重新展开源模型，因此已删除节点不会自动复活。当前导入器仍没有完整 glTF node 层级导入，本次没有改造该独立功能；运行时已经建立的组件层级可以保存恢复。

## 保存时序与一致性

Project 菜单或 Ctrl+S 只在 Draw 记录操作，下一 TickFrame、当前 GUI 绘制前执行 Save。读写都在既有场景控制线程串行，不新建工作线程或锁。场景修改状态与 m_bSceneDirty 分开：前者在 Save 成功后清除，后者只是待同步渲染更新，不能用绘制后的清除代替保存。

1. 校验节点身份与父关系，遍历当前对象收集几何、材质和纹理。
2. 创建唯一的新一代目录，写二进制数据和 Scene.json，并检查写入/关闭结果。
3. 将新的入口写到同目录临时文件，最后一次替换 RenderingLearn.project.json。Windows 使用 MoveFileExW 的 REPLACE_EXISTING/WRITE_THROUGH；其他平台用 rename。
4. 中途失败只移除本次尚未发布的目录和临时入口，旧入口继续指向完整旧数据。发布成功后才清除未保存状态。

这保证正常进程错误路径不会把新旧三个数据文件混用；不声称实现完整的断电事务或多写者协调。当前保留已发布的旧代文件，不自动删除历史；连续保存会增加磁盘占用，需要后续明确的清理策略。Save As 要求空目录，保存当前对象数据而不复制或依赖旧 Content，完成后切换到新工程位置。

## GPU 读回和状态

普通图片加载丢弃解码内存，因此工程纹理数据通过 [RenderTexture::ReadData](../../../src/Framework/RenderCore/Private/RenderResource.cpp) 读取当前 GPU 内容。每个 slice 的 mip=0 读回前记录其访问状态，读回后由 ProjectArchive::SetTextureAccess 在实际队列提交恢复屏障，更新该子资源状态和 LastAccessFence，并等待实际提交值。保存不读取其他 Mip。CPU 像素写盘后即可释放。没有 WaitIdle、ImageLayoutManager 或隐式全局保活。

每张纹理首次读回前先等待它此前记录的实际访问 Fence。这个完成点用于避免 OpenGL 同步资源任务越过尚未执行的逻辑命令队列；只等该资源已有的提交值，不调用全设备等待，也不改变普通绘制路径。

读取和上传的屏障必须带具体 mip/slice；只更新局部 Tracker 却转换整张纹理，会造成多层布局不一致。RHITexture::GetMipSize 的每个维度必须至少为 1；2D 纹理 Depth=1，在更高 mip 仍为 1。两项共性修复分别位于 RenderResource.cpp 和 [RHIResource.h](../../../src/Framework/RHI/PublicHeader/RHIResource.h)，没有后端专用分支。

上传的 Tracker 登记也使用具体 mip/layer/plane：原实现将某些维度替换成 All，容器按精确键查询具体子资源时会退回 Unknown。此次修正上传端的键粒度，不改 Tracker 或 RDG 的整体机制。OpenGL 原上传和读回固定 GL_TEXTURE_2D，立方体及数组恢复会出错；现在仍在同一 worker 中使用 DSA 按子资源处理，数组存储分配使用 ArraySize，并维护紧密行对齐。没有借用其他 context 或绕过 RHI 的原生上层调用。

SkyLightComponent 保存 EnvironmentLightResource 使用引用，EnvironmentLightResourceManager 拥有并复用输入 TextureAsset 和两张派生 IBL。加载先恢复输入 TextureAsset，再由管理器 Build 调用现有 Renderer::PreComputeIBL、ApplyEnvironment 绑定到天空光，输出配置保持当前 512×512 cube、diffuse 单层和 specular 六层。IBL 用途为 SRV/UAV，派生输出不加入工程纹理表；天空光的两个纹理 Getter 从所绑定结果读取，不再提供裸输出 Setter。environment=0 表示使用管理器生成的中性灰默认环境，默认输入及 IBL 均不写盘；非零引用指向普通工程纹理基础层。创建天空光不会自动加载示例图片，属性可选择用户 TextureAsset 或 Reset 默认；EditorScene 不额外保存天空资产。资源表和节点 environment 字段不变，无须再升级快照格式。

PreComputeIBL 提交计算后，已在输入和两张输出 Tracker 中记录同一个 Compute Fence。管理器 Build 核对该提交并在发布结果前等待实际 Fence：后续节点校验可能失败，不能让输出初始化未完成就被回收。ProjectArchive::Load 的函数异常处理在局部节点析构后 ReleaseUnused，仅清理没有使用引用的结果；活动场景仍持有自己的引用。场景正常移除/替换同样在原 GPU 完成点后显式清理，Engine 结束时清管理器拥有引用。只等待初始化提交，不增加 WaitIdle 或析构全设备等待；后续使用仍由调用方完成点管理。具体所有权见 [环境光管理](05-assets-materials-and-instance-data.md#环境光计算产物管理)，退出校验结果见 [11 月 4 日续验](../../dev/bugfix/故障与修复记录-2026-11-04.md)。

## 加载、失败与关闭

Load 先解析和恢复独立对象，尚不注册到正在绘制的场景。先构造纹理、材质、网格，再构造节点；检查重复 ID、缺失引用、有限数值、尺寸、拓扑和天空光数量。父关系先检查完整有向图，标记访问链以拒绝环，再按根到叶 AttachTo(false)，避免错误父关系进入变换递归。恢复局部变换，不重复应用世界变换。

EditorScene::ReplaceObjects 在一个尚未绘制的新 SceneInterface 上 Add/Flush 新组件。注册失败先清理已登记的借用指针，旧场景保留。准备成功后沿既有窗口/视口完成点清理旧场景，再移动新节点与资产所有者；没有新增全设备等待。旧窗口中的材质、预览等引用在原窗口提交完成后清理。

验证中还确认 Builder 原析构只 clear 原始 Pass 指针，按值捕获的 MeshBatch 因此保留 MaterialRenderProxy/TextureAsset，导致旧快照纹理一直存活。现在只补齐这些 new 创建 Pass 的 delete；CPU 录制任务已在 Execute 内完成，不用延后 Pass 生命周期或增加 GPU 等待。细节见 [RDG 所有权](03-rdg-execution-and-state.md)。

关闭/切换工程或原生窗口关闭时，交互入口为有修改的工程提供 Save/Discard/Cancel。保存失败不继续关闭。直接调用公开的 OpenProject/CloseProject 是明确的程序操作，供宿主和集成验证使用，不自行弹确认框；菜单和原生关闭负责交互确认。

## 代价和验证

几何扫描与写盘随顶点、索引和参数数量线性增长；当前保存纹理逐子资源同步读回，加载逐子资源上传，复杂工程会阻塞 UI。父关系验证及恢复的图处理为 O(N)，实际组件变换通知的代价还取决于层级深度。加载会重建距离场，未增加通用重新导入或热重载机制。

距离场 Atlas 仍固定为 128³、块大小 64³，仅 8 个槽位。原实现未在网格资产销毁时归还，反复恢复会耗尽槽位，并在 Allocate 返回 false 后上传零尺寸区域。StaticMeshAsset 析构现在通过 DistanceFieldManager::ReleaseMeshDistanceField 归还其网格各 LOD 的分配；构建中途失败也回滚本次分配并报错。释放要求调用方已按原约定完成场景 Remove/Flush 及相关 GPU 完成点，方法本身不新增等待。准备新场景时旧场景仍占槽，所以过大的新旧场景同时存在时会明确失败并保留旧场景；本次没有扩容 Atlas 或改变槽位身份规则。

验证入口为普通 Launcher 的 Editor 应用、Project Save/Save As/Ctrl+S，以及忽略目录中的独立诊断驱动。运行范围和已知校验问题见 [开发记录](../../dev/bugfix/故障与修复记录-2026-11-03.md)。编译、像素读回、异常保留和正常退出应分别记录，不能由文件存在推断保存内容正确。
