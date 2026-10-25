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

        void SetStaticMesh(StaticMesh* inMesh);
        StaticMesh* GetStaticMesh() const { return m_Mesh; }

        //----------------------------------------
        // Material Override
        //----------------------------------------

        void SetMaterial(uint32_t slotIndex, MaterialInterface* material);
        MaterialInterface* GetMaterial(uint32_t slotIndex) const;

        uint32_t GetNumMaterials() const;
        MaterialInstance* GetMaterialInstance(uint32_t slotIndex) const;
        MaterialInstance* GetOrCreateMaterialInstance(uint32_t slotIndex);
        void ResetMaterialInstance(uint32_t slotIndex);
        bool SetMaterialParameter(uint32_t slotIndex, const std::string& name, const MaterialParameterValue& value)
        {
            auto parameter = CreateMaterialParameter(name,value);
            ValidateMaterialParameter(*parameter,true);
            return GetOrCreateMaterialInstance(slotIndex)->SetParameter(name,value);
        }
        bool ResetMaterialParameter(uint32_t slotIndex, const std::string& name, EMaterialParameterType type)
        {
            FindMaterialParameterDefinition(name,type);
            auto* instance = GetMaterialInstance(slotIndex);
            return instance && instance->ResetParameter(name,type);
        }

        template<EMaterialParameterSemantic Semantic, typename ParamType>
        void SetMaterialParameterValue(uint32_t slotIndex, const std::string& name,
                                       const typename ParamType::Type& value)
        {
            TMaterialParameter<Semantic,ParamType> parameter(CanonicalMaterialParameterName(name),value);
            ValidateMaterialParameter(parameter,true);
            auto* instance = GetOrCreateMaterialInstance(slotIndex);
            instance->SetParameterValue<Semantic, ParamType>(name, value);
        }

        //----------------------------------------
        // LOD
        //----------------------------------------

        void SetForcedLOD(int32_t inLod)
        {
            m_ForcedLOD = inLod;
            MarkRenderStateDirty();
        }

        int32_t GetForcedLOD() const
        {
            return m_ForcedLOD;
        }

        //----------------------------------------
        // PrimitiveComponent Overrides
        //----------------------------------------

        PrimitiveSceneProxy* CreateSceneProxy() const override;

        const Core::BoxSphereBounds& GetBounds() const override;

    private:
        Core::BoxSphereBounds CalcBounds(
            const Core::Mat4& localToWorld) override;
        // CPU-side mesh asset reference
        std::shared_ptr<StaticMeshAsset> m_MeshAsset;
        StaticMesh* m_Mesh = nullptr;

        // Material overrides (same concept as UE OverrideMaterials)
        std::vector<std::shared_ptr<MaterialAsset>> m_OverrideMaterials;
        std::vector<std::unique_ptr<MaterialInstance>> m_MaterialInstances;

        // -1 = auto LOD
        int32_t m_ForcedLOD = -1;
    };

} // namespace Engine