---
name: renderer-dev
description: 使用 RenderCore 抽象实现渲染器功能的中文开发说明。
---

# 渲染器开发

以 RenderCore 作为基础。渲染器代码应该建立在它之上，而不是取代它。

## 核心规则

- Shader 参数元数据定义在 `src/Framework/RenderCore/PublicHeader/ShaderParameter.h`。
- 所有 shader 参数结构体都必须使用 `BEGIN_SHADER_PARAMETER_STRUCT` / `END_SHADER_PARAMETER_STRUCT`。
- 标量/向量成员使用 `SHADER_PARAMETER(...)`，纹理、采样器、缓冲区、UAV 等使用对应资源宏。
- 如果某个类型用于 `SHADER_PARAMETER_RDG_STRUCTURED_BUFFER` 或 `SHADER_PARAMETER_RDG_RWSTRUCTURED_BUFFER`，它必须是带有元数据的正确 shader 反射结构体，而不是普通运行时 struct。
- `ShaderParameterTypeInfo<T>` 中的 `TAlignedType` 表示真实的对齐存储类型，新增参数类型时要优先检查它。

## 工作流

1. 按 RenderCore 风格定义 shader 参数结构体。
2. 定义对应的 shader 类。
3. 使用正确的 shader 基类：
   - 全局 shader：RenderCore 的全局 shader 基类
   - 材质 shader：渲染器材质 shader 基类
   - 网格材质 / vertex-factory shader：mesh material shader 路径
4. 使用 `ShouldCompilePermutation`、`ModifyShaderCompilerEnvironment`、`GetShaderParameterMetadata` 等 shader 静态函数添加变体逻辑。
5. 将资源和参数绑定到实际使用位置。
6. 通过直接 RHI 命令提交或 `RenderGraphBuilder` pass 调度来执行。

## Shader 定义

项目有一套标准的 shader 架构：

- RenderCore 定义公共的 shader 抽象和反射系统。
- `ShaderCore.h` 包含编译/变体域逻辑。
- 渲染器模块在 RenderCore 上定义具体 shader。
- 网格材质 shader 是 vertex-factory 渲染的路径。

优先遵循现有项目模式，而不是自行发明新的自定义模型。

## 执行方式

### 直接 RHI 执行

适用于简单的独立任务。

典型流程：

- 获取 queue/context
- 创建 command list
- 绑定 pipeline state
- 设置 shader 参数
- dispatch/draw
- flush 或结束 command list

参考：测试 shader 参数执行路径和直接功能代码。

### RenderGraph 执行

适用于引擎级 pass 和资源管理渲染。

典型流程：

- 把 pass 添加到 `RenderGraphBuilder`
- 创建/访问 RDG 资源和缓冲区
- 定义 pass 依赖和资源转换
- 用 pass 输入数据设置参数
- 让 builder 调度执行

参考：渲染器 pass 实现以及 `DefferedSceneRenderer.cpp` 风格用法。

## 实现建议

新增渲染器代码时：

- 先阅读 `ShaderParameter.h`
- 优先使用 RenderCore 抽象，而不是临时硬编码低层类型
- 在需要时保持 shader metadata 与运行时数据分离
- 优先遵循引擎约定和现有 shader，而不是自定义模式
- 把 `RenderGraphBuilder` 当作渲染器 pass 的默认编排层

## 最终规则

渲染器功能应当作为 RenderCore 之上的一层实现：

`shader metadata -> shader class -> shader variant -> parameter binding -> 通过 RHI 或 RenderGraph 执行`

这是新增渲染器功能的预期架构。

## Copilot 中英文说明

- 英文通常更适合代理可读性，并且更符合代码注释、shader 命名和项目约定。
- 中文可以在完全本地化项目中使用，但英文更稳健，因为模型训练和指令遵循通常更强。
- 最佳实践：项目说明文件保持英文，但在本地团队约定需要时添加简短中文说明。

## 额外建议

- 新增 renderer 功能时，优先查找同类 pass 与 shader 的已有实现。
- 尽量以 RenderCore 抽象为主线，而不是在代码中直接混用 ad hoc 类型。
- 对 shader 参数、资源绑定和 pass 依赖保持清晰边界，避免出现“参数定义、绑定、执行”混杂在一个函数中的情况。
- 如果一个功能涉及多个 pass，优先考虑 RenderGraph，而不是直接手写命令流。
