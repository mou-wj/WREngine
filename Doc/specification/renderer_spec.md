# Renderer 功能与使用约定

## Virtual Geometry 场景绘制入口

### 入口与职责

[VirtualGeomtryRender.h](../../src/Framework/Renderer/VirtualGeometry/VirtualGeomtryRender.h) 声明 `Renderer::AddVirtualGeometryRender`，实现在同目录 cpp。该文件同时承担实例整理和 Pass 编排，没有额外的 Process 类、全局批次缓存或资源管理器。

```cpp
auto visibility = Renderer::AddVirtualGeometryRender(
    builder, scene, view, sceneDepth, gbufferA, gbufferB, gbufferC, 1.0f);
```

`scene` 是 Renderer 的 Scene，提供对象查询和现有实例 ID 映射。`view` 提供视口、视锥及相机矩阵，Scene 本身不保存这些信息。附件参数均为调用方当前 RenderGraph 中的引用。最后的 targetPixelError 可省略，默认 1 像素；0 请求完整最细 Cut，负数和非有限值报错。独立 Raster 调用可在每个 VirtualGeometryInstance 上配置 TargetPixelError。

此入口仅编排 Visibility Raster 和 Material Resolve，写入 SceneDepth 与 GBuffer A/B/C。它不提交图、不创建交换链、不执行光照，也不在调用时构建或上传虚拟几何。返回值复用 `VirtualGeometryRasterOutput`，供调试显示、Metadata 检查或其他后续消费者使用。

### 调用前提与附件语义

- 先完成场景 Flush 和视图更新，并与场景、组件和材质修改串行执行。入口不会自行刷新场景、申请另一套实例 ID 或等待 GPU。
- 需要参与绘制的网格应已对 `StaticMeshRenderData::VirtualGeometryData` 显式 Build 并 InitializeResources。当前视图中未构建虚拟几何的网格跳过，不自动切换传统网格绘制；构建有效但未上传的资源由 Raster 报错。
- SceneDepth 必须为单样本、单层的 Texture2D，格式为 D32_Float，包含 DepthStencil 与 ShaderResource 用途。Raster 直接写入传入的 RDG 纹理，返回的 VirtualGeometryDepthTexture 与其相同，不创建替代深度或复制深度。
- GBuffer A/B/C 必须为相同尺寸的单样本、单层 Texture2D，格式与既有 CreateGBufferInfo 的默认布局一致，并具有 UAV 用途。后续读取、读回等用途由调用方补充。
- 第一版仅支持从原点覆盖完整附件的一个视图。现有 Raster Clear 会清空整个深度，Resolve 会清空对应 GBuffer，不能将此入口作为保留既有场景内容的叠加绘制，也不能连续用于同一目标的多个子视口。
- 发生构图异常后，调用方应放弃该图，不能执行已添加的部分 Pass。入口不捕获异常或声称失败后仍有完整输出。

### 实例整理与执行顺序

`AddVirtualGeometryRender` 内部使用 `BuildVirtualGeometryInstances`。该函数调用 `Scene::GatherVisiblePrimitives(view)`，沿用场景现有的可见性和视锥查询，再筛选 StaticMeshProxy。网格数据有效时，从既有 StaticMeshProxyToInstanceId 映射取得 InstanceID，并保存 RenderData、LocalToWorld 和按槽排列的 MaterialRenderProxy 快照。缺失实例映射会报错，提示先完成场景更新。

收集结束后依次调用 `AddVirtualGeometryRasterPass(builder, view, instances, sceneDepth)` 和 `AddVirtualGeometryMaterialResolvePass`。前者先为每个实例运行 CPU LOD 选择，按完整 Group 替换构造合法 DAG Cut，再清空 Visibility 和调用方深度并绘制选中 Cluster；后者读取同一深度及 Visibility，重建所选各层属性并写入传入的 GBuffer。顶点/索引访问通过既有 RDG_BUFFER_ACCESS 声明，其余附件、SRV 和 UAV 继续沿用各 Pass 的参数声明，由 RDG 生成依赖、屏障和提交访问记录。

返回值和原有 Pass 捕获保存录制期所需的资源及材质快照。GPU 完成前的释放和重建仍遵守资源最后访问 Fence，不增加 WaitIdle、全局保活或另一套生命周期控制。

后续若接入延迟光照，调用方应继续使用原有 DeferredShadingCS，并按既有流程准备灯光和阴影数据。此入口没有 SceneColor 参数，因而不调用独立的 AddVirtualGeometryDeferredShadingPass。RenderModule 当前保持传统渲染流程，尚未调用此入口。

属性恢复与材质数据布局见 [Material Resolve 实现](../key_implemetion/algorithm/12-virtual-geometry-material-resolve.md)，Cluster 绘制和 ID 定位见 [Visibility 实现](../key_implemetion/algorithm/11-virtual-geometry-visibility.md)。

CPU 选择的独立入口、屏幕误差公式、多对多依赖与裂缝条件见 [LOD Selection](../key_implemetion/algorithm/13-virtual-geometry-lod-selection.md)。选择只使用既有 Build 的累计采样误差与历史包围范围，不改资源格式，也不代表严格最大像素误差保证；本阶段没有滞回、GPU Culling 或 Streaming。

### 当前限制与验证入口

MaterialParametersHelper 现支持显式 UV 梯度，Compute 使用 SampleGrad，原像素 Shader 仍使用 Sample。此前回退后的错误属于历史状态，见 [故障记录](../dev/bugfix/故障与修复记录-2026-11-13.md)；2026-11-14 的三后端 Material Resolve 复测通过。独立场景入口仍未接入主渲染流程，完整传统路径与虚拟几何路径的全画面对照尚未完成。

[VirtualGeometryRenderTest](../../src/Tests/VirtualGeometryRenderTest.cpp) 通过 `Tests.exe -test=VirtualGeometryRenderTest -rhi=Vulkan` 运行，也可选择 DirectX12 或 OpenGL。它只验证空 Scene 经完整入口的组合行为，包括调用方深度复用、提交 Fence、无效 Visibility，以及非 8 倍数尺寸的深度与 GBuffer 清空读回，不覆盖非空场景实例收集和材质求值正确性。原 VirtualGeometryRasterTest 继续覆盖独立 Raster 的非空网格、多实例和 ID 映射。

本轮最终空场景测试在 Vulkan、DirectX12 退出 0，标准错误为空。OpenGL 的 D32 读回发生 GL_INVALID_OPERATION，深度断言失败，不能将其记为完整通过；现有 GetOpenGLTextureFormat 没有 D32 分支，读回落到 GL_RGBA。没有据此修改渲染逻辑、降低断言或扩展修复后端。独立 Raster 在三个后端均通过。具体复测及早期测试自身问题见 [2026-11-14 记录](../dev/bugfix/故障与修复记录-2026-11-14.md)。

## VirtualGeometryViewer 消费方式

[VirtualGeometryViewer](virtualgeometryviewer_spec.md) 直接提供独立实例，调用 CPU 选择、Raster、Resolve 和 AddVirtualGeometryDeferredShadingPass，不注册到主 Scene。默认 Dynamic LOD，按当前相机与 Target pixel error 选择完整 Cut；关闭此选项才按 MipLevel 检查某一生成层，界面明确标记其不是完整 Cut。光照封装复用 SceneRenderer 的阴影访问表和级联参数准备，再运行既有 DeferredShadingCS；无 IBL 时不绑定环境参数。主 RenderModule 与 DefferedSceneRenderer 的流程没有改变。
