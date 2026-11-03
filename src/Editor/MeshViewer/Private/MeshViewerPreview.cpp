#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "MeshViewerPreview.h"
#include "StaticMeshAsset.h"
#include "StaticMesh.h"
#include "Transform.hpp"
#include "GlobalShader.h"
#include "ShaderParameter.h"
#include "ShaderCompiler.h"
#include "RenderGraphBuilder.h"
#include "RHIPipelineStateCache.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace Editor
{
    using namespace RenderCore;
    using namespace RHI;

    BEGIN_SHADER_PARAMETER_STRUCT(MeshViewerVertexParameters)
        SHADER_PARAMETER(Core::Float4x4, ViewProjection)
        SHADER_PARAMETER(Core::Float4, CenterAndScale)
        SHADER_PARAMETER(Core::Float4, DrawSettings)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, PreviewVertices)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, PreviewIndices)
    END_SHADER_PARAMETER_STRUCT(MeshViewerVertexParameters)
    BEGIN_SHADER_PARAMETER_STRUCT(MeshViewerPixelParameters)
        SHADER_PARAMETER(Core::Float4, CameraPosition)
        SHADER_PARAMETER(Core::Float4, LightDirection)
        SHADER_PARAMETER(Core::Float4, DrawSettings)
        SHADER_PARAMETER_RENDER_TARGET_BINDING_SLOTS(Targets)
    END_SHADER_PARAMETER_STRUCT(MeshViewerPixelParameters)
    BEGIN_SHADER_PARAMETER_STRUCT(MeshViewerPassParameters)
        SHADER_PARAMETER_STRUCT_INCLUDE(MeshViewerVertexParameters, Vertex)
        SHADER_PARAMETER_STRUCT_INCLUDE(MeshViewerPixelParameters, Pixel)
    END_SHADER_PARAMETER_STRUCT(MeshViewerPassParameters)

    static void RegisterMeshViewerHeaders()
    {
        ShaderCompiler::GenerateOrGetShaderPrameterMetaDataSF(*MeshViewerVertexParameters::GetMetaData());
        ShaderCompiler::GenerateOrGetShaderPrameterMetaDataSF(*MeshViewerPixelParameters::GetMetaData());
    }
    class MeshViewerVS : public GlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER_TYPE(MeshViewerVS)
        static void ModifyShaderCompilerEnvironment(const ShaderPermutationParameters&,ShaderCompilerEnvironment& environment)
        { RegisterMeshViewerHeaders(); environment.SetDefine("VERTEX_SHADER",1); }
        static const ShaderParametersMetadata* GetShaderParameterMetadata() { return MeshViewerVertexParameters::GetMetaData(); }
    };
    class MeshViewerPS : public GlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER_TYPE(MeshViewerPS)
        static void ModifyShaderCompilerEnvironment(const ShaderPermutationParameters&,ShaderCompilerEnvironment&)
        { RegisterMeshViewerHeaders(); }
        static const ShaderParametersMetadata* GetShaderParameterMetadata() { return MeshViewerPixelParameters::GetMetaData(); }
    };
    IMPLEMENT_GLOBAL_SHADER_TYPE(MeshViewerVS,"MeshViewerVS","/editor/MeshViewer.sf","MainVS",ERHIShaderFrequency::Vertex);
    IMPLEMENT_GLOBAL_SHADER_TYPE(MeshViewerPS,"MeshViewerPS","/editor/MeshViewer.sf","MainPS",ERHIShaderFrequency::Fragment);

    MeshViewerPreview::~MeshViewerPreview() { Reset(); }
    void MeshViewerPreview::WaitForLastDraw()
    {
        const auto wait=[](const auto& resource) {
            if (!resource) return;
            const auto fence=resource->GetTracker().GetLastAccessFence();
            if (fence.Value) GRHIApi->GetQueue(fence.QueueType)->WaitValue(fence.Value);
        };
        // Buffer upload may precede the first color draw. Cover that case too.
        wait(m_Color); wait(m_Vertices); wait(m_Indices);
    }
    void MeshViewerPreview::Reset()
    {
        WaitForLastDraw(); m_Color.reset(); m_Vertices.reset(); m_Indices.reset(); m_Asset.reset();
        m_VertexCount=m_IndexCount=0; m_Lod=-1; m_bGeneratedNormals=false;
    }
    void MeshViewerPreview::SetMesh(const std::shared_ptr<Engine::StaticMeshAsset>& asset,int lodIndex)
    {
        if (asset==m_Asset && lodIndex==m_Lod) return;
        Reset();
        const auto* mesh=asset ? asset->GetMesh() : nullptr;
        if (!mesh || static_cast<size_t>(lodIndex)>=mesh->GetLODCount()) return;
        const auto& lod=mesh->GetLODResource(lodIndex);
        const auto& position=lod.VertexBuffers.PositionBuffer;
        const auto count=position.GetNumVertices();
        if (position.NumComponents<3 || !count) throw std::runtime_error("Mesh has no valid three-component positions.");
        if (count>std::numeric_limits<uint32_t>::max()/(2*sizeof(Core::Float4)))
            throw std::runtime_error("Mesh exceeds the preview buffer upload size limit.");
        std::vector<Core::Float3> positions(count), normals(count);
        Core::AABB bounds;
        for (size_t vertex=0;vertex<count;++vertex)
        {
            const auto offset=vertex*position.NumComponents;
            positions[vertex]={position.Vertices[offset],position.Vertices[offset+1],position.Vertices[offset+2]};
            const auto& p=positions[vertex];
            if (!std::isfinite(p.X)||!std::isfinite(p.Y)||!std::isfinite(p.Z)) throw std::runtime_error("Mesh has non-finite positions.");
            bounds.ExpandBy(p);
        }
        std::vector<uint32_t> indices;
        const auto append=[&](uint32_t first,uint32_t size,int32_t base) {
            if (static_cast<uint64_t>(first)+size>lod.IndexBuffer.Indices.size() || size%3)
                throw std::runtime_error("Mesh region has an invalid triangle index range.");
            for (uint64_t index=first;index<static_cast<uint64_t>(first)+size;++index)
            {
                const int64_t vertex=static_cast<int64_t>(lod.IndexBuffer.Indices[index])+base;
                if (vertex<0 || static_cast<uint64_t>(vertex)>=count) throw std::runtime_error("Mesh index is outside its position stream.");
                indices.push_back(static_cast<uint32_t>(vertex));
            }
        };
        if (lod.Sections.empty()) append(0,static_cast<uint32_t>(lod.IndexBuffer.Indices.size()),0);
        else for (const auto& section:lod.Sections) append(section.FirstIndex,section.NumIndices,section.BaseVertexIndex);
        if (indices.empty() || indices.size()>std::numeric_limits<uint32_t>::max()/sizeof(uint32_t))
            throw std::runtime_error("Mesh has no triangles or exceeds the preview index limit.");
        // Generate fallback normals only for the derived preview; inspection keeps authored data.
        for (size_t index=0;index<indices.size();index+=3)
        {
            const auto a=indices[index], b=indices[index+1], c=indices[index+2];
            const auto normal=Core::Cross(positions[b]-positions[a],positions[c]-positions[a]);
            normals[a]+=normal; normals[b]+=normal; normals[c]+=normal;
        }
        const auto& authored=lod.VertexBuffers.NormalBuffer;
        std::vector<Core::Float4> packed; packed.reserve(count*2);
        for (size_t vertex=0;vertex<count;++vertex)
        {
            auto normal=normals[vertex]; bool generated=true;
            if (authored.NumComponents>=3 && vertex<authored.GetNumVertices())
            {
                const auto offset=vertex*authored.NumComponents;
                const Core::Float3 value{authored.Vertices[offset],authored.Vertices[offset+1],authored.Vertices[offset+2]};
                if (std::isfinite(value.X)&&std::isfinite(value.Y)&&std::isfinite(value.Z)&&value.LengthSquared()>1e-12f)
                { normal=value; generated=false; }
            }
            m_bGeneratedNormals|=generated;
            normal=normal.LengthSquared()>1e-12f ? Core::Normalize(normal) : Core::Float3{0,1,0};
            packed.push_back({positions[vertex].X,positions[vertex].Y,positions[vertex].Z,1});
            packed.push_back({normal.X,normal.Y,normal.Z,0});
        }
        const auto upload=[](const void* data,uint32_t size,uint32_t stride,const char* name) {
            RHIBufferDesc desc; desc.Size=size; desc.Stride=stride;
            desc.Usage=ERHIBufferUsageFlag::ShaderResource | ERHIBufferUsageFlag::TransferDst; desc.DebugName=name;
            auto result=std::make_unique<RenderBuffer>(desc); result->InitRHIResource(); result->UploadData(data,size); return result;
        };
        m_Vertices=upload(packed.data(),static_cast<uint32_t>(packed.size()*sizeof(Core::Float4)),sizeof(Core::Float4),"MeshViewerVertices");
        m_Indices=upload(indices.data(),static_cast<uint32_t>(indices.size()*sizeof(uint32_t)),sizeof(uint32_t),"MeshViewerIndices");
        const auto center=(bounds.Min+bounds.Max)*0.5f;
        m_CenterAndScale={center.X,center.Y,center.Z,1/std::max(0.00001f,(bounds.Max-bounds.Min).Length()*0.5f)};
        m_VertexCount=static_cast<uint32_t>(count); m_IndexCount=static_cast<uint32_t>(indices.size()); m_Asset=asset; m_Lod=lodIndex;
    }
    void MeshViewerPreview::Render(Core::Int2 size,bool wireframe,bool normals,float normalLength,float yaw,float pitch,float distance)
    {
        if (!m_Vertices || !m_Indices) return;
        if (!m_Color || m_Color->GetRHI()->GetDesc().Width!=size.X || m_Color->GetRHI()->GetDesc().Height!=size.Y)
        {
            WaitForLastDraw();
            RHITextureDesc desc; desc.Width=size.X; desc.Height=size.Y; desc.Format=ERHIFormat::R8G8B8A8_UNorm;
            desc.Usage=ERHITextureCreateFlag::RenderTarget | ERHITextureCreateFlag::ShaderResource | ERHITextureCreateFlag::TransferSrc;
            desc.DebugName="MeshViewerColor"; m_Color=std::make_unique<RenderTexture>(desc); m_Color->InitRHIResource();
        }
        auto* vs=GShaderMap.GetShader<MeshViewerVS>(0); auto* ps=GShaderMap.GetShader<MeshViewerPS>(0);
        if (!vs || !ps) throw std::runtime_error("MeshViewer shaders are unavailable.");
        RenderGraphBuilder builder;
        auto color=builder.RegisterExternalTexture("MeshViewerColor",m_Color.get());
        RenderGraphTextureDesc depthDesc; depthDesc.Width=size.X; depthDesc.Height=size.Y;
        depthDesc.Format=ERHIFormat::D32_Float; depthDesc.Usage=ERHITextureCreateFlag::DepthStencil;
        auto depth=builder.CreateTexture("MeshViewerDepth",depthDesc);
        const auto srv=[&](RenderBuffer* buffer,const char* name,uint32_t stride) {
            RenderGraphBufferSRVDesc desc; desc.Buffer=builder.RegisterExternalBuffer(name,buffer); desc.Stride=stride;
            desc.NumElements=buffer->GetRHI()->GetDesc().Size/stride; return builder.CreateBufferSRV(name,desc);
        };
        const auto vertices=srv(m_Vertices.get(),"MeshViewerVertices",sizeof(Core::Float4));
        const auto indices=srv(m_Indices.get(),"MeshViewerIndices",sizeof(uint32_t));
        const Core::Float3 camera{distance*std::cos(pitch)*std::sin(yaw),distance*std::sin(pitch),distance*std::cos(pitch)*std::cos(yaw)};
        const auto vp=Core::PerspectiveRhZo(Core::DegToRad(45.0f),static_cast<float>(size.X)/size.Y,0.05f,50)*Core::LookAtRH(camera,{0,0,0},{0,1,0});
        const auto addPass=[&](int mode,bool clear,uint32_t count) {
            auto* params=builder.AllocateParameter<MeshViewerPassParameters>();
            params->Vertex.ViewProjection=vp; params->Vertex.CenterAndScale=m_CenterAndScale;
            params->Vertex.DrawSettings={static_cast<float>(mode),normalLength,0,0};
            params->Vertex.PreviewVertices=vertices; params->Vertex.PreviewIndices=indices;
            params->Pixel.CameraPosition={camera.X,camera.Y,camera.Z,1}; params->Pixel.LightDirection={-0.4f,-0.8f,-0.5f,0};
            params->Pixel.DrawSettings=params->Vertex.DrawSettings;
            auto& targets=params->Pixel.Targets; targets.NumColorRenderTargets=1;
            targets[0].Texture=color; targets[0].ClearValue={0.035f,0.045f,0.06f,1};
            targets[0].Action=clear ? ERenderTargetActions::Clear_Store : ERenderTargetActions::Load_Store;
            targets.DepthStencil.Texture=depth; targets.DepthStencil.DepthAction=targets[0].Action;
            // RHI render passes currently use writable depth attachment layouts.
            // Keep RDG conservative; the normal-line pipeline disables depth writes.
            RHIGraphicsPipelineStateDesc desc;
            desc.ShaderStages.VertexShader=static_cast<RHIVertexShader*>(vs->GetRHIShader());
            desc.ShaderStages.FragmentShader=static_cast<RHIFragmentShader*>(ps->GetRHIShader());
            desc.VertexDescState=RHIPipelineStateCache::GetOrCreateVertexDescState({});
            RHIRasterizerStateDesc rasterizer; rasterizer.CullMode=ERHICullMode::None;
            desc.RasterizerState=RHIPipelineStateCache::GetOrCreateRasterizerState(rasterizer);
            RHIDepthStencilStateDesc ds; ds.bDepthTestEnable=true; ds.bDepthWriteEnable=mode!=2; ds.DepthCompareOp=ERHICompareOp::LessOrEqual;
            desc.DepthStencilState=RHIPipelineStateCache::GetOrCreateDepthStencilState(ds);
            RHIColorBlendStateDesc blend; blend.Attachments.resize(1); desc.ColorBlendState=RHIPipelineStateCache::GetOrCreateColorBlendState(blend);
            desc.AttachmentDesc.ColorAttachmentCount=1; desc.AttachmentDesc.ColorAttachments[0].Format=color->GetDesc().Format;
            desc.AttachmentDesc.ColorAttachments[0].Actions=targets[0].Action; desc.AttachmentDesc.bEnableDepth=true;
            desc.AttachmentDesc.DepthStencilFormat=depthDesc.Format; desc.AttachmentDesc.DepthActions=targets.DepthStencil.DepthAction;
            desc.PrimitiveTopology=mode ? EPrimitiveTopology::LineList : EPrimitiveTopology::TriangleList;
            auto* pipeline=RHIPipelineStateCache::GetOrCreateGraphicsPipelineState(desc);
            builder.AddPass<MeshViewerPassParameters>(mode==2 ? "MeshViewer.Normals" : "MeshViewer.Mesh",MeshViewerPassParameters::GetMetaData(),params,EPassFlag::Graphic,
                [=](RHICommandListBase& commands) {
                    auto& cmd=static_cast<RHIGraphicCommandList&>(commands);
                    RHIRenderPassInfo pass; pass.RenderTargets=params->Pixel.Targets.GetBoundRenderTarget();
                    pass.RenderArea={0,0,static_cast<uint32_t>(size.X),static_cast<uint32_t>(size.Y)};
                    cmd.BeginRenderPass(pass); cmd.SetGraphicPipelineState(pipeline);
                    cmd.SetViewport(0,0,static_cast<float>(size.X),static_cast<float>(size.Y),0,1); cmd.SetScissor(0,0,size.X,size.Y);
                    SetShaderParameters(cmd,vs,&params->Vertex); SetShaderParameters(cmd,ps,&params->Pixel); cmd.Draw(count); cmd.EndRenderPass();
                });
        };
        addPass(wireframe ? 1 : 0,true,wireframe ? m_IndexCount*2 : m_IndexCount);
        if (normals) addPass(2,false,m_VertexCount*10); // Shaft plus four arrowhead edges.
        builder.Execute();
    }
}
