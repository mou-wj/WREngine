#include "MaterialShaderParameter.h"
#include "Material.h"
#include "RenderGraphBuilder.h"
#include "RenderResource.h"
using namespace RHI;
using namespace RenderCore;
namespace Engine {

    void BuildShaderParameters(
            const MaterialRenderProxy*
            proxy,
            RenderCore::RenderGraphBuilder& builder,
            MaterialShaderParameters&
            out)
    {
        // ====================
        // default init
        // ====================

        out.UniformData
            .BaseColor =
        { 1,1,1,1 };

        out.UniformData
            .Metallic = 0.8f;

        out.UniformData
            .Roughness = 0.2f;

        out.UniformData
            .Specular = 0.5f;
        out.UniformData.AmbientOcclusion = 1;
        out.TextureData
            .TextureMask = 0;

        RenderCore::RenderGraphTextureDesc emptyDesc;
        emptyDesc.Format = ERHIFormat::R8G8B8A8_UNorm;
        emptyDesc.Width = 1;
        emptyDesc.Height = 1;
        emptyDesc.Usage = ERHITextureCreateFlag::ShaderResource;

		auto emptyTexture = builder.CreateTexture(
			"EmptyTexture",
            emptyDesc);

        //默认初始化纹理为空纹理
        out.TextureData.BaseColorTexture = emptyTexture;
        out.TextureData.OpacityTexture = emptyTexture;
        out.TextureData.NormalTexture = emptyTexture;
        out.TextureData.ORMTexture = emptyTexture;
        out.TextureData.EmissiveTexture = emptyTexture;
        out.SamplerData.LinearWrapSampler = GlobalSampler.get();

        // ====================
        // scalar
        // ====================

        float scalar;

        if (proxy->GetValue<
            EMaterialParameterSemantic
            ::Metallic,
            FScalarType>(
                "Metallic",
                scalar))
        {
            out.UniformData
                .Metallic =
                scalar;
        }

        if (proxy->GetValue<
            EMaterialParameterSemantic
            ::Roughness,
            FScalarType>(
                "Roughness",
                scalar))
        {
            out.UniformData
                .Roughness =
                scalar;
        }

        // ====================
        // vector
        // ====================

        std::array<float, 4>
            color;

        if (proxy->GetValue<
            EMaterialParameterSemantic
            ::BaseColor,
            FVectorType>(
                "BaseColor",
                color))
        {
            out.UniformData
                .BaseColor =
            {
                color[0],
                color[1],
                color[2],
                color[3]
            };
        }

        // ====================
        // textures
        // ====================

        RenderCore
            ::RenderTexture*
            texture;

        if (proxy->GetValue<
            EMaterialParameterSemantic
            ::BaseColor,
            FTextureType>(
                "BaseColor",
                texture))
        {
            auto rdgBaseColorTexture = builder.RegisterExternalTexture("BaseColorTexture",
                texture);
            out.TextureData
                .BaseColorTexture =
                rdgBaseColorTexture;

            out.TextureData
                .TextureMask
                |= 1 << 0;
        }

        if (proxy->GetValue<
            EMaterialParameterSemantic
            ::Normal,
            FTextureType>(
                "Normal",
                texture))
        {
            auto rdgNormalTexture = builder.RegisterExternalTexture("NormalTexture",
                texture);
            out.TextureData
                .NormalTexture =
                rdgNormalTexture;

            out.TextureData
                .TextureMask
                |= 1 << 1;
        }
    }


}