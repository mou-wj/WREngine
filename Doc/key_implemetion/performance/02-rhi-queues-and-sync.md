# RHI 队列、上下文与完成点

架构：[RHI](../../architecture/rhi/rhi.md)、[Vulkan](../../architecture/rhi/rhivulkan.md)、[OpenGL](../../architecture/rhi/rhiopengl.md)、[D3D12](../../architecture/rhi/rhidirectx12.md)。

## 四个不同阶段

`记录 RHICommand → ExecuteAll 调用后端 context → ExecuteContext 提交 → 完成点到达`。

[RHICommandList.h/.cpp](../../../src/Framework/RHI/PublicHeader/RHICommandList.h) 的 immediate 模式直接分派 context；延迟模式保存命令对象，ExecuteAll 再分派。这里的“即时”不代表 GPU 同步完成。OpenGL context 还会再记录 nativeCommands，形成另一层 CPU 延迟。

命令和资源视图广泛使用裸指针。记录期间、提交前以及执行期间都要保持相应对象有效，shared_ptr 的局部存在或 CPU 函数返回并不自动证明 GPU 生命周期。

## 后端完成语义对照

| 后端与源码 | 提交返回值 | 上下文复用 | 不能混淆的边界 |
| --- | --- | --- | --- |
| [VulkanQueue.cpp](../../../src/Framework/RHIVulkan/Private/VulkanQueue.cpp) | 本队列 timeline semaphore 值 | GarbageCollect 比较完成值，回收 context、command buffer 与临时 binary semaphore | CPU vkQueueSubmit 返回不代表 GPU 完成 |
| [DirectX12Queue.cpp](../../../src/Framework/RHIDirectX12/Private/DirectX12Queue.cpp) | 原生 queue Signal 的 fence 值 | 完成值达到 LastSubmission 后 Complete 并允许复用 | allocator、descriptor heap、上传资源不可提前重置 |
| [OpenGLQueue.cpp](../../../src/Framework/RHIOpenGL/Private/OpenGLQueue.cpp) | 每个逻辑 queue 分配的单调值 | in-flight 集合保护，worker 执行完后 FinishContextExecution | 当前完成值表示 worker 已调用 GL 命令，不是硬件 GPU fence |

fence 必须理解为 `(QueueType, Value)`，不同队列的数值不能直接比较大小。相同 Value 不代表相同进度。

## Vulkan：提交、依赖和回收

ExecuteContext 用 m_FlushContextMutex 串行组装提交；waitInfos 被转换成 semaphore、value、stage。显式 SyncPoint 优先；只给 QueueType 的依赖会查目标队列。图形设备可能让逻辑 graphics/compute 使用同一原生 VkQueue，这也不取消信号和锁的职责。

swapchain acquire 的 binary semaphore 与长期 timeline 是两种同步对象：acquire 解决后缓冲图像可用，timeline 解决引擎提交的完成。GarbageCollect 处理提交记录，不能把“调用 ReleaseCommandContext”理解为硬件自动允许重录。

复用 context 还必须清空 CPU 侧的原生状态缓存。VulkanPendingGfxState::Reset 现会清空所有 pending vertex streams；上一提交完成后旧缓冲可合法销毁，不能在新 command buffer 的后续 Draw 中把旧句柄重新绑定，即便当前管线没有使用那个槽位。2026-10-26 的实例缓冲重建测试暴露并修复了这个问题，没有延长旧资源寿命来绕过缓存错误。

## D3D12：原生 queue fence 和 context 引用

DirectX12SyncPoint::Signal 分配值并调用 queue Signal；Wait 首先比较 GetCompletedValue，必要时 SetEventOnCompletion + Windows event 等待。GetCompletedValue 为 UINT64_MAX 时检查设备移除。

DirectX12CommandContext 记录 UsedResources 和暂存 COM 引用；提交写入各资源的 LastQueue/LastFence，Complete 释放 context 使用计数等记录。它不等价于所有 Engine 资产都由 context 强持有，原始资源包装对象仍有生命周期要求。

当前 ExecuteContext 的 wait lambda 直接忽略没有 SyncPoint 的 waitInfo，而 RDG 等调用方有按 QueueType/Value 填写的路径；还存在资源自身 ResourceWaits 补充依赖的机制。文档不能因此声称所有逻辑队列等待在 D3D12 都等价实现，新增跨队列用例须核对实际原生 Wait。

## OpenGL：一个 context worker、两个逻辑队列

worker 初始化并长期绑定默认 context，先 DrainTasks，再尝试 graphics 和 compute 队列；无工作时最多等待约 1ms。资源创建通过 EnqueueTaskAndWait 转到 worker，析构通常异步入队并按值捕获句柄。

同一 worker 内转发任务直接内联，避免“worker 排队并等自己”。未启动 worker 的分支也直接调用任务，但此时调用线程是否有有效 GL context 仍须由初始化/关闭流程保证。

TryExecuteQueue 先检查队首依赖，不能满足则保留队首，不能越过它执行后续条目。执行 entry 的 callback 或 nativeCommands 后才设置完成值。Present 同样分配新的 fence 条目，按获取时的 back-buffer index 呈现，之后恢复默认 context。

OpenGLQueue::WaitIdle 仅等待最新分配条目在 worker 执行完；没有 glFinish。某些 transient 回收路径另行执行 glFinish，才得到更保守的 GPU 完成保证。EnqueueTaskAndWait 虽把异常写入 promise，但等待方用 wait，不会 get 后重抛。

## 修改前必须检查

同队列 FIFO、跨队列等待、worker 可达性、命令使用的原始资源所有者，以及资源重新录制/复用的时间点。CPU mutex、GPU barrier、queue wait 和对象引用计数分别解决不同问题，不能互相替代。

验证入口：RHIRenderTriangleTest、RHIShaderParameterTest、RHIRayTracingTest。它们默认长循环、没有自动像素断言；窗口出现不能证明退出、跨队列或资源回收完整通过。既往具体观察见 [故障记录](../../dev/bugfix/故障与修复记录-2026-10-25.md)。
