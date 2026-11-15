# Virtual Geometry GPU Page Cache 与异步流送

入口是 Renderer 的 [VirtualGeometryStreaming](../../../src/Framework/Renderer/VirtualGeometry/VirtualGeometryStreaming.h)，实现在同目录 cpp。压缩文件与分页属于 Engine 的 [VirtualGeometryPageStorage](../../../src/Framework/Engine/PublicHeader/VirtualGeometryPages.h)，位编码见 [算法专题](../algorithm/15-virtual-geometry-page-compression.md)。它们沿既有 Engine → RenderCore/RHI、Renderer → Engine 的依赖方向工作，不增加模块、全局流送管理器、队列或 RenderGraph 接口。

## 所有者与公开调用

一个 Streaming 对象对应一个 StaticMeshRenderData，强引用该对象和不可变的 PageStorage。物理缓冲仍由该数据中的 VirtualGeometryResource 独占，Streaming 只管理其驻留映射、加载任务和三个附加 Buffer。共享同一几何的多个实例必须共享该 Cache，不能为同一个资源创建两个互相改写物理池的 Cache。创建、构图、Execute、EndFrame 和资源编辑由调用方串行执行。

```cpp
// 先 Build CPU 几何并创建/打开匹配的 PageStorage；不必先全量 InitializeResources。
auto cache = std::make_shared<Renderer::VirtualGeometryStreaming>(data, storage, budgetBytes);
instance.RenderData = data;
instance.bGPUSelection = true;
instance.Streaming = cache;
auto result = Renderer::AddVirtualGeometryRasterPass(builder, view, {instance}, width, height, settings);
// 可继续原 Debug / MaterialResolve / Deferred。
builder.Execute();
cache->EndFrame(); // 在图销毁前记录反馈完成点，不等待 GPU。
```

Raster 会调用 Prepare，并在所有实例的选择完成后为每个去重的 Cache 追加一次反馈读回。相同图内 Prepare 幂等，同一 Cache 的实例将请求 OR 到同一张反馈表。下一图开始前必须 EndFrame；漏调用会明确报错。返回的图内引用不跨帧保存。

Viewer 持有 Cache，默认在 GPU Dynamic LOD 下开启，预算默认 16 MiB；CPU 模式和 Generated level 检查显式切回原全量缓冲。原场景 AddVirtualGeometryRender 封装不自动创建 Cache，目前流送由显式 VirtualGeometryInstance 的调用方启用，主场景 RenderModule 未增加新分支。

## Page Cache、预算与状态

InitializeStreamingResources(pageSlots) 沿原初始化入口创建六个有限大小的几何 Buffer：每槽 768 个位置/法线/切线/UV/颜色元素及 1536 个局部索引，合计 55,296 字节。不会先分配全量 GPU 几何再另建 Cache。Cluster/Group/Child/Parent/Root 等较小 DAG 元数据仍全量常驻，使 GPU 可以判断未驻留几何的误差与依赖。

预算扣除常驻元数据和 Residency、ClusterPages、Feedback Buffer 后，取剩余字节可容纳的整数槽数，并以总 Page 数为上限。CacheBytes 包含这些持久缓冲的申请字节，不包含驱动对齐、Readback/Upload staging、当前 RenderGraph 临时 selection、Visibility/GBuffer 或 Viewer 线框。它是该资源 Cache 的预算，不是全应用显存预算；多个独立 Cache 的预算由上层相加管理。

PageSlots[page]、SlotPages[slot] 用 UINT32_MAX 表示未占用；Resident[cluster] 表示本图访问的页有效，PhysicalClusters 保存重定位后的 Offset，CPU 原 Clusters 保持逻辑偏移。所有终端 Root 的 Child Page 固定 Pinned，缺预算容纳完整根 Cut 时拒绝初始化。单个 Group 所需 Child Page 加 Pinned 集超过槽数时，记录 BudgetBlocked 并保持粗层，不淘汰根来强行满足目标。

状态依次为：未驻留 → 所属 Group 请求 → 后台读取/解码 → Ready → 本图上传并发布 Residency → 后续可淘汰。CPU Resident 指的是“已纳入本图有序上传/读取”的状态，不声称构图返回时 GPU 已完成；GPU 的驻留表读取必须经过实际 Upload 的 RDG 依赖。失败页记录 Failed、错误文本和 FailedLoads，整批失败不发布部分 Page，保留粗 Cut；当前需重建 Cache 才重试失败页。

## GPU 缺页请求与合法 DAG Cut

不能将原 [最细向粗选择](../algorithm/13-virtual-geometry-lod-selection.md) 的缺页 Cluster 简单删除，也不能要求“根到细节的全部中间 Page 都驻留”：后者在细页已存在而中间页被淘汰时仍退回根，并可能反复加载。当前保留原 GPU 误差选择的 DesiredGroups，ResidentClosureCS 计算满足驻留约束的**必须粗化 Group 最小闭包**。

1. Applied 初始复制 DesiredGroups，把已应用 Group 放入 GPU 队列；每个缺失的原始叶 Cluster 强制应用其消费 Group，也加入队列。
2. 取出 Group g：应用 g 必须先生成其全部 Child，所以把每个 Child 的非空生成 Group 加入闭包。
3. g 会生成全部 Parent；其中若某 Parent 未驻留，就强制加入其消费 Group，保证这个非驻留 Parent 也被完整替换，不会留下缺面。
4. 只在 Applied 从 0 变成 1 时入队，每个 Group 最多一次，队列容量为 G；处理到队列为空。没有全 GPU 自旋、CPU 逐次回读或不确定次数的 Dispatch。

DesiredGroups 本身向生成依赖闭合；上述第 2 项保持这一性质。因此最终 Applied 可按原拓扑序执行一串完整 Child→Parent 替换，构成合法 DAG Cut。任意未驻留叶必被消费；任意未驻留生成 Cluster，只要其生成 Group 已应用，第 3 项又保证其被消费，所以最终 Cut 不含缺页 Cluster。Root 的全部 Child Page 固定驻留，Root 不会被迫“替换为空”，保证停滞区域、多根和整份几何都保留。

这个闭包只因真正被选择/依赖的缺页几何而粗化，中间层即使缺页，只要最终细层本身已经驻留，就不要求先加载整条链。反复入队抑制提供 O(C+G+引用数) 工作量和 O(G) 队列存储。第一版由一个 GPU invocation 串行处理元数据队列，原误差选择及后续请求、Cluster 完成仍并行；没有把它说成高吞吐并行工作队列，后续可在相同不变量下优化。

随后 RequestResidentPagesCS 并行检查“实际被迫粗化而理想选择未应用”的 Group，且其全部 Parent 当前都在 Cut 上，才为缺失的 Child 页请求下一步细化；可见性判断阻止整组在视锥外时产生 I/O。请求不修改已形成的 Applied。最后沿原公式 `(叶 || 生成Group已应用) && !消费Group已应用` 得到 Cut，再剔除并写原稳定间接参数。CPU SelectResidentReference 先从理想 Cut 逆向恢复 DesiredGroups，再计算闭包，供 GPU 对照；测试另把实际 Cut 逆替换为完整叶集合检查合法性。

这是受驻留约束的保守 Cut，可能比 TargetPixelError 更粗，预算和 I/O 延迟期间不能保证达到目标误差。原 Build 边界保持和无损位置避免新增量化裂缝；仍不提供未构建边界的修补、几何渐变、滞回或严格 Hausdorff 保证。

## 请求合并、后台任务与淘汰

Feedback 是 G+P 个 uint：前 G 项为 Group 缺页位；后 P 项的 bit0 表示 Page 被引用、bit1 表示当时已驻留、bit2 表示本帧可见 Cut 使用。多个 Cluster/实例对同一页用 InterlockedOr 去重。命中率是反馈帧内的唯一 Page 命中/引用累计比率，不是按三角形或按磁盘 I/O 次数统计。

消费完成的反馈后更新 LastUse。一次只接受一批 Group 的缺页请求，按逆拓扑顺序优先处理较粗的、当前可细化的区域。收集全部 Child 的页集合并保护它们及 Pinned 页，确认总容量后才挑选其他非 Pinned 页中的最久未使用者；不允许逐页加载把同一细化尚需的兄弟页反复淘汰。已有 Job 时不再发第二批相同请求；下一反馈发现已加载页后也会跳过。

读取/解码使用原 Core::TaskPool 的一个 worker，只捕获共享 LoadJob 和不可变 Storage，不访问 RHI、图或 GPU Buffer。任务返回后通过 TaskHandle.IsCompleted 和 Future.get 建立 CPU 发布关系，不在正常帧阻塞等待文件。I/O/Checksum 失败以错误结果返回，异常仍按原任务池的 future 传播，不新增 try/catch。LoadMilliseconds 是读取和 CPU 解码总时间，不是纯磁盘延迟、GPU 上传时间或 GPU timestamp。

淘汰仅改变下一图的 CPU 映射；新 ClusterBuffer/Residency 与槽数据上传在同一图中编排。旧帧使用原外部 Buffer 的最后 Fence，下一次 TransferDest 与后续 Vertex/SRV/Indirect 访问由 RDG 连接。这里按整个 Buffer 而不是按物理 Page 做同步，较保守，可能串行化 GPU 访问；没有为提高并发创建第二套生命周期机制。

预算不足以同时维持所有视区的细层时，非 Pinned 活跃页可在旧访问完成后被替换，下一图重新从根 Cut 细化。当前没有全局优先级或切换滞回，有限 Cache 下可能出现反复加载，统计如实计入 Evictions/Loads；正确回退不等于已经优化了流送稳定性。

## 上传、读回和释放完成点

Ready 的各属性向量对象移入本次 Builder arena，其大载荷仍在 vector 的堆存储。VirtualGeometryPageUpload 用 RDG_BUFFER_ACCESS(TransferDest) 声明六个池的写入，再在既有命令列表调用 UpdateBuffer 的 Offset/Size 进行部分上传；不是调用会阻塞的 RenderBuffer::UploadData。Metadata、Residency、ClusterPages、Feedback 清零和选择调度表同样由带访问参数的原 RDG Pass 直接 UpdateBuffer，Shader 只在上传后的读取依赖中观察新地址。

这是为了避开当前 RenderGraphAllocator 原始字节页的 64 KiB 上限：几何已分页，但全量 Cluster、Draw Metadata 或调度表仍可能超过单页，不能继续把整个数组交给会复制到 raw arena 的 AddUploadBuffer。向量对象本身很小，生命周期覆盖录制，RHI 在实际命令记录/执行时复制到自己的 staging。没有改 Builder、扩大 arena、拆出全局上传管理器或用临时保活列表掩盖问题。调用方仍按既有提交处维护访问 Fence。

每个 Cache 最多持有一个 CPU 反馈映射。Vulkan/D3D12 在图内 MapReadBuffer 记录 staging 复制，EndFrame 记录 Feedback Buffer 的提交 Fence；后续 Prepare 用现有 SyncPoint.GetCurrentValue 非阻塞检查，完成后读取并 Unmap。OpenGL 的 MapReadBuffer 本身是同步 worker 读取，因此在逻辑队列完成后调用已有读回接口；这条后端反馈仍可能阻塞，不冒充异步 DMA。忙时可跳过新的反馈抓取，不阻塞渲染，下一帧再获取。

Renderer 和测试的独立读回 Pass 可并行录制，因此 Vulkan/D3D12 RHIApi 的现有 mapped-staging 容器增加局部 mutex，插入、移除使用同一把锁，避免反馈和其他读回并发修改 map。锁不覆盖 GPU 等待或命令录制。QueueType/Value 形式的既有跨队列等待也修正为解析实际同步点；详见 [RHI 队列专题](02-rhi-queues-and-sync.md)。

Cache 销毁先等待自己的 CPU Job，然后等待未消费映射的 Fence 并 Unmap，再按池/元数据的最后访问 Fence 完成旧 GPU 用户，最后 ReleaseResources 释放这份流送资源的 GPU 缓冲，保留 CPU Build 数据。数据和 Storage 由强引用持有，不使用全局保活。资源已有活跃 Cache 时拒绝第二个 Cache，Prepare 也检查物理槽标志未被外部 Clear/Build 破坏。Viewer 关闭/重建/切换模式时沿原 WaitForLastDraw 退役旧资源，释放 Cache 后再替换数据；自建临时 `.vgpages` 在 worker 结束后删除。调用方不能在 Cache 活着时直接 Clear/Build 或 ReleaseResources 底层几何，也不能在 RHI 关闭后销毁它。

## 验证与已知边界

[VirtualGeometryStreamingTest](../../../src/Tests/VirtualGeometryStreamingTest.cpp) 用真实 Build 的 1152 面平面，逐位验证压缩往返；3 槽和全部页预算逐帧读回实际 Cut、间接物理地址、Visibility、D32 和 BaseColor，逆替换检查 Cut 能恢复完整叶集合。另测损坏 Page、仅根预算、共享资源双实例，以及不逐帧等待的连续在途图。每帧约束缓存申请字节、驻留上限、PageHits≤References，确认低预算未丢失轮廓、足预算能收敛、校验失败保留根。

同文件还注册 VirtualGeometryStreamingDAGTest，用 12 Cluster、7 Group、3 Root 的跨组图夹具验证多对多生成/消费、停滞叶岛和渐进驻留；它验证图逻辑，不冒充真实 Build 几何的边界证明。

命令为 `Tests.exe -test=VirtualGeometryStreamingTest -rhi=Vulkan`，另选 DirectX12/OpenGL 或替换为上述 DAG 测试。Viewer 验证、压缩率、槽碎片、原有 Vulkan 诊断超时及 Launcher 的实际范围见 [开发记录](../../dev/bugfix/故障与修复记录-2026-11-15.md#virtual-geometry-geometry-streaming--compression)。本版仍保留 CPU 全量 Build 几何，没有后台构建、资产打包系统、CPU 内存流送或压缩显存 Vertex Pulling；Page 文件为派生缓存而非项目保存格式。当前目标是可核查的正确流送，不宣称达到 Nanite 的压缩率、吞吐或大场景性能。
