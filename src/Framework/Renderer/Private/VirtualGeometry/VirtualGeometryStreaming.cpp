#include "VirtualGeometryStreaming.h"
#include "VirtualGeometryLODSelection.h"
#include "TaskPool.h"
#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <set>

using namespace Engine;
using namespace RenderCore;
using namespace RHI;

namespace Renderer
{
    BEGIN_SHADER_PARAMETER_STRUCT(VirtualGeometryPageUploadParameters)
    RDG_BUFFER_ACCESS(Positions, ERHIResourceAccess::TransferDest)
    RDG_BUFFER_ACCESS(Normals, ERHIResourceAccess::TransferDest)
    RDG_BUFFER_ACCESS(Tangents, ERHIResourceAccess::TransferDest)
    RDG_BUFFER_ACCESS(UV, ERHIResourceAccess::TransferDest)
    RDG_BUFFER_ACCESS(Colors, ERHIResourceAccess::TransferDest)
    RDG_BUFFER_ACCESS(Indices, ERHIResourceAccess::TransferDest)
    END_SHADER_PARAMETER_STRUCT(VirtualGeometryPageUploadParameters)

    BEGIN_SHADER_PARAMETER_STRUCT(VirtualGeometryPageFeedbackParameters)
    RDG_BUFFER_ACCESS(Feedback, ERHIResourceAccess::TransferSrc)
    END_SHADER_PARAMETER_STRUCT(VirtualGeometryPageFeedbackParameters)

    BEGIN_SHADER_PARAMETER_STRUCT(VirtualGeometryStreamingUploadParameters)
    RDG_BUFFER_ACCESS(Buffer, ERHIResourceAccess::TransferDest)
    END_SHADER_PARAMETER_STRUCT(VirtualGeometryStreamingUploadParameters)

    namespace
    {
        struct LoadedPage
        {
            uint32_t Page = 0, Slot = 0;
            VirtualGeometryPageData Data;
        };

        struct LoadJob
        {
            std::vector<uint32_t> Pages;
            std::vector<LoadedPage> Loaded;
            std::string Error;
            double Milliseconds = 0;
            Core::TaskHandle Task;
        };

        std::unique_ptr<RenderBuffer> CreateBuffer(uint64_t words, const char* name)
        {
            RHIBufferDesc desc;
            desc.Size = std::max<uint64_t>(1, words) * 4;
            desc.Stride = 4;
            desc.Usage = ERHIBufferUsageFlag::Structured | ERHIBufferUsageFlag::ShaderResource |
                         ERHIBufferUsageFlag::UnorderedAccess | ERHIBufferUsageFlag::TransferDst | ERHIBufferUsageFlag::TransferSrc;
            desc.DebugName = name;
            auto buffer = std::make_unique<RenderBuffer>(desc);
            buffer->InitRHIResource();
            return buffer;
        }

        bool Completed(const RHIFence& fence)
        {
            return !fence.Value || GRHIApi->GetQueue(fence.QueueType)->GetSyncPoint()->GetCurrentValue() >= fence.Value;
        }

        void Wait(RenderBuffer* buffer)
        {
            if (buffer)
            {
                const auto fence = buffer->GetTracker().GetLastAccessFence();
                if (fence.Value)
                {
                    GRHIApi->GetQueue(fence.QueueType)->WaitValue(fence.Value);
                }
            }
        }
    }

    struct VirtualGeometryStreaming::State
    {
        std::shared_ptr<StaticMeshRenderData> Data;
        std::shared_ptr<const VirtualGeometryPageStorage> Storage;
        Core::TaskPool Pool{1};
        std::shared_ptr<LoadJob> Job;
        std::vector<LoadedPage> Ready;
        std::vector<uint32_t> Resident, PageSlots, SlotPages, ClusterPages;
        std::vector<uint64_t> LastUse;
        std::vector<uint8_t> Failed;
        std::vector<VirtualGeometryCluster> PhysicalClusters;
        std::unique_ptr<RenderBuffer> ResidencyBuffer, ClusterPageBuffer, FeedbackBuffer;
        RenderGraphBufferRef Residency = nullptr, Pages = nullptr, Feedback = nullptr;
        RenderGraphBuilder* Graph = nullptr;
        void* Mapped = nullptr;
        RHIFence FeedbackFence;
        bool bPendingReadback = false, bCaptureThisFrame = false;
        uint64_t Epoch = 0;
        VirtualGeometryStreamingStatistics Statistics;
        std::string Error;

        void AssignPage(uint32_t page, uint32_t slot)
        {
            PageSlots[page] = slot;
            SlotPages[slot] = page;
            LastUse[page] = Epoch;
            const auto& locations = Storage->GetLocations();
            for (const auto cluster : Storage->GetPages()[page].Clusters)
            {
                auto& physical = PhysicalClusters[cluster];
                physical.VertexOffset = slot * VIRTUAL_GEOMETRY_PAGE_VERTICES + locations[cluster].VertexOffset;
                physical.IndexOffset = slot * VIRTUAL_GEOMETRY_PAGE_INDICES + locations[cluster].IndexOffset;
                Resident[cluster] = 1;
            }
        }

        void Evict(uint32_t page)
        {
            SlotPages[PageSlots[page]] = UINT32_MAX;
            PageSlots[page] = UINT32_MAX;
            for (const auto cluster : Storage->GetPages()[page].Clusters)
            {
                Resident[cluster] = 0;
                PhysicalClusters[cluster].VertexOffset = PhysicalClusters[cluster].IndexOffset = 0;
            }
            ++Statistics.Evictions;
        }

        void ProcessFeedback(const uint32_t* words)
        {
            const auto& geometry = Data->VirtualGeometryData;
            const auto& pages = Storage->GetPages();
            ++Epoch;
            ++Statistics.FeedbackFrames;
            for (uint32_t page = 0; page < pages.size(); ++page)
            {
                const auto flags = words[geometry.Groups.size() + page];
                Statistics.PageReferences += (flags & 1) != 0;
                Statistics.PageHits += (flags & 2) != 0;
                if (flags)
                {
                    LastUse[page] = Epoch;
                }
            }
            if (Job)
            {
                return;
            }
            // GPU requests are idempotent Group bits. Reserve ALL pages for a refinement together.
            for (uint32_t cursor = uint32_t(geometry.Groups.size()); cursor > 0; --cursor)
            {
                const uint32_t groupIndex = cursor - 1;
                if (!words[groupIndex])
                {
                    continue;
                }
                const auto& group = geometry.Groups[groupIndex];
                std::set<uint32_t> required;
                bool failed = false;
                for (uint32_t i = 0; i < group.ChildClusterCount; ++i)
                {
                    const auto page = ClusterPages[geometry.GroupChildClusterIndices[group.ChildClusterOffset + i]];
                    required.insert(page);
                    failed |= Failed[page] != 0;
                }
                uint32_t missing = 0, protectedPages = Statistics.PinnedPages;
                for (const auto page : required)
                {
                    missing += PageSlots[page] == UINT32_MAX;
                    protectedPages += !pages[page].bPinned;
                }
                if (failed || !missing)
                {
                    continue;
                }
                if (protectedPages > SlotPages.size())
                {
                    ++Statistics.BudgetBlocked;
                    continue;
                }
                auto job = std::make_shared<LoadJob>();
                for (const auto page : required)
                {
                    if (PageSlots[page] == UINT32_MAX)
                    {
                        job->Pages.push_back(page);
                    }
                }
                // Eviction is published with the upload in the NEXT RDG before any new selection.
                // Previous frames retain their GPU ordering via the same external buffers' last-use fences.
                while (std::count(SlotPages.begin(), SlotPages.end(), UINT32_MAX) < missing)
                {
                    uint32_t victim = UINT32_MAX;
                    for (uint32_t page = 0; page < pages.size(); ++page)
                    {
                        if (PageSlots[page] != UINT32_MAX && !pages[page].bPinned && !required.contains(page) &&
                            (victim == UINT32_MAX || LastUse[page] < LastUse[victim]))
                        {
                            victim = page;
                        }
                    }
                    if (victim == UINT32_MAX)
                    {
                        throw std::logic_error("Streaming page reservation invariant failed");
                    }
                    Evict(victim);
                }
                Job = job;
                Statistics.LoadingPages = missing;
                const auto storage = Storage;
                job->Task = Pool.AddTask([job, storage]()
                {
                    const auto start = std::chrono::steady_clock::now();
                    for (const auto page : job->Pages)
                    {
                        LoadedPage loaded;
                        loaded.Page = page;
                        if (!storage->ReadPage(page, loaded.Data, job->Error))
                        {
                            break;
                        }
                        job->Loaded.push_back(std::move(loaded));
                    }
                    job->Milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
                });
                break;
            }
        }
    };

    VirtualGeometryStreaming::VirtualGeometryStreaming(std::shared_ptr<StaticMeshRenderData> data,
        std::shared_ptr<const VirtualGeometryPageStorage> storage, uint64_t budgetBytes) : m_State(std::make_unique<State>())
    {
        auto& s = *m_State;
        if (!data || !storage || !data->VirtualGeometryData.IsValid() ||
            !storage->Matches(data->VirtualGeometryData))
        {
            throw std::invalid_argument("Streaming requires built geometry and a matching page archive");
        }
        s.Data = std::move(data);
        s.Storage = std::move(storage);
        auto& geometry = s.Data->VirtualGeometryData;
        if (geometry.StreamingPageSlots)
        {
            throw std::invalid_argument("A geometry resource may only have one live streaming cache");
        }
        const auto& pages = s.Storage->GetPages();
        auto& stats = s.Statistics;
        stats.TotalPages = uint32_t(pages.size());
        stats.PinnedPages = uint32_t(std::count_if(pages.begin(), pages.end(), [](const auto& page) { return page.bPinned; }));
        stats.MetadataBytes = geometry.Clusters.size() * (sizeof(VirtualGeometryCluster) + 8ull) +
            geometry.Groups.size() * (sizeof(VirtualGeometryGroup) + 4ull) + pages.size() * 4ull +
            (geometry.GroupChildClusterIndices.size() + geometry.GroupParentClusterIndices.size() + geometry.RootGroupIndices.size()) * 4ull;
        const uint64_t slots = std::min<uint64_t>(pages.size(), budgetBytes > stats.MetadataBytes ?
            (budgetBytes - stats.MetadataBytes) / VIRTUAL_GEOMETRY_PAGE_BYTES : 0);
        if (!stats.PinnedPages || slots < stats.PinnedPages || slots * VIRTUAL_GEOMETRY_PAGE_VERTICES * 16 > UINT32_MAX)
        {
            throw std::invalid_argument("Streaming budget cannot hold the pinned root cut or exceeds buffer limits");
        }
        stats.BudgetBytes = budgetBytes;
        stats.CacheBytes = slots * VIRTUAL_GEOMETRY_PAGE_BYTES + stats.MetadataBytes;
        stats.RawBytes = s.Storage->GetRawBytes();
        stats.StoredBytes = s.Storage->GetStoredBytes();
        s.PageSlots.resize(pages.size(), UINT32_MAX);
        s.SlotPages.resize(slots, UINT32_MAX);
        s.LastUse.resize(pages.size());
        s.Failed.resize(pages.size());
        s.Resident.resize(geometry.Clusters.size());
        s.PhysicalClusters = geometry.Clusters;
        for (auto& c : s.PhysicalClusters)
        {
            c.VertexOffset = c.IndexOffset = 0;
        }
        for (const auto& location : s.Storage->GetLocations())
        {
            s.ClusterPages.push_back(location.Page);
        }
        // Bootstrap is deliberately synchronous CPU I/O; root residency is guaranteed before the first frame.
        uint32_t slot = 0;
        for (uint32_t page = 0; page < pages.size(); ++page)
        {
            if (pages[page].bPinned)
            {
                LoadedPage loaded;
                loaded.Page = page;
                loaded.Slot = slot;
                if (!s.Storage->ReadPage(page, loaded.Data, s.Error))
                {
                    throw std::runtime_error(s.Error);
                }
                s.AssignPage(page, slot++);
                s.Ready.push_back(std::move(loaded));
            }
        }
        geometry.InitializeStreamingResources(uint32_t(slots));
        s.ResidencyBuffer = CreateBuffer(s.Resident.size(), "VirtualGeometry.Residency");
        s.ClusterPageBuffer = CreateBuffer(s.ClusterPages.size(), "VirtualGeometry.ClusterPages");
        s.FeedbackBuffer = CreateBuffer(geometry.Groups.size() + pages.size(), "VirtualGeometry.StreamingFeedback");
    }

    VirtualGeometryStreaming::~VirtualGeometryStreaming()
    {
        auto& s = *m_State;
        if (s.Job)
        {
            s.Job->Task.Wait();
        }
        if (s.Mapped)
        {
            GRHIApi->GetQueue(s.FeedbackFence.QueueType)->WaitValue(s.FeedbackFence.Value);
            GRHIApi->Unmap(s.Mapped);
        }
        auto& g = s.Data->VirtualGeometryData;
        for (auto* buffer : {g.PositionBuffer.get(), g.NormalBuffer.get(), g.TangentBuffer.get(), g.TexCoordBuffer.get(),
             g.ColorBuffer.get(), g.IndexBuffer.get(), g.ClusterBuffer.get(), g.GroupBuffer.get(),
             g.GroupChildClusterIndexBuffer.get(), g.GroupParentClusterIndexBuffer.get(), g.RootGroupIndexBuffer.get(), s.ResidencyBuffer.get(),
             s.ClusterPageBuffer.get(), s.FeedbackBuffer.get()})
        {
            Wait(buffer);
        }
        g.ReleaseResources();
    }

    void VirtualGeometryStreaming::Prepare(RenderGraphBuilder& builder)
    {
        auto& s = *m_State;
        if (s.Data->VirtualGeometryData.StreamingPageSlots != s.SlotPages.size())
        {
            throw std::logic_error("Streaming geometry was released or rebuilt while its cache was active");
        }
        if (s.Graph == &builder)
        {
            return;
        }
        if (s.Graph)
        {
            throw std::logic_error("Streaming EndFrame must follow graph Execute before another Prepare");
        }
        if (s.bPendingReadback && Completed(s.FeedbackFence))
        {
            if (GShaderPlatform == ERHIShaderPlatform::OpenGL)
            {
                auto* queue = GRHIApi->GetQueue(EQueueType::Graphics);
                auto* context = queue->AcquireCommandContext();
                RHICommandListBase cmd(context);
                cmd.SetImmediate(true);
                cmd.Begin();
                s.Mapped = GRHIApi->MapReadBuffer(cmd, s.FeedbackBuffer->GetRHI(), {});
                cmd.End();
                queue->WaitValue(queue->ExecuteContext(context));
            }
            if (!s.Mapped)
            {
                throw std::runtime_error("Streaming feedback map failed");
            }
            s.ProcessFeedback(static_cast<const uint32_t*>(s.Mapped));
            GRHIApi->Unmap(s.Mapped);
            s.Mapped = nullptr;
            s.bPendingReadback = false;
        }
        if (s.Job && s.Job->Task.IsCompleted())
        {
            s.Job->Task.Future.get();
            s.Statistics.LoadMilliseconds += s.Job->Milliseconds;
            s.Statistics.LoadingPages = 0;
            if (!s.Job->Error.empty())
            {
                s.Error = s.Job->Error;
                s.Statistics.FailedLoads += s.Job->Pages.size();
                for (const auto page : s.Job->Pages)
                {
                    s.Failed[page] = 1;
                }
            }
            else
            {
                for (auto& loaded : s.Job->Loaded)
                {
                    const auto free = std::find(s.SlotPages.begin(), s.SlotPages.end(), UINT32_MAX);
                    loaded.Slot = uint32_t(free - s.SlotPages.begin());
                    s.AssignPage(loaded.Page, loaded.Slot);
                    s.Ready.push_back(std::move(loaded));
                    ++s.Statistics.Loads;
                }
            }
            s.Job.reset();
        }
        s.Graph = &builder;
        const auto prefix = "VirtualGeometry.Stream." + std::to_string(reinterpret_cast<uintptr_t>(this));
        s.Residency = builder.RegisterExternalBuffer(prefix + ".Residency", s.ResidencyBuffer.get());
        s.Pages = builder.RegisterExternalBuffer(prefix + ".Pages", s.ClusterPageBuffer.get());
        s.Feedback = builder.RegisterExternalBuffer(prefix + ".Feedback", s.FeedbackBuffer.get());
        const auto upload = [&](RenderGraphBufferRef buffer, const auto& source)
        {
            auto* snapshot = builder.AllocateParameter<std::decay_t<decltype(source)>>();
            *snapshot = source;
            auto* p = builder.AllocateParameter<VirtualGeometryStreamingUploadParameters>();
            p->Buffer = buffer;
            builder.AddPass("VirtualGeometryStreamingTableUpload", VirtualGeometryStreamingUploadParameters::GetMetaData(),
                            p, EPassFlag::Graphic, [=](RHICommandListBase& cmd)
            {
                RHIUpdateBufferRegion region;
                region.Size = snapshot->size() * sizeof((*snapshot)[0]);
                GRHIApi->UpdateBuffer(cmd, p->Buffer->GetRHIBuffer(), snapshot->data(), region);
            });
        };
        upload(s.Residency, s.Resident);
        upload(s.Pages, s.ClusterPages);
        auto physical = builder.RegisterExternalBuffer(prefix + ".Clusters", s.Data->VirtualGeometryData.ClusterBuffer.get());
        upload(physical, s.PhysicalClusters);
        const std::vector<uint32_t> zeros(s.Data->VirtualGeometryData.Groups.size() + s.PageSlots.size(), 0);
        upload(s.Feedback, zeros);
        if (!s.Ready.empty())
        {
            auto* pages = builder.AllocateParameter<std::vector<LoadedPage>>();
            *pages = std::move(s.Ready);
            s.Ready.clear();
            auto& g = s.Data->VirtualGeometryData;
            auto* p = builder.AllocateParameter<VirtualGeometryPageUploadParameters>();
            p->Positions = builder.RegisterExternalBuffer(prefix + ".Positions", g.PositionBuffer.get());
            p->Normals = builder.RegisterExternalBuffer(prefix + ".Normals", g.NormalBuffer.get());
            p->Tangents = builder.RegisterExternalBuffer(prefix + ".Tangents", g.TangentBuffer.get());
            p->UV = builder.RegisterExternalBuffer(prefix + ".UV", g.TexCoordBuffer.get());
            p->Colors = builder.RegisterExternalBuffer(prefix + ".Colors", g.ColorBuffer.get());
            p->Indices = builder.RegisterExternalBuffer(prefix + ".Indices", g.IndexBuffer.get());
            builder.AddPass("VirtualGeometryPageUpload", VirtualGeometryPageUploadParameters::GetMetaData(), p,
                            EPassFlag::Graphic, [=](RHICommandListBase& cmd)
            {
                const auto copy = [&](RenderGraphBufferRef buffer, const auto& data, uint32_t base)
                {
                    using Element = typename std::decay_t<decltype(data)>::value_type;
                    RHIUpdateBufferRegion region;
                    region.Offset = uint64_t(base) * sizeof(Element);
                    region.Size = data.size() * sizeof(Element);
                    GRHIApi->UpdateBuffer(cmd, buffer->GetRHIBuffer(), data.data(), region);
                };
                for (const auto& page : *pages)
                {
                    const auto vertex = page.Slot * VIRTUAL_GEOMETRY_PAGE_VERTICES;
                    copy(p->Positions, page.Data.Positions, vertex);
                    copy(p->Normals, page.Data.Normals, vertex);
                    copy(p->Tangents, page.Data.Tangents, vertex);
                    copy(p->UV, page.Data.TexCoords, vertex);
                    copy(p->Colors, page.Data.Colors, vertex);
                    copy(p->Indices, page.Data.Indices, page.Slot * VIRTUAL_GEOMETRY_PAGE_INDICES);
                }
            });
            for (const auto& page : *pages)
            {
                s.Statistics.UploadedBytes += page.Data.Positions.size() * 64 + page.Data.Indices.size() * 4;
            }
        }
        s.Statistics.ResidentPages = uint32_t(std::count_if(s.PageSlots.begin(), s.PageSlots.end(),
            [](uint32_t slot) { return slot != UINT32_MAX; }));
    }

    void VirtualGeometryStreaming::AddFeedbackReadback(RenderGraphBuilder& builder)
    {
        auto& s = *m_State;
        if (s.bPendingReadback || s.bCaptureThisFrame)
        {
            return;
        }
        auto* p = builder.AllocateParameter<VirtualGeometryPageFeedbackParameters>();
        p->Feedback = s.Feedback;
        builder.AddPass("VirtualGeometryStreamingFeedback", VirtualGeometryPageFeedbackParameters::GetMetaData(), p,
                        EPassFlag::Graphic, [this, p](RHICommandListBase& cmd)
        {
            if (GShaderPlatform != ERHIShaderPlatform::OpenGL)
            {
                m_State->Mapped = GRHIApi->MapReadBuffer(cmd, p->Feedback->GetRHIBuffer(), {});
            }
        });
        s.bCaptureThisFrame = true;
    }

    void VirtualGeometryStreaming::EndFrame()
    {
        auto& s = *m_State;
        if (!s.Graph)
        {
            return;
        }
        if (s.bCaptureThisFrame)
        {
            s.FeedbackFence = s.FeedbackBuffer->GetTracker().GetLastAccessFence();
            s.bPendingReadback = true;
            s.bCaptureThisFrame = false;
        }
        s.Graph = nullptr;
        s.Residency = s.Pages = s.Feedback = nullptr;
    }

    RenderGraphBufferRef VirtualGeometryStreaming::GetResidencyBuffer() const { return m_State->Residency; }
    RenderGraphBufferRef VirtualGeometryStreaming::GetClusterPageBuffer() const { return m_State->Pages; }
    RenderGraphBufferRef VirtualGeometryStreaming::GetFeedbackBuffer() const { return m_State->Feedback; }
    const VirtualGeometryResource& VirtualGeometryStreaming::GetResource() const { return m_State->Data->VirtualGeometryData; }
    const std::vector<uint32_t>& VirtualGeometryStreaming::GetResidency() const { return m_State->Resident; }
    const std::vector<VirtualGeometryCluster>& VirtualGeometryStreaming::GetPhysicalClusters() const { return m_State->PhysicalClusters; }
    const VirtualGeometryStreamingStatistics& VirtualGeometryStreaming::GetStatistics() const { return m_State->Statistics; }
    const std::string& VirtualGeometryStreaming::GetLastError() const { return m_State->Error; }
    uint32_t VirtualGeometryStreaming::GetPageCount() const { return uint32_t(m_State->PageSlots.size()); }

    std::vector<uint32_t> VirtualGeometryStreaming::SelectResidentReference(const Core::Mat4& transform,
        const SceneView& view, float targetPixelError) const
    {
        const auto& g = GetResource();
        const auto desired = SelectVirtualGeometryClusters(g, transform, view, targetPixelError);
        std::vector<uint32_t> wanted(g.Groups.size()), applied(g.Groups.size());
        // Recover desired Group application from its Cut, in the inverse of the normal coarsening order.
        std::vector<uint8_t> selected(g.Clusters.size());
        for (const auto c : desired)
        {
            selected[c] = 1;
        }
        for (uint32_t cursor = uint32_t(g.Groups.size()); cursor > 0; --cursor)
        {
            const auto index = cursor - 1;
            const auto& group = g.Groups[index];
            bool present = group.ParentClusterCount != 0;
            for (uint32_t i = 0; i < group.ParentClusterCount; ++i)
            {
                present &= selected[g.GroupParentClusterIndices[group.ParentClusterOffset + i]] != 0;
            }
            if (present)
            {
                wanted[index] = 1;
                for (uint32_t i = 0; i < group.ParentClusterCount; ++i)
                {
                    selected[g.GroupParentClusterIndices[group.ParentClusterOffset + i]] = 0;
                }
                for (uint32_t i = 0; i < group.ChildClusterCount; ++i)
                {
                    selected[g.GroupChildClusterIndices[group.ChildClusterOffset + i]] = 1;
                }
            }
        }
        std::vector<uint32_t> queue;
        const auto enqueue = [&](uint32_t group)
        {
            if (!applied[group])
            {
                applied[group] = 1;
                queue.push_back(group);
            }
        };
        for (uint32_t group = 0; group < wanted.size(); ++group)
        {
            if (wanted[group])
            {
                enqueue(group);
            }
        }
        for (uint32_t c = 0; c < g.Clusters.size(); ++c)
        {
            if (g.Clusters[c].GeneratingGroupIndex == UINT32_MAX && !m_State->Resident[c])
            {
                enqueue(g.Clusters[c].GroupIndex);
            }
        }
        for (size_t head = 0; head < queue.size(); ++head)
        {
            const auto& group = g.Groups[queue[head]];
            for (uint32_t i = 0; i < group.ChildClusterCount; ++i)
            {
                const auto generator = g.Clusters[g.GroupChildClusterIndices[group.ChildClusterOffset + i]].GeneratingGroupIndex;
                if (generator != UINT32_MAX)
                {
                    enqueue(generator);
                }
            }
            for (uint32_t i = 0; i < group.ParentClusterCount; ++i)
            {
                const auto c = g.GroupParentClusterIndices[group.ParentClusterOffset + i];
                if (!m_State->Resident[c])
                {
                    enqueue(g.Clusters[c].GroupIndex);
                }
            }
        }
        std::vector<uint32_t> result;
        for (uint32_t c = 0; c < g.Clusters.size(); ++c)
        {
            const auto& cluster = g.Clusters[c];
            if ((cluster.GeneratingGroupIndex == UINT32_MAX || applied[cluster.GeneratingGroupIndex]) && !applied[cluster.GroupIndex])
            {
                result.push_back(c);
            }
        }
        return result;
    }
}
