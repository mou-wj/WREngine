# Virtual Geometry Material Resolve

实现入口为 [VirtualGeometryMaterialResolvePass](../../../src/Framework/Renderer/VirtualGeometry/VirtualGeometryMaterialResolvePass.cpp)。前序输入见 [Visibility Raster](11-virtual-geometry-visibility.md)。

2026-11-13 已按要求撤销 RenderModule 的场景集成和 MaterialParametersHelper 的采样宏改动。独立 Pass 和测试代码继续保留，但不再由主场景调用。Compute 的显式梯度与材质采样目前未接通，下文说明保留实现的数据流及该限制；回退前的测试结果只代表当时版本。

## 数据与材质对应

VirtualGeometryInstance 保存 RenderData、LocalToWorld、场景 InstanceID 和按材质槽排列的不可变 MaterialRenderProxy。RasterOutput 保存本次 Instances 快照。Draw Metadata 增加 InstanceIndex，与 InstanceID、ClusterID、ResourceIndex 一起组成四个 uint32。InstanceIndex 定位本次实例快照，ResourceIndex 定位资源表，Cluster.MaterialIndex 定位实例的材质槽。未指定槽位的 Cluster 使用槽零，缺失快照或非 Opaque 材质明确失败。

Compute 直接读取已经上传的位置、法线、切线、UV、索引及 Cluster Buffer。为避免 float3 数组在不同后端的结构步长差异，使用 stride=4 的 StructuredBuffer<float/uint> 视图。Cluster 按 sizeof(VirtualGeometryCluster)/4 跨越记录，只读取头部字段。C++ static_assert 验证 VertexOffset、IndexOffset、TriangleCount、MaterialIndex 的实际字节偏移，以及 Core 向量大小。这里读取现有 C++ 上传布局，修改该布局时必须同步这些断言和 Shader。

Triangle 的三个全局顶点下标为 VertexOffset 加 Indices[IndexOffset + PrimitiveID×3 + corner]。每个 Draw 提交一次覆盖视口的 Compute Dispatch，先以 Visibility.DrawID 筛选像素，再读取本 Draw 对应的缓冲和材质。绑定通过现有 MaterialShaderMap 和 BuildShaderParameters 完成。第一版计算量随 Draw 数乘视口像素数增长。

## 属性恢复和梯度

每个顶点先由 LocalToWorld 变换到世界空间，再由 ViewProjection 变换到裁剪空间。透视除法得到屏幕坐标，像素中心为整数坐标加 0.5。Vulkan/OpenGL 的当前原生 viewport 使用正 Y，DirectX12 使用负 Y，Shader 参数 ScreenYSign 明确表达此差异。

以三角形屏幕边向量求面积和仿射重心坐标 λ。透视插值权重为 wi=(λi/clipWi)/Σ(λj/clipWj)。WorldPosition、UV 和切线属性使用该权重。法线使用 LocalToWorld 的逆转置，切线使用其线性变换。插值后重新归一化并正交化切线，结合切线 W 和变换行列式符号重建副切线。ViewDirection 从相机位置减 WorldPosition 得到。

UV 插值是 U=N/D，其中 N=Σ(λi×UVi/clipWi)，D=Σ(λi/clipWi)。根据屏幕边计算 λ 的 X/Y 导数，再用商法则求 dU/dx、dU/dy。梯度同时乘 UVTiling，UVOffset 不贡献梯度。

MaterialParametersHelper 已恢复直接调用 Sample 的原实现，不再使用 MATERIAL_TEXTURE_SAMPLE。Resolve Shader 中原有 SampleGrad 宏因此不再控制材质采样，计算出的 UV 梯度也未传入采样。Compute 不能据此获得与像素 Shader 相同的隐式导数语义，当前不能声称其纹理 LOD 正确或维持回退前的跨后端验证结论。本次未新增替代采样机制。

非有限裁剪坐标、接近零的 W、退化屏幕面积及插值分母会跳过求值，保留清空后的 GBuffer。奇异实例变换在 CPU 构图时失败。当前没有对顶点 W 接近零的跨相机三角形重新执行裁剪，极端裁剪情况仍需后续完善。

## GBuffer 与延迟光照

先清空视口内的 A/B/C，然后各 Draw 的有效像素写入三张 UAV。编码继续调用 GBufferCommon 中的 Write 函数，A 保存 BaseColor/Metallic，B 保存编码后的世界法线/Roughness，C 保存 Specular/AO/归一化 ShadingModelID。Lit 的切线空间法线转换到世界空间。Unlit 沿现有规则将 BaseColor 与 EmissiveColor 的和写入 A。Lit 沿现有 GBuffer 能力处理。

独立 AddVirtualGeometryDeferredShadingPass 封装调用已有 AddStaticMeshDefferedShadingPass 执行直接光照，有环境贴图时再执行其 IBL 变体。调用方需要自行创建具有 UAV 用途的 GBuffer 和 SceneColor，Depth 使用 Raster 结果。当前这一光照封装要求视口覆盖完整输出目标，独立 Resolve 本身支持视口原点。主场景已恢复原有 GBuffer 用途与 SceneRenderer::Build 调用，不自动调用上述接口。此前对原光照 Dispatch 向上取整及 Shader 范围检查的修正仍保留。

AddVirtualGeometryRender 将场景实例整理、使用调用方 SceneDepth 的 Raster 和本 Resolve 组合为单个构图入口，返回原 Visibility 输出。它不调用光照封装，也未恢复主场景集成；使用前提和当前采样限制见 [Renderer 使用约定](../../specification/renderer_spec.md#virtual-geometry-场景绘制入口)。

Visibility、Depth、Metadata、属性和 Cluster 视图通过 Shader 参数声明 SRV，GBuffer 声明 UAV。既有 RDG 负责 Raster 到 Resolve、UAV 到光照 SRV 的依赖和屏障，以及实际提交处的 Fence 更新。实例和材质快照保持录制期引用，资源释放或重建仍需调用方完成已提交 GPU 使用。没有内部锁，构图与业务数据修改须串行。

## 验证入口与范围

[VirtualGeometryMaterialResolveTest](../../../src/Tests/VirtualGeometryMaterialResolveTest.cpp) 通过 Tests.exe -test=VirtualGeometryMaterialResolveTest -rhi=Vulkan 运行，也支持 DirectX12、OpenGL。测试覆盖共享及独立资源、多实例不同材质、交换绘制顺序、空场景、透视 UV、线性循环采样的边缘、法线和 PBR 通道读回。透视场景检查属性插值，遮挡顺序由正交场景检查。背景保留清空值。实际结果和后端修复见 [开发记录](../../dev/bugfix/故障与修复记录-2026-11-13.md)。

恢复原采样 Helper 后的同日复测未通过。DirectX12 的 Shader 编译因 Compute 隐式导数与当前 Shader Model 不兼容而失败，Vulkan/OpenGL 出现材质读回不匹配及失败退出或超时。独立 Raster 三后端复测通过，不能据此推断 Resolve 也通过。详细错误与退出情况见开发记录的回退章节。

当前测试没有完成任意复杂材质、不同材质槽、非均匀缩放法线及传统 StaticMesh 全画面的逐像素对照。传统 GBuffer PS 的法线贴图目前直接写入切线空间结果，本流程按世界法线约定转换，两者在旋转后的法线贴图场景有既有语义差异。此处明确记录该差异，传统材质 Shader 保持原有实现。
