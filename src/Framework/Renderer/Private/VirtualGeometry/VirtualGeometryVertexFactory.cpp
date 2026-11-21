#include "VirtualGeometryVertexFactory.h"
#include <stdexcept>
#include "Shader.h"

namespace Renderer
{
    uint32_t VirtualGeometryVertexFactory::PermutationTotalCount = 1;
    RenderCore::VertexFactoryType VirtualGeometryVertexFactory::StaticType(
        "VirtualGeometryVertexFactory", "/vertexfactory/VirtualGeometryVertexFactory.sf", 1, &ShouldCompilePermutation,
        &ModifyCompilationEnvironment, GetShaderParameterMetadata());

    RenderCore::VertexFactoryType* VirtualGeometryVertexFactory::GetType() const
    {
        return &StaticType;
    }

    VirtualGeometryVertexFactory::VirtualGeometryVertexFactory(const Engine::VirtualGeometryResource& resource)
    {
        if (!resource.PositionBuffer || !resource.PositionBuffer->GetRHI())
        {
            throw std::invalid_argument("Virtual geometry position buffer is not initialized");
        }
        _vertexFactoryFlags = 0;
        _rhiVertexDescState = nullptr;
        RenderCore::VertexStreamComponent position;
        position.Buffer = resource.PositionBuffer->GetRHI();
        position.Stride = sizeof(Core::Float3);
        position.Format = RHI::ERHIFormat::R32G32B32_Float;
        InitDeclaration({AccessStreamComponent(position, 0)}, &_rhiVertexDescState);
    }

    bool VirtualGeometryVertexFactory::ShouldCompilePermutation(
        const RenderCore::VertexFactoryShaderPermutationParameters& parameters)
    {
        return parameters.VertexFactoryFlags == 0 && parameters.ShaderType &&
               (parameters.ShaderType->Name == "VirtualGeometryRasterVS" ||
                parameters.ShaderType->Name == "VirtualGeometryRasterPS");
    }

    void VirtualGeometryVertexFactory::ModifyCompilationEnvironment(
        const RenderCore::VertexFactoryShaderPermutationParameters& parameters,
        RenderCore::ShaderCompilerEnvironment& environment)
    {
        environment.SetDefine("VIRTUAL_GEOMETRY_VERTEX_FACTORY", 1);
    }
} // namespace Renderer
