#include "GBufferInfo.h"
namespace Renderer {

    FGBufferInfo CreateGBufferInfo(const FGBufferParams& params)
    {
        FGBufferInfo info;

        info.Layout =
            params.bHasVelocity
            ? GBL_ForceVelocity
            : GBL_Default;

        info.NumTargets = 0;

        //=========================================================================
        // Targets
        //=========================================================================

        info.Targets[0] =
            FGBufferTarget(
                RHI::ERHIFormat::R8G8B8A8_UNorm,
                "GBufferA");

        info.Targets[1] =
            FGBufferTarget(
                RHI::ERHIFormat::R16G16B16A16_Float,
                "GBufferB");

        info.Targets[2] =
            FGBufferTarget(
                RHI::ERHIFormat::R8G8B8A8_UNorm,
                "GBufferC");

        info.NumTargets = 3;

        if (params.bHasVelocity)
        {
            info.Targets[3] =
                FGBufferTarget(
                    RHI::ERHIFormat::R16G16_Float,
                    "Velocity");

            info.NumTargets++;
        }

        //=========================================================================
        // Slot Mapping
        //=========================================================================

        info.Slots[GBS_BaseColor] =
            FGBufferItem(GBS_BaseColor, 0, 0, 3);

        info.Slots[GBS_Metallic] =
            FGBufferItem(GBS_Metallic, 0, 3, 1);

        info.Slots[GBS_WorldNormal] =
            FGBufferItem(GBS_WorldNormal, 1, 0, 3);

        info.Slots[GBS_Roughness] =
            FGBufferItem(GBS_Roughness, 1, 3, 1);
        info.Slots[GBS_ShadingModelId] =
            FGBufferItem(GBS_ShadingModelId, 2, 2, 1);

        if (params.bHasSpecular)
        {
            info.Slots[GBS_Specular] =
                FGBufferItem(GBS_Specular, 2, 0, 1);
        }

        if (params.bHasAO)
        {
            info.Slots[GBS_AO] =
                FGBufferItem(GBS_AO, 2, 1, 1);
        }

        if (params.bHasVelocity)
        {
            info.Slots[GBS_Velocity] =
                FGBufferItem(GBS_Velocity, 3, 0, 2);
        }

        return info;
    }

    const FGBufferItem* FindGBufferSlot(
        const FGBufferInfo& gBufferInfo,
        EGBufferSlot slot)
    {
        if (slot <= GBS_Invalid || slot >= GBS_Num)
        {
            return nullptr;
        }

        const FGBufferItem& item =
            gBufferInfo.Slots[slot];

        if (!item.bIsValid)
        {
            return nullptr;
        }

        return &item;
    }

    const FGBufferTarget* FindGBufferTargetByName(
        const FGBufferInfo& gBufferInfo,
        const std::string& name)
    {
        for (int i = 0; i < gBufferInfo.NumTargets; i++)
        {
            const FGBufferTarget& target =
                gBufferInfo.Targets[i];

            if (target.TargetName == name)
            {
                return &target;
            }
        }

        return nullptr;
    }


}