#include "RenderGraphResource.h"
#include "RenderGraphBuilder.h"
#include "RHIApi.h"
using namespace RHI;
namespace RenderCore {

// -------------------------------------------------------------------------------------------------
//  Render Graph Resource Base Class Implementation
// -------------------------------------------------------------------------------------------------
RenderGraphResource::RenderGraphResource(const std::string& name, RenderGraphResourceType type)
    : _name(name), _type(type), _resource(nullptr)
{
}

RenderGraphResource::~RenderGraphResource()
{
    // Cleanup if necessary
}

void RenderGraphResource::SetRHIResource(RHI::RHIResource* resource)
{
    _resource = resource;
}

// -------------------------------------------------------------------------------------------------
//  Render Graph Texture Implementation
// -------------------------------------------------------------------------------------------------
RenderGraphTexture::RenderGraphTexture(const std::string& name, const RenderGraphTextureDesc& desc)
    : RenderGraphResource(name, RenderGraphResourceType::Texture), m_Desc(desc)
{
    m_Tracker.Initialize(desc.ArraySize, desc.MipLevels, 1);
}

RenderGraphTexture::~RenderGraphTexture()
{
    // Cleanup if necessary
}

// -------------------------------------------------------------------------------------------------
//  Render Graph Buffer Implementation
// -------------------------------------------------------------------------------------------------
RenderGraphBuffer::RenderGraphBuffer(const std::string& name, const RenderGraphBufferDesc& desc)
    : RenderGraphResource(name, RenderGraphResourceType::Buffer), m_Desc(desc)
{
}

RenderGraphBuffer::~RenderGraphBuffer()
{
    // Cleanup if necessary
}


RenderGraphUAV::RenderGraphUAV(const std::string& name, RenderGraphResourceType type) : RenderGraphView(name, type)
{
}
RenderGraphUAV::~RenderGraphUAV()
{
}

RenderGraphSRV::RenderGraphSRV(const std::string& name, RenderGraphResourceType type) : RenderGraphView(name, type)
{
}
RenderGraphSRV::~RenderGraphSRV()
{
}

RenderGraphView::RenderGraphView(const std::string& name, RenderGraphResourceType type) : RenderGraphResource(name, type)
{
}
RenderGraphView::~RenderGraphView()
{
}



RenderGraphTextureSRV::RenderGraphTextureSRV(const std::string& name, const RenderGraphTextureSRVDesc& desc)   
    : RenderGraphSRV(name, RenderGraphResourceType::TextureSRV), m_Desc(desc)
{   

}

RenderGraphTextureSRV::~RenderGraphTextureSRV()
{
    // Cleanup if necessary
}

RenderGraphTextureUAV::RenderGraphTextureUAV(const std::string& name, const RenderGraphTextureUAVDesc& desc)
    : RenderGraphUAV(name, RenderGraphResourceType::TextureUAV), m_Desc(desc)
{   
    // TODO: Implement
}

RenderGraphTextureUAV::~RenderGraphTextureUAV()
{
    // Cleanup if necessary

}

RenderGraphBufferSRV::RenderGraphBufferSRV(const std::string& name, const RenderGraphBufferSRVDesc& desc)
    : RenderGraphSRV(name, RenderGraphResourceType::BufferSRV), m_Desc(desc)
{   
    // TODO: Implement
}

RenderGraphBufferSRV::~RenderGraphBufferSRV()
{
    // Cleanup if necessary
}

RenderGraphBufferUAV::RenderGraphBufferUAV(const std::string& name, const RenderGraphBufferUAVDesc& desc)
    : RenderGraphUAV(name, RenderGraphResourceType::BufferSRV), m_Desc(desc)
{
    // TODO: Implement
}
RenderGraphBufferUAV::~RenderGraphBufferUAV()
{
    // Cleanup if necessary
}



} // namespace WR::RenderCore