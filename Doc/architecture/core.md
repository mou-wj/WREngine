# Core 模块架构

## 定位与依赖

Core 位于框架底层，提供 DLL 模块管理、路径、日志、时间、线程标识、CPU 任务池，以及数学/包围体/空间索引基础。它不创建图形设备、不执行 RHI 资源状态转换，也不拥有场景或资产。

源码位于 [Core](../../src/Framework/Core)。工程初始化与应用循环仍见 [overall](overall.md)，实现细节索引见 [key_implemetion](../key_implemetion/README.md)。

## 模块体系

[Module.h](../../src/Framework/Core/PublicHeader/Module.h) 定义 Module 的 StartupModule、ShutdownModule、IsLoaded 等契约。IMPLEMENT_SIMPLE_MODULE 在 DLL 静态初始化时注册共享模块对象；LoadModule 从可执行目录加载动态库，注册不等于启动。

ModuleManager 根据依赖做 DFS，visiting 检测递归环，visited 防止重复访问，依赖先入启动序列。ShutdownAll 逆序关闭。Launcher 在 Loop::Init 读取运行目录 apps.json，选择应用并校验其依赖，再调用这些既有接口；JSON 解析不下沉到 Core。

ModuleManager 的注册、查找、启动和关闭使用 recursive_mutex；AddModuleDependency 不持锁，按当前启动阶段串行使用理解，不能据此宣称任意线程可修改依赖图。加载错误处理、重复 StartupAll 的序列维护等限制见 [构建与模块启动](../key_implemetion/performance/09-module-build-and-startup.md)。

## 任务与基础工具

- TaskPool 拥有 worker 和任务队列，返回带 shared_future 的 TaskHandle。它处理 CPU 执行完成，不代表 GPU 完成；细节见 [任务与渲染线程](../key_implemetion/performance/01-task-and-render-threads.md)。
- ThreadInfo 记录线程 ID；它是识别工具，不提供互斥或任务调度。
- Timer、PathInfo、Log、Flags、HashHelper、Singleton、TypeIDCast 等是基础服务。调用方仍负责对象生命周期、哈希等价性与并发约束。

## 数学与空间索引

Math、Matrix、Transform 提供向量、矩阵与变换；BoxSphereBounds 提供 AABB/球界；BVH 和 Octree 为借用图元数组的模板索引。这些类型的具体公式、剪枝行为和退化条件归入 [算法索引](../key_implemetion/README.md)，不在架构说明中重复。

当前 CPU BVH、Renderer 的场景 BVH，以及 Vulkan/DXR 的硬件加速结构是不同实现，不能把它们的构建、精确性或所有权约定混用。
