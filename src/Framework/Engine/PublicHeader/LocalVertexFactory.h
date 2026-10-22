#pragma once

#include "VertexFactory.h"
#include "EngineExport.h"
#include "ShaderParameter.h"
#include "RenderResource.h"
#include "InstanceDataMgr.h"

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
namespace Engine
{
    BEGIN_SHADER_PARAMETER_STRUCT(LocalVertexFactoryInstanceSubParameters)
        SHADER_PARAMETER(Core::Float4x4, LocalToWorld)
    END_SHADER_PARAMETER_STRUCT(LocalVertexFactoryInstanceSubParameters)

    BEGIN_SHADER_PARAMETER_STRUCT(LocalVertexFactoryInstanceParameters)
        SHADER_PARAMETER_RHI_STRUCTURED_BUFFER(LocalVertexFactoryInstanceSubParameters, InstanceData)
    END_SHADER_PARAMETER_STRUCT(LocalVertexFactoryInstanceParameters)


    BEGIN_SHADER_PARAMETER_STRUCT(LocalVertexFactoryParameters)
        SHADER_PARAMETER(Core::Float4x4, LocalToWorld)
        SHADER_PARAMETER(Core::Float4x4, ViewProjection)
        SHADER_PARAMETER(Core::Float3, CameraWorldPosition)
        SHADER_PARAMETER_STRUCT_REFERENCE(LocalVertexFactoryInstanceParameters, LocalVFInstanceInfo)
    END_SHADER_PARAMETER_STRUCT(LocalVertexFactoryParameters)

    union LocalVertexFactoryFeatureFlags
    {
        uint64_t PackedFlags = 0;

        struct
        {
            uint64_t SupportsTexCoord : 1;
            uint64_t SupportsNormal : 1;
            uint64_t SupportsTangent : 1;
            uint64_t SupportsVertexColor : 1;
            uint64_t SupportsInstanceData : 1;
        };
    };


    //
    // Mesh vertex stream resources.
    //
    struct LocalVertexFactoryData
    {
        RenderCore::VertexStreamComponent PositionComponent;
        RenderCore::VertexStreamComponent UVComponent;
        RenderCore::VertexStreamComponent NormalComponent;
        RenderCore::VertexStreamComponent TangentComponent;
        RenderCore::VertexStreamComponent ColorComponent;
    };

    //
    // LocalVertexFactory
    //
    class ENGINE_API LocalVertexFactory : public RenderCore::VertexFactory
    {
        DECLARE_VERTEX_FACTORY_TYPE(LocalVertexFactory)
    public:
        LocalVertexFactory() = default;

        void SetData(const LocalVertexFactoryData& inData);
        RHI::RHIVertexDescState* GetRHIInstancedVertexDescState() const;
		void BindInstanceBuffer(RHI::RHIGraphicCommandList& cmdList, RHI::RHIBuffer* instanceBuffer, uint32_t offset) const;

    public:

        static bool ShouldCompilePermutation(const RenderCore::VertexFactoryShaderPermutationParameters& parameters);
        static void ModifyCompilationEnvironment(const RenderCore::VertexFactoryShaderPermutationParameters& parameters, RenderCore::ShaderCompilerEnvironment& outEnvironment);
        static const RenderCore::ShaderParametersMetadata* GetShaderParameterMetadata()
        {
            return LocalVertexFactoryParameters::GetMetaData();
        }
    private:
        uint32_t m_InstanceLocation = 0;
        RHI::RHIVertexDescState* m_RHIInstancedVertexDescState = nullptr;
        LocalVertexFactoryData m_Data;
    };


    using LocalVertexFactoryInstanceManager = InstanceDataManager<LocalVertexFactoryInstanceSubParameters>;
    using LocalVertexFactoryInstanceBlock = InstanceDataManager<LocalVertexFactoryInstanceSubParameters>::BlockType;
    using LocalVertexFactoryInstanceBlockRef = InstanceDataManager<LocalVertexFactoryInstanceSubParameters>::BlockRef;

}