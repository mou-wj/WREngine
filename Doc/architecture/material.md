# 材质系统与编辑器数据接口

## 1. 范围与所有权

当前版本实现内置 `BuiltinSurface` 材质程序的运行时与编辑器数据基础，继续放在 Engine、Renderer 和 RenderCore 现有模块中。没有新增资产管理模块，也没有材质图编辑器或材质实例资产。

```text
MaterialAsset --独占拥有--> Material
StaticMeshComponent --资产引用--> MaterialAsset
StaticMeshComponent --按槽位独占、按需创建--> MaterialInstance
MaterialInstance --父资产引用--> MaterialAsset
Material / MaterialInstance --参数引用--> TextureAsset --拥有--> RenderTexture
Material --引用--> MaterialResource --复用--> 全局 ShaderMap
绘制批次 / Pass 闭包 --共享引用--> 不可变 MaterialRenderProxy
RenderCore 全局资源 --拥有--> 公共白纹理 / 平面法线纹理
```

Material 到所属资产仍为弱引用。渲染快照不反向强引用 MaterialAsset，不形成所有权环；它独立保存有效参数、纹理资产引用和 MaterialResource。组件释放或材质编辑不会修改已经发布的快照。

## 2. 文件职责

| 文件 | 职责 |
| --- | --- |
| `MaterialParameter.h/.cpp` | 参数类型、稳定键、内置声明、默认值、创建与校验 |
| `Material.h/.cpp` | 共享母材质、组件实例、参数继承、版本与渲染快照 |
| `MaterialCore.h/.cpp` | Shader 类型与缓存、编译资源、编译错误状态 |
| `MaterialAsset.h/.cpp` | JSON 加载、纹理依赖解析、显式保存副本 |
| `MaterialShaderParameter.h/.cpp` | 将快照转换为 RDG / Shader 参数，引用 RenderCore 公共默认纹理 |
| Renderer 的批次与 Pass | 选择 ShaderType / VertexFactory / PSO，明确 Pass 剔除策略 |
| RenderCore 的 RenderResource | 初始化、释放公共默认纹理资源 |

参数定义从原 Material.h 单独抽出，避免编辑器只枚举参数时必须依赖整套材质实现。MaterialResource 复用现有 MaterialCore 文件，不另建编译管理框架。

## 3. 参数契约与属性面板

`GetParameterDefinitions()` 返回内置 Surface 的参数声明，包含名称、语义、类型、分组、Shader 绑定名、默认值及实例可编辑性。当前共 17 项：BaseColor 向量、EmissiveColor、Opacity、OpacityMask、Metallic、Roughness、Specular、AmbientOcclusion、NormalIntensity、UVTiling、UVOffset、AlphaCutoff，以及 BaseColor / Normal / Opacity / ORM / Emissive 五种纹理。

键由名称、语义、显式类型构成，不再使用 `typeid().name()`。BaseColor 向量与 BaseColor 纹理可以共存，属性面板必须使用名称和类型共同定位参数。UV 的向量接口使用四个 float，Shader 绑定取前两个分量。

```cpp
// 枚举属性、读取有效值。
for (const auto& definition : material->GetParameterDefinitions())
{
    auto value = material->GetParameter(definition.Name, definition.Type)->GetValueVariant();
    // 按 definition.Type 绘制控件，按 definition.Group 分组。
}

// 编辑共享母材质：影响所有没有相应覆盖的实例。
material->SetParameter("Roughness", 0.6f);

// 编辑选中的网格：首次有效编辑创建组件独占实例。
component.SetMaterialParameter(0, "Roughness", 0.9f);
component.SetMaterialParameter(0, "Normal", normalTextureAsset);
component.ResetMaterialParameter(0, "Roughness", Engine::EMaterialParameterType::Scalar);

// 保留实例对象，删除全部覆盖；或使用 ResetMaterialInstance 丢弃整个实例。
component.GetMaterialInstance(0)->ResetAllParameters();
```

模板 SetParameterValue 接口继续保留，纹理值改为 `shared_ptr<TextureAsset>`。未知名称、错误类型、错误语义、非有限数值和没有资源的纹理资产会报错。内置程序尚不支持自定义参数或整数参数，不能设置成功后静默忽略。

实例只保存覆盖。删除覆盖后重新继承父值；父参数修改不会丢失其他覆盖。同值写入不增加版本。母材质 ResetParameter 恢复声明默认值，实例 ResetParameter 恢复父材质值，两者语义不同。

## 4. 版本、编译资源与缓存

`MaterialRevisions` 分为 Parameters、RenderState、Compilation。动态参数只增加 Parameters；双面等 PSO 状态只增加 RenderState；ShadingModel 改变同时影响 Compilation 与 RenderState。全局兼容 Revision 仍提供父实例失效判断。

MaterialResource 仅保存内置程序的编译输入（当前为 ShadingModel）、已请求的 Shader 强引用和最近一次 Shader 请求的状态 / 错误。Shader 查找与缓存只由全局 ShaderMap 负责，不在 Resource 中再维护一份按排列查询的缓存。颜色、粗糙度、纹理替换和纯光栅状态修改复用原 Resource；ShadingModel 改变时创建新 Resource，旧快照仍保有原编译结果。

MaterialRenderState 保存 BlendMode、CullMode 和 bTwoSided，由快照按值保存，与编译资源分离。CullMode 支持 None / Front / Back，默认 Back。解析优先级为：Pass 显式覆盖 > 双面时 None > 材质 CullMode。双面开关不会抹掉原剔除设置；关闭双面后恢复材质指定策略。FrontFace 仍使用网格批次的朝向。前向、延迟、预深度与阴影 Pass 都通过 ResolveCullMode 解析；预深度与阴影辅助接口可分别传入覆盖值。

Renderer 提供 Pass 所需 ShaderType、VertexFactory 和排列参数，MaterialResource 补充材质模型并从现有 GMeshMaterialShaderMap 获取共享 Shader。缓存键包含图形平台，普通参数值和资产地址不进入编译键。当前程序固定为 BuiltinSurface，不宣称已经实现任意 HLSL、材质图、异步编译、磁盘缓存或完整 UE ShaderMap ID。

状态 Unrequested / Ready / Failed 表示最近一次请求，Ready 不表示所有潜在 Pass 变体均已编译。编译失败保留具体错误并向调用方报错，没有悄悄替换为其他材质。原两个 CompileShader 成功分支已补充返回值。

## 5. 渲染快照与生命周期

Renderer 通过快照读取 BlendMode、ShadingModel、剔除配置和参数，不再用 GetParent 回读可变业务对象。批次及 Pass 闭包保留 shared_ptr 快照，编辑导致缓存替换时旧快照仍有效。

没有 MaterialFrame、材质专用的按帧列表或全局 retained 对象列表。RenderGraphBuilder 的 Pass 管理与提交逻辑保持原样，材质调整不改变 RDG 生命周期，也不增加关闭时的等待。

快照中的 shared_ptr 表达 CPU 所有权：只要批次、闭包等对象仍引用该快照，其参数资产和编译资源就有效。它不代表 GPU 已完成访问，也不取代原有资源释放与同步约定。UpdateLastAccessFence 和显式状态转换仍在原提交位置执行，没有新增 WaitIdle 或修改 RHI 后端。

原有 RDG 使用 new 创建 Pass、析构时只清空裸指针列表的问题仍待独立处理，不能将其描述为已修复，也不能依靠该泄漏作为资源生命周期保证。本轮不把材质重构扩大为 RDG 回收方案。

此版本的材质编辑及 GetRenderProxyShared 发布仍须与帧构建串行。渲染侧可继续持有旧快照，但不代表可从任意线程同时修改 Material、组件或全局 ShaderMap。

## 6. Shader 参数与支持范围

参数构建覆盖现有 MaterialUniformData 全部字段和五种纹理。默认 Metallic=0.8、Roughness=0.2、Specular=0.5、AO=1，保留现有场景外观；UVTiling=(1,1)、Opacity=1、NormalIntensity=1。缺省纹理由 RenderCore 的 GlobalWhiteTexture / GlobalFlatNormalTexture 提供，在 InitGlobalRenderResource 创建并上传，在 ReleaseGlobalRenderResource 释放；不包装成 TextureAsset，也不写入材质参数或资产依赖。用户实际指定的纹理仍由 TextureAsset 拥有。

TextureMask 与 MaterialParametersHelper.sf 一致：BaseColor=bit0、Opacity=bit1、Normal=bit2、ORM=bit3、Emissive=bit4。Forward / Deferred 不再强制 Lit，Unlit 使用材质的 BaseColor 与 Emissive 输入。

当前支持 Opaque 的 Lit / Unlit 与上述剔除策略。Masked / Translucent 可被加载和保存以表达资产状态，前向和延迟 Surface Pass 在构建时通过 ValidateSurfacePass 明确拒绝未实现的绘制模式。该校验不再放进编译资源中。Opacity、AlphaCutoff 等字段接通不表示透明 / 遮罩 Pass 已实现；延迟渲染也尚无独立 Emissive GBuffer 通道。后续编辑器应按实际 Pass 能力展示提示。

## 7. 保存与后续编辑器

首版 [Editor](app/editor.md) 已使用这些属性接口实现母材质/组件实例编辑、继承恢复、纹理赋值及母材质保存副本。实例仍是网格内部资源，不引入实例资产；没有节点图或撤销重做。

MaterialAsset::SaveToFile(path) 写入现有 version=1 分组格式，保存状态、参数及文件纹理依赖。根字段 cullMode 使用 None / Front / Back，缺省为 Back，旧资产行为保持不变。保存副本不修改当前资产路径或 AssetManager 索引；嵌入材质需要给出独立文件路径。显式指定的运行时纹理没有文件依赖路径，保存时明确报错；RenderCore 的默认纹理不参与序列化。

后续属性编辑器可以直接使用参数定义、运行时类型值、覆盖查询 / 恢复接口和编译错误。节点图、静态参数变体、撤销重做、资产热重载与导入器扩展应分别增加，避免与本轮运行时边界调整混合。

验证入口包括原 AssetOwnershipTest、新 MaterialContractTest，以及 Langcher 场景回归；实际执行结果记录在当日故障与修复文档。

## 相关核心实现细节

本篇保留架构职责说明；执行时序、数学原理及源码限制见以下专题。本轮为源码整理，专题不等于运行验证通过。

- [资产所有权、参数快照和更新](../key_implemetion/performance/05-assets-materials-and-instance-data.md)
- [Shader 编译与实际绑定](../key_implemetion/performance/06-shader-compilation-and-binding.md)
- [PBR、IBL 与参数的算法含义](../key_implemetion/algorithm/05-pbr-ibl-and-sampling.md)
