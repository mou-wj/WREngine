#include "ShadowMapAllocator.h"
using namespace RHI;
using namespace RenderCore;
namespace Renderer
{

    ShadowMapAllocator::ShadowMapAllocator()
    {
    }

    ShadowMapAllocator::~ShadowMapAllocator()
    {
        Shutdown();
    }

    bool ShadowMapAllocator::Initialize(
        const ShadowAllocatorDesc& desc)
    {
        m_Desc = desc;

        return InitializeAtlasResources();
    }

    const ShadowAllocatorDesc& ShadowMapAllocator::GetDesc() const
    {
        return m_Desc;
    }

    void ShadowMapAllocator::Shutdown()
    {
        m_ShadowAtlas = nullptr;
        m_DedicatedShadowTextures.clear();
    }

    bool ShadowMapAllocator::InitializeAtlasResources()
    {
        RHITextureDesc desc;
        desc.Width = m_Desc.SpotShadowAtlas.Width;
        desc.Height = m_Desc.SpotShadowAtlas.Height;
        desc.Depth = 1;
        desc.MipLevels = 1;
        desc.ArraySize = 1;

        desc.Format = ERHIFormat::R16G16_Float;
        desc.Type = ERHITextureType::Texture2D;
        desc.SampleCount = 1;

        desc.Usage =
            ERHITextureCreateFlag::RenderTarget |
            ERHITextureCreateFlag::ShaderResource;

        desc.bGenerateMips = false;
        desc.DebugName = "SpotShadowAtlas";

        m_ShadowAtlas =
            std::make_shared<RenderCore::RenderTexture>(desc);

        m_ShadowAtlas->InitRHIResource();

        return m_ShadowAtlas != nullptr;
    }

    ShadowAllocation ShadowMapAllocator::AllocateDirectionalCSMShadow(
        uint32_t resolution,
        uint32_t cascadeCount)
    {
        ShadowAllocation result;
        result.ShadowType = EShadowType::Directional;

        result.Slices.resize(cascadeCount);

        AllocateDedicatedTexture(
            resolution,
            resolution,
            EShadowType::Directional,
            result);
        return result;
    }

    ShadowAllocation ShadowMapAllocator::AllocatePointShadow(
        uint32_t resolution)
    {
        ShadowAllocation result;
        result.ShadowType = EShadowType::Point;

        result.Slices.resize(MaxPointLightFaces);

        AllocateDedicatedTexture(
            resolution,
            resolution,
            EShadowType::Point,
            result);

        return result;
    }

    ShadowAllocation ShadowMapAllocator::AllocateSpotShadow(
        uint32_t resolution)
    {
        ShadowAllocation result;
        result.ShadowType = EShadowType::Spot;

        result.Slices.resize(1);

        AllocateFromAtlas(
            resolution,
            resolution,
            result);

        return result;
    }

    RenderCore::RenderTexture* ShadowMapAllocator::GetShadowAtlas() const
    {
        return m_ShadowAtlas.get();
    }

    const std::vector<RenderCore::RenderTexture*>& ShadowMapAllocator::GetDedicatedPointLightShadowTextures() const
    {
        return m_PointLightShadowTextures;
    }

    const std::vector<RenderCore::RenderTexture*>& ShadowMapAllocator::GetDedicatedParallelLightShadowTextures() const
    {
        return m_ParallelLightShadowTextures;
    }

    void ShadowMapAllocator::ReleaseShadow(ShadowAllocation& allocation)
    {
        if (!allocation.IsValid())
        {
            return;
        }

        switch (allocation.ShadowType)
        {
            //--------------------------------------------------
            // Dedicated shadow texture
            //--------------------------------------------------
        case EShadowType::Directional:
        case EShadowType::Point:
        {
            auto* texture = allocation.Texture;

            if (texture)
            {
                auto it = std::find_if(
                    m_DedicatedShadowTextures.begin(),
                    m_DedicatedShadowTextures.end(),
                    [texture](const RenderCore::RenderTextureSP& tex)
                    {
                        return tex.get() == texture;
                    });

                if (it != m_DedicatedShadowTextures.end())
                {
                    m_DedicatedShadowTextures.erase(it);
                }

                //--------------------------------------------------
                // Remove from lookup array
                //--------------------------------------------------

                auto eraseTexture =
                    [texture](std::vector<RenderCore::RenderTexture*>& list)
                    {
                        auto it =
                            std::find(list.begin(), list.end(), texture);

                        if (it != list.end())
                        {
                            list.erase(it);
                        }
                    };

                eraseTexture(m_PointLightShadowTextures);
                eraseTexture(m_ParallelLightShadowTextures);
            }

            break;
        }

        //--------------------------------------------------
        // Atlas
        //--------------------------------------------------
        case EShadowType::Spot:
            // 后续实现 Atlas FreeList
            break;

        default:
            break;
        }

        allocation.Texture = nullptr;
        allocation.TextureIndex = 0;
        allocation.Slices.clear();
        allocation.ShadowType = EShadowType::Unknown;
    }

    bool ShadowMapAllocator::AllocateDedicatedTexture(
        uint32_t width,
        uint32_t height,
        EShadowType shadowType,
        ShadowAllocation& outAllocation)
    {
        RHITextureDesc desc;

        desc.Width = width;
        desc.Height = height;
        desc.Depth = 1;

        desc.MipLevels = 1;
        desc.ArraySize = 1;

        desc.Type = ERHITextureType::Texture2D;
        switch (shadowType)
        {
        case EShadowType::Directional:
            desc.Type = ERHITextureType::Texture2DArray;
            desc.ArraySize = MaxShadowCascadeCount;
            desc.DebugName = "DirectionalShadow";
            break;

        case EShadowType::Point:
            desc.Type = ERHITextureType::TextureCube;
            desc.ArraySize = 6;
            desc.DebugName = "PointShadow";
            break;

        default:
            return false;
        }
        desc.Format = ERHIFormat::R16G16_Float;
        desc.SampleCount = 1;

        desc.Usage =
            ERHITextureCreateFlag::RenderTarget |
            ERHITextureCreateFlag::ShaderResource;

        desc.bGenerateMips = false;
        desc.DebugName = "ShadowMap";

        auto texture =
            std::make_shared<RenderCore::RenderTexture>(desc);

        texture->InitRHIResource();

        m_DedicatedShadowTextures.push_back(texture);

        outAllocation.Texture = texture.get();
        outAllocation.ShadowType = shadowType;

        switch (shadowType)
        {
        case EShadowType::Point:
        {
            outAllocation.TextureIndex =
                (uint32_t)m_PointLightShadowTextures.size();

            m_PointLightShadowTextures.push_back(texture.get());
            break;
        }

        case EShadowType::Directional:
        {
            outAllocation.TextureIndex =
                (uint32_t)m_ParallelLightShadowTextures.size();

            m_ParallelLightShadowTextures.push_back(texture.get());
            break;
        }

        default:
            outAllocation.TextureIndex = 0;
            break;
        }
        //-----------------------------------------
        // Fill slices
        //-----------------------------------------

        outAllocation.Slices.resize(desc.ArraySize);

        for (uint32_t i = 0; i < desc.ArraySize; ++i)
        {
            auto& slice = outAllocation.Slices[i];

            slice.Layer = i;
            slice.Mip = 0;

            slice.X = 0;
            slice.Y = 0;

            slice.Width = width;
            slice.Height = height;

            slice.UVScale =
            {
                1.0f,
                1.0f
            };

            slice.UVOffset =
            {
                0.0f,
                0.0f
            };
        }

        return true;
    }

    bool ShadowMapAllocator::AllocateFromAtlas(
        uint32_t width,
        uint32_t height,
        ShadowAllocation& outAllocation)
    {
        const uint32_t atlasWidth = m_Desc.SpotShadowAtlas.Width;
        const uint32_t atlasHeight = m_Desc.SpotShadowAtlas.Height;

        //--------------------------------------------------
        // 当前行放不下，换行
        //--------------------------------------------------

        if (m_AtlasCursorX + width > atlasWidth)
        {
            m_AtlasCursorX = 0;
            m_AtlasCursorY += m_AtlasCurrentRowHeight;
            m_AtlasCurrentRowHeight = 0;
        }

        //--------------------------------------------------
        // Atlas 已满
        //--------------------------------------------------

        if (m_AtlasCursorY + height > atlasHeight)
        {
            return false;
        }

        //--------------------------------------------------
        // 创建 Allocation
        //--------------------------------------------------

        outAllocation.ShadowType = EShadowType::Spot;
        outAllocation.Texture = m_ShadowAtlas.get();
        outAllocation.TextureIndex = 0;

        outAllocation.Slices.clear();
        outAllocation.Slices.emplace_back();

        auto& slice = outAllocation.Slices.front();

        slice.Layer = 0;
        slice.Mip = 0;

        slice.X = m_AtlasCursorX;
        slice.Y = m_AtlasCursorY;

        slice.Width = width;
        slice.Height = height;

        slice.UVScale =
        {
            float(width) / float(atlasWidth),
            float(height) / float(atlasHeight)
        };

        slice.UVOffset =
        {
            float(m_AtlasCursorX) / float(atlasWidth),
            float(m_AtlasCursorY) / float(atlasHeight)
        };

        //--------------------------------------------------
        // 更新游标
        //--------------------------------------------------

        m_AtlasCursorX += width;

        m_AtlasCurrentRowHeight =
            m_AtlasCurrentRowHeight > height ? m_AtlasCurrentRowHeight : height;

        return true;
    }
}