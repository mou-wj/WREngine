# Virtual Geometry VertexFactory 与 Visibility Raster

实现位于 [VirtualGeometryVertexFactory](../../../src/Framework/Renderer/VirtualGeometry/VirtualGeometryVertexFactory.h) 和 [VirtualGeometryRasterPass](../../../src/Framework/Renderer/VirtualGeometry/VirtualGeometryRasterPass.h)。CPU 分簇与 DAG 的输入约定见 [虚拟几何构建](10-virtual-geometry-cpu-build.md)，Renderer 总体组织见 [Renderer 架构](../../architecture/render/renderer.md)。

## 输入与启用

调用方先对 StaticMeshRenderData::VirtualGeometryData 执行 Build，再执行 InitializeResources，然后自行提供实例并调用 AddVirtualGeometryRasterPass 和 AddVirtualGeometryVisibilityDebugPass 编排 RenderGraph。上传缺失时，Raster 接口给出明确错误。输入位置保持 Mesh Local Space，LocalToWorld 来自实例，ViewProjection 来自 SceneView。

实例输入由 RenderData 强引用、LocalToWorld、InstanceID 及可选材质快照组成。返回值包含 VirtualGeometryVisibilityTexture、VirtualGeometryDepthTexture、DrawMetadata SRV、CPU Draws、实例快照及去重后的 Resources 引用表。后续消费者须显式使用这次输出的颜色与深度。

2026-11-13 已回退 RenderModule 的 Virtual Geometry 分支及对应 SceneSetting 开关。主场景不再自动收集实例或调用这些 Pass，传统渲染流程保持原样。独立 Raster 和 Debug 的实现、资源初始化接口及验证入口继续保留。

场景调用方也可通过独立的 AddVirtualGeometryRender 整理可见实例并组合 Raster 与 Resolve，参数及限制见 [Renderer 使用约定](../../specification/renderer_spec.md#virtual-geometry-场景绘制入口)。该入口仍未接入 RenderModule。

## 顶点输入与绘制范围

VirtualGeometryVertexFactory 继承 RenderCore::VertexFactory，注册 VertexFactoryType，绑定整个 PositionBuffer。ATTRIBUTE0 使用 R32G32B32_Float，步长为 sizeof(Core::Float3)。位置转换由 [VertexFactory Shader](../../../shaders/vertexfactory/VirtualGeometryVertexFactory.sf) 完成，计算 `clip = ViewProjection × LocalToWorld × float4(position, 1)`。DrawID 通过 nointerpolation 的整数 varying 传递。

Raster 默认调用独立的 [CPU LOD Selection](13-virtual-geometry-lod-selection.md)，按实例 TargetPixelError、变换和当前视图取得合法 DAG Cut，默认预算为 1 像素，0 表示完整叶集合。Metadata 和 DrawIndexed 使用同一份选中列表，不再固定选择最细层。Viewer 显式启用 bInspectGeneratedLevel 时仍可按 MipLevel 检查某一轮产物；这个诊断分支不是完整 Cut，不能用作场景 LOD。每个 Cluster 调用一次 DrawIndexed，IndexCount 为 TriangleCount×3，FirstIndex 为 IndexOffset，BaseVertex 为 VertexOffset，InstanceCount 为 1。索引中保存的是 Cluster Local Index，因此第 k 个三角形的第 j 个角对应如下位置。

```cpp
uint32_t local = geometry.Indices[cluster.IndexOffset + k * 3 + j];
Core::Float3 position = geometry.Positions[cluster.VertexOffset + local];
```

整个顶点缓冲绑定与 BaseVertex 相结合，使同一顶点工厂适配所有 Cluster。绘制范围的选择由 Raster Pass 完成。VS/PS 复用 MeshMaterialShaderMap 的 VertexFactory 编译路径，选择 Unlit 编译键。VertexFactory 的 ShouldCompile 只接受本功能的 Raster Shader，VS 和 PS 共用 VirtualGeometryVaryings，保持语义和插值限定一致。

## Visibility、Metadata 与三角形定位

Visibility 使用 R32G32_UInt，每像素占 8 字节。X 保存帧内 DrawID，Y 保存该次 Draw 的 SV_PrimitiveID。颜色附件使用整数输出，混合关闭。深度使用 D32_Float，清为 1，Raster 开启 Less 比较和深度写入。第一版采用双面绘制。

Draw Metadata 使用 Shader 参数元数据宏声明四个 uint32，依次为 InstanceID、ClusterID、ResourceIndex、InstanceIndex。InstanceIndex 关联本次实例的变换和材质快照。CPU 数组步长为 sizeof(VirtualGeometryDrawMetadata)，GPU StructuredBuffer 使用同一反射结构。ResourceIndex 指向返回值中的 Resources，多个实例使用同一资源时共用该索引，每个实例仍拥有独立的 DrawID 区间。Scene 的 InstanceID 属于该场景当前实例表，调用方提供的独立 Raster 输入也可以使用自己的实例 ID。

定位过程依次读取 Visibility.X、Draws[DrawID]、Resources[ResourceIndex] 和 VirtualGeometryData.Clusters[ClusterID]。Visibility.Y 是该 Cluster 内的三角形编号，再用上面的局部索引公式取得三个角。实例变换由 InstanceID 对应的实例表取得。DrawID 和 Metadata 随每次编排重新生成，后续消费者须使用本次输出的同一份表。

无效像素为 uint2(0xFFFFFFFF, 0xFFFFFFFF)。专用 Clear Shader 通过一个全屏三角形写入这两个整数，并写深度 1。Clear 的颜色附件使用 DontCare_Store，避免依赖浮点 ClearValue 到整数颜色的转换。此处深度比较使用 Always。

## Debug 显示

[Debug Shader](../../../shaders/virtualgeometry/Debug.sf) 使用 Texture.Load 读取屏幕位置的整数 Visibility，对合法 DrawID 查 StructuredBuffer 中的 ClusterID。颜色散列为 `h = (ClusterID + 1) × 1664525 + 1013904223`，取低三个字节并映射到 0.2 至 1.0 的 RGB 区间。背景为 0.025 灰色，Alpha 为 1。最终使用全屏图形 Pass 写入目标 RenderTarget。

Raster 的 CPU 选择复杂度为每实例 O(Cluster+Group+引用数)，Draw 次数为选中 Cluster 数。Metadata 内存为 Draw 数乘元素步长，Visibility 和深度分别需要每像素 8 和 4 字节，另有 Debug 颜色目标。CPU 参考仍使用普通 Vertex Input 和 DrawIndexed。GPU 模式使用全部候选的稳定 Metadata、GPU DAG 选择/视锥剔除和固定槽间接参数，详见 [LOD 选择](13-virtual-geometry-lod-selection.md)；候选数不等于实际选择数。

Raster 的可选 VirtualGeometryRasterSettings 默认 Hardware；Hybrid/SoftwarePreferred 在 GPU 选择后按投影尺寸分流，Compute 两次覆盖分别归约深度和获胜 ID，再与硬件中间附件合并为相同的 uint2/D32 输出。原 Debug、Material Resolve 和 GBuffer 不改变 ID 协议。覆盖规则、边界回退、内存及精度限制见 [软件光栅专题](14-virtual-geometry-software-rasterization.md)。可选的 [Page Streaming](../performance/11-virtual-geometry-streaming.md) 继续使用同一 GPU ClusterBuffer 的物理地址，不改变这些输出协议。

## RDG、所有权和完成点

Clear 和 Raster 通过 RenderTargetBindingSlots 声明颜色、深度附件，Debug 声明 Visibility SRV、Metadata SRV 和输出附件。Metadata 由声明 RDG_BUFFER_ACCESS(TransferDest) 的上传 Pass 调用原 UpdateBuffer，CPU 向量快照归本图 arena 所有，其堆载荷不受原始字节 arena 单页 64 KiB 限制；空场景上传一条占位记录并令 DrawCount 为零。位置与索引注册为外部 Buffer，通过参数中的 RDG_BUFFER_ACCESS 声明 VertexOrIndexBuffer 访问。RDG 按名称缓存对象，资源名称采用 graph arena 中 Clear 参数的稳定地址作为本次调用前缀，并按几何资源地址区分位置/索引缓冲。跨视图及不同资源具有独立名称，同一实际 RHI Buffer 的再次注册由原外部表复用。框架现有分析及提交流程负责状态转换和最后访问 Fence 更新。

Raster 保留按 width/height 创建深度的原接口，并增加接收 sceneDepth 的重载。后者检查单样本、单层 Texture2D、D32_Float 格式和深度/Shader 可读用途后，直接将该对象写入原有附件参数。Clear 仍覆盖完整目标，未增加深度复制或保留已有深度的混合路径。

RDG_TEXTURE_ACCESS 和 RDG_BUFFER_ACCESS 的访问元数据由 Builder 解析到原有意向集合，Shader 源码生成和绑定会跳过这些成员。读回测试通过前者声明 TransferSrc。RDG 的只读状态发生改变时也必须生成屏障，例如 SRV 到 TransferSrc，以及 SRV 到 VertexOrIndexBuffer。声明相同读取权限才可以省略这类转换。

返回值和 Raster lambda 捕获的 shared_ptr 维持 CPU 资源及工厂的录制期所有权。Metadata 的 CPU 上传快照存放在既有 graph arena，存活到图销毁。图执行返回后，调用方仍须遵守已提交 GPU 访问的 Fence，随后才能释放或重建 VirtualGeometryResource。这里沿用既有显式同步和资源回收机制。

## 后端适配与验证

R32G32_UInt 追加到 RHI 格式枚举末尾，并补齐 FormatInfo、字节数及 Vulkan、DirectX12、OpenGL Utils 的格式映射。Vulkan 在设备创建时按已查询的支持情况启用 geometryShader，满足 Fragment PrimitiveID 的 SPIR-V Geometry capability。DirectX12 使用共用 varying 结构维持阶段签名一致。

OpenGL 深度比较通过 Utils 映射传入值，索引绘制使用支持 BaseVertex/BaseInstance 的实例绘制接口。Structured Buffer SRV 显式绑定到 SSBO。Texture.Load 的 GLSL dummy sampler 保留原纹理绑定槽位，保证整数 texelFetch 访问正确纹理。

[VirtualGeometryRasterTest](../../../src/Tests/VirtualGeometryRasterTest.cpp) 通过 `Tests.exe -test=VirtualGeometryRasterTest -rhi=Vulkan` 执行，也支持 DirectX12 和 OpenGL。用例构建 288 面网格、三个最细 Cluster 及更粗层，创建共享资源的两个实例，验证六条 Draw Metadata、非零 Cluster 顶点/索引偏移、PrimitiveID 范围和正值、实例遮挡、背景无效整数及 Debug 像素颜色。交换近远实例提交顺序，并单独验证空场景和两个独立几何资源，后者检查 ResourceIndex 及各自顶点/索引缓冲状态和 Fence。Vulkan/DirectX12 在图内登记读回，OpenGL 的 MapReadTexture 是同步读取接口，测试在已提交工作完成后读取。最终断言相同。

实际编译、校验与 Launcher 范围见 [实现与修复记录](../../dev/bugfix/故障与修复记录-2026-11-12.md) 及 [最终验证记录](../../dev/bugfix/故障与修复记录-2026-11-13.md)。
