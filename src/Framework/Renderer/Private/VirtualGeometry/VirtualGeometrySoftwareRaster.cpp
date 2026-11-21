#include "VirtualGeometrySoftwareRaster.h"
#include "GlobalShader.h"
#include "RHIPipelineStateCache.h"
#include <algorithm>
#include <array>
#include <stdexcept>

using namespace RenderCore;
using namespace RHI;

namespace Renderer
{
    BEGIN_SHADER_PARAMETER_STRUCT(VirtualGeometrySoftwareWord)
    SHADER_PARAMETER(uint32_t, Value)
    END_SHADER_PARAMETER_STRUCT(VirtualGeometrySoftwareWord)

    BEGIN_SHADER_PARAMETER_STRUCT(VirtualGeometryRasterRoutingParameters)
    SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, RoutingClusters)
    SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, FullArguments)
    SHADER_PARAMETER_RDG_RWSTRUCTURED_BUFFER(VirtualGeometrySoftwareWord, HardwareArguments)
    SHADER_PARAMETER_RDG_RWSTRUCTURED_BUFFER(VirtualGeometrySoftwareWord, RasterPaths)
    SHADER_PARAMETER_RDG_RWSTRUCTURED_BUFFER(VirtualGeometrySoftwareWord, RoutingStatistics)
    SHADER_PARAMETER(Core::Mat4, RoutingLocalToClip)
    SHADER_PARAMETER(Core::Float4, RoutingRowLengths)
    SHADER_PARAMETER(Core::Float2, RoutingViewportSize)
    SHADER_PARAMETER(float, SoftwareClusterPixels)
    SHADER_PARAMETER(uint32_t, RasterMode)
    SHADER_PARAMETER(uint32_t, RoutingClusterCount)
    SHADER_PARAMETER(uint32_t, NativeZeroToOne)
    END_SHADER_PARAMETER_STRUCT(VirtualGeometryRasterRoutingParameters)

    BEGIN_SHADER_PARAMETER_STRUCT(VirtualGeometrySoftwareClearParameters)
    SHADER_PARAMETER_RDG_RWSTRUCTURED_BUFFER(VirtualGeometrySoftwareWord, SoftwareDepth)
    SHADER_PARAMETER_RDG_RWSTRUCTURED_BUFFER(VirtualGeometrySoftwareWord, SoftwareVisibility)
    SHADER_PARAMETER(Core::Int2, SoftwareTargetSize)
    END_SHADER_PARAMETER_STRUCT(VirtualGeometrySoftwareClearParameters)

    BEGIN_SHADER_PARAMETER_STRUCT(VirtualGeometrySoftwareRasterParameters)
    SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float>, SoftwarePositions)
    SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, SoftwareIndices)
    SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, SoftwareClusters)
    SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, SoftwarePaths)
    SHADER_PARAMETER_RDG_RWSTRUCTURED_BUFFER(VirtualGeometrySoftwareWord, SoftwareDepth)
    SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, WinnerDepth)
    SHADER_PARAMETER_RDG_RWSTRUCTURED_BUFFER(VirtualGeometrySoftwareWord, SoftwareVisibility)
    SHADER_PARAMETER(Core::Mat4, SoftwareLocalToWorld)
    SHADER_PARAMETER(Core::Mat4, SoftwareViewProjection)
    SHADER_PARAMETER(Core::Int2, SoftwareViewOrigin)
    SHADER_PARAMETER(Core::Int2, SoftwareViewSize)
    SHADER_PARAMETER(Core::Int2, SoftwareTargetSize)
    SHADER_PARAMETER(Core::Float2, NativeDepthScaleBias)
    SHADER_PARAMETER(float, SoftwareScreenYSign)
    SHADER_PARAMETER(uint32_t, SubPixelBits)
    SHADER_PARAMETER(uint32_t, SoftwareFirstDraw)
    SHADER_PARAMETER(uint32_t, SoftwareClusterCount)
    END_SHADER_PARAMETER_STRUCT(VirtualGeometrySoftwareRasterParameters)

    BEGIN_SHADER_PARAMETER_STRUCT(VirtualGeometryRasterMergeParameters)
    SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, MergeSoftwareDepth)
    SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, MergeSoftwareVisibility)
    SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint2>, HardwareVisibility)
    SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, HardwareDepth)
    SHADER_PARAMETER(Core::Int2, MergeTargetSize)
    SHADER_PARAMETER_RENDER_TARGET_BINDING_SLOTS(Targets)
    END_SHADER_PARAMETER_STRUCT(VirtualGeometryRasterMergeParameters)

#define VG_SOFTWARE_SHADER(ClassName, Parameters, File, Entry, Frequency) \
    class ClassName : public GlobalShader \
    { \
    public: \
        DECLARE_GLOBAL_SHADER_TYPE(ClassName) \
        static void ModifyShaderCompilerEnvironment(const ShaderPermutationParameters&, ShaderCompilerEnvironment& env) \
        { \
            env.SetDefine("VG_CLUSTER_WORDS", sizeof(Engine::VirtualGeometryCluster) / 4); \
            env.SetDefine("SOFTWARE_WRITE_VISIBILITY", std::string(Entry) == "VisibilityCS" ? 1 : 0); \
        } \
        static const ShaderParametersMetadata* GetShaderParameterMetadata() { return Parameters::GetMetaData(); } \
    }; \
    IMPLEMENT_GLOBAL_SHADER_TYPE(ClassName, #ClassName, File, Entry, Frequency);

    VG_SOFTWARE_SHADER(VirtualGeometryRoutingCS, VirtualGeometryRasterRoutingParameters,
        "/virtualgeometry/RasterRouting.sf", "MainCS", ERHIShaderFrequency::Compute)
    VG_SOFTWARE_SHADER(VirtualGeometrySoftwareClearCS, VirtualGeometrySoftwareClearParameters,
        "/virtualgeometry/SoftwareClear.sf", "MainCS", ERHIShaderFrequency::Compute)
    VG_SOFTWARE_SHADER(VirtualGeometrySoftwareDepthCS, VirtualGeometrySoftwareRasterParameters,
        "/virtualgeometry/SoftwareRaster.sf", "DepthCS", ERHIShaderFrequency::Compute)
    VG_SOFTWARE_SHADER(VirtualGeometrySoftwareVisibilityCS, VirtualGeometrySoftwareRasterParameters,
        "/virtualgeometry/SoftwareRaster.sf", "VisibilityCS", ERHIShaderFrequency::Compute)
    VG_SOFTWARE_SHADER(VirtualGeometryRasterMergeVS, VirtualGeometryRasterMergeParameters,
        "/virtualgeometry/RasterMerge.sf", "MainVS", ERHIShaderFrequency::Vertex)
    VG_SOFTWARE_SHADER(VirtualGeometryRasterMergePS, VirtualGeometryRasterMergeParameters,
        "/virtualgeometry/RasterMerge.sf", "MainPS", ERHIShaderFrequency::Fragment)
#undef VG_SOFTWARE_SHADER

    namespace
    {
        RenderGraphBufferRef Buffer(RenderGraphBuilder& builder, const std::string& name, uint64_t words, bool indirect = false)
        {
            RenderGraphBufferDesc desc;
            desc.Size = words * 4;
            desc.Stride = 4;
            desc.Usage = ERHIBufferUsageFlag::Structured | ERHIBufferUsageFlag::ShaderResource |
                ERHIBufferUsageFlag::UnorderedAccess | ERHIBufferUsageFlag::TransferSrc | ERHIBufferUsageFlag::TransferDst;
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

        template<typename Parameters>
        void Compute(RenderGraphBuilder& builder, Shader* shader, Parameters* parameters, uint32_t groups)
        {
            if (!shader)
            {
                throw std::runtime_error("Virtual geometry software raster shader is unavailable");
            }
            RHIComputePipelineStateDesc desc;
            desc.ComputeShader = static_cast<RHIComputeShader*>(shader->GetRHIShader());
            auto* pipeline = RHIPipelineStateCache::GetOrCreateComputePipelineState(desc);
            builder.AddPass(shader->GetName(), Parameters::GetMetaData(), parameters, EPassFlag::Compute,
                            [=](RHICommandListBase& list)
            {
                auto& cmd = static_cast<RHIComputeCommandList&>(list);
                cmd.SetComputePipelineState(pipeline);
                SetShaderParameters(cmd, shader, parameters);
                cmd.Dispatch(std::min(65535u, groups), (groups + 65534) / 65535, 1);
            });
        }
    }

    VirtualGeometryRasterRouting AddVirtualGeometryRasterRouting(RenderGraphBuilder& builder,
        const Engine::VirtualGeometryResource& resource, const VirtualGeometryGPUSelection& selection,
        const Core::Mat4& localToWorld, const Engine::SceneView& view, const VirtualGeometryRasterSettings& settings)
    {
        auto* p = builder.AllocateParameter<VirtualGeometryRasterRoutingParameters>();
        const auto prefix = "VirtualGeometry.Routing." + std::to_string(reinterpret_cast<uintptr_t>(p));
        VirtualGeometryRasterRouting result;
        result.HardwareArguments = Buffer(builder, prefix + ".Arguments", resource.Clusters.size() * 5ull, true);
        result.Paths = Buffer(builder, prefix + ".Paths", resource.Clusters.size());
        result.Statistics = Buffer(builder, prefix + ".Stats", 4);
        auto* zero = builder.AllocateParameter<std::array<uint32_t, 4>>();
        builder.AddUploadBuffer(result.Statistics, zero->data(), sizeof(*zero));
        p->RoutingClusters = SRV(builder, builder.RegisterExternalBuffer(prefix + ".Clusters", resource.ClusterBuffer.get()));
        p->FullArguments = SRV(builder, selection.IndirectArguments);
        p->HardwareArguments = UAV(builder, result.HardwareArguments);
        p->RasterPaths = UAV(builder, result.Paths);
        p->RoutingStatistics = UAV(builder, result.Statistics);
        p->RoutingLocalToClip = view.ViewProjectionMatrix * localToWorld;
        for (uint32_t row = 0; row < 4; ++row)
        {
            p->RoutingRowLengths[row] = Core::Float3{p->RoutingLocalToClip(row, 0), p->RoutingLocalToClip(row, 1),
                                                    p->RoutingLocalToClip(row, 2)}.Length();
        }
        p->RoutingViewportSize = {float(view.Viewport.Width), float(view.Viewport.Height)};
        p->SoftwareClusterPixels = settings.SoftwareClusterPixels;
        p->RasterMode = static_cast<uint32_t>(settings.Mode);
        p->RoutingClusterCount = static_cast<uint32_t>(resource.Clusters.size());
        p->NativeZeroToOne = GShaderPlatform != ERHIShaderPlatform::OpenGL;
        Compute(builder, GShaderMap.GetShader<VirtualGeometryRoutingCS>(0), p, (p->RoutingClusterCount + 63) / 64);
        return result;
    }

    VirtualGeometrySoftwareTargets AddVirtualGeometrySoftwareClear(RenderGraphBuilder& builder, Core::Int2 size)
    {
        auto* p = builder.AllocateParameter<VirtualGeometrySoftwareClearParameters>();
        const auto prefix = "VirtualGeometry.Software." + std::to_string(reinterpret_cast<uintptr_t>(p));
        const uint64_t pixels = uint64_t(size.X) * size.Y;
        VirtualGeometrySoftwareTargets result{Buffer(builder, prefix + ".Depth", pixels),
                                              Buffer(builder, prefix + ".Visibility", pixels), size};
        p->SoftwareDepth = UAV(builder, result.Depth);
        p->SoftwareVisibility = UAV(builder, result.Visibility);
        p->SoftwareTargetSize = size;
        Compute(builder, GShaderMap.GetShader<VirtualGeometrySoftwareClearCS>(0), p, static_cast<uint32_t>((pixels + 63) / 64));
        return result;
    }

    void AddVirtualGeometrySoftwareRaster(RenderGraphBuilder& builder, const Engine::VirtualGeometryResource& resource,
        const VirtualGeometryRasterRouting& routing, const VirtualGeometrySoftwareTargets& targets,
        const Core::Mat4& localToWorld, const Engine::SceneView& view, uint32_t firstDraw, bool writeVisibility)
    {
        auto* p = builder.AllocateParameter<VirtualGeometrySoftwareRasterParameters>();
        const auto prefix = "VirtualGeometry.SoftwareInput." + std::to_string(reinterpret_cast<uintptr_t>(p));
        p->SoftwarePositions = SRV(builder, builder.RegisterExternalBuffer(prefix + ".Positions", resource.PositionBuffer.get()));
        p->SoftwareIndices = SRV(builder, builder.RegisterExternalBuffer(prefix + ".Indices", resource.IndexBuffer.get()));
        p->SoftwareClusters = SRV(builder, builder.RegisterExternalBuffer(prefix + ".Clusters", resource.ClusterBuffer.get()));
        p->SoftwarePaths = SRV(builder, routing.Paths);
        if (writeVisibility)
        {
            p->WinnerDepth = SRV(builder, targets.Depth);
            p->SoftwareVisibility = UAV(builder, targets.Visibility);
        }
        else
        {
            p->SoftwareDepth = UAV(builder, targets.Depth);
        }
        p->SoftwareLocalToWorld = localToWorld;
        p->SoftwareViewProjection = view.ViewProjectionMatrix;
        p->SoftwareViewOrigin = {view.Viewport.X, view.Viewport.Y};
        p->SoftwareViewSize = {view.Viewport.Width, view.Viewport.Height};
        p->SoftwareTargetSize = targets.Size;
        const bool openGL = GShaderPlatform == ERHIShaderPlatform::OpenGL;
        p->NativeDepthScaleBias = openGL ? Core::Float2{0.5f, 0.5f} : Core::Float2{1, 0};
        // VulkanPendingGfxState negates viewport height; D3D12 also maps positive NDC Y upward.
        p->SoftwareScreenYSign = openGL ? 1 : -1;
        p->SubPixelBits = GRHIApi->GetPlatformInfo().RasterSubPixelBits;
        p->SoftwareFirstDraw = firstDraw;
        p->SoftwareClusterCount = static_cast<uint32_t>(resource.Clusters.size());
        auto* shader = writeVisibility ? GShaderMap.GetShader<VirtualGeometrySoftwareVisibilityCS>(0) :
                                        GShaderMap.GetShader<VirtualGeometrySoftwareDepthCS>(0);
        Compute(builder, shader, p, p->SoftwareClusterCount);
    }

    void AddVirtualGeometryRasterMerge(RenderGraphBuilder& builder, const Engine::SceneView& view,
        const VirtualGeometrySoftwareTargets& software, RenderGraphTextureRef hardwareVisibility,
        RenderGraphTextureRef hardwareDepth, RenderGraphTextureRef finalVisibility, RenderGraphTextureRef finalDepth)
    {
        auto* p = builder.AllocateParameter<VirtualGeometryRasterMergeParameters>();
        p->MergeSoftwareDepth = SRV(builder, software.Depth);
        p->MergeSoftwareVisibility = SRV(builder, software.Visibility);
        p->HardwareVisibility = hardwareVisibility;
        p->HardwareDepth = hardwareDepth;
        p->MergeTargetSize = software.Size;
        p->Targets.NumColorRenderTargets = 1;
        p->Targets[0].Texture = finalVisibility;
        p->Targets[0].Action = ERenderTargetActions::DontCare_Store;
        p->Targets.DepthStencil.Texture = finalDepth;
        p->Targets.DepthStencil.DepthAction = ERenderTargetActions::DontCare_Store;
        auto* vs = GShaderMap.GetShader<VirtualGeometryRasterMergeVS>(0);
        auto* ps = GShaderMap.GetShader<VirtualGeometryRasterMergePS>(0);
        if (!vs || !ps)
        {
            throw std::runtime_error("Virtual geometry merge shader is unavailable");
        }
        RHIGraphicsPipelineStateDesc desc;
        desc.ShaderStages.VertexShader = static_cast<RHIVertexShader*>(vs->GetRHIShader());
        desc.ShaderStages.FragmentShader = static_cast<RHIFragmentShader*>(ps->GetRHIShader());
        desc.VertexDescState = RHIPipelineStateCache::GetOrCreateVertexDescState({});
        RHIRasterizerStateDesc rasterizer;
        rasterizer.CullMode = ERHICullMode::None;
        desc.RasterizerState = RHIPipelineStateCache::GetOrCreateRasterizerState(rasterizer);
        RHIDepthStencilStateDesc depth;
        depth.bDepthTestEnable = depth.bDepthWriteEnable = true;
        depth.DepthCompareOp = ERHICompareOp::Always;
        desc.DepthStencilState = RHIPipelineStateCache::GetOrCreateDepthStencilState(depth);
        RHIColorBlendStateDesc blend;
        blend.Attachments.resize(1);
        desc.ColorBlendState = RHIPipelineStateCache::GetOrCreateColorBlendState(blend);
        desc.AttachmentDesc.ColorAttachmentCount = 1;
        desc.AttachmentDesc.ColorAttachments[0].Format = ERHIFormat::R32G32_UInt;
        desc.AttachmentDesc.ColorAttachments[0].Actions = ERenderTargetActions::DontCare_Store;
        desc.AttachmentDesc.bEnableDepth = true;
        desc.AttachmentDesc.DepthStencilFormat = ERHIFormat::D32_Float;
        desc.AttachmentDesc.DepthActions = ERenderTargetActions::DontCare_Store;
        auto* pipeline = RHIPipelineStateCache::GetOrCreateGraphicsPipelineState(desc);
        builder.AddPass("VirtualGeometryRasterMerge", VirtualGeometryRasterMergeParameters::GetMetaData(), p,
                        EPassFlag::Graphic, [=](RHICommandListBase& list)
        {
            auto& cmd = static_cast<RHIGraphicCommandList&>(list);
            cmd.SetGraphicPipelineState(pipeline);
            cmd.SetViewport(0, 0, software.Size.X, software.Size.Y, 0, 1);
            cmd.SetScissor(0, 0, software.Size.X, software.Size.Y);
            SetShaderParameters(cmd, ps, p);
            RHIRenderPassInfo pass;
            pass.RenderTargets = p->Targets.GetBoundRenderTarget();
            pass.RenderArea = {0, 0, uint32_t(software.Size.X), uint32_t(software.Size.Y)};
            cmd.BeginRenderPass(pass);
            cmd.Draw(3);
            cmd.EndRenderPass();
        });
    }
}
