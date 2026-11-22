#pragma once

#include "GlobalShader.h"
#include "RHIPipelineStateCache.h"
#include "RenderGraphBuilder.h"
#include "SceneView.h"
#include <stdexcept>

namespace Renderer::VolumeViewPass
{
    using namespace RenderCore;
    using namespace RHI;

    inline Core::Float4 Pack(Core::Float3 vector, float w)
    {
        return {vector.X, vector.Y, vector.Z, w};
    }

    inline void ValidateInputs(const Engine::SceneView& view, RenderGraphTextureRef color, RenderGraphTextureRef depth)
    {
        if (!color || !depth || !view.bDepthRangeZeroToOne || view.ProjectionMatrix(3, 2) != -1.0f ||
            view.Viewport.X != 0 || view.Viewport.Y != 0)
        {
            throw std::invalid_argument("Volume view effects require a full-size perspective view and zero-to-one depth.");
        }
        const auto& desc = color->GetDesc();
        if (view.Viewport.Width != int(desc.Width) || view.Viewport.Height != int(desc.Height) ||
            depth->GetDesc().Width != desc.Width || depth->GetDesc().Height != desc.Height ||
            desc.Format != ERHIFormat::R32G32B32A32_Float || !EnumHasAnyFlags(desc.Usage, ERHITextureCreateFlag::TransferDest))
        {
            throw std::invalid_argument("Volume view effects require matching depth and transferable linear RGBA32F scene color.");
        }
    }

    inline RenderGraphTextureRef Image(RenderGraphBuilder& builder, const char* name, uint32_t width, uint32_t height,
        ERHIFormat format = ERHIFormat::R32G32B32A32_Float, ERHITextureCreateFlag extraUsage = ERHITextureCreateFlag::None)
    {
        RenderGraphTextureDesc desc{};
        desc.Width = width;
        desc.Height = height;
        desc.Format = format;
        desc.Usage = ERHITextureCreateFlag::UAV | ERHITextureCreateFlag::ShaderResource | extraUsage;
        return builder.CreateTexture(name, desc);
    }

    inline RenderGraphTextureUAVRef WriteImage(RenderGraphBuilder& builder, RenderGraphTextureRef texture)
    {
        RenderGraphTextureUAVDesc desc{};
        desc.Texture = texture;
        desc.Format = texture->GetDesc().Format;
        return builder.CreateTextureUAV(texture->GetName() + ".UAV", desc);
    }

    template<class Shader, class Parameters>
    void AddCompute(RenderGraphBuilder& builder, const char* name, Parameters* parameters, uint32_t width, uint32_t height)
    {
        const auto* shader = GShaderMap.GetShader<Shader>(0);
        if (!shader)
        {
            throw std::runtime_error(std::string("Missing shader for ") + name);
        }
        RHIComputePipelineStateDesc desc{};
        desc.ComputeShader = static_cast<RHIComputeShader*>(shader->GetRHIShader());
        const auto pipeline = RHIPipelineStateCache::GetOrCreateComputePipelineState(desc);
        builder.AddPass(name, Parameters::GetMetaData(), parameters, EPassFlag::Compute, [=](RHICommandListBase& list)
        {
            auto& cmd = static_cast<RHIComputeCommandList&>(list);
            cmd.SetComputePipelineState(pipeline);
            SetShaderParameters(cmd, shader, parameters);
            cmd.Dispatch((width + 7) / 8, (height + 7) / 8, 1);
        });
    }

    BEGIN_SHADER_PARAMETER_STRUCT(VolumeViewCopyParameters)
        RDG_TEXTURE_ACCESS(Source, ERHIResourceAccess::TransferSrc)
        RDG_TEXTURE_ACCESS(Dest, ERHIResourceAccess::TransferDest)
    END_SHADER_PARAMETER_STRUCT(VolumeViewCopyParameters)

    inline void AddColorCopy(RenderGraphBuilder& builder, const char* name, RenderGraphTextureRef source, RenderGraphTextureRef dest)
    {
        auto* copy = builder.AllocateParameter<VolumeViewCopyParameters>();
        copy->Source = source;
        copy->Dest = dest;
        builder.AddPass(name, VolumeViewCopyParameters::GetMetaData(), copy, EPassFlag::Graphic, [copy](RHICommandListBase& cmd)
        {
            const auto& desc = copy->Source->GetDesc();
            RHICopyTextureDesc region{};
            region.SrcRegion = region.DstRegion = {0, 0, 0, desc.Width, desc.Height, 1};
            cmd.CopyTexture(copy->Source->GetRHITexture(), copy->Dest->GetRHITexture(), region);
        });
    }
}
