# 架构文档索引

这里汇总了当前项目中各个模块的源码导向架构说明，按“整体 → 引擎 → RHI → Render → Slate → Application”顺序组织，便于快速定位系统边界和依赖关系。

## 1. 总览

- [overall.md](overall.md) — 项目整体模块组织、启动流程、模块依赖和主循环
- [core.md](core.md) — Core 基础设施、模块管理、任务与数学工具的职责边界

## 2. Engine

- [engine.md](engine.md) — Engine 模块的核心结构、运行时与场景能力
- [material.md](material.md) — 材质参数、编译资源、光栅状态及编辑接口

## 3. RHI

- [rhi/rhi.md](rhi/rhi.md) — RHI 抽象层概览
- [rhi/rhivulkan.md](rhi/rhivulkan.md) — Vulkan 后端实现概览
- [rhi/rhiopengl.md](rhi/rhiopengl.md) — OpenGL 后端、上下文线程、命令队列与能力边界
- [rhi/rhidirectx12.md](rhi/rhidirectx12.md) — DirectX12 后端实现、资源访问契约与能力边界

## 4. Render

- [render/rendercore.md](render/rendercore.md) — RenderCore 渲染基础设施层
- [render/renderer.md](render/renderer.md) — Renderer 具体渲染实现层

## 5. Slate

- [slate/slatecore.md](slate/slatecore.md) — SlateCore 窗口与事件抽象层
- [slate/imguislate.md](slate/imguislate.md) — ImGuiSlate 的具体 UI 实现层

## 6. Application

- [app/applicationbase.md](app/applicationbase.md) — ApplicationBase 应用生命周期抽象
- [app/application.md](app/application.md) — App::Application 具体应用入口实现

## 7. 说明

这些文档的目标不是泛泛介绍，而是尽量贴近项目源码中的真实模块边界、依赖关系和职责分层。阅读时建议按如下路径理解：

- 底层图形接口：RHI / RHIVulkan / RHIOpenGL / RHIDirectX12
- 渲染基础设施：RenderCore
- 具体渲染实现：Renderer
- 窗口与 UI 框架：SlateCore / ImGuiSlate
- 应用程式入口：ApplicationBase / Application
- 运行时核心：Engine

如果需要进一步查看特定模块的源码实现，可以按文档中的模块名直接定位到对应的 `src/Framework` 或 `src/Application` 目录。

并发、内存、资源同步及关键数学/图形算法的详细实现，见 [核心实现索引](../key_implemetion/README.md)；该目录补充本架构索引，不替代现有模块划分。
