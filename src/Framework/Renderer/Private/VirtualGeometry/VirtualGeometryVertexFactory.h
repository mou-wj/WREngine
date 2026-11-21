#pragma once
#include "VertexFactory.h"
#include "ShaderParameter.h"
#include "VirtualGeometryResource.h"

namespace Renderer
{
    BEGIN_SHADER_PARAMETER_STRUCT(VirtualGeometryVertexFactoryParameters)
    SHADER_PARAMETER(Core::Mat4, LocalToWorld)
    SHADER_PARAMETER(Core::Mat4, ViewProjection)
    SHADER_PARAMETER(uint32_t, DrawID)
    END_SHADER_PARAMETER_STRUCT(VirtualGeometryVertexFactoryParameters)

    class RENDERER_API VirtualGeometryVertexFactory final : public RenderCore::VertexFactory
    {
        DECLARE_VERTEX_FACTORY_TYPE(VirtualGeometryVertexFactory)
    public:
        explicit VirtualGeometryVertexFactory(const Engine::VirtualGeometryResource& resource);
        static bool ShouldCompilePermutation(const RenderCore::VertexFactoryShaderPermutationParameters& parameters);
        static void ModifyCompilationEnvironment(const RenderCore::VertexFactoryShaderPermutationParameters& parameters,
                                                 RenderCore::ShaderCompilerEnvironment& environment);

        static const RenderCore::ShaderParametersMetadata* GetShaderParameterMetadata()
        {
            return VirtualGeometryVertexFactoryParameters::GetMetaData();
        }
    };
} // namespace Renderer
