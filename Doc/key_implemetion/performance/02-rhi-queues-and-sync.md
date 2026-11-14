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

OpenGL 图形上下文的顶点流缓存是借用的资源指针。复用上下文时，设置 PSO 只将布局标记为 dirty，Draw/DrawIndexed 在本次 SetStreamSource 命令完成后应用顶点属性。这样跨场景释放旧网格后，PSO 设置阶段不会读取旧缓存。调用方仍需在绘制时绑定有效且存活的顶点流。具体异常栈及修正见 [开发记录](../../dev/bugfix/故障与修复记录-2026-11-13.md#rdg-非-shader-访问回归参数元数据)。

fence 必须理解为 `(QueueType, Value)`，不同队列的数值不能直接比较大小。相同 Value 不代表相同进度。

## Vulkan：提交、依赖和回收

ExecuteContext 用 m_FlushContextMutex 串行组装提交；waitInfos 被转换成 semaphore、value、stage。显式 SyncPoint 优先；只给 QueueType 的依赖会查目标队列。图形设备可能让逻辑 graphics/compute 使用同一原生 VkQueue，这也不取消信号和锁的职责。

swapchain acquire 的 binary semaphore 与长期 timeline 是两种同步对象：acquire 解决后缓冲图像可用，timeline 解决引擎提交的完成。GarbageCollect 处理提交记录，不能把“调用 ReleaseCommandContext”理解为硬件自动允许重录。

复用 context 还必须清空 CPU 侧的原生状态缓存。VulkanPendingGfxState::Reset 现会清空所有 pending vertex streams；上一提交完成后旧缓冲可合法销毁，不能在新 command buffer 的后续 Draw 中把旧句柄重新绑定，即便当前管线没有使用那个槽位。2026-10-26 的实例缓冲重建测试暴露并修复了这个问题，没有延长旧资源寿命来绕过缓存错误。

## D3D12：原生 queue fence 和 context 引用

DirectX12SyncPoint::Signal 分配值并调用 queue Signal；Wait 首先比较 GetCompletedValue，必要时 SetEventOnCompletion + Windows event 等待。GetCompletedValue 为 UINT64_MAX 时检查设备移除。

DirectX12CommandContext 记录 UsedResources 和暂存 COM 引用；提交写入各资源的 LastQueue/LastFence，Complete 释放 context 使用计数等记录。它不等价于所有 Engine 资产都由 context 强持有，原始资源包装对象仍有生命周期要求。

DirectX12Queue 的申请、归还、提交和完成遍历共享 m_QueueMutex 递归锁，保护上下文 vector 和可复用标记，防止 RDG 并行录制同时扩容容器。Release 沿既有路径调用 WaitValue，完成回调可重入队列方法，因此使用递归锁。普通 WaitValue 先等待 Fence，再持锁清理仍可复用的上下文，已被借出录制的上下文跳过。锁会串行化这些上下文管理操作，GPU 命令录制仍在各自任务中执行。此次堆损坏的证据和回归结果见 [开发记录](../../dev/bugfix/故障与修复记录-2026-11-13.md#rdg-非-shader-访问回归参数元数据)。

当前 ExecuteContext 的 wait lambda 直接忽略没有 SyncPoint 的 waitInfo，而 RDG 等调用方有按 QueueType/Value 填写的路径；还存在资源自身 ResourceWaits 补充依赖的机制。文档不能因此声称所有逻辑队列等待在 D3D12 都等价实现，新增跨队列用例须核对实际原生 Wait。

## OpenGL：一个 context worker、两个逻辑队列

worker 初始化并长期绑定默认 context，先 DrainTasks，再尝试 graphics 和 compute 队列；无工作时最多等待约 1ms。资源创建通过 EnqueueTaskAndWait 转到 worker，析构通常异步入队并按值捕获句柄。

同一 worker 内转发任务直接内联，避免“worker 排队并等自己”。未启动 worker 的分支也直接调用任务，但此时调用线程是否有有效 GL context 仍须由初始化/关闭流程保证。

TryExecuteQueue 先检查队首依赖，不能满足则保留队首，不能越过它执行后续条目。执行 entry 的 callback 或 nativeCommands 后才设置完成值。Present 同样分配新的 fence 条目，按获取时的 back-buffer index 呈现，之后恢复默认 context。

OpenGLQueue::WaitIdle 仅等待最新分配条目在 worker 执行完；没有 glFinish。某些 transient 回收路径另行执行 glFinish，才得到更保守的 GPU 完成保证。EnqueueTaskAndWait 虽把异常写入 promise，但等待方用 wait，不会 get 后重抛。

## 索引间接绘制与 GPU 生成参数

[RHIGraphicCommandList::DrawIndexedIndirect](../../../src/Framework/RHI/Private/RHICommandList.cpp) 沿用 RHICommand → RHIGraphicContex → 原生 API 的链路，没有新队列、提交器或资源所有者。入口借用 indexBuffer、argumentBuffer 和字节偏移。调用方必须保持资源在 GPU 使用期间有效，声明索引为 VertexOrIndexBuffer、参数为 IndirectArgs，并在实际提交处更新 Fence；VG 路径交由现有 RDG 完成。

RHIDrawIndexedIndirectArguments 的顺序是 `uint IndexCount, InstanceCount, FirstIndex; int VertexOffset; uint FirstInstance`，共 20 字节。入口验证索引用途、16/32 位步长、参数的 Indirect 用途、四字节对齐与剩余范围；数据内容在执行时由 GPU 读取，RHI 不为检查 GPU 数据而读回。IndexCount=0 用来拒绝绘制。当前 VG 写 FirstInstance=0；本接口不增加多 Draw、计数缓冲或非索引 Draw 的变体，调用方也不能假设当前 Vulkan 已启用间接命令的非零 FirstInstance 特性。

| 后端 | 原生执行及所有权 |
| --- | --- |
| [VulkanContex.cpp](../../../src/Framework/RHIVulkan/Private/VulkanContex.cpp) | 准备既有图形状态、绑定索引，然后 vkCmdDrawIndexedIndirect，drawCount=1、stride=20。函数由既有 VKFunc wrapper 装载；IndirectArgs 已映射到 DRAW_INDIRECT 阶段和 INDIRECT_COMMAND_READ 访问 |
| [DirectX12Context.cpp](../../../src/Framework/RHIDirectX12/Private/DirectX12Context.cpp) | 每个图形 Context 首次使用时创建只含 DRAW_INDEXED 的 CommandSignature，Context 自有 ComPtr 复用并随其销毁。沿 RequireAccess 检查索引和参数状态，再 ExecuteIndirect 一条命令；不改 Root 参数 |
| [OpenGLContext.cpp](../../../src/Framework/RHIOpenGL/Private/OpenGLContext.cpp) | lambda 按值捕获资源指针/偏移，实际 GL 调用仍在原 worker。恢复/应用 VAO 顶点描述、绑定 ELEMENT_ARRAY_BUFFER 和 DRAW_INDIRECT_BUFFER，调用 glDrawElementsIndirect 后解绑间接缓冲；元素首地址按索引数量解释 |

原生语义可对照 [Vulkan vkCmdDrawIndexedIndirect](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdDrawIndexedIndirect.html)、[D3D12 ExecuteIndirect](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-executeindirect) 和 [OpenGL 4.6 规范](https://registry.khronos.org/OpenGL/specs/gl/glspec46.core.pdf)。项目只使用这里列出的单条索引命令子集。

### OpenGL 的存储写入、屏障和读回

Buffer UAV 必须像 Buffer SRV 一样，将视图的 Offset、NumElements×Stride（未指定数量时为剩余范围）通过 glBindBufferRange 绑定到 GL_SHADER_STORAGE_BUFFER。原有“buffer UAV 无需绑定”分支并不能建立 Shader 访问，本轮 GPU 选择用例已暴露并修正它。

OpenGLRHIApi::RHICreateTransition 将 UAV 写后消费者的屏障位存入既有 RHITransition 私有区域；EndTransitions 汇总为按值捕获的 mask，由 worker 执行 glMemoryBarrier，不捕获短生命周期 transition 指针。枚举映射在 [OpenGLUtils](../../../src/Framework/RHIOpenGL/Private/OpenGLUtils.cpp)：IndirectArgs→COMMAND，SRV→SHADER_STORAGE/TEXTURE_FETCH/UNIFORM，UAV→SHADER_STORAGE/SHADER_IMAGE_ACCESS，VertexOrIndexBuffer→VERTEX_ATTRIB_ARRAY/ELEMENT_ARRAY，传输和 CPURead→BUFFER_UPDATE/TEXTURE_UPDATE/PIXEL_BUFFER，附件→FRAMEBUFFER。由于 GL 屏障按消费者种类生效，这些位是全局类型屏障，不是假装对单个资源设置布局。

GPU Group 批次之间依靠 UAV→UAV 屏障；参数生成到绘制依靠 UAV→IndirectArgs。它们保证 GPU 访问的顺序和可见性，不代表 CPU 已能读取结果。正常选择/绘制没有 CPU 等待。Viewer 的 Capture GPU 是显式诊断：在图中声明 Flags/Statistics 为 TransferSrc；Vulkan/D3D12 记录映射对应的复制并在提交完成后读取，OpenGL 在图提交完成后通过 worker 的 glGetBufferSubData 读回。映射只在该次 Capture 内使用并 Unmap，未跨帧保存 transient 指针。

GPU Cut、紧凑列表、固定参数槽与实际开销见 [LOD 算法](../algorithm/13-virtual-geometry-lod-selection.md#gpu-依赖求值与完整-cut)。间接绘制使 GPU 决定是否产生几何，但当前 CPU 仍录制每个候选的一条命令，不能据此声称已经消除 CPU Draw 录制成本。

### 软件光栅两阶段依赖与诊断计时

VG 的软件深度和 ID 是本次图内的两个 uint Buffer：先完成所有实例的 DepthCS UAV 原子最小值，再由 VisibilityCS 只读深度、原子写 ID，最后 Merge 读取二者并写最终颜色/深度附件。参数 SRV/UAV 及附件声明让原 RDG 建立写后读/写屏障和队列依赖，不通过 CPU 每阶段等待或 WaitIdle 实现。中间 Buffer 和硬件附件来自原 transient 分配器，外部几何及最终预览纹理继续在实际提交/采样处更新 Fence；数学、内存成本及 ID 一致性证明见 [软件光栅专题](../algorithm/14-virtual-geometry-software-rasterization.md)。

Viewer 的 Measure completed frame 是显式诊断：先沿已有资源 Fence 排除上一帧，再从构图前开始计时，Execute 后等待当前颜色最后访问 Fence。正常帧不因新增计时而等待，Capture 读回和计时分到两帧执行，避免把统计复制混入计时。数值包括 CPU 构图、录制、提交及队列完成等待，不是各 Pass 的 GPU timestamp，也不是纯 GPU 光栅时间。Vulkan/D3D12 等待原生 Fence；OpenGL 的现有 WaitValue 只保证 worker 执行结束，不能将其数值称作 GPU 完成时间，界面明确提示该区别。没有为计时增加 glFinish、查询池、跨帧保活或 RHI 同步新体系。

## 修改前必须检查

同队列 FIFO、跨队列等待、worker 可达性、命令使用的原始资源所有者，以及资源重新录制/复用的时间点。CPU mutex、GPU barrier、queue wait 和对象引用计数分别解决不同问题，不能互相替代。

验证入口：RHIRenderTriangleTest、RHIShaderParameterTest、RHIRayTracingTest。它们默认长循环、没有自动像素断言；窗口出现不能证明退出、跨队列或资源回收完整通过。既往具体观察见 [故障记录](../../dev/bugfix/故障与修复记录-2026-10-25.md)。
