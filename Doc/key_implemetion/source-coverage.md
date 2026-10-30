# 源码检索与文档覆盖清单

更新日期：2026-10-31；基于 `9e6d934` 及本次编辑器位移、缩放和旋转工具工作区实现。首次整理基线为 `04f910d`。本清单来自全量文件枚举、全文件文本关键词检索及专题调用链核对；不是逐行正确性审计，也不是运行验证。每行列出的专题是阅读入口，不保证已解释该文件的每个辅助函数。

范围：自有 src、全部 Shader、CMake/BuildDeps、依赖元信息和 VS Code 配置。extern/glad 是外部生成 loader；deps、构建目录、生成文件、二进制资源、validation 产物、temp 实验不逐项展开。外部库的使用边界见工程专题，驱动和第三方算法不冒充自有实现。

检索标记：并发=线程/锁/任务/完成点；内存=分配/回收/临时工作区；算法=几何/数学/空间查询；渲染=Shader/管线/绘制。标记只是命中，不是已确认问题；未命中不代表没有核心语义。

本次清单共 **432 个文件、78792 行**；文件数包含接口、配置和构建声明。原始逐文件内容 hash 与关键词计数仅保存在忽略目录 `out/diagnostics/key-implementation-source-scan.json`，正式阅读以本清单和源码链接为准。

## 现有 architecture 的补充关系

| 既有说明 | 本轮补充细节 |
| --- | --- |
| overall / Core | 模块 DFS、任务异常、线程等待与构建依赖 |
| Engine / Material | 在途加载合并、驻留 generation、不可变参数快照、实例索引、导入与距离场 |
| RenderCore / Renderer | RDG 分组与屏障、真实释放边界、缓存/反射、GI/阴影/屏幕算法 |
| RHI 与三个后端 | CPU/GPU 完成语义、瞬态 first-fit、backend 回收差异、AS scratch/SBT |
| Slate / Application | UI 顶点索引、坐标、采样纹理 Fence 和窗口完成点 |

核心框架继续保存在 architecture；下面的实现入口都指向真实已有代码，没有借文档整理创建新框架。

## .vscode（2 个文件）

| 源码/配置 | 行数 | 检索标记 | 阅读入口 |
| --- | ---: | --- | --- |
| [.vscode/launch.json](../../.vscode/launch.json) | 7 | 接口/配置/基础辅助 | [09-module-build-and-startup](performance/09-module-build-and-startup.md) |
| [.vscode/settings.json](../../.vscode/settings.json) | 14 | 接口/配置/基础辅助 | [09-module-build-and-startup](performance/09-module-build-and-startup.md) |

## 根目录构建（3 个文件）

| 源码/配置 | 行数 | 检索标记 | 阅读入口 |
| --- | ---: | --- | --- |
| [BuildDeps.py](../../BuildDeps.py) | 530 | 接口/配置/基础辅助 | [09-module-build-and-startup](performance/09-module-build-and-startup.md) |
| [CMakeLists.txt](../../CMakeLists.txt) | 28 | 接口/配置/基础辅助 | [09-module-build-and-startup](performance/09-module-build-and-startup.md) |
| [CMakeSettings.json](../../CMakeSettings.json) | 15 | 接口/配置/基础辅助 | [09-module-build-and-startup](performance/09-module-build-and-startup.md) |

## cmake（3 个文件）

| 源码/配置 | 行数 | 检索标记 | 阅读入口 |
| --- | ---: | --- | --- |
| [cmake/Find3rdsGenerated.cmake](../../cmake/Find3rdsGenerated.cmake) | 129 | 接口/配置/基础辅助 | [09-module-build-and-startup](performance/09-module-build-and-startup.md) |
| [cmake/Functions.cmake](../../cmake/Functions.cmake) | 56 | 接口/配置/基础辅助 | [09-module-build-and-startup](performance/09-module-build-and-startup.md) |
| [cmake/private/_3rds_copy_dll_set.cmake](../../cmake/private/_3rds_copy_dll_set.cmake) | 11 | 接口/配置/基础辅助 | [09-module-build-and-startup](performance/09-module-build-and-startup.md) |

## config（11 个文件）

| 源码/配置 | 行数 | 检索标记 | 阅读入口 |
| --- | ---: | --- | --- |
| [config/3rds/directx-shader-compiler.json](../../config/3rds/directx-shader-compiler.json) | 38 | 接口/配置/基础辅助 | [09-module-build-and-startup](performance/09-module-build-and-startup.md) |
| [config/3rds/glad.json](../../config/3rds/glad.json) | 23 | 接口/配置/基础辅助 | [09-module-build-and-startup](performance/09-module-build-and-startup.md) |
| [config/3rds/glfw.json](../../config/3rds/glfw.json) | 43 | 接口/配置/基础辅助 | [09-module-build-and-startup](performance/09-module-build-and-startup.md) |
| [config/3rds/glslang.json](../../config/3rds/glslang.json) | 44 | 接口/配置/基础辅助 | [09-module-build-and-startup](performance/09-module-build-and-startup.md) |
| [config/3rds/imgui.json](../../config/3rds/imgui.json) | 35 | 接口/配置/基础辅助 | [09-module-build-and-startup](performance/09-module-build-and-startup.md) |
| [config/3rds/nlohmann_json.json](../../config/3rds/nlohmann_json.json) | 42 | 接口/配置/基础辅助 | [09-module-build-and-startup](performance/09-module-build-and-startup.md) |
| [config/3rds/renderdoc.json](../../config/3rds/renderdoc.json) | 40 | 接口/配置/基础辅助 | [09-module-build-and-startup](performance/09-module-build-and-startup.md) |
| [config/3rds/spirv-cross.json](../../config/3rds/spirv-cross.json) | 43 | 接口/配置/基础辅助 | [09-module-build-and-startup](performance/09-module-build-and-startup.md) |
| [config/3rds/spirv-tools.json](../../config/3rds/spirv-tools.json) | 47 | 接口/配置/基础辅助 | [09-module-build-and-startup](performance/09-module-build-and-startup.md) |
| [config/3rds/tinygltf.json](../../config/3rds/tinygltf.json) | 41 | 接口/配置/基础辅助 | [09-module-build-and-startup](performance/09-module-build-and-startup.md) |
| [config/3rds/vulkan.json](../../config/3rds/vulkan.json) | 39 | 接口/配置/基础辅助 | [09-module-build-and-startup](performance/09-module-build-and-startup.md) |

## shaders（39 个文件）

| 源码/配置 | 行数 | 检索标记 | 阅读入口 |
| --- | ---: | --- | --- |
| [shaders/core/EnvIBLParametersHelper.sf](../../shaders/core/EnvIBLParametersHelper.sf) | 135 | 算法、渲染 | [05-pbr-ibl-and-sampling](algorithm/05-pbr-ibl-and-sampling.md)；[06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md) |
| [shaders/core/GBufferCommon.sf](../../shaders/core/GBufferCommon.sf) | 60 | 接口/配置/基础辅助 | [05-pbr-ibl-and-sampling](algorithm/05-pbr-ibl-and-sampling.md)；[06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md) |
| [shaders/core/GBufferParametersHelper.sf](../../shaders/core/GBufferParametersHelper.sf) | 63 | 渲染 | [05-pbr-ibl-and-sampling](algorithm/05-pbr-ibl-and-sampling.md)；[06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md) |
| [shaders/core/GlobalDefine.sf](../../shaders/core/GlobalDefine.sf) | 10 | 接口/配置/基础辅助 | [06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md)；[03-transforms-frustum-and-depth](algorithm/03-transforms-frustum-and-depth.md) |
| [shaders/core/GlobalDistanceFieldHelper.sf](../../shaders/core/GlobalDistanceFieldHelper.sf) | 170 | 内存、渲染 | [04-distance-fields-and-jump-flood](algorithm/04-distance-fields-and-jump-flood.md) |
| [shaders/core/MaterialParametersHelper.sf](../../shaders/core/MaterialParametersHelper.sf) | 185 | 渲染 | [05-pbr-ibl-and-sampling](algorithm/05-pbr-ibl-and-sampling.md)；[06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md) |
| [shaders/core/Sample.sf](../../shaders/core/Sample.sf) | 167 | 算法 | [05-pbr-ibl-and-sampling](algorithm/05-pbr-ibl-and-sampling.md)；[06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md) |
| [shaders/core/SceneParametersHelper.sf](../../shaders/core/SceneParametersHelper.sf) | 196 | 渲染 | [05-pbr-ibl-and-sampling](algorithm/05-pbr-ibl-and-sampling.md)；[06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md) |
| [shaders/core/ShadingCommon.sf](../../shaders/core/ShadingCommon.sf) | 405 | 算法 | [05-pbr-ibl-and-sampling](algorithm/05-pbr-ibl-and-sampling.md)；[06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md) |
| [shaders/core/ShadingContext.sf](../../shaders/core/ShadingContext.sf) | 32 | 接口/配置/基础辅助 | [05-pbr-ibl-and-sampling](algorithm/05-pbr-ibl-and-sampling.md)；[06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md) |
| [shaders/core/ShadowParametersHelper.sf](../../shaders/core/ShadowParametersHelper.sf) | 276 | 渲染 | [06-shadow-mapping-and-cascades](algorithm/06-shadow-mapping-and-cascades.md) |
| [shaders/editor/TranslationGizmo.sf](../../shaders/editor/TranslationGizmo.sf) | 22 | 渲染 | [03-transforms-frustum-and-depth](algorithm/03-transforms-frustum-and-depth.md)；[08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md)；[06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md) |
| [shaders/editor/ViewAxes.sf](../../shaders/editor/ViewAxes.sf) | 44 | 算法、渲染 | [03-transforms-frustum-and-depth](algorithm/03-transforms-frustum-and-depth.md)；[08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md)；[06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md) |
| [shaders/GI/GlobalDiffuseLightMapDiffuseCS.sf](../../shaders/GI/GlobalDiffuseLightMapDiffuseCS.sf) | 85 | 内存、算法、渲染 | [04-distance-fields-and-jump-flood](algorithm/04-distance-fields-and-jump-flood.md) |
| [shaders/GI/GlobalDiffuseLightMapInitCS.sf](../../shaders/GI/GlobalDiffuseLightMapInitCS.sf) | 73 | 算法、渲染 | [04-distance-fields-and-jump-flood](algorithm/04-distance-fields-and-jump-flood.md) |
| [shaders/material/StaticMeshMaterialDefferedShadingCS.sf](../../shaders/material/StaticMeshMaterialDefferedShadingCS.sf) | 69 | 算法、渲染 | [05-pbr-ibl-and-sampling](algorithm/05-pbr-ibl-and-sampling.md)；[06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md) |
| [shaders/material/StaticMeshMaterialGBufferShaderPS.sf](../../shaders/material/StaticMeshMaterialGBufferShaderPS.sf) | 50 | 接口/配置/基础辅助 | [05-pbr-ibl-and-sampling](algorithm/05-pbr-ibl-and-sampling.md)；[06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md) |
| [shaders/material/StaticMeshMaterialShaderPS.sf](../../shaders/material/StaticMeshMaterialShaderPS.sf) | 52 | 渲染 | [05-pbr-ibl-and-sampling](algorithm/05-pbr-ibl-and-sampling.md)；[06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md) |
| [shaders/material/StaticMeshMaterialShaderVS.sf](../../shaders/material/StaticMeshMaterialShaderVS.sf) | 9 | 渲染 | [05-pbr-ibl-and-sampling](algorithm/05-pbr-ibl-and-sampling.md)；[06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md) |
| [shaders/tools/BlitTextureCS.sf](../../shaders/tools/BlitTextureCS.sf) | 42 | 算法、渲染 | [06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md)；[03-transforms-frustum-and-depth](algorithm/03-transforms-frustum-and-depth.md) |
| [shaders/tools/CopyTextureCS.sf](../../shaders/tools/CopyTextureCS.sf) | 32 | 渲染 | [06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md)；[03-transforms-frustum-and-depth](algorithm/03-transforms-frustum-and-depth.md) |
| [shaders/tools/DepthPyramidGenerateCS.sf](../../shaders/tools/DepthPyramidGenerateCS.sf) | 47 | 渲染 | [07-screen-space-and-antialiasing](algorithm/07-screen-space-and-antialiasing.md) |
| [shaders/tools/DepthShadowPassPS.sf](../../shaders/tools/DepthShadowPassPS.sf) | 24 | 渲染 | [06-shadow-mapping-and-cascades](algorithm/06-shadow-mapping-and-cascades.md) |
| [shaders/tools/DistanceFieldJumpFlood3DCS.sf](../../shaders/tools/DistanceFieldJumpFlood3DCS.sf) | 96 | 算法、渲染 | [04-distance-fields-and-jump-flood](algorithm/04-distance-fields-and-jump-flood.md) |
| [shaders/tools/DistanceFieldMergeCS.sf](../../shaders/tools/DistanceFieldMergeCS.sf) | 56 | 渲染 | [04-distance-fields-and-jump-flood](algorithm/04-distance-fields-and-jump-flood.md) |
| [shaders/tools/DistanceFieldVoxelizeCS.sf](../../shaders/tools/DistanceFieldVoxelizeCS.sf) | 122 | 算法、渲染 | [04-distance-fields-and-jump-flood](algorithm/04-distance-fields-and-jump-flood.md) |
| [shaders/tools/DrawSkyPS.sf](../../shaders/tools/DrawSkyPS.sf) | 25 | 渲染 | [05-pbr-ibl-and-sampling](algorithm/05-pbr-ibl-and-sampling.md)；[06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md) |
| [shaders/tools/DrawSkyVS.sf](../../shaders/tools/DrawSkyVS.sf) | 57 | 渲染 | [05-pbr-ibl-and-sampling](algorithm/05-pbr-ibl-and-sampling.md)；[06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md) |
| [shaders/tools/DrawTexturePS.sf](../../shaders/tools/DrawTexturePS.sf) | 27 | 渲染 | [06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md)；[03-transforms-frustum-and-depth](algorithm/03-transforms-frustum-and-depth.md) |
| [shaders/tools/FrustumCullCS.sf](../../shaders/tools/FrustumCullCS.sf) | 54 | 算法、渲染 | [03-transforms-frustum-and-depth](algorithm/03-transforms-frustum-and-depth.md) |
| [shaders/tools/FullScreenVS.sf](../../shaders/tools/FullScreenVS.sf) | 30 | 接口/配置/基础辅助 | [06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md)；[03-transforms-frustum-and-depth](algorithm/03-transforms-frustum-and-depth.md) |
| [shaders/tools/FxaaCS.sf](../../shaders/tools/FxaaCS.sf) | 62 | 算法、渲染 | [07-screen-space-and-antialiasing](algorithm/07-screen-space-and-antialiasing.md) |
| [shaders/tools/IBLPrecomputeCS.sf](../../shaders/tools/IBLPrecomputeCS.sf) | 319 | 算法、渲染 | [05-pbr-ibl-and-sampling](algorithm/05-pbr-ibl-and-sampling.md)；[06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md) |
| [shaders/tools/PositionOnlyVS.sf](../../shaders/tools/PositionOnlyVS.sf) | 36 | 渲染 | [06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md)；[03-transforms-frustum-and-depth](algorithm/03-transforms-frustum-and-depth.md) |
| [shaders/tools/ScreenSpaceGICS.sf](../../shaders/tools/ScreenSpaceGICS.sf) | 332 | 算法、渲染 | [07-screen-space-and-antialiasing](algorithm/07-screen-space-and-antialiasing.md) |
| [shaders/tools/ScreenSpaceReflectionCS.sf](../../shaders/tools/ScreenSpaceReflectionCS.sf) | 182 | 算法、渲染 | [07-screen-space-and-antialiasing](algorithm/07-screen-space-and-antialiasing.md) |
| [shaders/tools/taaTAA.sf](../../shaders/tools/taaTAA.sf) | 28 | 渲染 | [07-screen-space-and-antialiasing](algorithm/07-screen-space-and-antialiasing.md) |
| [shaders/tools/TestRedPS.sf](../../shaders/tools/TestRedPS.sf) | 14 | 渲染 | [06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md)；[03-transforms-frustum-and-depth](algorithm/03-transforms-frustum-and-depth.md) |
| [shaders/vertexfactory/LocalVertexFactory.sf](../../shaders/vertexfactory/LocalVertexFactory.sf) | 276 | 算法、渲染 | [08-mesh-import-and-geometry](algorithm/08-mesh-import-and-geometry.md) |

## src/Application（13 个文件）

| 源码/配置 | 行数 | 检索标记 | 阅读入口 |
| --- | ---: | --- | --- |
| [src/Application/CMakeLists.txt](../../src/Application/CMakeLists.txt) | 63 | 接口/配置/基础辅助 | [09-module-build-and-startup](performance/09-module-build-and-startup.md) |
| [src/Application/Private/Application.cpp](../../src/Application/Private/Application.cpp) | 130 | 渲染 | [08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md)；[09-module-build-and-startup](performance/09-module-build-and-startup.md) |
| [src/Application/Private/AppModule.cpp](../../src/Application/Private/AppModule.cpp) | 30 | 内存 | [08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md)；[09-module-build-and-startup](performance/09-module-build-and-startup.md) |
| [src/Application/Private/AppViewportClient.cpp](../../src/Application/Private/AppViewportClient.cpp) | 342 | 内存、算法 | [08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md)；[09-module-build-and-startup](performance/09-module-build-and-startup.md) |
| [src/Application/Private/AssetBrowserWidget.cpp](../../src/Application/Private/AssetBrowserWidget.cpp) | 51 | 接口/配置/基础辅助 | [08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md)；[09-module-build-and-startup](performance/09-module-build-and-startup.md) |
| [src/Application/Private/PropertyInspectorWidget.cpp](../../src/Application/Private/PropertyInspectorWidget.cpp) | 64 | 接口/配置/基础辅助 | [08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md)；[09-module-build-and-startup](performance/09-module-build-and-startup.md) |
| [src/Application/Private/SceneHierarchyWidget.cpp](../../src/Application/Private/SceneHierarchyWidget.cpp) | 63 | 接口/配置/基础辅助 | [08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md)；[09-module-build-and-startup](performance/09-module-build-and-startup.md) |
| [src/Application/PublicHeader/Application.h](../../src/Application/PublicHeader/Application.h) | 51 | 接口/配置/基础辅助 | [08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md)；[09-module-build-and-startup](performance/09-module-build-and-startup.md) |
| [src/Application/PublicHeader/AppModule.h](../../src/Application/PublicHeader/AppModule.h) | 15 | 接口/配置/基础辅助 | [08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md)；[09-module-build-and-startup](performance/09-module-build-and-startup.md) |
| [src/Application/PublicHeader/AppViewportClient.h](../../src/Application/PublicHeader/AppViewportClient.h) | 69 | 接口/配置/基础辅助 | [08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md)；[09-module-build-and-startup](performance/09-module-build-and-startup.md) |
| [src/Application/PublicHeader/AssetBrowserWidget.h](../../src/Application/PublicHeader/AssetBrowserWidget.h) | 22 | 接口/配置/基础辅助 | [08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md)；[09-module-build-and-startup](performance/09-module-build-and-startup.md) |
| [src/Application/PublicHeader/PropertyInspectorWidget.h](../../src/Application/PublicHeader/PropertyInspectorWidget.h) | 26 | 接口/配置/基础辅助 | [08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md)；[09-module-build-and-startup](performance/09-module-build-and-startup.md) |
| [src/Application/PublicHeader/SceneHierarchyWidget.h](../../src/Application/PublicHeader/SceneHierarchyWidget.h) | 25 | 接口/配置/基础辅助 | [08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md)；[09-module-build-and-startup](performance/09-module-build-and-startup.md) |

## src（1 个文件）

| 源码/配置 | 行数 | 检索标记 | 阅读入口 |
| --- | ---: | --- | --- |
| [src/CMakeLists.txt](../../src/CMakeLists.txt) | 9 | 接口/配置/基础辅助 | [09-module-build-and-startup](performance/09-module-build-and-startup.md) |

## src/Editor（31 个文件）

| 源码/配置 | 行数 | 检索标记 | 阅读入口 |
| --- | ---: | --- | --- |
| [src/Editor/AssetEditor/CMakeLists.txt](../../src/Editor/AssetEditor/CMakeLists.txt) | 10 | 接口/配置/基础辅助 | [09-module-build-and-startup](performance/09-module-build-and-startup.md) |
| [src/Editor/AssetEditor/Private/AssetEditorModule.cpp](../../src/Editor/AssetEditor/Private/AssetEditorModule.cpp) | 15 | 接口/配置/基础辅助 | [架构：editor](../architecture/app/editor.md)；[08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md)；[05-assets-materials-and-instance-data](performance/05-assets-materials-and-instance-data.md) |
| [src/Editor/AssetEditor/Private/EditorAssetPanel.cpp](../../src/Editor/AssetEditor/Private/EditorAssetPanel.cpp) | 425 | 接口/配置/基础辅助 | [架构：editor](../architecture/app/editor.md)；[08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md)；[05-assets-materials-and-instance-data](performance/05-assets-materials-and-instance-data.md) |
| [src/Editor/AssetEditor/PublicHeader/EditorAssetPanel.h](../../src/Editor/AssetEditor/PublicHeader/EditorAssetPanel.h) | 73 | 接口/配置/基础辅助 | [架构：editor](../architecture/app/editor.md)；[08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md)；[05-assets-materials-and-instance-data](performance/05-assets-materials-and-instance-data.md) |
| [src/Editor/CMakeLists.txt](../../src/Editor/CMakeLists.txt) | 5 | 接口/配置/基础辅助 | [09-module-build-and-startup](performance/09-module-build-and-startup.md) |
| [src/Editor/Editor/CMakeLists.txt](../../src/Editor/Editor/CMakeLists.txt) | 10 | 接口/配置/基础辅助 | [09-module-build-and-startup](performance/09-module-build-and-startup.md) |
| [src/Editor/Editor/Private/EditorApplication.cpp](../../src/Editor/Editor/Private/EditorApplication.cpp) | 362 | 接口/配置/基础辅助 | [架构：editor](../architecture/app/editor.md)；[08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md)；[05-assets-materials-and-instance-data](performance/05-assets-materials-and-instance-data.md) |
| [src/Editor/Editor/Private/EditorModule.cpp](../../src/Editor/Editor/Private/EditorModule.cpp) | 24 | 接口/配置/基础辅助 | [架构：editor](../architecture/app/editor.md)；[08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md)；[05-assets-materials-and-instance-data](performance/05-assets-materials-and-instance-data.md) |
| [src/Editor/Editor/PublicHeader/EditorApplication.h](../../src/Editor/Editor/PublicHeader/EditorApplication.h) | 62 | 接口/配置/基础辅助 | [架构：editor](../architecture/app/editor.md)；[08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md)；[05-assets-materials-and-instance-data](performance/05-assets-materials-and-instance-data.md) |
| [src/Editor/MaterialEditor/CMakeLists.txt](../../src/Editor/MaterialEditor/CMakeLists.txt) | 10 | 接口/配置/基础辅助 | [09-module-build-and-startup](performance/09-module-build-and-startup.md) |
| [src/Editor/MaterialEditor/Private/EditorMaterialPanel.cpp](../../src/Editor/MaterialEditor/Private/EditorMaterialPanel.cpp) | 403 | 内存、渲染 | [架构：editor](../architecture/app/editor.md)；[08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md)；[05-assets-materials-and-instance-data](performance/05-assets-materials-and-instance-data.md) |
| [src/Editor/MaterialEditor/Private/MaterialEditorModule.cpp](../../src/Editor/MaterialEditor/Private/MaterialEditorModule.cpp) | 15 | 接口/配置/基础辅助 | [架构：editor](../architecture/app/editor.md)；[08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md)；[05-assets-materials-and-instance-data](performance/05-assets-materials-and-instance-data.md) |
| [src/Editor/MaterialEditor/Private/MaterialLibrary.cpp](../../src/Editor/MaterialEditor/Private/MaterialLibrary.cpp) | 198 | 内存 | [架构：editor](../architecture/app/editor.md)；[08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md)；[05-assets-materials-and-instance-data](performance/05-assets-materials-and-instance-data.md) |
| [src/Editor/MaterialEditor/PublicHeader/EditorMaterialPanel.h](../../src/Editor/MaterialEditor/PublicHeader/EditorMaterialPanel.h) | 74 | 接口/配置/基础辅助 | [架构：editor](../architecture/app/editor.md)；[08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md)；[05-assets-materials-and-instance-data](performance/05-assets-materials-and-instance-data.md) |
| [src/Editor/MaterialEditor/PublicHeader/MaterialLibrary.h](../../src/Editor/MaterialEditor/PublicHeader/MaterialLibrary.h) | 41 | 接口/配置/基础辅助 | [架构：editor](../architecture/app/editor.md)；[08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md)；[05-assets-materials-and-instance-data](performance/05-assets-materials-and-instance-data.md) |
| [src/Editor/NodeEditor/CMakeLists.txt](../../src/Editor/NodeEditor/CMakeLists.txt) | 9 | 接口/配置/基础辅助 | [09-module-build-and-startup](performance/09-module-build-and-startup.md) |
| [src/Editor/NodeEditor/Private/EditorNodePanel.cpp](../../src/Editor/NodeEditor/Private/EditorNodePanel.cpp) | 83 | 接口/配置/基础辅助 | [架构：editor](../architecture/app/editor.md)；[08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md)；[05-assets-materials-and-instance-data](performance/05-assets-materials-and-instance-data.md) |
| [src/Editor/NodeEditor/Private/NodeEditorModule.cpp](../../src/Editor/NodeEditor/Private/NodeEditorModule.cpp) | 14 | 接口/配置/基础辅助 | [架构：editor](../architecture/app/editor.md)；[08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md)；[05-assets-materials-and-instance-data](performance/05-assets-materials-and-instance-data.md) |
| [src/Editor/NodeEditor/PublicHeader/EditorNodePanel.h](../../src/Editor/NodeEditor/PublicHeader/EditorNodePanel.h) | 29 | 接口/配置/基础辅助 | [架构：editor](../architecture/app/editor.md)；[08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md)；[05-assets-materials-and-instance-data](performance/05-assets-materials-and-instance-data.md) |
| [src/Editor/SceneEditor/CMakeLists.txt](../../src/Editor/SceneEditor/CMakeLists.txt) | 9 | 接口/配置/基础辅助 | [09-module-build-and-startup](performance/09-module-build-and-startup.md) |
| [src/Editor/SceneEditor/Private/EditorRotationGizmo.cpp](../../src/Editor/SceneEditor/Private/EditorRotationGizmo.cpp) | 287 | 算法 | [架构：editor](../architecture/app/editor.md)；[03-transforms-frustum-and-depth](algorithm/03-transforms-frustum-and-depth.md)；[08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md)；[06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md) |
| [src/Editor/SceneEditor/Private/EditorScaleGizmo.cpp](../../src/Editor/SceneEditor/Private/EditorScaleGizmo.cpp) | 188 | 算法 | [架构：editor](../architecture/app/editor.md)；[03-transforms-frustum-and-depth](algorithm/03-transforms-frustum-and-depth.md)；[08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md)；[06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md) |
| [src/Editor/SceneEditor/Private/EditorScene.cpp](../../src/Editor/SceneEditor/Private/EditorScene.cpp) | 556 | 并发、算法 | [架构：editor](../architecture/app/editor.md)；[08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md)；[05-assets-materials-and-instance-data](performance/05-assets-materials-and-instance-data.md) |
| [src/Editor/SceneEditor/Private/EditorScenePanel.cpp](../../src/Editor/SceneEditor/Private/EditorScenePanel.cpp) | 126 | 接口/配置/基础辅助 | [架构：editor](../architecture/app/editor.md)；[08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md)；[05-assets-materials-and-instance-data](performance/05-assets-materials-and-instance-data.md) |
| [src/Editor/SceneEditor/Private/EditorTranslationGizmo.cpp](../../src/Editor/SceneEditor/Private/EditorTranslationGizmo.cpp) | 440 | 渲染 | [架构：editor](../architecture/app/editor.md)；[03-transforms-frustum-and-depth](algorithm/03-transforms-frustum-and-depth.md)；[08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md)；[06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md) |
| [src/Editor/SceneEditor/Private/EditorViewAxes.cpp](../../src/Editor/SceneEditor/Private/EditorViewAxes.cpp) | 171 | 渲染 | [架构：editor](../architecture/app/editor.md)；[03-transforms-frustum-and-depth](algorithm/03-transforms-frustum-and-depth.md)；[08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md)；[06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md) |
| [src/Editor/SceneEditor/Private/EditorViewAxes.h](../../src/Editor/SceneEditor/Private/EditorViewAxes.h) | 11 | 接口/配置/基础辅助 | [架构：editor](../architecture/app/editor.md)；[03-transforms-frustum-and-depth](algorithm/03-transforms-frustum-and-depth.md)；[08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md)；[06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md) |
| [src/Editor/SceneEditor/Private/SceneEditorModule.cpp](../../src/Editor/SceneEditor/Private/SceneEditorModule.cpp) | 15 | 接口/配置/基础辅助 | [架构：editor](../architecture/app/editor.md)；[08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md)；[05-assets-materials-and-instance-data](performance/05-assets-materials-and-instance-data.md) |
| [src/Editor/SceneEditor/PublicHeader/EditorScene.h](../../src/Editor/SceneEditor/PublicHeader/EditorScene.h) | 97 | 接口/配置/基础辅助 | [架构：editor](../architecture/app/editor.md)；[08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md)；[05-assets-materials-and-instance-data](performance/05-assets-materials-and-instance-data.md) |
| [src/Editor/SceneEditor/PublicHeader/EditorScenePanel.h](../../src/Editor/SceneEditor/PublicHeader/EditorScenePanel.h) | 41 | 接口/配置/基础辅助 | [架构：editor](../architecture/app/editor.md)；[08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md)；[05-assets-materials-and-instance-data](performance/05-assets-materials-and-instance-data.md) |
| [src/Editor/SceneEditor/PublicHeader/EditorTranslationGizmo.h](../../src/Editor/SceneEditor/PublicHeader/EditorTranslationGizmo.h) | 112 | 接口/配置/基础辅助 | [架构：editor](../architecture/app/editor.md)；[03-transforms-frustum-and-depth](algorithm/03-transforms-frustum-and-depth.md)；[08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md)；[06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md) |

## src/Framework（1 个文件）

| 源码/配置 | 行数 | 检索标记 | 阅读入口 |
| --- | ---: | --- | --- |
| [src/Framework/CMakeLists.txt](../../src/Framework/CMakeLists.txt) | 11 | 接口/配置/基础辅助 | [09-module-build-and-startup](performance/09-module-build-and-startup.md) |

## src/Framework/Core（24 个文件）

| 源码/配置 | 行数 | 检索标记 | 阅读入口 |
| --- | ---: | --- | --- |
| [src/Framework/Core/CMakeLists.txt](../../src/Framework/Core/CMakeLists.txt) | 63 | 接口/配置/基础辅助 | [09-module-build-and-startup](performance/09-module-build-and-startup.md) |
| [src/Framework/Core/Private/Log.cpp](../../src/Framework/Core/Private/Log.cpp) | 119 | 并发 | [架构：core](../architecture/core.md) |
| [src/Framework/Core/Private/PathInfo.cpp](../../src/Framework/Core/Private/PathInfo.cpp) | 154 | 渲染 | [架构：core](../architecture/core.md) |
| [src/Framework/Core/Private/TaskPool.cpp](../../src/Framework/Core/Private/TaskPool.cpp) | 158 | 并发 | [01-task-and-render-threads](performance/01-task-and-render-threads.md) |
| [src/Framework/Core/Private/ThreadInfo.cpp](../../src/Framework/Core/Private/ThreadInfo.cpp) | 16 | 并发 | [01-task-and-render-threads](performance/01-task-and-render-threads.md) |
| [src/Framework/Core/Private/Timer.cpp](../../src/Framework/Core/Private/Timer.cpp) | 16 | 接口/配置/基础辅助 | [架构：core](../architecture/core.md) |
| [src/Framework/Core/PublicHeader/BoxSphereBounds.h](../../src/Framework/Core/PublicHeader/BoxSphereBounds.h) | 233 | 算法 | [03-transforms-frustum-and-depth](algorithm/03-transforms-frustum-and-depth.md) |
| [src/Framework/Core/PublicHeader/BVH.hpp](../../src/Framework/Core/PublicHeader/BVH.hpp) | 194 | 算法 | [02-bvh-octree-and-scene-query](algorithm/02-bvh-octree-and-scene-query.md)；[01-ray-triangle-and-distance](algorithm/01-ray-triangle-and-distance.md) |
| [src/Framework/Core/PublicHeader/BVH.inl](../../src/Framework/Core/PublicHeader/BVH.inl) | 407 | 算法 | [02-bvh-octree-and-scene-query](algorithm/02-bvh-octree-and-scene-query.md)；[01-ray-triangle-and-distance](algorithm/01-ray-triangle-and-distance.md) |
| [src/Framework/Core/PublicHeader/Common.h](../../src/Framework/Core/PublicHeader/Common.h) | 3 | 接口/配置/基础辅助 | [架构：core](../architecture/core.md) |
| [src/Framework/Core/PublicHeader/Flags.h](../../src/Framework/Core/PublicHeader/Flags.h) | 82 | 接口/配置/基础辅助 | [架构：core](../architecture/core.md) |
| [src/Framework/Core/PublicHeader/HashHelper.hpp](../../src/Framework/Core/PublicHeader/HashHelper.hpp) | 19 | 接口/配置/基础辅助 | [架构：core](../architecture/core.md) |
| [src/Framework/Core/PublicHeader/Log.h](../../src/Framework/Core/PublicHeader/Log.h) | 78 | 并发 | [架构：core](../architecture/core.md) |
| [src/Framework/Core/PublicHeader/Math.hpp](../../src/Framework/Core/PublicHeader/Math.hpp) | 541 | 算法 | [03-transforms-frustum-and-depth](algorithm/03-transforms-frustum-and-depth.md) |
| [src/Framework/Core/PublicHeader/Matrix.hpp](../../src/Framework/Core/PublicHeader/Matrix.hpp) | 375 | 算法 | [03-transforms-frustum-and-depth](algorithm/03-transforms-frustum-and-depth.md) |
| [src/Framework/Core/PublicHeader/Module.h](../../src/Framework/Core/PublicHeader/Module.h) | 209 | 并发 | [架构：core](../architecture/core.md) |
| [src/Framework/Core/PublicHeader/Octree.hpp](../../src/Framework/Core/PublicHeader/Octree.hpp) | 519 | 算法 | [02-bvh-octree-and-scene-query](algorithm/02-bvh-octree-and-scene-query.md) |
| [src/Framework/Core/PublicHeader/PathInfo.h](../../src/Framework/Core/PublicHeader/PathInfo.h) | 67 | 渲染 | [架构：core](../architecture/core.md) |
| [src/Framework/Core/PublicHeader/Singleton.hpp](../../src/Framework/Core/PublicHeader/Singleton.hpp) | 17 | 接口/配置/基础辅助 | [架构：core](../architecture/core.md) |
| [src/Framework/Core/PublicHeader/TaskPool.h](../../src/Framework/Core/PublicHeader/TaskPool.h) | 82 | 并发 | [01-task-and-render-threads](performance/01-task-and-render-threads.md) |
| [src/Framework/Core/PublicHeader/ThreadInfo.h](../../src/Framework/Core/PublicHeader/ThreadInfo.h) | 10 | 接口/配置/基础辅助 | [01-task-and-render-threads](performance/01-task-and-render-threads.md) |
| [src/Framework/Core/PublicHeader/Timer.h](../../src/Framework/Core/PublicHeader/Timer.h) | 15 | 接口/配置/基础辅助 | [架构：core](../architecture/core.md) |
| [src/Framework/Core/PublicHeader/Transform.hpp](../../src/Framework/Core/PublicHeader/Transform.hpp) | 355 | 算法 | [03-transforms-frustum-and-depth](algorithm/03-transforms-frustum-and-depth.md) |
| [src/Framework/Core/PublicHeader/TypeIDCast.h](../../src/Framework/Core/PublicHeader/TypeIDCast.h) | 115 | 接口/配置/基础辅助 | [架构：core](../architecture/core.md) |

## src/Framework/Engine（73 个文件）

| 源码/配置 | 行数 | 检索标记 | 阅读入口 |
| --- | ---: | --- | --- |
| [src/Framework/Engine/CMakeLists.txt](../../src/Framework/Engine/CMakeLists.txt) | 60 | 接口/配置/基础辅助 | [09-module-build-and-startup](performance/09-module-build-and-startup.md) |
| [src/Framework/Engine/Private/AssetManager.cpp](../../src/Framework/Engine/Private/AssetManager.cpp) | 177 | 并发 | [05-assets-materials-and-instance-data](performance/05-assets-materials-and-instance-data.md) |
| [src/Framework/Engine/Private/AssetRegistry.cpp](../../src/Framework/Engine/Private/AssetRegistry.cpp) | 41 | 接口/配置/基础辅助 | [05-assets-materials-and-instance-data](performance/05-assets-materials-and-instance-data.md) |
| [src/Framework/Engine/Private/Camera.cpp](../../src/Framework/Engine/Private/Camera.cpp) | 335 | 算法 | [03-transforms-frustum-and-depth](algorithm/03-transforms-frustum-and-depth.md)；[08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md) |
| [src/Framework/Engine/Private/DistanceFieldData.cpp](../../src/Framework/Engine/Private/DistanceFieldData.cpp) | 261 | 内存 | [04-distance-fields-and-jump-flood](algorithm/04-distance-fields-and-jump-flood.md)；[04-memory-and-transient-resources](performance/04-memory-and-transient-resources.md) |
| [src/Framework/Engine/Private/DistanceFieldMgr.cpp](../../src/Framework/Engine/Private/DistanceFieldMgr.cpp) | 791 | 并发、内存、算法、渲染 | [04-distance-fields-and-jump-flood](algorithm/04-distance-fields-and-jump-flood.md)；[04-memory-and-transient-resources](performance/04-memory-and-transient-resources.md) |
| [src/Framework/Engine/Private/DistanceFieldProcess.cpp](../../src/Framework/Engine/Private/DistanceFieldProcess.cpp) | 375 | 算法、渲染 | [04-distance-fields-and-jump-flood](algorithm/04-distance-fields-and-jump-flood.md)；[04-memory-and-transient-resources](performance/04-memory-and-transient-resources.md) |
| [src/Framework/Engine/Private/DistanceFieldProcess.h](../../src/Framework/Engine/Private/DistanceFieldProcess.h) | 102 | 算法、渲染 | [04-distance-fields-and-jump-flood](algorithm/04-distance-fields-and-jump-flood.md)；[04-memory-and-transient-resources](performance/04-memory-and-transient-resources.md) |
| [src/Framework/Engine/Private/Engine.cpp](../../src/Framework/Engine/Private/Engine.cpp) | 7 | 接口/配置/基础辅助 | [架构：engine](../architecture/engine.md)；[05-assets-materials-and-instance-data](performance/05-assets-materials-and-instance-data.md) |
| [src/Framework/Engine/Private/EngineGlobal.cpp](../../src/Framework/Engine/Private/EngineGlobal.cpp) | 14 | 接口/配置/基础辅助 | [架构：engine](../architecture/engine.md)；[05-assets-materials-and-instance-data](performance/05-assets-materials-and-instance-data.md) |
| [src/Framework/Engine/Private/EngineModule.cpp](../../src/Framework/Engine/Private/EngineModule.cpp) | 46 | 接口/配置/基础辅助 | [架构：engine](../architecture/engine.md)；[05-assets-materials-and-instance-data](performance/05-assets-materials-and-instance-data.md) |
| [src/Framework/Engine/Private/EngineModule.h](../../src/Framework/Engine/Private/EngineModule.h) | 23 | 接口/配置/基础辅助 | [架构：engine](../architecture/engine.md)；[05-assets-materials-and-instance-data](performance/05-assets-materials-and-instance-data.md) |
| [src/Framework/Engine/Private/IEngine.cpp](../../src/Framework/Engine/Private/IEngine.cpp) | 33 | 接口/配置/基础辅助 | [架构：engine](../architecture/engine.md)；[05-assets-materials-and-instance-data](performance/05-assets-materials-and-instance-data.md) |
| [src/Framework/Engine/Private/IEngine.h](../../src/Framework/Engine/Private/IEngine.h) | 17 | 接口/配置/基础辅助 | [架构：engine](../architecture/engine.md)；[05-assets-materials-and-instance-data](performance/05-assets-materials-and-instance-data.md) |
| [src/Framework/Engine/Private/LightComponent.cpp](../../src/Framework/Engine/Private/LightComponent.cpp) | 198 | 内存 | [架构：engine](../architecture/engine.md)；[05-assets-materials-and-instance-data](performance/05-assets-materials-and-instance-data.md) |
| [src/Framework/Engine/Private/LightSceneProxy.cpp](../../src/Framework/Engine/Private/LightSceneProxy.cpp) | 244 | 接口/配置/基础辅助 | [架构：engine](../architecture/engine.md)；[05-assets-materials-and-instance-data](performance/05-assets-materials-and-instance-data.md) |
| [src/Framework/Engine/Private/LocalVertexFactory.cpp](../../src/Framework/Engine/Private/LocalVertexFactory.cpp) | 196 | 渲染 | [08-mesh-import-and-geometry](algorithm/08-mesh-import-and-geometry.md)；[05-assets-materials-and-instance-data](performance/05-assets-materials-and-instance-data.md) |
| [src/Framework/Engine/Private/Material.cpp](../../src/Framework/Engine/Private/Material.cpp) | 169 | 渲染 | [05-assets-materials-and-instance-data](performance/05-assets-materials-and-instance-data.md) |
| [src/Framework/Engine/Private/MaterialAsset.cpp](../../src/Framework/Engine/Private/MaterialAsset.cpp) | 150 | 接口/配置/基础辅助 | [05-assets-materials-and-instance-data](performance/05-assets-materials-and-instance-data.md) |
| [src/Framework/Engine/Private/MaterialCore.cpp](../../src/Framework/Engine/Private/MaterialCore.cpp) | 295 | 渲染 | [06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md)；[05-assets-materials-and-instance-data](performance/05-assets-materials-and-instance-data.md) |
| [src/Framework/Engine/Private/MaterialParameter.cpp](../../src/Framework/Engine/Private/MaterialParameter.cpp) | 86 | 接口/配置/基础辅助 | [05-assets-materials-and-instance-data](performance/05-assets-materials-and-instance-data.md) |
| [src/Framework/Engine/Private/MaterialShaderParameter.cpp](../../src/Framework/Engine/Private/MaterialShaderParameter.cpp) | 51 | 渲染 | [05-assets-materials-and-instance-data](performance/05-assets-materials-and-instance-data.md) |
| [src/Framework/Engine/Private/PrimitiveComponent.cpp](../../src/Framework/Engine/Private/PrimitiveComponent.cpp) | 100 | 接口/配置/基础辅助 | [架构：engine](../architecture/engine.md)；[05-assets-materials-and-instance-data](performance/05-assets-materials-and-instance-data.md) |
| [src/Framework/Engine/Private/PrimitiveSceneProxy.cpp](../../src/Framework/Engine/Private/PrimitiveSceneProxy.cpp) | 22 | 接口/配置/基础辅助 | [架构：engine](../architecture/engine.md)；[05-assets-materials-and-instance-data](performance/05-assets-materials-and-instance-data.md) |
| [src/Framework/Engine/Private/SceneComponent.cpp](../../src/Framework/Engine/Private/SceneComponent.cpp) | 356 | 算法 | [03-transforms-frustum-and-depth](algorithm/03-transforms-frustum-and-depth.md)；[08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md) |
| [src/Framework/Engine/Private/SceneObject.cpp](../../src/Framework/Engine/Private/SceneObject.cpp) | 33 | 接口/配置/基础辅助 | [架构：engine](../architecture/engine.md)；[05-assets-materials-and-instance-data](performance/05-assets-materials-and-instance-data.md) |
| [src/Framework/Engine/Private/SceneView.cpp](../../src/Framework/Engine/Private/SceneView.cpp) | 286 | 算法 | [03-transforms-frustum-and-depth](algorithm/03-transforms-frustum-and-depth.md)；[08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md) |
| [src/Framework/Engine/Private/SceneViewport.cpp](../../src/Framework/Engine/Private/SceneViewport.cpp) | 190 | 渲染 | [03-transforms-frustum-and-depth](algorithm/03-transforms-frustum-and-depth.md)；[08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md) |
| [src/Framework/Engine/Private/Shape.cpp](../../src/Framework/Engine/Private/Shape.cpp) | 484 | 算法 | [08-mesh-import-and-geometry](algorithm/08-mesh-import-and-geometry.md)；[05-assets-materials-and-instance-data](performance/05-assets-materials-and-instance-data.md) |
| [src/Framework/Engine/Private/SkyLightAsset.cpp](../../src/Framework/Engine/Private/SkyLightAsset.cpp) | 28 | 渲染 | [05-assets-materials-and-instance-data](performance/05-assets-materials-and-instance-data.md) |
| [src/Framework/Engine/Private/StaticMesh.cpp](../../src/Framework/Engine/Private/StaticMesh.cpp) | 161 | 接口/配置/基础辅助 | [08-mesh-import-and-geometry](algorithm/08-mesh-import-and-geometry.md)；[05-assets-materials-and-instance-data](performance/05-assets-materials-and-instance-data.md) |
| [src/Framework/Engine/Private/StaticMeshAsset.cpp](../../src/Framework/Engine/Private/StaticMeshAsset.cpp) | 251 | 接口/配置/基础辅助 | [05-assets-materials-and-instance-data](performance/05-assets-materials-and-instance-data.md) |
| [src/Framework/Engine/Private/StaticMeshComponent.cpp](../../src/Framework/Engine/Private/StaticMeshComponent.cpp) | 133 | 内存 | [08-mesh-import-and-geometry](algorithm/08-mesh-import-and-geometry.md)；[05-assets-materials-and-instance-data](performance/05-assets-materials-and-instance-data.md) |
| [src/Framework/Engine/Private/StaticMeshProxy.cpp](../../src/Framework/Engine/Private/StaticMeshProxy.cpp) | 19 | 算法 | [08-mesh-import-and-geometry](algorithm/08-mesh-import-and-geometry.md)；[05-assets-materials-and-instance-data](performance/05-assets-materials-and-instance-data.md) |
| [src/Framework/Engine/Private/TextureAsset.cpp](../../src/Framework/Engine/Private/TextureAsset.cpp) | 20 | 接口/配置/基础辅助 | [05-assets-materials-and-instance-data](performance/05-assets-materials-and-instance-data.md) |
| [src/Framework/Engine/Private/Viewport.cpp](../../src/Framework/Engine/Private/Viewport.cpp) | 10 | 接口/配置/基础辅助 | [08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md) |
| [src/Framework/Engine/Private/ViewportClient.cpp](../../src/Framework/Engine/Private/ViewportClient.cpp) | 4 | 接口/配置/基础辅助 | [08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md) |
| [src/Framework/Engine/PublicHeader/Asset.h](../../src/Framework/Engine/PublicHeader/Asset.h) | 28 | 接口/配置/基础辅助 | [05-assets-materials-and-instance-data](performance/05-assets-materials-and-instance-data.md) |
| [src/Framework/Engine/PublicHeader/AssetManager.h](../../src/Framework/Engine/PublicHeader/AssetManager.h) | 106 | 并发 | [05-assets-materials-and-instance-data](performance/05-assets-materials-and-instance-data.md) |
| [src/Framework/Engine/PublicHeader/AssetRegistry.h](../../src/Framework/Engine/PublicHeader/AssetRegistry.h) | 25 | 并发 | [05-assets-materials-and-instance-data](performance/05-assets-materials-and-instance-data.md) |
| [src/Framework/Engine/PublicHeader/Camera.h](../../src/Framework/Engine/PublicHeader/Camera.h) | 159 | 接口/配置/基础辅助 | [03-transforms-frustum-and-depth](algorithm/03-transforms-frustum-and-depth.md)；[08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md) |
| [src/Framework/Engine/PublicHeader/DistanceFieldData.h](../../src/Framework/Engine/PublicHeader/DistanceFieldData.h) | 293 | 内存、渲染 | [04-distance-fields-and-jump-flood](algorithm/04-distance-fields-and-jump-flood.md)；[04-memory-and-transient-resources](performance/04-memory-and-transient-resources.md) |
| [src/Framework/Engine/PublicHeader/DistanceFieldMgr.h](../../src/Framework/Engine/PublicHeader/DistanceFieldMgr.h) | 101 | 并发、算法 | [04-distance-fields-and-jump-flood](algorithm/04-distance-fields-and-jump-flood.md)；[04-memory-and-transient-resources](performance/04-memory-and-transient-resources.md) |
| [src/Framework/Engine/PublicHeader/Engine.h](../../src/Framework/Engine/PublicHeader/Engine.h) | 21 | 接口/配置/基础辅助 | [架构：engine](../architecture/engine.md)；[05-assets-materials-and-instance-data](performance/05-assets-materials-and-instance-data.md) |
| [src/Framework/Engine/PublicHeader/EngineExport.h](../../src/Framework/Engine/PublicHeader/EngineExport.h) | 16 | 接口/配置/基础辅助 | [架构：engine](../architecture/engine.md)；[05-assets-materials-and-instance-data](performance/05-assets-materials-and-instance-data.md) |
| [src/Framework/Engine/PublicHeader/EngineGlobal.h](../../src/Framework/Engine/PublicHeader/EngineGlobal.h) | 9 | 接口/配置/基础辅助 | [架构：engine](../architecture/engine.md)；[05-assets-materials-and-instance-data](performance/05-assets-materials-and-instance-data.md) |
| [src/Framework/Engine/PublicHeader/InstanceDataMgr.h](../../src/Framework/Engine/PublicHeader/InstanceDataMgr.h) | 90 | 并发、渲染 | [05-assets-materials-and-instance-data](performance/05-assets-materials-and-instance-data.md) |
| [src/Framework/Engine/PublicHeader/InstanceDataMgr.inl](../../src/Framework/Engine/PublicHeader/InstanceDataMgr.inl) | 259 | 并发、内存、渲染 | [05-assets-materials-and-instance-data](performance/05-assets-materials-and-instance-data.md) |
| [src/Framework/Engine/PublicHeader/LightComponent.h](../../src/Framework/Engine/PublicHeader/LightComponent.h) | 182 | 接口/配置/基础辅助 | [架构：engine](../architecture/engine.md)；[05-assets-materials-and-instance-data](performance/05-assets-materials-and-instance-data.md) |
| [src/Framework/Engine/PublicHeader/LightSceneProxy.h](../../src/Framework/Engine/PublicHeader/LightSceneProxy.h) | 178 | 接口/配置/基础辅助 | [架构：engine](../architecture/engine.md)；[05-assets-materials-and-instance-data](performance/05-assets-materials-and-instance-data.md) |
| [src/Framework/Engine/PublicHeader/LocalVertexFactory.h](../../src/Framework/Engine/PublicHeader/LocalVertexFactory.h) | 91 | 渲染 | [08-mesh-import-and-geometry](algorithm/08-mesh-import-and-geometry.md)；[05-assets-materials-and-instance-data](performance/05-assets-materials-and-instance-data.md) |
| [src/Framework/Engine/PublicHeader/Material.h](../../src/Framework/Engine/PublicHeader/Material.h) | 148 | 接口/配置/基础辅助 | [05-assets-materials-and-instance-data](performance/05-assets-materials-and-instance-data.md) |
| [src/Framework/Engine/PublicHeader/MaterialAsset.h](../../src/Framework/Engine/PublicHeader/MaterialAsset.h) | 30 | 接口/配置/基础辅助 | [05-assets-materials-and-instance-data](performance/05-assets-materials-and-instance-data.md) |
| [src/Framework/Engine/PublicHeader/MaterialCore.h](../../src/Framework/Engine/PublicHeader/MaterialCore.h) | 395 | 内存、渲染 | [06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md)；[05-assets-materials-and-instance-data](performance/05-assets-materials-and-instance-data.md) |
| [src/Framework/Engine/PublicHeader/MaterialParameter.h](../../src/Framework/Engine/PublicHeader/MaterialParameter.h) | 99 | 渲染 | [05-assets-materials-and-instance-data](performance/05-assets-materials-and-instance-data.md) |
| [src/Framework/Engine/PublicHeader/MaterialShaderParameter.h](../../src/Framework/Engine/PublicHeader/MaterialShaderParameter.h) | 168 | 渲染 | [05-assets-materials-and-instance-data](performance/05-assets-materials-and-instance-data.md) |
| [src/Framework/Engine/PublicHeader/PrimitiveComponent.h](../../src/Framework/Engine/PublicHeader/PrimitiveComponent.h) | 93 | 接口/配置/基础辅助 | [架构：engine](../architecture/engine.md)；[05-assets-materials-and-instance-data](performance/05-assets-materials-and-instance-data.md) |
| [src/Framework/Engine/PublicHeader/PrimitiveSceneProxy.h](../../src/Framework/Engine/PublicHeader/PrimitiveSceneProxy.h) | 61 | 接口/配置/基础辅助 | [架构：engine](../architecture/engine.md)；[05-assets-materials-and-instance-data](performance/05-assets-materials-and-instance-data.md) |
| [src/Framework/Engine/PublicHeader/SceneComponent.h](../../src/Framework/Engine/PublicHeader/SceneComponent.h) | 110 | 接口/配置/基础辅助 | [03-transforms-frustum-and-depth](algorithm/03-transforms-frustum-and-depth.md)；[08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md) |
| [src/Framework/Engine/PublicHeader/SceneInterface.h](../../src/Framework/Engine/PublicHeader/SceneInterface.h) | 48 | 接口/配置/基础辅助 | [架构：engine](../architecture/engine.md)；[05-assets-materials-and-instance-data](performance/05-assets-materials-and-instance-data.md) |
| [src/Framework/Engine/PublicHeader/SceneObject.h](../../src/Framework/Engine/PublicHeader/SceneObject.h) | 63 | 接口/配置/基础辅助 | [架构：engine](../architecture/engine.md)；[05-assets-materials-and-instance-data](performance/05-assets-materials-and-instance-data.md) |
| [src/Framework/Engine/PublicHeader/SceneView.h](../../src/Framework/Engine/PublicHeader/SceneView.h) | 156 | 算法 | [03-transforms-frustum-and-depth](algorithm/03-transforms-frustum-and-depth.md)；[08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md)；[03-rdg-execution-and-state](performance/03-rdg-execution-and-state.md) |
| [src/Framework/Engine/PublicHeader/SceneViewport.h](../../src/Framework/Engine/PublicHeader/SceneViewport.h) | 71 | 接口/配置/基础辅助 | [03-transforms-frustum-and-depth](algorithm/03-transforms-frustum-and-depth.md)；[08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md) |
| [src/Framework/Engine/PublicHeader/Shape.h](../../src/Framework/Engine/PublicHeader/Shape.h) | 15 | 接口/配置/基础辅助 | [08-mesh-import-and-geometry](algorithm/08-mesh-import-and-geometry.md)；[05-assets-materials-and-instance-data](performance/05-assets-materials-and-instance-data.md) |
| [src/Framework/Engine/PublicHeader/SkyLightAsset.h](../../src/Framework/Engine/PublicHeader/SkyLightAsset.h) | 26 | 接口/配置/基础辅助 | [05-assets-materials-and-instance-data](performance/05-assets-materials-and-instance-data.md) |
| [src/Framework/Engine/PublicHeader/StaticMesh.h](../../src/Framework/Engine/PublicHeader/StaticMesh.h) | 46 | 接口/配置/基础辅助 | [08-mesh-import-and-geometry](algorithm/08-mesh-import-and-geometry.md)；[05-assets-materials-and-instance-data](performance/05-assets-materials-and-instance-data.md) |
| [src/Framework/Engine/PublicHeader/StaticMeshAsset.h](../../src/Framework/Engine/PublicHeader/StaticMeshAsset.h) | 22 | 接口/配置/基础辅助 | [05-assets-materials-and-instance-data](performance/05-assets-materials-and-instance-data.md) |
| [src/Framework/Engine/PublicHeader/StaticMeshComponent.h](../../src/Framework/Engine/PublicHeader/StaticMeshComponent.h) | 105 | 接口/配置/基础辅助 | [08-mesh-import-and-geometry](algorithm/08-mesh-import-and-geometry.md)；[05-assets-materials-and-instance-data](performance/05-assets-materials-and-instance-data.md) |
| [src/Framework/Engine/PublicHeader/StaticMeshProxy.h](../../src/Framework/Engine/PublicHeader/StaticMeshProxy.h) | 26 | 接口/配置/基础辅助 | [08-mesh-import-and-geometry](algorithm/08-mesh-import-and-geometry.md)；[05-assets-materials-and-instance-data](performance/05-assets-materials-and-instance-data.md) |
| [src/Framework/Engine/PublicHeader/StaticMeshResources.h](../../src/Framework/Engine/PublicHeader/StaticMeshResources.h) | 125 | 接口/配置/基础辅助 | [08-mesh-import-and-geometry](algorithm/08-mesh-import-and-geometry.md)；[05-assets-materials-and-instance-data](performance/05-assets-materials-and-instance-data.md) |
| [src/Framework/Engine/PublicHeader/TextureAsset.h](../../src/Framework/Engine/PublicHeader/TextureAsset.h) | 23 | 接口/配置/基础辅助 | [05-assets-materials-and-instance-data](performance/05-assets-materials-and-instance-data.md) |
| [src/Framework/Engine/PublicHeader/Viewport.h](../../src/Framework/Engine/PublicHeader/Viewport.h) | 30 | 接口/配置/基础辅助 | [08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md) |
| [src/Framework/Engine/PublicHeader/ViewportClient.h](../../src/Framework/Engine/PublicHeader/ViewportClient.h) | 23 | 接口/配置/基础辅助 | [08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md) |

## src/Framework/ImGUISlate（15 个文件）

| 源码/配置 | 行数 | 检索标记 | 阅读入口 |
| --- | ---: | --- | --- |
| [src/Framework/ImGUISlate/CMakeLists.txt](../../src/Framework/ImGUISlate/CMakeLists.txt) | 73 | 接口/配置/基础辅助 | [09-module-build-and-startup](performance/09-module-build-and-startup.md) |
| [src/Framework/ImGUISlate/Private/imgui_impl_rhi.cpp](../../src/Framework/ImGUISlate/Private/imgui_impl_rhi.cpp) | 351 | 并发、内存、渲染 | [08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md) |
| [src/Framework/ImGUISlate/Private/imgui_impl_rhi.h](../../src/Framework/ImGUISlate/Private/imgui_impl_rhi.h) | 47 | 内存、渲染 | [08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md) |
| [src/Framework/ImGUISlate/Private/ImMainWindow.cpp](../../src/Framework/ImGUISlate/Private/ImMainWindow.cpp) | 217 | 渲染 | [08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md) |
| [src/Framework/ImGUISlate/Private/ImSlateRenderer.cpp](../../src/Framework/ImGUISlate/Private/ImSlateRenderer.cpp) | 329 | 并发、内存、渲染 | [08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md) |
| [src/Framework/ImGUISlate/Private/ImWidget.cpp](../../src/Framework/ImGUISlate/Private/ImWidget.cpp) | 173 | 接口/配置/基础辅助 | [08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md) |
| [src/Framework/ImGUISlate/Private/LayoutMgr.cpp](../../src/Framework/ImGUISlate/Private/LayoutMgr.cpp) | 197 | 接口/配置/基础辅助 | [08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md) |
| [src/Framework/ImGUISlate/Private/Widgets/ImCommonWidgets.cpp](../../src/Framework/ImGUISlate/Private/Widgets/ImCommonWidgets.cpp) | 197 | 算法 | [08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md) |
| [src/Framework/ImGUISlate/Private/Widgets/ImFileDialogWidget.cpp](../../src/Framework/ImGUISlate/Private/Widgets/ImFileDialogWidget.cpp) | 847 | 接口/配置/基础辅助 | [08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md) |
| [src/Framework/ImGUISlate/PublicHeader/ImMainWindow.h](../../src/Framework/ImGUISlate/PublicHeader/ImMainWindow.h) | 61 | 接口/配置/基础辅助 | [08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md) |
| [src/Framework/ImGUISlate/PublicHeader/ImSlateRenderer.h](../../src/Framework/ImGUISlate/PublicHeader/ImSlateRenderer.h) | 61 | 接口/配置/基础辅助 | [08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md) |
| [src/Framework/ImGUISlate/PublicHeader/ImWidget.h](../../src/Framework/ImGUISlate/PublicHeader/ImWidget.h) | 46 | 接口/配置/基础辅助 | [08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md) |
| [src/Framework/ImGUISlate/PublicHeader/LayoutMgr.h](../../src/Framework/ImGUISlate/PublicHeader/LayoutMgr.h) | 127 | 接口/配置/基础辅助 | [08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md) |
| [src/Framework/ImGUISlate/PublicHeader/Widgets/ImCommonWidgets.h](../../src/Framework/ImGUISlate/PublicHeader/Widgets/ImCommonWidgets.h) | 91 | 接口/配置/基础辅助 | [08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md) |
| [src/Framework/ImGUISlate/PublicHeader/Widgets/ImFileDialogWidget.h](../../src/Framework/ImGUISlate/PublicHeader/Widgets/ImFileDialogWidget.h) | 127 | 接口/配置/基础辅助 | [08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md) |

## src/Framework/RenderCore（28 个文件）

| 源码/配置 | 行数 | 检索标记 | 阅读入口 |
| --- | ---: | --- | --- |
| [src/Framework/RenderCore/CMakeLists.txt](../../src/Framework/RenderCore/CMakeLists.txt) | 70 | 接口/配置/基础辅助 | [09-module-build-and-startup](performance/09-module-build-and-startup.md) |
| [src/Framework/RenderCore/Private/GlobalShader.cpp](../../src/Framework/RenderCore/Private/GlobalShader.cpp) | 121 | 渲染 | [06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md) |
| [src/Framework/RenderCore/Private/RenderGraphBuilder.cpp](../../src/Framework/RenderCore/Private/RenderGraphBuilder.cpp) | 1352 | 并发、内存、渲染 | [03-rdg-execution-and-state](performance/03-rdg-execution-and-state.md) |
| [src/Framework/RenderCore/Private/RenderGraphPass.cpp](../../src/Framework/RenderCore/Private/RenderGraphPass.cpp) | 79 | 接口/配置/基础辅助 | [03-rdg-execution-and-state](performance/03-rdg-execution-and-state.md) |
| [src/Framework/RenderCore/Private/RenderGraphResource.cpp](../../src/Framework/RenderCore/Private/RenderGraphResource.cpp) | 122 | 接口/配置/基础辅助 | [03-rdg-execution-and-state](performance/03-rdg-execution-and-state.md) |
| [src/Framework/RenderCore/Private/RenderInterface.cpp](../../src/Framework/RenderCore/Private/RenderInterface.cpp) | 5 | 接口/配置/基础辅助 | [06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md) |
| [src/Framework/RenderCore/Private/RenderResource.cpp](../../src/Framework/RenderCore/Private/RenderResource.cpp) | 902 | 并发、内存、渲染 | [04-memory-and-transient-resources](performance/04-memory-and-transient-resources.md)；[03-rdg-execution-and-state](performance/03-rdg-execution-and-state.md) |
| [src/Framework/RenderCore/Private/RenderThread.cpp](../../src/Framework/RenderCore/Private/RenderThread.cpp) | 132 | 并发 | [01-task-and-render-threads](performance/01-task-and-render-threads.md) |
| [src/Framework/RenderCore/Private/Shader.cpp](../../src/Framework/RenderCore/Private/Shader.cpp) | 206 | 内存、渲染 | [06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md) |
| [src/Framework/RenderCore/Private/ShaderCompiler.cpp](../../src/Framework/RenderCore/Private/ShaderCompiler.cpp) | 2116 | 内存、渲染 | [06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md) |
| [src/Framework/RenderCore/Private/ShaderParameter.cpp](../../src/Framework/RenderCore/Private/ShaderParameter.cpp) | 198 | 渲染 | [06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md) |
| [src/Framework/RenderCore/Private/ToolShaders.cpp](../../src/Framework/RenderCore/Private/ToolShaders.cpp) | 13 | 渲染 | [06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md) |
| [src/Framework/RenderCore/Private/VertexFactory.cpp](../../src/Framework/RenderCore/Private/VertexFactory.cpp) | 105 | 渲染 | [08-mesh-import-and-geometry](algorithm/08-mesh-import-and-geometry.md)；[06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md) |
| [src/Framework/RenderCore/PublicHeader/GlobalShader.h](../../src/Framework/RenderCore/PublicHeader/GlobalShader.h) | 100 | 渲染 | [06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md) |
| [src/Framework/RenderCore/PublicHeader/RenderGraphBuilder.h](../../src/Framework/RenderCore/PublicHeader/RenderGraphBuilder.h) | 324 | 并发、内存、渲染 | [03-rdg-execution-and-state](performance/03-rdg-execution-and-state.md) |
| [src/Framework/RenderCore/PublicHeader/RenderGraphPass.h](../../src/Framework/RenderCore/PublicHeader/RenderGraphPass.h) | 221 | 渲染 | [03-rdg-execution-and-state](performance/03-rdg-execution-and-state.md) |
| [src/Framework/RenderCore/PublicHeader/RenderGraphResource.h](../../src/Framework/RenderCore/PublicHeader/RenderGraphResource.h) | 238 | 渲染 | [03-rdg-execution-and-state](performance/03-rdg-execution-and-state.md) |
| [src/Framework/RenderCore/PublicHeader/RenderInterface.h](../../src/Framework/RenderCore/PublicHeader/RenderInterface.h) | 31 | 接口/配置/基础辅助 | [06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md) |
| [src/Framework/RenderCore/PublicHeader/RenderResource.h](../../src/Framework/RenderCore/PublicHeader/RenderResource.h) | 558 | 并发、内存、渲染 | [04-memory-and-transient-resources](performance/04-memory-and-transient-resources.md)；[03-rdg-execution-and-state](performance/03-rdg-execution-and-state.md) |
| [src/Framework/RenderCore/PublicHeader/RenderResourceTracker.h](../../src/Framework/RenderCore/PublicHeader/RenderResourceTracker.h) | 230 | 内存 | [04-memory-and-transient-resources](performance/04-memory-and-transient-resources.md)；[03-rdg-execution-and-state](performance/03-rdg-execution-and-state.md) |
| [src/Framework/RenderCore/PublicHeader/RenderThread.h](../../src/Framework/RenderCore/PublicHeader/RenderThread.h) | 70 | 并发 | [01-task-and-render-threads](performance/01-task-and-render-threads.md) |
| [src/Framework/RenderCore/PublicHeader/Shader.h](../../src/Framework/RenderCore/PublicHeader/Shader.h) | 429 | 内存、渲染 | [06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md) |
| [src/Framework/RenderCore/PublicHeader/ShaderCompiledDataPacker.h](../../src/Framework/RenderCore/PublicHeader/ShaderCompiledDataPacker.h) | 228 | 渲染 | [06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md) |
| [src/Framework/RenderCore/PublicHeader/ShaderCompiler.h](../../src/Framework/RenderCore/PublicHeader/ShaderCompiler.h) | 234 | 内存、渲染 | [06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md) |
| [src/Framework/RenderCore/PublicHeader/ShaderCore.h](../../src/Framework/RenderCore/PublicHeader/ShaderCore.h) | 210 | 渲染 | [06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md) |
| [src/Framework/RenderCore/PublicHeader/ShaderParameter.h](../../src/Framework/RenderCore/PublicHeader/ShaderParameter.h) | 854 | 内存、渲染 | [06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md) |
| [src/Framework/RenderCore/PublicHeader/ToolShaders.h](../../src/Framework/RenderCore/PublicHeader/ToolShaders.h) | 58 | 渲染 | [06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md) |
| [src/Framework/RenderCore/PublicHeader/VertexFactory.h](../../src/Framework/RenderCore/PublicHeader/VertexFactory.h) | 195 | 渲染 | [08-mesh-import-and-geometry](algorithm/08-mesh-import-and-geometry.md)；[06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md) |

## src/Framework/Renderer（47 个文件）

| 源码/配置 | 行数 | 检索标记 | 阅读入口 |
| --- | ---: | --- | --- |
| [src/Framework/Renderer/CMakeLists.txt](../../src/Framework/Renderer/CMakeLists.txt) | 65 | 接口/配置/基础辅助 | [09-module-build-and-startup](performance/09-module-build-and-startup.md) |
| [src/Framework/Renderer/Private/DefferedSceneRenderer.cpp](../../src/Framework/Renderer/Private/DefferedSceneRenderer.cpp) | 279 | 渲染 | [03-rdg-execution-and-state](performance/03-rdg-execution-and-state.md)；[架构：renderer](../architecture/render/renderer.md) |
| [src/Framework/Renderer/Private/DefferedSceneRenderer.h](../../src/Framework/Renderer/Private/DefferedSceneRenderer.h) | 9 | 接口/配置/基础辅助 | [03-rdg-execution-and-state](performance/03-rdg-execution-and-state.md)；[架构：renderer](../architecture/render/renderer.md) |
| [src/Framework/Renderer/Private/DepthPyramidGenerate.cpp](../../src/Framework/Renderer/Private/DepthPyramidGenerate.cpp) | 121 | 渲染 | [07-screen-space-and-antialiasing](algorithm/07-screen-space-and-antialiasing.md) |
| [src/Framework/Renderer/Private/DepthPyramidGenerate.h](../../src/Framework/Renderer/Private/DepthPyramidGenerate.h) | 51 | 渲染 | [07-screen-space-and-antialiasing](algorithm/07-screen-space-and-antialiasing.md) |
| [src/Framework/Renderer/Private/DrawSkyPass.cpp](../../src/Framework/Renderer/Private/DrawSkyPass.cpp) | 170 | 渲染 | [05-pbr-ibl-and-sampling](algorithm/05-pbr-ibl-and-sampling.md)；[06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md) |
| [src/Framework/Renderer/Private/DrawSkyPass.h](../../src/Framework/Renderer/Private/DrawSkyPass.h) | 67 | 渲染 | [05-pbr-ibl-and-sampling](algorithm/05-pbr-ibl-and-sampling.md)；[06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md) |
| [src/Framework/Renderer/Private/ForwardSceneRenderer.cpp](../../src/Framework/Renderer/Private/ForwardSceneRenderer.cpp) | 220 | 渲染 | [03-rdg-execution-and-state](performance/03-rdg-execution-and-state.md)；[架构：renderer](../architecture/render/renderer.md) |
| [src/Framework/Renderer/Private/ForwardSceneRenderer.h](../../src/Framework/Renderer/Private/ForwardSceneRenderer.h) | 9 | 接口/配置/基础辅助 | [03-rdg-execution-and-state](performance/03-rdg-execution-and-state.md)；[架构：renderer](../architecture/render/renderer.md) |
| [src/Framework/Renderer/Private/FrustumCullPass.cpp](../../src/Framework/Renderer/Private/FrustumCullPass.cpp) | 185 | 并发、算法、渲染 | [03-transforms-frustum-and-depth](algorithm/03-transforms-frustum-and-depth.md) |
| [src/Framework/Renderer/Private/FrustumCullPass.h](../../src/Framework/Renderer/Private/FrustumCullPass.h) | 57 | 算法、渲染 | [03-transforms-frustum-and-depth](algorithm/03-transforms-frustum-and-depth.md) |
| [src/Framework/Renderer/Private/GBufferInfo.cpp](../../src/Framework/Renderer/Private/GBufferInfo.cpp) | 124 | 接口/配置/基础辅助 | [05-pbr-ibl-and-sampling](algorithm/05-pbr-ibl-and-sampling.md)；[06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md) |
| [src/Framework/Renderer/Private/GBufferInfo.h](../../src/Framework/Renderer/Private/GBufferInfo.h) | 211 | 渲染 | [05-pbr-ibl-and-sampling](algorithm/05-pbr-ibl-and-sampling.md)；[06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md) |
| [src/Framework/Renderer/Private/GI/GlobalDiffuseLightMap.cpp](../../src/Framework/Renderer/Private/GI/GlobalDiffuseLightMap.cpp) | 479 | 内存、算法、渲染 | [04-distance-fields-and-jump-flood](algorithm/04-distance-fields-and-jump-flood.md) |
| [src/Framework/Renderer/Private/GI/GlobalDiffuseLightMap.h](../../src/Framework/Renderer/Private/GI/GlobalDiffuseLightMap.h) | 294 | 内存、算法、渲染 | [04-distance-fields-and-jump-flood](algorithm/04-distance-fields-and-jump-flood.md) |
| [src/Framework/Renderer/Private/GI/GlobalLightProbe.h](../../src/Framework/Renderer/Private/GI/GlobalLightProbe.h) | 53 | 接口/配置/基础辅助 | [04-distance-fields-and-jump-flood](algorithm/04-distance-fields-and-jump-flood.md) |
| [src/Framework/Renderer/Private/GI/ScreenSpaceGI.cpp](../../src/Framework/Renderer/Private/GI/ScreenSpaceGI.cpp) | 94 | 渲染 | [07-screen-space-and-antialiasing](algorithm/07-screen-space-and-antialiasing.md) |
| [src/Framework/Renderer/Private/GI/ScreenSpaceGI.h](../../src/Framework/Renderer/Private/GI/ScreenSpaceGI.h) | 72 | 渲染 | [07-screen-space-and-antialiasing](algorithm/07-screen-space-and-antialiasing.md) |
| [src/Framework/Renderer/Private/GlobalDistanceField.cpp](../../src/Framework/Renderer/Private/GlobalDistanceField.cpp) | 692 | 内存、渲染 | [04-distance-fields-and-jump-flood](algorithm/04-distance-fields-and-jump-flood.md) |
| [src/Framework/Renderer/Private/GlobalDistanceField.h](../../src/Framework/Renderer/Private/GlobalDistanceField.h) | 362 | 内存、渲染 | [04-distance-fields-and-jump-flood](algorithm/04-distance-fields-and-jump-flood.md) |
| [src/Framework/Renderer/Private/HZBOcclusionTest.cpp](../../src/Framework/Renderer/Private/HZBOcclusionTest.cpp) | 263 | 并发、渲染 | [07-screen-space-and-antialiasing](algorithm/07-screen-space-and-antialiasing.md) |
| [src/Framework/Renderer/Private/HZBOcclusionTest.h](../../src/Framework/Renderer/Private/HZBOcclusionTest.h) | 83 | 渲染 | [07-screen-space-and-antialiasing](algorithm/07-screen-space-and-antialiasing.md) |
| [src/Framework/Renderer/Private/IBLPrecomputeShader.cpp](../../src/Framework/Renderer/Private/IBLPrecomputeShader.cpp) | 19 | 渲染 | [05-pbr-ibl-and-sampling](algorithm/05-pbr-ibl-and-sampling.md)；[06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md) |
| [src/Framework/Renderer/Private/IBLPrecomputeShader.h](../../src/Framework/Renderer/Private/IBLPrecomputeShader.h) | 67 | 渲染 | [05-pbr-ibl-and-sampling](algorithm/05-pbr-ibl-and-sampling.md)；[06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md) |
| [src/Framework/Renderer/Private/MeshMaterialShader.cpp](../../src/Framework/Renderer/Private/MeshMaterialShader.cpp) | 13 | 渲染 | [05-pbr-ibl-and-sampling](algorithm/05-pbr-ibl-and-sampling.md)；[06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md) |
| [src/Framework/Renderer/Private/PostProcess/AntiAliasingPostProcess.cpp](../../src/Framework/Renderer/Private/PostProcess/AntiAliasingPostProcess.cpp) | 21 | 渲染 | [07-screen-space-and-antialiasing](algorithm/07-screen-space-and-antialiasing.md) |
| [src/Framework/Renderer/Private/PostProcess/AntiAliasingPostProcess.h](../../src/Framework/Renderer/Private/PostProcess/AntiAliasingPostProcess.h) | 66 | 渲染 | [07-screen-space-and-antialiasing](algorithm/07-screen-space-and-antialiasing.md) |
| [src/Framework/Renderer/Private/RenderModule.cpp](../../src/Framework/Renderer/Private/RenderModule.cpp) | 308 | 内存、算法、渲染 | [03-rdg-execution-and-state](performance/03-rdg-execution-and-state.md)；[架构：renderer](../architecture/render/renderer.md) |
| [src/Framework/Renderer/Private/Scene.cpp](../../src/Framework/Renderer/Private/Scene.cpp) | 1079 | 内存、算法、渲染 | [02-bvh-octree-and-scene-query](algorithm/02-bvh-octree-and-scene-query.md)；[05-assets-materials-and-instance-data](performance/05-assets-materials-and-instance-data.md) |
| [src/Framework/Renderer/Private/Scene.h](../../src/Framework/Renderer/Private/Scene.h) | 329 | 并发、内存 | [02-bvh-octree-and-scene-query](algorithm/02-bvh-octree-and-scene-query.md)；[05-assets-materials-and-instance-data](performance/05-assets-materials-and-instance-data.md) |
| [src/Framework/Renderer/Private/SceneRenderer.cpp](../../src/Framework/Renderer/Private/SceneRenderer.cpp) | 894 | 内存、算法、渲染 | [03-rdg-execution-and-state](performance/03-rdg-execution-and-state.md)；[架构：renderer](../architecture/render/renderer.md) |
| [src/Framework/Renderer/Private/SceneRenderer.h](../../src/Framework/Renderer/Private/SceneRenderer.h) | 80 | 内存、渲染 | [03-rdg-execution-and-state](performance/03-rdg-execution-and-state.md)；[架构：renderer](../architecture/render/renderer.md) |
| [src/Framework/Renderer/Private/SceneShaderParameters.cpp](../../src/Framework/Renderer/Private/SceneShaderParameters.cpp) | 167 | 内存、渲染 | [05-pbr-ibl-and-sampling](algorithm/05-pbr-ibl-and-sampling.md)；[06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md) |
| [src/Framework/Renderer/Private/SceneShaderParameters.h](../../src/Framework/Renderer/Private/SceneShaderParameters.h) | 127 | 渲染 | [05-pbr-ibl-and-sampling](algorithm/05-pbr-ibl-and-sampling.md)；[06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md) |
| [src/Framework/Renderer/Private/ScreenPass.cpp](../../src/Framework/Renderer/Private/ScreenPass.cpp) | 38 | 渲染 | [03-rdg-execution-and-state](performance/03-rdg-execution-and-state.md)；[架构：renderer](../architecture/render/renderer.md) |
| [src/Framework/Renderer/Private/ScreenSpaceReflection.cpp](../../src/Framework/Renderer/Private/ScreenSpaceReflection.cpp) | 199 | 渲染 | [07-screen-space-and-antialiasing](algorithm/07-screen-space-and-antialiasing.md) |
| [src/Framework/Renderer/Private/ScreenSpaceReflection.h](../../src/Framework/Renderer/Private/ScreenSpaceReflection.h) | 117 | 渲染 | [07-screen-space-and-antialiasing](algorithm/07-screen-space-and-antialiasing.md) |
| [src/Framework/Renderer/Private/ShadowMapAllocator.cpp](../../src/Framework/Renderer/Private/ShadowMapAllocator.cpp) | 385 | 内存、渲染 | [06-shadow-mapping-and-cascades](algorithm/06-shadow-mapping-and-cascades.md) |
| [src/Framework/Renderer/Private/ShadowMapAllocator.h](../../src/Framework/Renderer/Private/ShadowMapAllocator.h) | 194 | 内存、渲染 | [06-shadow-mapping-and-cascades](algorithm/06-shadow-mapping-and-cascades.md) |
| [src/Framework/Renderer/Private/StaticMeshMaterialShader.cpp](../../src/Framework/Renderer/Private/StaticMeshMaterialShader.cpp) | 296 | 渲染 | [05-pbr-ibl-and-sampling](algorithm/05-pbr-ibl-and-sampling.md)；[06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md) |
| [src/Framework/Renderer/Private/StaticMeshMaterialShader.h](../../src/Framework/Renderer/Private/StaticMeshMaterialShader.h) | 204 | 渲染 | [05-pbr-ibl-and-sampling](algorithm/05-pbr-ibl-and-sampling.md)；[06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md) |
| [src/Framework/Renderer/Private/StaticMeshProcess.cpp](../../src/Framework/Renderer/Private/StaticMeshProcess.cpp) | 324 | 渲染 | [08-mesh-import-and-geometry](algorithm/08-mesh-import-and-geometry.md)；[05-assets-materials-and-instance-data](performance/05-assets-materials-and-instance-data.md) |
| [src/Framework/Renderer/PublicHeader/MateiralShader.h](../../src/Framework/Renderer/PublicHeader/MateiralShader.h) | 72 | 渲染 | [03-rdg-execution-and-state](performance/03-rdg-execution-and-state.md)；[架构：renderer](../architecture/render/renderer.md) |
| [src/Framework/Renderer/PublicHeader/MeshMaterialShader.h](../../src/Framework/Renderer/PublicHeader/MeshMaterialShader.h) | 78 | 渲染 | [05-pbr-ibl-and-sampling](algorithm/05-pbr-ibl-and-sampling.md)；[06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md) |
| [src/Framework/Renderer/PublicHeader/RenderModule.h](../../src/Framework/Renderer/PublicHeader/RenderModule.h) | 34 | 接口/配置/基础辅助 | [03-rdg-execution-and-state](performance/03-rdg-execution-and-state.md)；[架构：renderer](../architecture/render/renderer.md) |
| [src/Framework/Renderer/PublicHeader/ScreenPass.h](../../src/Framework/Renderer/PublicHeader/ScreenPass.h) | 11 | 渲染 | [03-rdg-execution-and-state](performance/03-rdg-execution-and-state.md)；[架构：renderer](../architecture/render/renderer.md) |
| [src/Framework/Renderer/PublicHeader/StaticMeshProcess.h](../../src/Framework/Renderer/PublicHeader/StaticMeshProcess.h) | 67 | 渲染 | [08-mesh-import-and-geometry](algorithm/08-mesh-import-and-geometry.md)；[05-assets-materials-and-instance-data](performance/05-assets-materials-and-instance-data.md) |

## src/Framework/RHI（24 个文件）

| 源码/配置 | 行数 | 检索标记 | 阅读入口 |
| --- | ---: | --- | --- |
| [src/Framework/RHI/CMakeLists.txt](../../src/Framework/RHI/CMakeLists.txt) | 69 | 接口/配置/基础辅助 | [09-module-build-and-startup](performance/09-module-build-and-startup.md) |
| [src/Framework/RHI/Private/RHIApi.cpp](../../src/Framework/RHI/Private/RHIApi.cpp) | 29 | 渲染 | [02-rhi-queues-and-sync](performance/02-rhi-queues-and-sync.md)；[03-rdg-execution-and-state](performance/03-rdg-execution-and-state.md) |
| [src/Framework/RHI/Private/RHICaptureHelper.cpp](../../src/Framework/RHI/Private/RHICaptureHelper.cpp) | 156 | 接口/配置/基础辅助 | [09-module-build-and-startup](performance/09-module-build-and-startup.md)；[02-rhi-queues-and-sync](performance/02-rhi-queues-and-sync.md) |
| [src/Framework/RHI/Private/RHICommandContex.cpp](../../src/Framework/RHI/Private/RHICommandContex.cpp) | 6 | 接口/配置/基础辅助 | [02-rhi-queues-and-sync](performance/02-rhi-queues-and-sync.md)；[03-rdg-execution-and-state](performance/03-rdg-execution-and-state.md) |
| [src/Framework/RHI/Private/RHICommandList.cpp](../../src/Framework/RHI/Private/RHICommandList.cpp) | 484 | 渲染 | [02-rhi-queues-and-sync](performance/02-rhi-queues-and-sync.md)；[03-rdg-execution-and-state](performance/03-rdg-execution-and-state.md) |
| [src/Framework/RHI/Private/RHIPipelineStateCache.cpp](../../src/Framework/RHI/Private/RHIPipelineStateCache.cpp) | 213 | 并发、渲染 | [06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md)；[07-ray-tracing-as-and-sbt](performance/07-ray-tracing-as-and-sbt.md) |
| [src/Framework/RHI/Private/RHIShaderLibrary.cpp](../../src/Framework/RHI/Private/RHIShaderLibrary.cpp) | 9 | 渲染 | [06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md)；[07-ray-tracing-as-and-sbt](performance/07-ray-tracing-as-and-sbt.md) |
| [src/Framework/RHI/Private/RHITransientResource.cpp](../../src/Framework/RHI/Private/RHITransientResource.cpp) | 345 | 并发、内存 | [04-memory-and-transient-resources](performance/04-memory-and-transient-resources.md) |
| [src/Framework/RHI/Private/RHITransition.cpp](../../src/Framework/RHI/Private/RHITransition.cpp) | 7 | 接口/配置/基础辅助 | [02-rhi-queues-and-sync](performance/02-rhi-queues-and-sync.md)；[03-rdg-execution-and-state](performance/03-rdg-execution-and-state.md) |
| [src/Framework/RHI/Private/RHIUtils.cpp](../../src/Framework/RHI/Private/RHIUtils.cpp) | 48 | 接口/配置/基础辅助 | [02-rhi-queues-and-sync](performance/02-rhi-queues-and-sync.md)；[03-rdg-execution-and-state](performance/03-rdg-execution-and-state.md) |
| [src/Framework/RHI/Private/ShaderLibrary.cpp](../../src/Framework/RHI/Private/ShaderLibrary.cpp) | 0 | 接口/配置/基础辅助 | [06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md)；[07-ray-tracing-as-and-sbt](performance/07-ray-tracing-as-and-sbt.md) |
| [src/Framework/RHI/PublicHeader/RHIApi.h](../../src/Framework/RHI/PublicHeader/RHIApi.h) | 110 | 渲染 | [02-rhi-queues-and-sync](performance/02-rhi-queues-and-sync.md)；[03-rdg-execution-and-state](performance/03-rdg-execution-and-state.md) |
| [src/Framework/RHI/PublicHeader/RHICaptureHelper.h](../../src/Framework/RHI/PublicHeader/RHICaptureHelper.h) | 47 | 接口/配置/基础辅助 | [09-module-build-and-startup](performance/09-module-build-and-startup.md)；[02-rhi-queues-and-sync](performance/02-rhi-queues-and-sync.md) |
| [src/Framework/RHI/PublicHeader/RHICommandContex.h](../../src/Framework/RHI/PublicHeader/RHICommandContex.h) | 202 | 并发、渲染 | [02-rhi-queues-and-sync](performance/02-rhi-queues-and-sync.md)；[03-rdg-execution-and-state](performance/03-rdg-execution-and-state.md) |
| [src/Framework/RHI/PublicHeader/RHICommandList.h](../../src/Framework/RHI/PublicHeader/RHICommandList.h) | 311 | 渲染 | [02-rhi-queues-and-sync](performance/02-rhi-queues-and-sync.md)；[03-rdg-execution-and-state](performance/03-rdg-execution-and-state.md) |
| [src/Framework/RHI/PublicHeader/RHIDefine.h](../../src/Framework/RHI/PublicHeader/RHIDefine.h) | 881 | 算法、渲染 | [02-rhi-queues-and-sync](performance/02-rhi-queues-and-sync.md)；[03-rdg-execution-and-state](performance/03-rdg-execution-and-state.md) |
| [src/Framework/RHI/PublicHeader/RHIModule.h](../../src/Framework/RHI/PublicHeader/RHIModule.h) | 0 | 接口/配置/基础辅助 | [02-rhi-queues-and-sync](performance/02-rhi-queues-and-sync.md)；[03-rdg-execution-and-state](performance/03-rdg-execution-and-state.md) |
| [src/Framework/RHI/PublicHeader/RHIPipelineStateCache.h](../../src/Framework/RHI/PublicHeader/RHIPipelineStateCache.h) | 58 | 并发、渲染 | [06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md)；[07-ray-tracing-as-and-sbt](performance/07-ray-tracing-as-and-sbt.md) |
| [src/Framework/RHI/PublicHeader/RHIResource.h](../../src/Framework/RHI/PublicHeader/RHIResource.h) | 824 | 内存、渲染 | [04-memory-and-transient-resources](performance/04-memory-and-transient-resources.md) |
| [src/Framework/RHI/PublicHeader/RHIShaderLibrary.h](../../src/Framework/RHI/PublicHeader/RHIShaderLibrary.h) | 40 | 并发、渲染 | [06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md)；[07-ray-tracing-as-and-sbt](performance/07-ray-tracing-as-and-sbt.md) |
| [src/Framework/RHI/PublicHeader/RHIShaderParameter.h](../../src/Framework/RHI/PublicHeader/RHIShaderParameter.h) | 66 | 渲染 | [06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md)；[07-ray-tracing-as-and-sbt](performance/07-ray-tracing-as-and-sbt.md) |
| [src/Framework/RHI/PublicHeader/RHITransientResource.h](../../src/Framework/RHI/PublicHeader/RHITransientResource.h) | 153 | 并发、内存 | [04-memory-and-transient-resources](performance/04-memory-and-transient-resources.md) |
| [src/Framework/RHI/PublicHeader/RHITransition.h](../../src/Framework/RHI/PublicHeader/RHITransition.h) | 164 | 并发、算法 | [02-rhi-queues-and-sync](performance/02-rhi-queues-and-sync.md)；[03-rdg-execution-and-state](performance/03-rdg-execution-and-state.md) |
| [src/Framework/RHI/PublicHeader/RHIUtils.h](../../src/Framework/RHI/PublicHeader/RHIUtils.h) | 13 | 接口/配置/基础辅助 | [02-rhi-queues-and-sync](performance/02-rhi-queues-and-sync.md)；[03-rdg-execution-and-state](performance/03-rdg-execution-and-state.md) |

## src/Framework/RHIDirectX12（19 个文件）

| 源码/配置 | 行数 | 检索标记 | 阅读入口 |
| --- | ---: | --- | --- |
| [src/Framework/RHIDirectX12/CMakeLists.txt](../../src/Framework/RHIDirectX12/CMakeLists.txt) | 54 | 接口/配置/基础辅助 | [09-module-build-and-startup](performance/09-module-build-and-startup.md) |
| [src/Framework/RHIDirectX12/Private/DirectX12Context.cpp](../../src/Framework/RHIDirectX12/Private/DirectX12Context.cpp) | 1098 | 并发、内存、算法、渲染 | [02-rhi-queues-and-sync](performance/02-rhi-queues-and-sync.md)；[03-rdg-execution-and-state](performance/03-rdg-execution-and-state.md) |
| [src/Framework/RHIDirectX12/Private/DirectX12Device.cpp](../../src/Framework/RHIDirectX12/Private/DirectX12Device.cpp) | 334 | 并发、内存、渲染 | [02-rhi-queues-and-sync](performance/02-rhi-queues-and-sync.md)；[03-rdg-execution-and-state](performance/03-rdg-execution-and-state.md) |
| [src/Framework/RHIDirectX12/Private/DirectX12PipelineState.cpp](../../src/Framework/RHIDirectX12/Private/DirectX12PipelineState.cpp) | 424 | 渲染 | [06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md)；[07-ray-tracing-as-and-sbt](performance/07-ray-tracing-as-and-sbt.md) |
| [src/Framework/RHIDirectX12/Private/DirectX12Queue.cpp](../../src/Framework/RHIDirectX12/Private/DirectX12Queue.cpp) | 218 | 并发 | [02-rhi-queues-and-sync](performance/02-rhi-queues-and-sync.md)；[03-rdg-execution-and-state](performance/03-rdg-execution-and-state.md) |
| [src/Framework/RHIDirectX12/Private/DirectX12Resource.cpp](../../src/Framework/RHIDirectX12/Private/DirectX12Resource.cpp) | 965 | 并发、内存、渲染 | [04-memory-and-transient-resources](performance/04-memory-and-transient-resources.md)；[07-ray-tracing-as-and-sbt](performance/07-ray-tracing-as-and-sbt.md) |
| [src/Framework/RHIDirectX12/Private/DirectX12RHIApi.cpp](../../src/Framework/RHIDirectX12/Private/DirectX12RHIApi.cpp) | 581 | 并发、内存、算法、渲染 | [02-rhi-queues-and-sync](performance/02-rhi-queues-and-sync.md)；[03-rdg-execution-and-state](performance/03-rdg-execution-and-state.md) |
| [src/Framework/RHIDirectX12/Private/DirectX12RHIUtils.cpp](../../src/Framework/RHIDirectX12/Private/DirectX12RHIUtils.cpp) | 503 | 内存、渲染 | [02-rhi-queues-and-sync](performance/02-rhi-queues-and-sync.md)；[03-rdg-execution-and-state](performance/03-rdg-execution-and-state.md) |
| [src/Framework/RHIDirectX12/Private/DirectX12RHIUtils.h](../../src/Framework/RHIDirectX12/Private/DirectX12RHIUtils.h) | 105 | 内存、渲染 | [02-rhi-queues-and-sync](performance/02-rhi-queues-and-sync.md)；[03-rdg-execution-and-state](performance/03-rdg-execution-and-state.md) |
| [src/Framework/RHIDirectX12/Private/DirectX12ShaderAccess.cpp](../../src/Framework/RHIDirectX12/Private/DirectX12ShaderAccess.cpp) | 117 | 渲染 | [06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md)；[07-ray-tracing-as-and-sbt](performance/07-ray-tracing-as-and-sbt.md) |
| [src/Framework/RHIDirectX12/Private/DirectX12ShaderAccess.h](../../src/Framework/RHIDirectX12/Private/DirectX12ShaderAccess.h) | 12 | 接口/配置/基础辅助 | [06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md)；[07-ray-tracing-as-and-sbt](performance/07-ray-tracing-as-and-sbt.md) |
| [src/Framework/RHIDirectX12/Private/DirectX12TransientResource.cpp](../../src/Framework/RHIDirectX12/Private/DirectX12TransientResource.cpp) | 43 | 接口/配置/基础辅助 | [04-memory-and-transient-resources](performance/04-memory-and-transient-resources.md)；[07-ray-tracing-as-and-sbt](performance/07-ray-tracing-as-and-sbt.md) |
| [src/Framework/RHIDirectX12/Private/DirectX12TransientResource.h](../../src/Framework/RHIDirectX12/Private/DirectX12TransientResource.h) | 26 | 接口/配置/基础辅助 | [04-memory-and-transient-resources](performance/04-memory-and-transient-resources.md)；[07-ray-tracing-as-and-sbt](performance/07-ray-tracing-as-and-sbt.md) |
| [src/Framework/RHIDirectX12/PublicHeader/DirectX12Context.h](../../src/Framework/RHIDirectX12/PublicHeader/DirectX12Context.h) | 149 | 并发、内存、渲染 | [02-rhi-queues-and-sync](performance/02-rhi-queues-and-sync.md)；[03-rdg-execution-and-state](performance/03-rdg-execution-and-state.md) |
| [src/Framework/RHIDirectX12/PublicHeader/DirectX12Device.h](../../src/Framework/RHIDirectX12/PublicHeader/DirectX12Device.h) | 121 | 并发、内存、渲染 | [02-rhi-queues-and-sync](performance/02-rhi-queues-and-sync.md)；[03-rdg-execution-and-state](performance/03-rdg-execution-and-state.md) |
| [src/Framework/RHIDirectX12/PublicHeader/DirectX12PipelineState.h](../../src/Framework/RHIDirectX12/PublicHeader/DirectX12PipelineState.h) | 106 | 渲染 | [06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md)；[07-ray-tracing-as-and-sbt](performance/07-ray-tracing-as-and-sbt.md) |
| [src/Framework/RHIDirectX12/PublicHeader/DirectX12Queue.h](../../src/Framework/RHIDirectX12/PublicHeader/DirectX12Queue.h) | 98 | 并发 | [02-rhi-queues-and-sync](performance/02-rhi-queues-and-sync.md)；[03-rdg-execution-and-state](performance/03-rdg-execution-and-state.md) |
| [src/Framework/RHIDirectX12/PublicHeader/DirectX12Resource.h](../../src/Framework/RHIDirectX12/PublicHeader/DirectX12Resource.h) | 458 | 并发、内存、渲染 | [04-memory-and-transient-resources](performance/04-memory-and-transient-resources.md)；[07-ray-tracing-as-and-sbt](performance/07-ray-tracing-as-and-sbt.md) |
| [src/Framework/RHIDirectX12/PublicHeader/DirectX12RHIApi.h](../../src/Framework/RHIDirectX12/PublicHeader/DirectX12RHIApi.h) | 122 | 渲染 | [02-rhi-queues-and-sync](performance/02-rhi-queues-and-sync.md)；[03-rdg-execution-and-state](performance/03-rdg-execution-and-state.md) |

## src/Framework/RHIOpenGL（19 个文件）

| 源码/配置 | 行数 | 检索标记 | 阅读入口 |
| --- | ---: | --- | --- |
| [src/Framework/RHIOpenGL/CMakeLists.txt](../../src/Framework/RHIOpenGL/CMakeLists.txt) | 67 | 接口/配置/基础辅助 | [09-module-build-and-startup](performance/09-module-build-and-startup.md) |
| [src/Framework/RHIOpenGL/Private/OpenGLContext.cpp](../../src/Framework/RHIOpenGL/Private/OpenGLContext.cpp) | 847 | 内存、渲染 | [02-rhi-queues-and-sync](performance/02-rhi-queues-and-sync.md)；[03-rdg-execution-and-state](performance/03-rdg-execution-and-state.md) |
| [src/Framework/RHIOpenGL/Private/OpenGLPipelineState.cpp](../../src/Framework/RHIOpenGL/Private/OpenGLPipelineState.cpp) | 110 | 并发、渲染 | [06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md)；[07-ray-tracing-as-and-sbt](performance/07-ray-tracing-as-and-sbt.md) |
| [src/Framework/RHIOpenGL/Private/OpenGLPlatformSurport.cpp](../../src/Framework/RHIOpenGL/Private/OpenGLPlatformSurport.cpp) | 378 | 内存、渲染 | [02-rhi-queues-and-sync](performance/02-rhi-queues-and-sync.md)；[03-rdg-execution-and-state](performance/03-rdg-execution-and-state.md) |
| [src/Framework/RHIOpenGL/Private/OpenGLPlatformSurport.h](../../src/Framework/RHIOpenGL/Private/OpenGLPlatformSurport.h) | 88 | 接口/配置/基础辅助 | [02-rhi-queues-and-sync](performance/02-rhi-queues-and-sync.md)；[03-rdg-execution-and-state](performance/03-rdg-execution-and-state.md) |
| [src/Framework/RHIOpenGL/Private/OpenGLQueue.cpp](../../src/Framework/RHIOpenGL/Private/OpenGLQueue.cpp) | 672 | 并发、内存、渲染 | [02-rhi-queues-and-sync](performance/02-rhi-queues-and-sync.md)；[03-rdg-execution-and-state](performance/03-rdg-execution-and-state.md) |
| [src/Framework/RHIOpenGL/Private/OpenGLRenderTarget.cpp](../../src/Framework/RHIOpenGL/Private/OpenGLRenderTarget.cpp) | 137 | 并发、渲染 | [02-rhi-queues-and-sync](performance/02-rhi-queues-and-sync.md)；[03-rdg-execution-and-state](performance/03-rdg-execution-and-state.md) |
| [src/Framework/RHIOpenGL/Private/OpenGLRenderTarget.h](../../src/Framework/RHIOpenGL/Private/OpenGLRenderTarget.h) | 36 | 渲染 | [02-rhi-queues-and-sync](performance/02-rhi-queues-and-sync.md)；[03-rdg-execution-and-state](performance/03-rdg-execution-and-state.md) |
| [src/Framework/RHIOpenGL/Private/OpenGLResource.cpp](../../src/Framework/RHIOpenGL/Private/OpenGLResource.cpp) | 538 | 并发、内存、渲染 | [04-memory-and-transient-resources](performance/04-memory-and-transient-resources.md) |
| [src/Framework/RHIOpenGL/Private/OpenGLRHIApi.cpp](../../src/Framework/RHIOpenGL/Private/OpenGLRHIApi.cpp) | 425 | 并发、内存、渲染 | [02-rhi-queues-and-sync](performance/02-rhi-queues-and-sync.md)；[03-rdg-execution-and-state](performance/03-rdg-execution-and-state.md) |
| [src/Framework/RHIOpenGL/Private/OpenGLTransientResource.cpp](../../src/Framework/RHIOpenGL/Private/OpenGLTransientResource.cpp) | 53 | 并发 | [04-memory-and-transient-resources](performance/04-memory-and-transient-resources.md) |
| [src/Framework/RHIOpenGL/Private/OpenGLUtils.cpp](../../src/Framework/RHIOpenGL/Private/OpenGLUtils.cpp) | 154 | 算法 | [02-rhi-queues-and-sync](performance/02-rhi-queues-and-sync.md)；[03-rdg-execution-and-state](performance/03-rdg-execution-and-state.md) |
| [src/Framework/RHIOpenGL/PublicHeader/OpenGLContext.h](../../src/Framework/RHIOpenGL/PublicHeader/OpenGLContext.h) | 316 | 内存、渲染 | [02-rhi-queues-and-sync](performance/02-rhi-queues-and-sync.md)；[03-rdg-execution-and-state](performance/03-rdg-execution-and-state.md) |
| [src/Framework/RHIOpenGL/PublicHeader/OpenGLPipelineState.h](../../src/Framework/RHIOpenGL/PublicHeader/OpenGLPipelineState.h) | 35 | 渲染 | [06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md)；[07-ray-tracing-as-and-sbt](performance/07-ray-tracing-as-and-sbt.md) |
| [src/Framework/RHIOpenGL/PublicHeader/OpenGLQueue.h](../../src/Framework/RHIOpenGL/PublicHeader/OpenGLQueue.h) | 203 | 并发、内存、渲染 | [02-rhi-queues-and-sync](performance/02-rhi-queues-and-sync.md)；[03-rdg-execution-and-state](performance/03-rdg-execution-and-state.md) |
| [src/Framework/RHIOpenGL/PublicHeader/OpenGLResource.h](../../src/Framework/RHIOpenGL/PublicHeader/OpenGLResource.h) | 319 | 渲染 | [04-memory-and-transient-resources](performance/04-memory-and-transient-resources.md) |
| [src/Framework/RHIOpenGL/PublicHeader/OpenGLRHIApi.h](../../src/Framework/RHIOpenGL/PublicHeader/OpenGLRHIApi.h) | 103 | 渲染 | [02-rhi-queues-and-sync](performance/02-rhi-queues-and-sync.md)；[03-rdg-execution-and-state](performance/03-rdg-execution-and-state.md) |
| [src/Framework/RHIOpenGL/PublicHeader/OpenGLTransientResource.h](../../src/Framework/RHIOpenGL/PublicHeader/OpenGLTransientResource.h) | 32 | 接口/配置/基础辅助 | [04-memory-and-transient-resources](performance/04-memory-and-transient-resources.md) |
| [src/Framework/RHIOpenGL/PublicHeader/OpenGLUtils.h](../../src/Framework/RHIOpenGL/PublicHeader/OpenGLUtils.h) | 16 | 接口/配置/基础辅助 | [02-rhi-queues-and-sync](performance/02-rhi-queues-and-sync.md)；[03-rdg-execution-and-state](performance/03-rdg-execution-and-state.md) |

## src/Framework/RHIVulkan（39 个文件）

| 源码/配置 | 行数 | 检索标记 | 阅读入口 |
| --- | ---: | --- | --- |
| [src/Framework/RHIVulkan/CMakeLists.txt](../../src/Framework/RHIVulkan/CMakeLists.txt) | 70 | 接口/配置/基础辅助 | [09-module-build-and-startup](performance/09-module-build-and-startup.md) |
| [src/Framework/RHIVulkan/Private/VulkanBarriers.cpp](../../src/Framework/RHIVulkan/Private/VulkanBarriers.cpp) | 637 | 渲染 | [02-rhi-queues-and-sync](performance/02-rhi-queues-and-sync.md)；[03-rdg-execution-and-state](performance/03-rdg-execution-and-state.md) |
| [src/Framework/RHIVulkan/Private/VulkanBarriers.h](../../src/Framework/RHIVulkan/Private/VulkanBarriers.h) | 120 | 渲染 | [02-rhi-queues-and-sync](performance/02-rhi-queues-and-sync.md)；[03-rdg-execution-and-state](performance/03-rdg-execution-and-state.md) |
| [src/Framework/RHIVulkan/Private/VulkanCommandBuffer.cpp](../../src/Framework/RHIVulkan/Private/VulkanCommandBuffer.cpp) | 215 | 内存 | [02-rhi-queues-and-sync](performance/02-rhi-queues-and-sync.md)；[03-rdg-execution-and-state](performance/03-rdg-execution-and-state.md) |
| [src/Framework/RHIVulkan/Private/VulkanContex.cpp](../../src/Framework/RHIVulkan/Private/VulkanContex.cpp) | 637 | 内存、渲染 | [02-rhi-queues-and-sync](performance/02-rhi-queues-and-sync.md)；[03-rdg-execution-and-state](performance/03-rdg-execution-and-state.md) |
| [src/Framework/RHIVulkan/Private/VulkanDescriptorSets.cpp](../../src/Framework/RHIVulkan/Private/VulkanDescriptorSets.cpp) | 309 | 内存、渲染 | [06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md)；[07-ray-tracing-as-and-sbt](performance/07-ray-tracing-as-and-sbt.md) |
| [src/Framework/RHIVulkan/Private/VulkanDevice.cpp](../../src/Framework/RHIVulkan/Private/VulkanDevice.cpp) | 604 | 并发、内存、渲染 | [02-rhi-queues-and-sync](performance/02-rhi-queues-and-sync.md)；[03-rdg-execution-and-state](performance/03-rdg-execution-and-state.md) |
| [src/Framework/RHIVulkan/Private/VulkanFuncWrapper.cpp](../../src/Framework/RHIVulkan/Private/VulkanFuncWrapper.cpp) | 1059 | 并发、渲染 | [02-rhi-queues-and-sync](performance/02-rhi-queues-and-sync.md)；[03-rdg-execution-and-state](performance/03-rdg-execution-and-state.md) |
| [src/Framework/RHIVulkan/Private/VulkanMemory.cpp](../../src/Framework/RHIVulkan/Private/VulkanMemory.cpp) | 350 | 并发、内存 | [04-memory-and-transient-resources](performance/04-memory-and-transient-resources.md) |
| [src/Framework/RHIVulkan/Private/VulkanPendingPipelineState.cpp](../../src/Framework/RHIVulkan/Private/VulkanPendingPipelineState.cpp) | 110 | 内存、渲染 | [06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md)；[07-ray-tracing-as-and-sbt](performance/07-ray-tracing-as-and-sbt.md) |
| [src/Framework/RHIVulkan/Private/VulkanPipelineState.cpp](../../src/Framework/RHIVulkan/Private/VulkanPipelineState.cpp) | 659 | 内存、渲染 | [06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md)；[07-ray-tracing-as-and-sbt](performance/07-ray-tracing-as-and-sbt.md) |
| [src/Framework/RHIVulkan/Private/VulkanPlatformSurport.cpp](../../src/Framework/RHIVulkan/Private/VulkanPlatformSurport.cpp) | 125 | 渲染 | [02-rhi-queues-and-sync](performance/02-rhi-queues-and-sync.md)；[03-rdg-execution-and-state](performance/03-rdg-execution-and-state.md) |
| [src/Framework/RHIVulkan/Private/VulkanQueue.cpp](../../src/Framework/RHIVulkan/Private/VulkanQueue.cpp) | 398 | 并发、内存、渲染 | [02-rhi-queues-and-sync](performance/02-rhi-queues-and-sync.md)；[03-rdg-execution-and-state](performance/03-rdg-execution-and-state.md) |
| [src/Framework/RHIVulkan/Private/VulkanRenderPass.cpp](../../src/Framework/RHIVulkan/Private/VulkanRenderPass.cpp) | 565 | 并发、内存、渲染 | [02-rhi-queues-and-sync](performance/02-rhi-queues-and-sync.md)；[03-rdg-execution-and-state](performance/03-rdg-execution-and-state.md) |
| [src/Framework/RHIVulkan/Private/VulkanResource.cpp](../../src/Framework/RHIVulkan/Private/VulkanResource.cpp) | 1619 | 内存、渲染 | [04-memory-and-transient-resources](performance/04-memory-and-transient-resources.md)；[07-ray-tracing-as-and-sbt](performance/07-ray-tracing-as-and-sbt.md) |
| [src/Framework/RHIVulkan/Private/VulkanRHIApi.cpp](../../src/Framework/RHIVulkan/Private/VulkanRHIApi.cpp) | 983 | 内存、算法、渲染 | [02-rhi-queues-and-sync](performance/02-rhi-queues-and-sync.md)；[03-rdg-execution-and-state](performance/03-rdg-execution-and-state.md) |
| [src/Framework/RHIVulkan/Private/VulkanRHIUtils.cpp](../../src/Framework/RHIVulkan/Private/VulkanRHIUtils.cpp) | 443 | 算法、渲染 | [02-rhi-queues-and-sync](performance/02-rhi-queues-and-sync.md)；[03-rdg-execution-and-state](performance/03-rdg-execution-and-state.md) |
| [src/Framework/RHIVulkan/Private/VulkanShader.cpp](../../src/Framework/RHIVulkan/Private/VulkanShader.cpp) | 46 | 渲染 | [06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md)；[07-ray-tracing-as-and-sbt](performance/07-ray-tracing-as-and-sbt.md) |
| [src/Framework/RHIVulkan/Private/VulkanSwapchain.cpp](../../src/Framework/RHIVulkan/Private/VulkanSwapchain.cpp) | 244 | 渲染 | [02-rhi-queues-and-sync](performance/02-rhi-queues-and-sync.md)；[08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md) |
| [src/Framework/RHIVulkan/Private/VulkanSync.cpp](../../src/Framework/RHIVulkan/Private/VulkanSync.cpp) | 293 | 并发、内存 | [02-rhi-queues-and-sync](performance/02-rhi-queues-and-sync.md)；[03-rdg-execution-and-state](performance/03-rdg-execution-and-state.md) |
| [src/Framework/RHIVulkan/Private/VulkanTransientResource.cpp](../../src/Framework/RHIVulkan/Private/VulkanTransientResource.cpp) | 121 | 并发、内存 | [04-memory-and-transient-resources](performance/04-memory-and-transient-resources.md)；[07-ray-tracing-as-and-sbt](performance/07-ray-tracing-as-and-sbt.md) |
| [src/Framework/RHIVulkan/PublicHeader/VulkanCommandBuffer.h](../../src/Framework/RHIVulkan/PublicHeader/VulkanCommandBuffer.h) | 118 | 内存 | [02-rhi-queues-and-sync](performance/02-rhi-queues-and-sync.md)；[03-rdg-execution-and-state](performance/03-rdg-execution-and-state.md) |
| [src/Framework/RHIVulkan/PublicHeader/VulkanCommandContex.h](../../src/Framework/RHIVulkan/PublicHeader/VulkanCommandContex.h) | 255 | 渲染 | [02-rhi-queues-and-sync](performance/02-rhi-queues-and-sync.md)；[03-rdg-execution-and-state](performance/03-rdg-execution-and-state.md) |
| [src/Framework/RHIVulkan/PublicHeader/VulkanDescriptorSets.h](../../src/Framework/RHIVulkan/PublicHeader/VulkanDescriptorSets.h) | 317 | 内存、渲染 | [06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md)；[07-ray-tracing-as-and-sbt](performance/07-ray-tracing-as-and-sbt.md) |
| [src/Framework/RHIVulkan/PublicHeader/VulkanDevice.h](../../src/Framework/RHIVulkan/PublicHeader/VulkanDevice.h) | 193 | 并发、渲染 | [02-rhi-queues-and-sync](performance/02-rhi-queues-and-sync.md)；[03-rdg-execution-and-state](performance/03-rdg-execution-and-state.md) |
| [src/Framework/RHIVulkan/PublicHeader/VulkanFuncWrapper.h](../../src/Framework/RHIVulkan/PublicHeader/VulkanFuncWrapper.h) | 199 | 并发、渲染 | [02-rhi-queues-and-sync](performance/02-rhi-queues-and-sync.md)；[03-rdg-execution-and-state](performance/03-rdg-execution-and-state.md) |
| [src/Framework/RHIVulkan/PublicHeader/VulkanMemory.h](../../src/Framework/RHIVulkan/PublicHeader/VulkanMemory.h) | 139 | 并发、内存 | [04-memory-and-transient-resources](performance/04-memory-and-transient-resources.md) |
| [src/Framework/RHIVulkan/PublicHeader/VulkanPendingPipelineState.h](../../src/Framework/RHIVulkan/PublicHeader/VulkanPendingPipelineState.h) | 1520 | 内存、渲染 | [06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md)；[07-ray-tracing-as-and-sbt](performance/07-ray-tracing-as-and-sbt.md) |
| [src/Framework/RHIVulkan/PublicHeader/VulkanPipelineState.h](../../src/Framework/RHIVulkan/PublicHeader/VulkanPipelineState.h) | 309 | 内存、渲染 | [06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md)；[07-ray-tracing-as-and-sbt](performance/07-ray-tracing-as-and-sbt.md) |
| [src/Framework/RHIVulkan/PublicHeader/VulkanPlatformSurport.h](../../src/Framework/RHIVulkan/PublicHeader/VulkanPlatformSurport.h) | 15 | 渲染 | [02-rhi-queues-and-sync](performance/02-rhi-queues-and-sync.md)；[03-rdg-execution-and-state](performance/03-rdg-execution-and-state.md) |
| [src/Framework/RHIVulkan/PublicHeader/VulkanQueue.h](../../src/Framework/RHIVulkan/PublicHeader/VulkanQueue.h) | 91 | 并发、内存 | [02-rhi-queues-and-sync](performance/02-rhi-queues-and-sync.md)；[03-rdg-execution-and-state](performance/03-rdg-execution-and-state.md) |
| [src/Framework/RHIVulkan/PublicHeader/VulkanRenderPass.h](../../src/Framework/RHIVulkan/PublicHeader/VulkanRenderPass.h) | 140 | 并发、渲染 | [02-rhi-queues-and-sync](performance/02-rhi-queues-and-sync.md)；[03-rdg-execution-and-state](performance/03-rdg-execution-and-state.md) |
| [src/Framework/RHIVulkan/PublicHeader/VulkanResource.h](../../src/Framework/RHIVulkan/PublicHeader/VulkanResource.h) | 591 | 内存、渲染 | [04-memory-and-transient-resources](performance/04-memory-and-transient-resources.md)；[07-ray-tracing-as-and-sbt](performance/07-ray-tracing-as-and-sbt.md) |
| [src/Framework/RHIVulkan/PublicHeader/VulkanRHIApi.h](../../src/Framework/RHIVulkan/PublicHeader/VulkanRHIApi.h) | 126 | 渲染 | [02-rhi-queues-and-sync](performance/02-rhi-queues-and-sync.md)；[03-rdg-execution-and-state](performance/03-rdg-execution-and-state.md) |
| [src/Framework/RHIVulkan/PublicHeader/VulkanRHIUtils.h](../../src/Framework/RHIVulkan/PublicHeader/VulkanRHIUtils.h) | 42 | 渲染 | [02-rhi-queues-and-sync](performance/02-rhi-queues-and-sync.md)；[03-rdg-execution-and-state](performance/03-rdg-execution-and-state.md) |
| [src/Framework/RHIVulkan/PublicHeader/VulkanShader.h](../../src/Framework/RHIVulkan/PublicHeader/VulkanShader.h) | 241 | 并发、渲染 | [06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md)；[07-ray-tracing-as-and-sbt](performance/07-ray-tracing-as-and-sbt.md) |
| [src/Framework/RHIVulkan/PublicHeader/VulkanSwapchain.h](../../src/Framework/RHIVulkan/PublicHeader/VulkanSwapchain.h) | 57 | 接口/配置/基础辅助 | [02-rhi-queues-and-sync](performance/02-rhi-queues-and-sync.md)；[08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md) |
| [src/Framework/RHIVulkan/PublicHeader/VulkanSync.h](../../src/Framework/RHIVulkan/PublicHeader/VulkanSync.h) | 158 | 并发、内存 | [02-rhi-queues-and-sync](performance/02-rhi-queues-and-sync.md)；[03-rdg-execution-and-state](performance/03-rdg-execution-and-state.md) |
| [src/Framework/RHIVulkan/PublicHeader/VulkanTransientResource.h](../../src/Framework/RHIVulkan/PublicHeader/VulkanTransientResource.h) | 37 | 内存 | [04-memory-and-transient-resources](performance/04-memory-and-transient-resources.md)；[07-ray-tracing-as-and-sbt](performance/07-ray-tracing-as-and-sbt.md) |

## src/Framework/SlateCore（18 个文件）

| 源码/配置 | 行数 | 检索标记 | 阅读入口 |
| --- | ---: | --- | --- |
| [src/Framework/SlateCore/CMakeLists.txt](../../src/Framework/SlateCore/CMakeLists.txt) | 65 | 接口/配置/基础辅助 | [09-module-build-and-startup](performance/09-module-build-and-startup.md) |
| [src/Framework/SlateCore/Private/ApplicationBase.cpp](../../src/Framework/SlateCore/Private/ApplicationBase.cpp) | 15 | 接口/配置/基础辅助 | [08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md) |
| [src/Framework/SlateCore/Private/EventHandler.cpp](../../src/Framework/SlateCore/Private/EventHandler.cpp) | 53 | 接口/配置/基础辅助 | [08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md) |
| [src/Framework/SlateCore/Private/PlatformSurfaceOwner.cpp](../../src/Framework/SlateCore/Private/PlatformSurfaceOwner.cpp) | 108 | 接口/配置/基础辅助 | [08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md) |
| [src/Framework/SlateCore/Private/SlateWidget.cpp](../../src/Framework/SlateCore/Private/SlateWidget.cpp) | 131 | 接口/配置/基础辅助 | [08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md) |
| [src/Framework/SlateCore/Private/Widget.cpp](../../src/Framework/SlateCore/Private/Widget.cpp) | 1 | 接口/配置/基础辅助 | [08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md) |
| [src/Framework/SlateCore/Private/Win32Surface.cpp](../../src/Framework/SlateCore/Private/Win32Surface.cpp) | 507 | 渲染 | [08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md) |
| [src/Framework/SlateCore/Private/Window.cpp](../../src/Framework/SlateCore/Private/Window.cpp) | 239 | 渲染 | [08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md) |
| [src/Framework/SlateCore/PublicHeader/ApplicationBase.h](../../src/Framework/SlateCore/PublicHeader/ApplicationBase.h) | 26 | 接口/配置/基础辅助 | [08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md) |
| [src/Framework/SlateCore/PublicHeader/EventHandler.h](../../src/Framework/SlateCore/PublicHeader/EventHandler.h) | 152 | 接口/配置/基础辅助 | [08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md) |
| [src/Framework/SlateCore/PublicHeader/PlatformSurface.h](../../src/Framework/SlateCore/PublicHeader/PlatformSurface.h) | 68 | 渲染 | [08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md) |
| [src/Framework/SlateCore/PublicHeader/PlatformSurfaceOwner.h](../../src/Framework/SlateCore/PublicHeader/PlatformSurfaceOwner.h) | 68 | 接口/配置/基础辅助 | [08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md) |
| [src/Framework/SlateCore/PublicHeader/SlateRenderer.h](../../src/Framework/SlateCore/PublicHeader/SlateRenderer.h) | 33 | 接口/配置/基础辅助 | [08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md) |
| [src/Framework/SlateCore/PublicHeader/SlateViewport.h](../../src/Framework/SlateCore/PublicHeader/SlateViewport.h) | 22 | 接口/配置/基础辅助 | [08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md) |
| [src/Framework/SlateCore/PublicHeader/SlateWidget.h](../../src/Framework/SlateCore/PublicHeader/SlateWidget.h) | 45 | 接口/配置/基础辅助 | [08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md) |
| [src/Framework/SlateCore/PublicHeader/Widget.h](../../src/Framework/SlateCore/PublicHeader/Widget.h) | 137 | 接口/配置/基础辅助 | [08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md) |
| [src/Framework/SlateCore/PublicHeader/Win32Surface.h](../../src/Framework/SlateCore/PublicHeader/Win32Surface.h) | 66 | 渲染 | [08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md) |
| [src/Framework/SlateCore/PublicHeader/Window.h](../../src/Framework/SlateCore/PublicHeader/Window.h) | 94 | 渲染 | [08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md) |

## src/Framework/SlateRHIRenderer（5 个文件）

| 源码/配置 | 行数 | 检索标记 | 阅读入口 |
| --- | ---: | --- | --- |
| [src/Framework/SlateRHIRenderer/CMakeLists.txt](../../src/Framework/SlateRHIRenderer/CMakeLists.txt) | 62 | 接口/配置/基础辅助 | [09-module-build-and-startup](performance/09-module-build-and-startup.md) |
| [src/Framework/SlateRHIRenderer/Private/SlateRHIModule.cpp](../../src/Framework/SlateRHIRenderer/Private/SlateRHIModule.cpp) | 35 | 内存 | [08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md) |
| [src/Framework/SlateRHIRenderer/Private/SlateRHIRenderer.cpp](../../src/Framework/SlateRHIRenderer/Private/SlateRHIRenderer.cpp) | 80 | 并发、渲染 | [08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md) |
| [src/Framework/SlateRHIRenderer/Private/SlateRHIRenderer.h](../../src/Framework/SlateRHIRenderer/Private/SlateRHIRenderer.h) | 24 | 接口/配置/基础辅助 | [08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md) |
| [src/Framework/SlateRHIRenderer/PublicHeader/SlateRHIModule.h](../../src/Framework/SlateRHIRenderer/PublicHeader/SlateRHIModule.h) | 22 | 接口/配置/基础辅助 | [08-ui-rendering-and-window-lifetime](performance/08-ui-rendering-and-window-lifetime.md) |

## src/Lancher（5 个文件）

| 源码/配置 | 行数 | 检索标记 | 阅读入口 |
| --- | ---: | --- | --- |
| [src/Lancher/apps.json](../../src/Lancher/apps.json) | 27 | 接口/配置/基础辅助 | [09-module-build-and-startup](performance/09-module-build-and-startup.md) |
| [src/Lancher/CMakeLists.txt](../../src/Lancher/CMakeLists.txt) | 18 | 接口/配置/基础辅助 | [09-module-build-and-startup](performance/09-module-build-and-startup.md) |
| [src/Lancher/Loop.cpp](../../src/Lancher/Loop.cpp) | 95 | 接口/配置/基础辅助 | [09-module-build-and-startup](performance/09-module-build-and-startup.md) |
| [src/Lancher/Loop.h](../../src/Lancher/Loop.h) | 8 | 接口/配置/基础辅助 | [09-module-build-and-startup](performance/09-module-build-and-startup.md) |
| [src/Lancher/main.cpp](../../src/Lancher/main.cpp) | 16 | 接口/配置/基础辅助 | [09-module-build-and-startup](performance/09-module-build-and-startup.md) |

## src/Tests（12 个文件）

| 源码/配置 | 行数 | 检索标记 | 阅读入口 |
| --- | ---: | --- | --- |
| [src/Tests/AssetOwnershipTest.cpp](../../src/Tests/AssetOwnershipTest.cpp) | 159 | 并发 | [05-assets-materials-and-instance-data](performance/05-assets-materials-and-instance-data.md)；[06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md) |
| [src/Tests/CMakeLists.txt](../../src/Tests/CMakeLists.txt) | 13 | 接口/配置/基础辅助 | [09-module-build-and-startup](performance/09-module-build-and-startup.md) |
| [src/Tests/main.cpp](../../src/Tests/main.cpp) | 145 | 内存、渲染 | [09-module-build-and-startup](performance/09-module-build-and-startup.md) |
| [src/Tests/MaterialBindingTest.cpp](../../src/Tests/MaterialBindingTest.cpp) | 136 | 渲染 | [05-assets-materials-and-instance-data](performance/05-assets-materials-and-instance-data.md)；[06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md) |
| [src/Tests/MaterialContractTest.cpp](../../src/Tests/MaterialContractTest.cpp) | 139 | 渲染 | [05-assets-materials-and-instance-data](performance/05-assets-materials-and-instance-data.md)；[06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md) |
| [src/Tests/MathTest.cpp](../../src/Tests/MathTest.cpp) | 266 | 接口/配置/基础辅助 | [03-transforms-frustum-and-depth](algorithm/03-transforms-frustum-and-depth.md) |
| [src/Tests/RenderGraphBuildTest.cpp](../../src/Tests/RenderGraphBuildTest.cpp) | 361 | 内存、渲染 | [03-rdg-execution-and-state](performance/03-rdg-execution-and-state.md) |
| [src/Tests/RHIRayTracingTest.cpp](../../src/Tests/RHIRayTracingTest.cpp) | 590 | 并发、内存、算法、渲染 | [07-ray-tracing-as-and-sbt](performance/07-ray-tracing-as-and-sbt.md) |
| [src/Tests/RHIRenderTriangleTest.cpp](../../src/Tests/RHIRenderTriangleTest.cpp) | 500 | 并发、内存、渲染 | [02-rhi-queues-and-sync](performance/02-rhi-queues-and-sync.md)；[06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md) |
| [src/Tests/RHIShaderParameterTest.cpp](../../src/Tests/RHIShaderParameterTest.cpp) | 614 | 并发、内存、渲染 | [02-rhi-queues-and-sync](performance/02-rhi-queues-and-sync.md)；[06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md) |
| [src/Tests/StaticMeshRenderTest.cpp](../../src/Tests/StaticMeshRenderTest.cpp) | 491 | 并发、内存、渲染 | [02-rhi-queues-and-sync](performance/02-rhi-queues-and-sync.md)；[06-shader-compilation-and-binding](performance/06-shader-compilation-and-binding.md) |
| [src/Tests/TestBase.h](../../src/Tests/TestBase.h) | 83 | 接口/配置/基础辅助 | [09-module-build-and-startup](performance/09-module-build-and-startup.md) |

## 后续维护

新增文件或核心调用链后同步登记；删除/迁移源文件时修复链接。接口/枚举/通用小工具以架构入口说明边界，关键机制在专题中展开。对于缺失 Shader、占位 GI、坐标/符号不一致、未接入功能和并发/释放风险，以专题中的当前限制为准；本清单不把它们标成通过。
