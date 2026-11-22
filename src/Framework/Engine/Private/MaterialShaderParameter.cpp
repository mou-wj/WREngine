#include "MaterialShaderParameter.h"
#include "Material.h"
#include "TextureAsset.h"
#include "RenderGraphBuilder.h"
#include "RenderResource.h"
#include <algorithm>
#include <stdexcept>

using namespace RHI;
using namespace RenderCore;
namespace Engine {
void BuildShaderParameters(const MaterialRenderProxy* proxy, RenderGraphBuilder& builder, MaterialShaderParameters& out)
{
    if (!proxy || !GlobalWhiteTexture || !GlobalFlatNormalTexture) throw std::runtime_error("Material defaults are not initialized");
    const auto scalar = [&](const char* name) {
        return std::get<float>(proxy->GetParameter(name,EMaterialParameterType::Scalar)->GetValueVariant());
    };
    const auto vector = [&](const char* name) {
        return std::get<std::array<float,4>>(proxy->GetParameter(name,EMaterialParameterType::Vector)->GetValueVariant());
    };
    const auto baseColor = vector("BaseColor");
    const auto emissive = vector("EmissiveColor");
    const auto tiling = vector("UVTiling");
    const auto offset = vector("UVOffset");
    out.UniformData.BaseColor = {baseColor[0],baseColor[1],baseColor[2],baseColor[3]};
    out.UniformData.EmissiveColor = {emissive[0],emissive[1],emissive[2]};
    out.UniformData.Opacity = scalar("Opacity");
    out.UniformData.OpacityMask = scalar("OpacityMask");
    out.UniformData.Metallic = scalar("Metallic");
    out.UniformData.Roughness = scalar("Roughness");
    out.UniformData.Specular = scalar("Specular");
    out.UniformData.AmbientOcclusion = scalar("AmbientOcclusion");
    out.UniformData.NormalIntensity = scalar("NormalIntensity");
    out.UniformData.UVTiling = {tiling[0],tiling[1]};
    out.UniformData.UVOffset = {offset[0],offset[1]};
    out.UniformData.AlphaCutoff = scalar("AlphaCutoff");
    const auto saturate = [](float value) { return std::clamp(value, 0.0f, 1.0f); };
    out.UniformData.ShadingModelData = {};
    if (proxy->GetShadingModel() == EShadingModel::Subsurface)
    {
        const auto subsurface = vector("SubsurfaceColor");
        const float strength = saturate(scalar("SubsurfaceStrength"));
        out.UniformData.ShadingModelData = {saturate(subsurface[0]) * strength, saturate(subsurface[1]) * strength,
            saturate(subsurface[2]) * strength, saturate(scalar("SubsurfaceRadius"))};
    }
    else if (proxy->GetShadingModel() == EShadingModel::ClearCoat)
    {
        out.UniformData.ShadingModelData = {saturate(scalar("ClearCoatStrength")), saturate(scalar("ClearCoatRoughness")), 0.0f, 0.0f};
    }
    out.TextureData.TextureMask = 0;
    const auto texture = [&](const char* name, uint32_t bit, const RenderTextureSP& fallback) {
        auto asset = std::get<std::shared_ptr<TextureAsset>>(proxy->GetParameter(name,EMaterialParameterType::Texture)->GetValueVariant());
        if (asset) out.TextureData.TextureMask |= 1u << bit;
        auto* renderTexture = asset ? asset->GetTexture() : fallback.get();
        return builder.RegisterExternalTexture(std::string(name) + "#" + std::to_string(reinterpret_cast<uintptr_t>(renderTexture->GetRHI())),renderTexture);
    };
    // Bits match MaterialParametersHelper.sf, including Normal at bit 2.
    out.TextureData.BaseColorTexture = texture("BaseColor",0,GlobalWhiteTexture);
    out.TextureData.OpacityTexture = texture("Opacity",1,GlobalWhiteTexture);
    out.TextureData.NormalTexture = texture("Normal",2,GlobalFlatNormalTexture);
    out.TextureData.ORMTexture = texture("ORM",3,GlobalWhiteTexture);
    out.TextureData.EmissiveTexture = texture("Emissive",4,GlobalWhiteTexture);
    out.SamplerData.LinearWrapSampler = GlobalSampler.get();
}
}