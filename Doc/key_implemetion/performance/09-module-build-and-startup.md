# 构建依赖、模块注册与启动顺序

架构：[overall](../../architecture/overall.md)、[Core](../../architecture/core.md)。源码：[BuildDeps.py](../../../BuildDeps.py)、[CMakeLists.txt](../../../CMakeLists.txt)、[Functions.cmake](../../../cmake/Functions.cmake)、[Module.h](../../../src/Framework/Core/PublicHeader/Module.h)、[Loop.cpp](../../../src/Lancher/Loop.cpp)。

## 两张依赖图不能混用

BuildDeps.py 根据第三方元数据准备源代码或预编译文件，并生成 CMake 导入信息；CMake 描述编译/链接和 DLL 拷贝。运行时 ModuleManager 的依赖图控制模块 Startup/Shutdown 的先后，不等于链接器依赖图。

一个 DLL 能链接、被 LoadLibrary 找到、完成静态注册、成功 Startup，是四个分别需要验证的步骤。

## 构建准备的数据流

BuildDeps.py 读取依赖元信息，展开安装路径占位符，按依赖拓扑处理不同来源：源码导入、远端源码构建安装、手工导入的系统/文件路径等。外部 CMake 使用各配置的构建目录，最后生成 `cmake/Find3rdsGenerated.cmake`。

元信息位于 [config/3rds](../../../config/3rds)，其中 imgui 元信息仍列有官方 opengl3 后端源码。被构建/安装的第三方文件不代表当前 ImGUISlate 正在调用它；运行时接口接入应以调用链为准。[extern/glad](../../../extern/glad) 是外部 GL loader 代码，不在自有算法推导范围。

该生成文件是构建前置产物，不应将开发者机器上的绝对路径复制为正式架构约定。初始化失败应核查元数据、编译器/生成器、实际安装目录和生成文件，不能只因 VS 能运行就假定 VS Code 使用了同一构建树。

根 CMake 将 runtime/library/archive 输出定位为 BINARY_DIR，并用生成器表达式避免多配置生成器再附加 Debug/Release。_define_export_macro 为 DLL 生产者定义 dllexport、使用者导入 dllimport；runtime 拷贝助手负责第三方 DLL 到可执行目录。运行时资源路径还有 PathInfo 的约定，输出目录与工作目录不是一个概念。

## DLL 静态注册和依赖 DFS

IMPLEMENT_SIMPLE_MODULE 创建 registrar，构造时建立模块 shared_ptr 并 RegisterModule。它明确不直接 Startup，防止静态初始化时依赖尚未齐备。LoadModule 通过可执行目录构造 DLL 路径触发加载。

StartupAll 的 DFS 先 visiting 标记，在依赖返回后加入 sorted 序列，visited 避免重复访问；逆序 ShutdownAll 保证依赖者先关闭。理想复杂度 O(V+E)，哈希容器遍历不保证独立模块的固定顺序。

目前存在这些限制：LoadModule 不返回可供调用方核查的加载结果；AddModuleDependency 未持 manager 锁；缺失依赖和重复调用 StartupAll 的排序状态没有完整验证/重置协议。recursive_mutex 允许启动回调再次访问 manager，但不证明任意回调线程都能安全变更模块图。

## 当前 Launcher

Loop::Init 手工加载 RHIVulkan、Renderer、Engine、SlateRHIRenderer、Application 并登记依赖。Run 每次先 Engine Tick，再 Application TickFrame；Shutdown 调用模块逆序关闭。当前没有 src/Editor 模块或已启用的 apps.json 选择逻辑；不能将此前讨论过的目标方案写成现状。

Engine Shutdown 会收束资产加载、释放距离场；Renderer Shutdown 清池、ShaderMap、公共资源并停止渲染线程。排序图和各模块内部的释放顺序必须一起检查，单看“逆拓扑”无法证明未声明的跨模块资源引用已消失。

测试程序与 Launcher 不一定共享模块 Startup 全流程。例如直接加载网格的测试若没有初始化距离场 Atlas，会在模型导入时触发空纹理访问；这与材质 Shader 是否编译成功不同，已有 [故障记录](../../dev/bugfix/故障与修复记录-2026-10-25.md)。

本轮未运行 BuildDeps 或修改生成配置，也未重新构建。需要复查时分开记录依赖准备、configure、build、启动、画面和退出，避免用其中一个阶段替代其他阶段。
