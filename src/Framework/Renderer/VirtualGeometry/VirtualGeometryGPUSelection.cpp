#include "VirtualGeometryGPUSelection.h"
#include "GlobalShader.h"
#include "RHIPipelineStateCache.h"
#include <algorithm>
#include <cstddef>
#include <cmath>
#include <stdexcept>

using namespace RenderCore;
using namespace RHI;

namespace Renderer
{
    BEGIN_SHADER_PARAMETER_STRUCT(VirtualGeometryGPUWord)
    SHADER_PARAMETER(uint32_t, Value)
    END_SHADER_PARAMETER_STRUCT(VirtualGeometryGPUWord)

    BEGIN_SHADER_PARAMETER_STRUCT(VirtualGeometryGPUSelectionParameters)
    SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, SelectionClusters)
    SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, SelectionGroups)
    SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, SelectionChildren)
    SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, SelectionOrder)
    SHADER_PARAMETER_RDG_RWSTRUCTURED_BUFFER(VirtualGeometryGPUWord, AppliedGroups)
    SHADER_PARAMETER_RDG_RWSTRUCTURED_BUFFER(VirtualGeometryGPUWord, ClusterFlags)
    SHADER_PARAMETER_RDG_RWSTRUCTURED_BUFFER(VirtualGeometryGPUWord, VisibleClusters)
    SHADER_PARAMETER_RDG_RWSTRUCTURED_BUFFER(VirtualGeometryGPUWord, Statistics)
    SHADER_PARAMETER_RDG_RWSTRUCTURED_BUFFER(VirtualGeometryGPUWord, IndirectArguments)
    SHADER_PARAMETER(Core::Mat4, SelectionLocalToClip)
    SHADER_PARAMETER(Core::Float4, SelectionRowLengths)
    SHADER_PARAMETER(Core::Float4, SelectionNearPlane)
    SHADER_PARAMETER(Core::Float2, SelectionViewportSize)
    SHADER_PARAMETER(float, SelectionTargetError)
    SHADER_PARAMETER(uint32_t, SelectionZeroToOne)
    SHADER_PARAMETER(uint32_t, SelectionCull)
    SHADER_PARAMETER(uint32_t, SelectionClusterCount)
    SHADER_PARAMETER(uint32_t, SelectionOrderOffset)
    SHADER_PARAMETER(uint32_t, SelectionWorkCount)
    END_SHADER_PARAMETER_STRUCT(VirtualGeometryGPUSelectionParameters)

    class VirtualGeometrySelectGroupsCS : public GlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER_TYPE(VirtualGeometrySelectGroupsCS)
        static bool ShouldCompilePermutation(const ShaderPermutationParameters&) { return true; }
        static void ModifyShaderCompilerEnvironment(const ShaderPermutationParameters&, ShaderCompilerEnvironment& env)
        {
            env.SetDefine("VG_CLUSTER_WORDS", sizeof(Engine::VirtualGeometryCluster) / 4);
            env.SetDefine("VG_GROUP_WORDS", sizeof(Engine::VirtualGeometryGroup) / 4);
        }
        static const ShaderParametersMetadata* GetShaderParameterMetadata()
        {
            return VirtualGeometryGPUSelectionParameters::GetMetaData();
        }
    };
    class VirtualGeometrySelectClustersCS : public GlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER_TYPE(VirtualGeometrySelectClustersCS)
        static bool ShouldCompilePermutation(const ShaderPermutationParameters&) { return true; }
        static void ModifyShaderCompilerEnvironment(const ShaderPermutationParameters& parameters, ShaderCompilerEnvironment& env)
        {
            VirtualGeometrySelectGroupsCS::ModifyShaderCompilerEnvironment(parameters, env);
        }
        static const ShaderParametersMetadata* GetShaderParameterMetadata()
        {
            return VirtualGeometryGPUSelectionParameters::GetMetaData();
        }
    };
    IMPLEMENT_GLOBAL_SHADER_TYPE(VirtualGeometrySelectGroupsCS, "VirtualGeometrySelectGroupsCS",
                                 "/virtualgeometry/GPUSelection.sf", "SelectGroupsCS", ERHIShaderFrequency::Compute);
    IMPLEMENT_GLOBAL_SHADER_TYPE(VirtualGeometrySelectClustersCS, "VirtualGeometrySelectClustersCS",
                                 "/virtualgeometry/GPUSelection.sf", "SelectClustersCS", ERHIShaderFrequency::Compute);

    namespace
    {
        RenderGraphBufferRef Buffer(RenderGraphBuilder& builder, const std::string& name, uint64_t words,
                                    bool indirect = false)
        {
            RenderGraphBufferDesc desc;
            desc.Size = words * sizeof(uint32_t);
            desc.Stride = sizeof(uint32_t);
            desc.Usage = ERHIBufferUsageFlag::Structured | ERHIBufferUsageFlag::ShaderResource |
                         ERHIBufferUsageFlag::UnorderedAccess | ERHIBufferUsageFlag::TransferDst |
                         ERHIBufferUsageFlag::TransferSrc;
            if (indirect)
            {
                desc.Usage |= ERHIBufferUsageFlag::Indirect;
            }
            return builder.CreateBuffer(name, desc);
        }

        RenderGraphBufferSRVRef SRV(RenderGraphBuilder& builder, RenderGraphBufferRef buffer)
        {
            RenderGraphBufferSRVDesc desc;
            desc.Buffer = buffer;
            desc.Stride = 4;
            desc.NumElements = buffer->GetDesc().Size / 4;
            return builder.CreateBufferSRV(buffer->GetName() + ".SRV", desc);
        }

        RenderGraphBufferUAVRef UAV(RenderGraphBuilder& builder, RenderGraphBufferRef buffer)
        {
            RenderGraphBufferUAVDesc desc;
            desc.Buffer = buffer;
            desc.Stride = 4;
            desc.NumElements = buffer->GetDesc().Size / 4;
            return builder.CreateBufferUAV(buffer->GetName() + ".UAV", desc);
        }

        RenderGraphBufferSRVRef External(RenderGraphBuilder& builder, RenderBuffer* buffer, const std::string& name)
        {
            if (!buffer || !buffer->GetRHI())
            {
                throw std::invalid_argument("GPU virtual geometry selection requires uploaded DAG buffers");
            }
            return SRV(builder, builder.RegisterExternalBuffer(name, buffer));
        }

        void Dispatch(RenderGraphBuilder& builder, Shader* shader, VirtualGeometryGPUSelectionParameters* parameters)
        {
            if (!shader)
            {
                throw std::runtime_error("GPU virtual geometry selection shader compilation failed");
            }
            RHIComputePipelineStateDesc desc;
            desc.ComputeShader = dynamic_cast<RHIComputeShader*>(shader->GetRHIShader());
            auto pipeline = RHIPipelineStateCache::GetOrCreateComputePipelineState(desc);
            builder.AddPass("VirtualGeometryGPUSelection", VirtualGeometryGPUSelectionParameters::GetMetaData(),
                            parameters, EPassFlag::Compute, [=](RHICommandListBase& list)
            {
                auto& cmd = static_cast<RHIComputeCommandList&>(list);
                cmd.SetComputePipelineState(pipeline);
                SetShaderParameters(cmd, shader, parameters);
                const uint32_t groups = (parameters->SelectionWorkCount + 63) / 64;
                cmd.Dispatch(std::min(65535u, groups), (groups + 65534) / 65535, 1);
            });
        }
    }

    VirtualGeometryGPUSelection AddVirtualGeometryGPUSelectionPass(RenderGraphBuilder& builder,
        const Engine::VirtualGeometryResource& resource, const Core::Mat4& localToWorld,
        const Engine::SceneView& view, float targetPixelError, bool frustumCulling)
    {
        ValidateVirtualGeometryLODInput(resource, localToWorld, view, targetPixelError);
        if (resource.Clusters.size() > UINT32_MAX / (sizeof(Engine::VirtualGeometryCluster) / 4) ||
            resource.Groups.size() > UINT32_MAX / (sizeof(Engine::VirtualGeometryGroup) / 4) ||
            resource.GroupChildClusterIndices.size() > UINT32_MAX)
        {
            throw std::invalid_argument("GPU virtual geometry selection exceeds scalar buffer indexing limits");
        }
        // Scalar buffer decoding must stay identical to the existing CPU Build upload layout.
        static_assert(sizeof(Engine::VirtualGeometryCluster) == 72 && sizeof(Engine::VirtualGeometryGroup) == 112);
        static_assert(offsetof(Engine::VirtualGeometryCluster, Bounds) == 32);
        static_assert(offsetof(Engine::VirtualGeometryGroup, ParentMetric) == 60);
        static_assert(offsetof(Core::BoxSphereBounds, Sphere) == 24);
        static_assert(offsetof(Engine::VirtualGeometryLODMetric, Error) == 40);

        // This is a static topological schedule, not CPU LOD selection. All eligibility decisions run on GPU.
        std::vector<uint32_t> depths(resource.Groups.size());
        uint32_t levelCount = 1;
        for (uint32_t g = 0; g < resource.Groups.size(); ++g)
        {
            const auto& group = resource.Groups[g];
            for (uint32_t i = 0; i < group.ChildClusterCount; ++i)
            {
                const auto child = resource.GroupChildClusterIndices[group.ChildClusterOffset + i];
                const auto generator = resource.Clusters[child].GeneratingGroupIndex;
                if (generator != Engine::INVALID_VIRTUAL_GEOMETRY_INDEX)
                {
                    depths[g] = std::max(depths[g], depths[generator] + 1);
                }
            }
            levelCount = std::max(levelCount, depths[g] + 1);
        }
        std::vector<uint32_t> offsets(levelCount + 1);
        for (const auto depth : depths)
        {
            ++offsets[depth + 1];
        }
        for (uint32_t i = 1; i <= levelCount; ++i)
        {
            offsets[i] += offsets[i - 1];
        }
        auto* order = builder.AllocateParameter<std::vector<uint32_t>>();
        order->resize(resource.Groups.size());
        auto cursors = offsets;
        for (uint32_t g = 0; g < depths.size(); ++g)
        {
            (*order)[cursors[depths[g]]++] = g;
        }
        auto* base = builder.AllocateParameter<VirtualGeometryGPUSelectionParameters>();
        const auto prefix = "VirtualGeometry.GPU." + std::to_string(reinterpret_cast<uintptr_t>(base));
        VirtualGeometryGPUSelection output;
        output.DependencyLevels = levelCount;
        output.AppliedGroups = Buffer(builder, prefix + ".Applied", resource.Groups.size());
        output.ClusterFlags = Buffer(builder, prefix + ".Flags", resource.Clusters.size());
        output.VisibleClusters = Buffer(builder, prefix + ".Visible", resource.Clusters.size());
        output.Statistics = Buffer(builder, prefix + ".Stats", 4);
        output.IndirectArguments = Buffer(builder, prefix + ".Arguments", resource.Clusters.size() * 5ull, true);
        auto orderBuffer = Buffer(builder, prefix + ".Order", order->size());
        builder.AddUploadBuffer(orderBuffer, order->data(), order->size() * 4);
        auto* zeroes = builder.AllocateParameter<std::vector<uint32_t>>();
        zeroes->resize(std::max<size_t>(4, resource.Groups.size()), 0);
        builder.AddUploadBuffer(output.AppliedGroups, zeroes->data(), resource.Groups.size() * 4);
        builder.AddUploadBuffer(output.Statistics, zeroes->data(), 16);
        base->SelectionClusters = External(builder, resource.ClusterBuffer.get(), prefix + ".Clusters");
        base->SelectionGroups = External(builder, resource.GroupBuffer.get(), prefix + ".Groups");
        base->SelectionChildren = External(builder, resource.GroupChildClusterIndexBuffer.get(), prefix + ".Children");
        base->SelectionOrder = SRV(builder, orderBuffer);
        base->AppliedGroups = UAV(builder, output.AppliedGroups);
        base->SelectionLocalToClip = view.ViewProjectionMatrix * localToWorld;
        for (uint32_t row = 0; row < 4; ++row)
        {
            for (uint32_t col = 0; col < 4; ++col)
            {
                if (!std::isfinite(base->SelectionLocalToClip(row, col)))
                {
                    throw std::invalid_argument("GPU virtual geometry transform exceeds the FP32 range");
                }
            }
            base->SelectionRowLengths[row] = Core::Float3{base->SelectionLocalToClip(row, 0),
                base->SelectionLocalToClip(row, 1), base->SelectionLocalToClip(row, 2)}.Length();
            if (!std::isfinite(base->SelectionRowLengths[row]))
            {
                throw std::invalid_argument("GPU virtual geometry projection norm exceeds the FP32 range");
            }
            base->SelectionNearPlane[row] = base->SelectionLocalToClip(2, row) +
                (view.bDepthRangeZeroToOne ? 0 : base->SelectionLocalToClip(3, row));
        }
        base->SelectionViewportSize = {float(view.Viewport.Width), float(view.Viewport.Height)};
        base->SelectionTargetError = targetPixelError;
        base->SelectionZeroToOne = view.bDepthRangeZeroToOne;
        base->SelectionCull = frustumCulling;
        base->SelectionClusterCount = static_cast<uint32_t>(resource.Clusters.size());
        auto groupShader = GShaderMap.GetShader<VirtualGeometrySelectGroupsCS>(0);
        for (uint32_t level = 0; level < levelCount; ++level)
        {
            auto* parameters = builder.AllocateParameter<VirtualGeometryGPUSelectionParameters>();
            *parameters = *base;
            parameters->SelectionOrderOffset = offsets[level];
            parameters->SelectionWorkCount = offsets[level + 1] - offsets[level];
            // Consecutive UAV accesses declare the dependency and emit a dispatch-to-dispatch memory barrier.
            Dispatch(builder, groupShader, parameters);
        }
        auto* finalize = builder.AllocateParameter<VirtualGeometryGPUSelectionParameters>();
        *finalize = *base;
        finalize->SelectionWorkCount = static_cast<uint32_t>(resource.Clusters.size());
        finalize->ClusterFlags = UAV(builder, output.ClusterFlags);
        finalize->VisibleClusters = UAV(builder, output.VisibleClusters);
        finalize->Statistics = UAV(builder, output.Statistics);
        finalize->IndirectArguments = UAV(builder, output.IndirectArguments);
        Dispatch(builder, GShaderMap.GetShader<VirtualGeometrySelectClustersCS>(0), finalize);
        return output;
    }
}
