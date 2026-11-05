#include "TextureViewerPreview.h"
#include "GlobalShader.h"
#include "ShaderCompiler.h"
#include "ShaderParameter.h"
#include "RenderGraphBuilder.h"
#include "RHIPipelineStateCache.h"
#include <algorithm>
#include <stdexcept>

namespace Editor
{
    using namespace RenderCore;
    using namespace RHI;
    BEGIN_SHADER_PARAMETER_STRUCT(TextureViewerPixelParameters)
        SHADER_PARAMETER(Core::Float4, Settings)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, Input2D)
        SHADER_PARAMETER_RDG_TEXTURE(TextureCube, InputCube)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2DArray, InputArray)
        SHADER_PARAMETER_SAMPLER(InputSampler)
        SHADER_PARAMETER_RENDER_TARGET_BINDING_SLOTS(Targets)
    END_SHADER_PARAMETER_STRUCT(TextureViewerPixelParameters)

    static void RegisterTextureViewerHeader()
    { ShaderCompiler::GenerateOrGetShaderPrameterMetaDataSF(*TextureViewerPixelParameters::GetMetaData()); }
    class TextureViewerVS : public GlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER_TYPE(TextureViewerVS)
        static void ModifyShaderCompilerEnvironment(const ShaderPermutationParameters& parameters,ShaderCompilerEnvironment& environment)
        {
            RegisterTextureViewerHeader(); environment.SetDefine("VERTEX_SHADER",1);
            environment.SetDefine("VIEWER_FLIP_Y",parameters.Platform==uint32_t(ERHIShaderPlatform::OpenGL) ? 0 : 1);
        }
        static const ShaderParametersMetadata* GetShaderParameterMetadata() { return nullptr; }
    };
    static constexpr char TextureViewerKindMacro[]="TEXTURE_KIND";
    using TextureViewerKind=FPermutationDimensionEnum<TextureViewerKindMacro,3>;
    class TextureViewerPS : public GlobalShader
    {
    public:
        using PermutationDomain=ShaderPermutationDomain<TextureViewerKind>;
        DECLARE_GLOBAL_SHADER_TYPE(TextureViewerPS)
        static void ModifyShaderCompilerEnvironment(const ShaderPermutationParameters& parameters,ShaderCompilerEnvironment& environment)
        {
            RegisterTextureViewerHeader(); PermutationDomain domain; domain.SetFromId(parameters.PermutationId);
            domain.ModifyCompilationEnvironment(environment);
        }
        static const ShaderParametersMetadata* GetShaderParameterMetadata() { return TextureViewerPixelParameters::GetMetaData(); }
    };
    IMPLEMENT_GLOBAL_SHADER_TYPE(TextureViewerVS,"TextureViewerVS","/editor/TextureViewer.sf","MainVS",ERHIShaderFrequency::Vertex);
    IMPLEMENT_GLOBAL_SHADER_TYPE(TextureViewerPS,"TextureViewerPS","/editor/TextureViewer.sf","MainPS",ERHIShaderFrequency::Fragment);

    std::string TextureViewerPreview::UnsupportedReason(RenderTexture* texture)
    {
        if (!texture || !texture->GetRHI()) return "Texture is not loaded";
        const auto& desc=texture->GetRHI()->GetDesc();
        if (desc.SampleCount!=1) return "Multisampled textures are not supported";
        if (desc.Type!=ERHITextureType::Texture2D && desc.Type!=ERHITextureType::TextureCube && desc.Type!=ERHITextureType::Texture2DArray)
            return "Only 2D, cube and 2D-array textures are supported";
        const auto format=GFormatInfoMap.find(desc.Format);
        if (format==GFormatInfoMap.end() || format->second.bIsDepth || format->second.bIsStencil || !format->second.BytesPerPixel)
            return "Unsupported color format";
        if (!desc.Usage.HasAny(ERHITextureCreateFlag::ShaderResource)) return "Texture has no shader-resource usage";
        return {};
    }
    TextureViewerPreview::~TextureViewerPreview() { Reset(); }
    void TextureViewerPreview::WaitForLastDraw()
    {
        for (auto* texture : {m_Color.get(),m_Source.GetTexture()}) if (texture) {
            const auto fence=texture->GetTracker().GetLastAccessFence();
            if (fence.Value) GRHIApi->GetQueue(fence.QueueType)->WaitValue(fence.Value);
        }
    }
    void TextureViewerPreview::Reset()
    { WaitForLastDraw(); m_Color.reset(); m_Source={}; }
    void TextureViewerPreview::Render(const TextureViewerSource& source,int mip,int slice)
    {
        auto* texture=source.GetTexture(); const auto error=UnsupportedReason(texture);
        if (!error.empty()) throw std::runtime_error(error);
        const auto& input=texture->GetRHI()->GetDesc();
        if (mip<0 || uint32_t(mip)>=input.MipLevels || slice<0 || uint32_t(slice)>=input.ArraySize)
            throw std::runtime_error("Texture mip or slice is out of range");
        WaitForLastDraw(); m_Source=source;
        const uint32_t width=std::max(1u,input.Width>>mip), height=std::max(1u,input.Height>>mip);
        if (!m_Color || m_Color->GetRHI()->GetDesc().Width!=width || m_Color->GetRHI()->GetDesc().Height!=height) {
            RHITextureDesc desc; desc.Width=width; desc.Height=height; desc.Format=ERHIFormat::R8G8B8A8_UNorm;
            desc.Usage=ERHITextureCreateFlag::RenderTarget | ERHITextureCreateFlag::ShaderResource | ERHITextureCreateFlag::TransferSrc;
            desc.DebugName="TextureViewerPreview"; m_Color=std::make_unique<RenderTexture>(desc); m_Color->InitRHIResource();
        }
        const int kind=input.Type==ERHITextureType::TextureCube ? 1 : input.Type==ERHITextureType::Texture2DArray ? 2 : 0;
        auto* vs=GShaderMap.GetShader<TextureViewerVS>(0); auto* ps=GShaderMap.GetShader<TextureViewerPS>(kind);
        if (!vs || !ps) throw std::runtime_error("TextureViewer shaders are unavailable");
        RenderGraphBuilder builder;
        auto* sourceTexture=builder.RegisterExternalTexture("TextureViewer.Input",texture);
        auto* color=builder.RegisterExternalTexture("TextureViewer.Color",m_Color.get());
        auto* params=builder.AllocateParameter<TextureViewerPixelParameters>();
        params->Input2D=nullptr; params->InputCube=nullptr; params->InputArray=nullptr;
        // Existing RDG texture parameters declare the complete source as SRV.
        // Mip/layer select the samples without bypassing graph access tracking.
        params->Settings={float(mip),float(slice),0,0}; params->InputSampler=GlobalNearestSampler.get();
        if (kind==0) params->Input2D=sourceTexture;
        else if (kind==1) params->InputCube=sourceTexture;
        else params->InputArray=sourceTexture;
        params->Targets.NumColorRenderTargets=1; params->Targets[0].Texture=color;
        params->Targets[0].Action=ERenderTargetActions::DontCare_Store;
        RHIGraphicsPipelineStateDesc desc;
        desc.ShaderStages.VertexShader=static_cast<RHIVertexShader*>(vs->GetRHIShader());
        desc.ShaderStages.FragmentShader=static_cast<RHIFragmentShader*>(ps->GetRHIShader());
        desc.VertexDescState=RHIPipelineStateCache::GetOrCreateVertexDescState({});
        RHIRasterizerStateDesc rasterizer; rasterizer.CullMode=ERHICullMode::None;
        desc.RasterizerState=RHIPipelineStateCache::GetOrCreateRasterizerState(rasterizer);
        RHIDepthStencilStateDesc depth; depth.bDepthTestEnable=false; depth.bDepthWriteEnable=false;
        desc.DepthStencilState=RHIPipelineStateCache::GetOrCreateDepthStencilState(depth);
        RHIColorBlendStateDesc blend; blend.Attachments.resize(1);
        desc.ColorBlendState=RHIPipelineStateCache::GetOrCreateColorBlendState(blend);
        desc.AttachmentDesc.ColorAttachmentCount=1; desc.AttachmentDesc.ColorAttachments[0].Format=ERHIFormat::R8G8B8A8_UNorm;
        desc.AttachmentDesc.ColorAttachments[0].Actions=params->Targets[0].Action;
        desc.PrimitiveTopology=EPrimitiveTopology::TriangleList;
        auto* pipeline=RHIPipelineStateCache::GetOrCreateGraphicsPipelineState(desc);
        builder.AddPass<TextureViewerPixelParameters>("TextureViewer.Preview",TextureViewerPixelParameters::GetMetaData(),params,EPassFlag::Graphic,
            [=](RHICommandListBase& commands) {
                auto& cmd=static_cast<RHIGraphicCommandList&>(commands);
                RHIRenderPassInfo pass; pass.RenderTargets=params->Targets.GetBoundRenderTarget(); pass.RenderArea={0,0,width,height};
                cmd.BeginRenderPass(pass); cmd.SetGraphicPipelineState(pipeline);
                cmd.SetViewport(0,0,float(width),float(height),0,1); cmd.SetScissor(0,0,width,height);
                SetShaderParameters(cmd,ps,params); cmd.Draw(3); cmd.EndRenderPass();
            });
        builder.Execute();
    }
}
