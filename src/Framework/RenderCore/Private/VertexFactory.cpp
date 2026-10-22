#include "VertexFactory.h"
#include "RHIApi.h"
#include "RHIPipelineStateCache.h"
using namespace RHI;

namespace RenderCore {
	RHI::RHIVertexDescState* VertexFactory::GetRHIVertexDescState() const
	{
		return _rhiVertexDescState;
	}

    void VertexFactory::Bind(RHI::RHIGraphicCommandList& rhiCmdList) const
    {
        for (int i = 0; i < _streams.size(); i++) {
            if( _streams[i].Buffer)
            rhiCmdList.SetStreamSource(_streams[i].Binding, _streams[i].Buffer, _streams[i].Offset);
        }
    }

	VertexFactory::VertexElement VertexFactory::AccessStreamComponent(const VertexStreamComponent& component, uint32_t attributeIndex)
	{
        for (const auto& stream : _streams)
        {
            bool bMatch =
                stream.Buffer == component.Buffer &&
                stream.Stride == component.Stride &&
                stream.InputRate == component.InputRate &&
                stream.Offset == 0;

            if (bMatch)
            {
                return VertexElement
                {
                    .Binding = stream.Binding,
                    .Location = attributeIndex,
                    .ElementOffset = component.ComponentOffset,
                    .Format = component.Format
                };
            }
        }

        uint32_t newBinding = (uint32_t)_streams.size();

        VertexStream newStream;
        newStream.Buffer = component.Buffer;
        newStream.Stride = component.Stride;
        newStream.Offset = 0;
        newStream.Binding = newBinding;
        newStream.InputRate = component.InputRate;

        _streams.push_back(newStream);

        return VertexElement
        {
            .Binding = newBinding,
            .Location = attributeIndex,
            .ElementOffset = component.ComponentOffset,
            .Format = component.Format
        };
	}

	void VertexFactory::InitDeclaration(const std::vector<VertexElement>& elements, RHI::RHIVertexDescState** outVertexDescState)
	{
        RHI::RHIVertexDescStateDesc desc;

        //
        // 创建 Binding Descriptions
        //
        for (uint32_t i = 0; i < _streams.size(); ++i)
        {
            const VertexStream& stream = _streams[i];

            RHI::RHIVertexBindingDesc bindingDesc;
            bindingDesc.Binding = stream.Binding;
            bindingDesc.Stride = stream.Stride;
            bindingDesc.InputRate = stream.InputRate;

            desc.Bindings.push_back(bindingDesc);
        }

        //
        // 创建 Attribute Descriptions
        //
        for (const VertexElement& element : elements)
        {
            RHI::RHIVertexAttributeDesc attrDesc;

            attrDesc.Location = element.Location;
            attrDesc.Binding = element.Binding;
            attrDesc.Offset = element.ElementOffset;
            attrDesc.Format = element.Format;

            desc.Attributes.push_back(attrDesc);
        }

        //
        // 创建最终 VertexInputState
        //
        *outVertexDescState = RHIPipelineStateCache::GetOrCreateVertexDescState(desc);

	}



} // namespace RenderCore