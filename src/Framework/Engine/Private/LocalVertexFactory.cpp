#include "LocalVertexFactory.h"

using namespace RenderCore;
namespace Engine
{

    IMPLEMENT_VERTEX_FACTORY_TYPE(
        LocalVertexFactory,
        "/vertexfactory/LocalVertexFactory.sf",
        1 << 6
    )


        //
        // Build the vertex layout.
        //
        void LocalVertexFactory::SetData(
            const LocalVertexFactoryData& inData)
    {
        m_Data = inData;

        //
        // Clear the existing vertex streams.
        //
        Streams.clear();

        //
        // Build vertex elements from the available streams.
        //
        std::vector<VertexElement> elements;

        //
        // POSITION -> location 0
        //
        auto assignComponent = [&elements,this](const RenderCore::VertexStreamComponent& component, uint32_t& location) {
            if (component.Buffer != nullptr) {
                elements.push_back(
                    AccessStreamComponent(
                        component,
                        location));
                location++;
            }

        };
        uint32_t optionalLocation = 0;
        assignComponent(m_Data.PositionComponent, optionalLocation);
        LocalVertexFactoryFeatureFlags flags;
		
        //
        // UV -> location 1
        //
        assignComponent(m_Data.UVComponent, optionalLocation);
        flags.SupportsTexCoord = m_Data.UVComponent.Buffer != nullptr;


        //
        // NORMAL -> location 2
        //
        assignComponent(m_Data.NormalComponent, optionalLocation);
        flags.SupportsNormal = m_Data.NormalComponent.Buffer != nullptr;
        //
        // TANGENT -> location 3
        //
        assignComponent(m_Data.TangentComponent, optionalLocation);
        flags.SupportsTangent = m_Data.TangentComponent.Buffer != nullptr;
        //
        // COLOR -> location 4
        //
        assignComponent(m_Data.ColorComponent, optionalLocation);
        flags.SupportsVertexColor = m_Data.ColorComponent.Buffer != nullptr;
        //
        // Initialize the regular vertex input declaration.
        //
        InitDeclaration(elements,&RHIVertexDescState);
        VertexFactoryFlags = flags.PackedFlags;
        //
        // Instance ID -> location 5
        //
        RenderCore::VertexStreamComponent instanceComponent;
        instanceComponent.Buffer = nullptr;
        instanceComponent.InputRate = RHI::ERHIInputRate::PerInstance;
        instanceComponent.Format = RHI::ERHIFormat::R32_UInt;
        instanceComponent.Stride = sizeof(uint32_t);
        m_InstanceLocation = optionalLocation;
        elements.push_back(
            AccessStreamComponent(
                instanceComponent,
                optionalLocation));
        //
        // Initialize the instanced vertex input declaration.
        //
        InitDeclaration(elements, &m_RHIInstancedVertexDescState);
        

    }
    RHI::RHIVertexDescState* LocalVertexFactory::GetRHIInstancedVertexDescState() const {
        return m_RHIInstancedVertexDescState;
    }
    void LocalVertexFactory::BindInstanceBuffer(RHI::RHIGraphicCommandList& cmdList, RHI::RHIBuffer* instanceBuffer, uint32_t offset) const
    {
        if (m_InstanceLocation < Streams.size())
            cmdList.SetStreamSource(Streams[m_InstanceLocation].Binding, instanceBuffer, offset);
    }
    //
    // Shader Permutation
    //
    bool LocalVertexFactory::ShouldCompilePermutation(
        const VertexFactoryShaderPermutationParameters& parameters)
    {
        LocalVertexFactoryFeatureFlags flags = {};
        flags.PackedFlags = parameters.VertexFactoryFlags;
        
        return flags.PackedFlags == 3 || flags.PackedFlags == 19;
        
        /*
           ===========================================================================
               Tangent requires Normal
           ===========================================================================
           */

        if (flags.SupportsTangent &&
            !flags.SupportsNormal)
        {
            return false;
        }

        /*
        ===========================================================================
            Instance data currently unsupported
        ===========================================================================
        */

        if (flags.SupportsInstanceData)
        {
            return false;
        }

        return true;
    }


    //
    // Shader Compile Environment
    //
    void LocalVertexFactory::ModifyCompilationEnvironment(
        const VertexFactoryShaderPermutationParameters& parameters,
        ShaderCompilerEnvironment& outEnvironment)
    {

        LocalVertexFactoryFeatureFlags flags = {};
		flags.PackedFlags = parameters.VertexFactoryFlags;

        /*
        ===========================================================================
            Vertex Factory Type
        ===========================================================================
        */

        outEnvironment.SetDefine("LOCAL_VERTEX_FACTORY", 1);

        /*
        ===========================================================================
            Geometry Attribute Features
        ===========================================================================
        */

        outEnvironment.SetDefine(
            "VF_SUPPORTS_TEXCOORD",
            flags.SupportsTexCoord);

        outEnvironment.SetDefine(
            "VF_SUPPORTS_NORMAL",
            flags.SupportsNormal);

        outEnvironment.SetDefine(
            "VF_SUPPORTS_TANGENT",
            flags.SupportsTangent);

        outEnvironment.SetDefine(
            "VF_SUPPORTS_VERTEX_COLOR",
            flags.SupportsVertexColor);

        /*
        ===========================================================================
            Instance Features
        ===========================================================================
        */

        outEnvironment.SetDefine(
            "VF_SUPPORTS_INSTANCE",
            flags.SupportsInstanceData);

    }


}        