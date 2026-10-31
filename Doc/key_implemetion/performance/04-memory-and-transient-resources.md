# 内存、资源池与瞬态资源

架构：[RHI](../../architecture/rhi/rhi.md)、[RenderCore](../../architecture/render/rendercore.md)。这里区分 CPU 包装对象、后端资源句柄和底层物理分配，三者寿命不自动相等。

## 三种不同的复用

| 机制 | 源码 | 复用单位 |
| --- | --- | --- |
| RenderTargetPool | [RenderResource.cpp](../../../src/Framework/RenderCore/Private/RenderResource.cpp) | 描述符匹配的长期 RenderTexture 包装 |
| RHITransientResourceManager | [RHITransientResource.cpp](../../../src/Framework/RHI/Private/RHITransientResource.cpp) | 满足描述符/引用/后端完成条件的完整资源 |
| RHITransientHeap、VulkanMemoryBlock | 同上、[VulkanMemory.cpp](../../../src/Framework/RHIVulkan/Private/VulkanMemory.cpp) | 原生内存中的字节区间 |

资源已不再被本帧逻辑引用，不等于 GPU 已不用它；空闲字节很多，也不等于存在满足大小和对齐的连续区间。

## 通用 transient heap 的 first-fit

RHITransientHeap 保存按位置排列的 Block{Offset,Size,bUsed}，Allocate 在 mutex 内找第一个可满足请求的空闲块。要求 alignment 为非零二次幂，并检查上溢。

`aligned = (offset + alignment - 1) & ~(alignment - 1)`；padding=aligned-offset，检查 `size <= freeSize-padding`，再拆成前导空闲、使用中、尾部空闲三段。Release 必须精确匹配仍在使用的 Offset/Size，随后合并相邻空闲块。RHITransientAllocation 的析构把区间交回 Heap；它强引用 Heap，避免底层块先消失。

例如空闲 `[0,1024)`，分配 size=100、align=256 得 `[0,100)`；再分配同尺寸得 `[256,356)`，中间 `[100,256)` 仍然空闲。FreeBytes 包含这段，但未必满足下一次大分配。搜索和 vector 插删为线性成本，不是 buddy 或 slab 分配器。

## 完整资源的复用条件

manager 的 Entry 包含资源强引用、wrapper 弱引用和 bReleased。可用条件是 `(显式释放或 wrapper 已失效) && Object.use_count()==1`，再比较完整描述符、调用后端 CanRecycle。图内 begin/end 只作标签，不允许活资源因为 Pass 序号不重叠就共享同一物理区间。

CreateOwned 用 aliasing shared_ptr 同时保留 DebugName 字符串和原生资源，防止 desc.DebugName 指向短生命周期内存。显式释放 wrapper 不会强行让还存在的视图或外部资源引用失效。GarbageCollect 删除可用且后端允许回收的 entries，再裁剪无人引用且全空的 heaps。

锁保护管理数据，但 raw-pointer view/context 仍要求调用者保活；不能从 manager 有 mutex 推导整个资源系统线程安全。

## 三个后端的真实差异

- [VulkanTransientResource.cpp](../../../src/Framework/RHIVulkan/Private/VulkanTransientResource.cpp)：先创建未绑定 image/buffer、查询 requirements，再按 memory type、device-address 类和 image 类选择 Heap，绑定 offset。ImageClass 将图像与缓冲区区分；地址标记决定 allocation flags。当前 CanRecycle 和专用 deleter 使用保守的队列 WaitIdle，析构时也等待后清理。这是现有实现的同步成本，不是无阻塞精确 fence 分配器。
- [DirectX12TransientResource.cpp](../../../src/Framework/RHIDirectX12/Private/DirectX12TransientResource.cpp)：当前创建完整 committed 资源，不使用通用 Heap 子分配。CanRecycle 结合 ContextUses 和 LastQueue/LastFence，PrepareForReuse 清除 DeclaredAccess。不能将接口里的 Heap 类型理解成已实现 D3D12 placed-resource aliasing。
- [OpenGLTransientResource.cpp](../../../src/Framework/RHIOpenGL/Private/OpenGLTransientResource.cpp)：复用完整 GL 对象；非 worker 回收时等待逻辑队列并在 worker 执行 glFinish，worker 内 CanRecycle 返回 false，避免自等。OpenGL 不暴露与 Vulkan 类似的显式物理 heap。

## RenderCore 的池、Tracker 与分配器

RenderTargetPool 将 MarkUsed 与最后访问 Fence 一起用于可复用判断；代码中的完成值比较含严格 `<`，不要文档化为普遍的 `<=`。TransientResourceAllocator 保存已分配包装，GarbageCollect 用 graphics/compute 完成值比较 Tracker，完成后通知 backend manager 释放。

它们都要求提交方正确 UpdateLastAccessFence。缺少更新会过早复用；随意补 WaitIdle 只能隐藏时序错误。当前没有材质专用 MaterialFrame 或通用 retained 对象机制。

## 非 transient 内存、上传和销毁

VulkanMemoryManager 按 memory type 管理通常 64 MiB 起的大块，block 内 first-fit 对齐切分，Free 后排序合并空闲段。staging manager 保存上传/映射 buffer，并关联提交的 command buffer 再回收；不能在 CPU memcpy 返回时直接复用 GPU 仍读取的 staging。

VulkanResource 的句柄销毁会进入 Device deferred delete 队列，但当前 ReleaseResources 遍历释放，frameDelay 未实际用于完成点判断；普通内存 Free 与延后句柄销毁也不是同一操作。**不能仅依据类名 Deferred 就宣称资源自动按 GPU fence 安全销毁。**

D3D12 区分 DEFAULT/UPLOAD/READBACK heap，纹理上传/读回依据原生 footprint 处理 row pitch，读回再去行填充；descriptor 槽有独立分配/释放路径。context 的暂存资源需要覆盖实际命令执行期，见 [队列专题](02-rhi-queues-and-sync.md)。

GL 句柄创建/删除转发 context worker；删除按值捕获句柄，不能捕获正在析构的 this。worker 停止后资源仍存活是另一类错误，不能用异步投递修复缺失 context。

默认白/平面法线纹理由 InitGlobalRenderResource 创建、上传，在 ReleaseGlobalRenderResource 释放，不是 TextureAsset。距离场 Atlas、阴影 Atlas 及实例缓冲的逻辑分配另见 [资产和实例](05-assets-materials-and-instance-data.md)、[距离场](../algorithm/04-distance-fields-and-jump-flood.md)、[阴影](../algorithm/06-shadow-mapping-and-cascades.md)。

验证入口：MaterialBindingTest 的真实默认纹理读回只能证明该上传/读回路径；RDG 与 RHI 图形测试不能自动证明碎片、并发回收或异常销毁安全。本轮没有运行 allocator 压力测试。

## RDG 上传快照的单页上限

[RenderGraphAllocator::AllocateRaw](../../../src/Framework/RenderCore/PublicHeader/RenderGraphBuilder.h) 默认每页 64KiB。超过当前页时只切换到另一张同样大小的页面，没有为 size>pageSize 创建大页或拒绝请求；随后 memcpy 会越界。因此不能从 AllocateBytes 接口形式推断任意大上传都被支持。分块网格首次使用 10000×Float4（160000 字节）上传时触发 Windows 堆错误 0xC0000374，见 [故障记录](../../dev/bugfix/故障与修复记录-2026-10-31.md)。

当前 HintRender 控制格点只有整数 x/z=0…99，按每轴 16 位打包后为 40000 字节，区域索引最大为 39204 字节，每项上传都在单页内；AddUploadBuffer 会在页剩余不足时单独换页。打包不损失本身的坐标精度，Shader 再解码并乘区域边长。它只说明该调用满足现有分配器边界，不代表通用 arena 的大请求缺陷已经修复；其他大上传及过度对齐请求仍须单独检查。未增加大页管理器、常驻网格缓存或全局保活机制。
