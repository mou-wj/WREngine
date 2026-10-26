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
