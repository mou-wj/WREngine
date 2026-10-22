// StaticMeshComponent.h
#pragma once

#include "PrimitiveComponent.h"
#include "StaticMesh.h"
#include "EngineExport.h"
#include "Material.h"
#include <memory>
#include <vector>

namespace Engine
{
    class MaterialAsset;
    class StaticMeshAsset;
    class StaticMesh;
    class StaticMeshProxy;

    class ENGINE_API StaticMeshComponent : public PrimitiveComponent
    {
        DECLARE_TYPE_ID_DERIVED_TYPE(StaticMeshComponent, PrimitiveComponent)

    public:
        StaticMeshComponent();
        ~StaticMeshComponent() override;

        //----------------------------------------
        // Mesh
        //----------------------------------------

        void SetStaticMesh(StaticMesh* InMesh);
        StaticMesh* GetStaticMesh() const { return Mesh; }

        //----------------------------------------
        // Material Override
        //----------------------------------------

        void SetMaterial(uint32_t SlotIndex, MaterialInterface* Material);
        MaterialInterface* GetMaterial(uint32_t SlotIndex) const;

        uint32_t GetNumMaterials() const;
        MaterialInstance* GetMaterialInstance(uint32_t slotIndex) const;
        MaterialInstance* GetOrCreateMaterialInstance(uint32_t slotIndex);
        void ResetMaterialInstance(uint32_t slotIndex);

        template<EMaterialParameterSemantic Semantic, typename ParamType>
        void SetMaterialParameterValue(uint32_t slotIndex, const std::string& name,
                                       const typename ParamType::Type& value)
        {
            auto* instance = GetOrCreateMaterialInstance(slotIndex);
            instance->SetParameterValue<Semantic, ParamType>(name, value);
            MarkRenderStateDirty();
        }

        //----------------------------------------
        // LOD
        //----------------------------------------

        void SetForcedLOD(int32_t InLOD)
        {
            ForcedLOD = InLOD;
            MarkRenderStateDirty();
        }

        int32_t GetForcedLOD() const
        {
            return ForcedLOD;
        }

        //----------------------------------------
        // PrimitiveComponent Overrides
        //----------------------------------------

        PrimitiveSceneProxy* CreateSceneProxy() const override;

        const Core::BoxSphereBounds& GetBounds() const override;

    private:
        Core::BoxSphereBounds CalcBounds(
            const Core::Mat4& LocalToWorld) override;
        // CPU-side mesh asset reference
        std::shared_ptr<StaticMeshAsset> MeshAsset;
        StaticMesh* Mesh = nullptr;

        // Material overrides (same concept as UE OverrideMaterials)
        std::vector<std::shared_ptr<MaterialAsset>> OverrideMaterials;
        std::vector<std::unique_ptr<MaterialInstance>> MaterialInstances;

        // -1 = auto LOD
        int32_t ForcedLOD = -1;
    };

} // namespace Engine