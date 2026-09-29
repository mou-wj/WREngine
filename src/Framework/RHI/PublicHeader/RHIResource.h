#pragma once
#include "RHIDefine.h"
#include "Math.hpp"
#include <vector>
#include <memory>
#include <assert.h>

namespace RHI
{

// 资源基类
class RHI_API RHIResource
{
public:
    explicit RHIResource(ERHIResourceType type = ERHIResourceType::Unknown)
        : ResourceType(type) {}
    virtual ~RHIResource() = default;

    ERHIResourceType GetResourceType() const { return ResourceType; }

protected:
    ERHIResourceType ResourceType;
};
// --------------------------------------------------
// 可被视图访问的资源基类
// --------------------------------------------------
class RHI_API RHIViewableResource : public RHIResource
{
public:
    explicit RHIViewableResource(ERHIResourceType type)
        : RHIResource(type)
    {
    }

    virtual ~RHIViewableResource() = default;
};
// 纹理资源
class RHI_API RHITexture : public RHIViewableResource
{
public:

    RHITexture(const RHITextureDesc& desc) : RHIViewableResource(ERHIResourceType::Texture), Desc(desc) {
        MipSizes.resize(Desc.MipLevels);
        for (uint32_t i = 0; i < Desc.MipLevels; ++i) {
            MipSizes[i] = Core::Int3(Desc.Width >> i, Desc.Height >> i, Desc.Depth >> i);
        }
    }
    virtual ~RHITexture() = default;

    // 获取纹理描述
    const RHITextureDesc& GetDesc() const { return Desc; }

    const Core::Int3& GetMipSize(uint32_t MipLevel) const { return MipSizes[MipLevel]; }

protected:
    RHITextureDesc Desc;
    std::vector<Core::Int3> MipSizes;
};

// 缓冲区资源
class RHI_API RHIBuffer : public RHIViewableResource
{
public:

    RHIBuffer(const RHIBufferDesc& desc) : RHIViewableResource(ERHIResourceType::Buffer), Desc(desc) {}
    virtual ~RHIBuffer() = default;

    // 获取缓冲区描述
    const RHIBufferDesc& GetDesc() const { return Desc; }

protected:
    RHIBufferDesc Desc;
};


class RHI_API RHIResourceView {
public:
    explicit RHIResourceView(RHIViewableResource* Resource) : Resource(Resource) {}
    RHIViewableResource* GetResource() const { return Resource; }
private:
    RHIViewableResource* Resource = nullptr;
};

class RHI_API RHIShaderResourceView : public RHIResourceView,public RHIResource  {
public:
    explicit RHIShaderResourceView(RHIViewableResource* Resource)
        : RHIResourceView(Resource),RHIResource(ERHIResourceType::ShaderResourceView) {}
};

class RHI_API RHIUnorderedAccessView : public RHIResourceView,public RHIResource  {
public:
    explicit RHIUnorderedAccessView(RHIViewableResource* Resource)
        : RHIResourceView(Resource),RHIResource(ERHIResourceType::UnorderedAccessView) {}
};
struct RayTracingAccelerationStructureSizeInfo
{
    uint64_t ResultSize = 0;
    uint64_t BuildScratchSize = 0;
    uint64_t UpdateScratchSize = 0;
};
enum class ERayTracingAccelerationStructureType {
    BottomLevel,
    TopLevel
};

class RHI_API RHIRayTracingAccelerationStructure : public RHIResource
{
public:
    RHIRayTracingAccelerationStructure() : RHIResource(ERHIResourceType::RayTracingAccelerationStructure) {}

    const RayTracingAccelerationStructureSizeInfo& GetSizeInfo() const { return SizeInfo; }

    virtual ERayTracingAccelerationStructureType GetType() const = 0;
protected:
    RayTracingAccelerationStructureSizeInfo SizeInfo;

};
struct RHIRayTracingGeometryDesc
{
    RHIBuffer* VertexBuffer = nullptr;
    RHIBuffer* IndexBuffer = nullptr;

    uint32_t VertexStride = 0;

    uint32_t VertexCount = 0;
    uint32_t IndexCount = 0;

    bool AllowUpdate = false;
    bool AllowCompaction = false;
};
class RHI_API RHIRayTracingGeometry : public RHIRayTracingAccelerationStructure
{
public:
    RHIRayTracingGeometry()
        : RHIRayTracingAccelerationStructure()
    {
    }

    virtual ~RHIRayTracingGeometry() = default;

    virtual void SetGeometryDesc(
        const RHIRayTracingGeometryDesc& Desc) = 0;

    const RHIRayTracingGeometryDesc& GetGeometryDesc() const
    {
        return GeometryDesc;
    }
    virtual ERayTracingAccelerationStructureType GetType() const {
        return ERayTracingAccelerationStructureType::BottomLevel;
    }

protected:
    RHIRayTracingGeometryDesc GeometryDesc;
};
struct RHIRayTracingInstanceDesc
{
    RHIRayTracingGeometry* Geometry = nullptr;

    Core::Float4x4 Transform;

    uint32_t InstanceID = 0;

    uint8_t Mask = 0xff;

};
struct RHIRayTracingInstancesDesc
{
    std::vector<RHIRayTracingInstanceDesc> Instances;
};
class RHI_API RHIRayTracingInstance : public RHIRayTracingAccelerationStructure
{
public:
    RHIRayTracingInstance()
        : RHIRayTracingAccelerationStructure()
    {
    }

    virtual ~RHIRayTracingInstance() = default;

    virtual void SetInstancesDesc(
        const RHIRayTracingInstancesDesc& Instances) = 0;

    const RHIRayTracingInstancesDesc& GetInstancesDesc() const
    {
        return Instances;
    }
    virtual ERayTracingAccelerationStructureType GetType() const {
        return ERayTracingAccelerationStructureType::TopLevel;
    }
protected:
    RHIRayTracingInstancesDesc Instances;
};

class RHIStagingBuffer
{
public:
    RHIStagingBuffer(uint32_t InSize) : Size(InSize) {}
    virtual void* Map(uint32_t Offset, uint32_t NumBytes) = 0;
    virtual void Unmap() = 0;
protected:
    uint32_t Size;
};

// Shader基类
class RHI_API RHIShader : public RHIResource
{
public:
    RHIShader() : RHIResource(ERHIResourceType::Shader) {}
    virtual ~RHIShader() = default;
    // 获取着色器类型
    ERHIShaderFrequency GetShaderType() const { return ShaderType; }
protected:
    ERHIShaderFrequency ShaderType = ERHIShaderFrequency::Unknown; // 着色器类型
};

class RHI_API RHIGraphicShader : public RHIShader
{
public:
    
protected:
    
};

// 顶点着色器
class RHI_API RHIVertexShader : public RHIGraphicShader
{
public:
    RHIVertexShader() {
        ResourceType = ERHIResourceType::VertexShader;
        ShaderType = ERHIShaderFrequency::Vertex;
    }
    virtual ~RHIVertexShader() = default;
};

// 片元/像素着色器
class RHI_API RHIFragmentShader : public RHIGraphicShader
{
public:
    RHIFragmentShader() {
        ResourceType = ERHIResourceType::FragmentShader;
        ShaderType = ERHIShaderFrequency::Fragment;
    }
    virtual ~RHIFragmentShader() = default;
};

// 几何着色器
class RHI_API RHIGeometryShader : public RHIGraphicShader
{
public:
    RHIGeometryShader() {
        ResourceType = ERHIResourceType::GeometryShader;
        ShaderType = ERHIShaderFrequency::Geometry;
    }
    virtual ~RHIGeometryShader() = default;
};

// 计算着色器
class RHI_API RHIComputeShader : public RHIShader
{
public:
    RHIComputeShader() {
        ResourceType = ERHIResourceType::ComputeShader;
        ShaderType = ERHIShaderFrequency::Compute;
    }
    virtual ~RHIComputeShader() = default;
};

// 细分控制着色器
class RHI_API RHITessControlShader : public RHIGraphicShader
{
public:
    RHITessControlShader() {
        ResourceType = ERHIResourceType::TessControlShader;
        ShaderType = ERHIShaderFrequency::TessControl;
    }
    virtual ~RHITessControlShader() = default;
};

// 细分评估着色器
class RHI_API RHITessEvalShader : public RHIGraphicShader
{
public:
    RHITessEvalShader() {
        ResourceType = ERHIResourceType::TessEvalShader;
        ShaderType = ERHIShaderFrequency::TessEvaluation;
    }
    virtual ~RHITessEvalShader() = default;
};

// Mesh Shader
class RHI_API RHIMeshShader : public RHIGraphicShader
{
public:
    RHIMeshShader() {
        ResourceType = ERHIResourceType::MeshShader;
        ShaderType = ERHIShaderFrequency::Mesh;
    }
    virtual ~RHIMeshShader() = default;
};

// Task Shader
class RHI_API RHITaskShader : public RHIGraphicShader
{
public:
    RHITaskShader() {
        ResourceType = ERHIResourceType::TaskShader;
        ShaderType = ERHIShaderFrequency::Task;
    }
    virtual ~RHITaskShader() = default;
};
class RHI_API RHIRayTracingShader : public RHIShader
{
public:
	RHIRayTracingShader() {
	}
	virtual ~RHIRayTracingShader() = default;
};

// 光线追踪管线着色器
class RHI_API RHIRayGenShader : public RHIRayTracingShader
{
public:
    RHIRayGenShader() {
        ResourceType = ERHIResourceType::RayGenShader;
        ShaderType = ERHIShaderFrequency::RayGen;
    }
    virtual ~RHIRayGenShader() = default;
};

class RHI_API RHICloseHitShader : public RHIRayTracingShader
{
public:
    RHICloseHitShader() {
        ResourceType = ERHIResourceType::CloseHitShader;
        ShaderType = ERHIShaderFrequency::ClosestHit;
    }
    virtual ~RHICloseHitShader() = default;
};

class RHI_API RHIMissShader : public RHIRayTracingShader
{
public:
    RHIMissShader() {
        ResourceType = ERHIResourceType::MissShader;
        ShaderType = ERHIShaderFrequency::Miss;
    }
    virtual ~RHIMissShader() = default;
};

class RHI_API RHIAnyHitShader : public RHIRayTracingShader
{
public:
    RHIAnyHitShader() {
        ResourceType = ERHIResourceType::AnyHitShader;
        ShaderType = ERHIShaderFrequency::AnyHit;
    }
    virtual ~RHIAnyHitShader() = default;
};

class RHI_API RHIIntersectionShader : public RHIRayTracingShader
{
public:
    RHIIntersectionShader() {
        ResourceType = ERHIResourceType::IntersectionShader;
        ShaderType = ERHIShaderFrequency::Intersection;
    }
    virtual ~RHIIntersectionShader() = default;
};

class RHI_API RHICallableShader : public RHIRayTracingShader
{
public:
    RHICallableShader() {
        ResourceType = ERHIResourceType::CallableShader;
        ShaderType = ERHIShaderFrequency::Callable;
    }
    virtual ~RHICallableShader() = default;
};

// 顶点描述状态
class RHI_API RHIVertexDescState : public RHIResource
{
public:

    RHIVertexDescState(const RHIVertexDescStateDesc& desc) : RHIResource(ERHIResourceType::VertexDescState), desc(desc) {}
	// 获取顶点描述状态描述
	const RHIVertexDescStateDesc& GetDesc() const { return desc; }
    virtual ~RHIVertexDescState() = default;
private:
    RHIVertexDescStateDesc desc;
};

// 光栅化状态
class RHI_API RHIRasterizerState : public RHIResource
{
public:


    RHIRasterizerState(const RHIRasterizerStateDesc& desc) : RHIResource(ERHIResourceType::RasterizerState), desc(desc) {}
	// 获取光栅化状态描述
	const RHIRasterizerStateDesc& GetDesc() const { return desc; }
    virtual ~RHIRasterizerState() = default;
private:
    RHIRasterizerStateDesc desc;
};

// 颜色混合状态
class RHI_API RHIColorBlendState : public RHIResource
{
public:

    RHIColorBlendState(const RHIColorBlendStateDesc& desc) : RHIResource(ERHIResourceType::ColorBlendState), desc(desc) {}
	// 获取颜色混合状态描述
	const RHIColorBlendStateDesc& GetDesc() const { return desc; }
    virtual ~RHIColorBlendState() = default;
private:
    RHIColorBlendStateDesc desc;
};

// 深度测试和模板状态
class RHI_API RHIDepthStencilState : public RHIResource
{
public:

    RHIDepthStencilState(const RHIDepthStencilStateDesc& desc) : RHIResource(ERHIResourceType::DepthStencilState), desc(desc) {}
	// 获取深度模板状态描述
	const RHIDepthStencilStateDesc& GetDesc() const { return desc; }
    virtual ~RHIDepthStencilState() = default;
private:
    RHIDepthStencilStateDesc desc;
};

// 采样器资源
class RHI_API RHISampler : public RHIResource
{
public:
    RHISampler(const RHISamplerDesc& desc) : RHIResource(ERHIResourceType::Sampler), desc(desc) {}
    virtual ~RHISampler() = default;

private:
    RHISamplerDesc desc;
    // 采样器相关接口
};






// 资源智能指针类型别名（可根据需要扩展到所有资源类型）
using RHIResourceSP = std::shared_ptr<RHIResource>;
using RHITextureSP = std::shared_ptr<RHITexture>;
using RHIBufferSP = std::shared_ptr<RHIBuffer>;
using RHIStagingBufferSP = std::shared_ptr<RHIStagingBuffer>;
using RHIShaderResourceViewSP = std::shared_ptr<RHIShaderResourceView>;
using RHIUnorderedAccessViewSP = std::shared_ptr<RHIUnorderedAccessView>;
using RHIRayTracingGeometrySP = std::shared_ptr<RHIRayTracingGeometry>;
using RHIRayTracingInstanceSP = std::shared_ptr<RHIRayTracingInstance>;

using RHIShaderSP = std::shared_ptr<RHIShader>;
using RHIVertexShaderSP = std::shared_ptr<RHIVertexShader>;
using RHIFragmentShaderSP = std::shared_ptr<RHIFragmentShader>;
using RHIComputeShaderSP = std::shared_ptr<RHIComputeShader>;
using RHIGeometryShaderSP = std::shared_ptr<RHIGeometryShader>;
using RHITessControlShaderSP = std::shared_ptr<RHITessControlShader>;
using RHITessEvalShaderSP = std::shared_ptr<RHITessEvalShader>;
using RHIMeshShaderSP = std::shared_ptr<RHIMeshShader>;
using RHITaskShaderSP = std::shared_ptr<RHITaskShader>;
using RHIRayGenShaderSP = std::shared_ptr<RHIRayGenShader>;
using RHICloseHitShaderSP = std::shared_ptr<RHICloseHitShader>;
using RHIMissShaderSP = std::shared_ptr<RHIMissShader>;
using RHIAnyHitShaderSP = std::shared_ptr<RHIAnyHitShader>;
using RHIIntersectionShaderSP = std::shared_ptr<RHIIntersectionShader>;
using RHICallableShaderSP = std::shared_ptr<RHICallableShader>;


using RHISamplerSP = std::shared_ptr<RHISampler>;
using RHIVertexDescStateSP = std::shared_ptr<RHIVertexDescState>;
using RHIRasterizerStateSP = std::shared_ptr<RHIRasterizerState>;
using RHIColorBlendStateSP = std::shared_ptr<RHIColorBlendState>;
using RHIDepthStencilStateSP = std::shared_ptr<RHIDepthStencilState>;


// Shader Stage
struct RHI_API RHIGraphicShaderStageDesc
{
    RHIVertexShader* vertexShader = nullptr; // 指向对应阶段的shader对象
    RHIFragmentShader* fragmentShader = nullptr; // 指向对应阶段的shader对象
    // 可扩展entry point等
    
};


// RenderTarget 描述
struct RHIColorAttachmentDesc
{
    ERHIFormat format = ERHIFormat::Unknown;                    // 像素格式（PF_XXX 或自定义枚举）
    ERenderTargetActions actions = ERenderTargetActions::Load_Store;
    bool enableResolve = false;
    uint32_t sampleCount = 1;               // MSAA
    static inline uint64_t CalculateHash(const RHIColorAttachmentDesc& att)
    {
        uint64_t hash = 14695981039346656037ull; // FNV64 offset
        hash ^= static_cast<uint64_t>(att.format); hash *= 1099511628211ull;
        hash ^= static_cast<uint64_t>(att.actions); hash *= 1099511628211ull;
        hash ^= static_cast<uint64_t>(att.enableResolve); hash *= 1099511628211ull;
        hash ^= static_cast<uint64_t>(att.sampleCount); hash *= 1099511628211ull;
        return hash;
    }
};



struct RHIGraphicAttachmentDesc {
    // RenderTarget 信息
    uint32_t colorAttachmentCount = 0;
    RHIColorAttachmentDesc colorAttachments[MAX_RENDER_TARGETS]{};

    // Depth/Stencil Target 信息
    bool enableDepth = false;                 // Depth 是否启用
    bool enableStencil = false;               // Stencil 是否启用
    ERHIFormat depthStencilFormat = ERHIFormat::Unknown;      // PF_XXX
    ERenderTargetActions depthActions = ERenderTargetActions::Load_Store;
    ERenderTargetActions stencilActions = ERenderTargetActions::Load_Store;

    uint32_t numSamples = 1;              // MSAA 样本数

    // 子通道（Vulkan Subpass hint）
    uint32_t subpassIndex = 0;
    static uint64_t CalculateHash(const RHIGraphicAttachmentDesc& desc)
    {
        uint64_t hash = 14695981039346656037ull; // FNV64 offset

        // Color attachments
        hash ^= static_cast<uint64_t>(desc.colorAttachmentCount); hash *= 1099511628211ull;
        for (uint32_t i = 0; i < desc.colorAttachmentCount; ++i)
        {
            hash ^= RHIColorAttachmentDesc::CalculateHash(desc.colorAttachments[i]); hash *= 1099511628211ull;
        }

        // Depth/Stencil
        hash ^= static_cast<uint64_t>(desc.enableDepth); hash *= 1099511628211ull;
        hash ^= static_cast<uint64_t>(desc.enableStencil); hash *= 1099511628211ull;
        hash ^= static_cast<uint64_t>(desc.depthStencilFormat); hash *= 1099511628211ull;
        hash ^= static_cast<uint64_t>(desc.depthActions); hash *= 1099511628211ull;
        hash ^= static_cast<uint64_t>(desc.stencilActions); hash *= 1099511628211ull;

        // Global
        hash ^= static_cast<uint64_t>(desc.numSamples); hash *= 1099511628211ull;
        hash ^= static_cast<uint64_t>(desc.subpassIndex); hash *= 1099511628211ull;

        return hash;
    }
};


// Graphics Pipeline
struct RHI_API RHIGraphicsPipelineStateDesc
{
    EQueueType InitialQueueType = EQueueType::Graphics;
    RHIGraphicShaderStageDesc shaderStages;
    // 可扩展其它管线相关状态引用，如顶点描述、光栅化、混合、深度模板等
    RHIVertexDescState* vertexDescState = nullptr; // 顶点描述状态
    EPrimitiveTopology primitiveTopology = EPrimitiveTopology::TriangleList;
    RHIRasterizerState* rasterizerState = nullptr; // 光栅化状态
    RHIColorBlendState* colorBlendState = nullptr; // 颜色混合状态
    RHIDepthStencilState* depthStencilState = nullptr; // 深度测试和模板状态
    RHIGraphicAttachmentDesc attachmentDesc;

};

// 管线状态资源
class RHI_API RHIGraphicsPipelineState : public RHIResource
{
public:
    RHIGraphicsPipelineState(const RHIGraphicsPipelineStateDesc& desc) : RHIResource(ERHIResourceType::GraphicPipelineState), desc(desc) {}

    virtual ~RHIGraphicsPipelineState() = default;
    const RHIGraphicsPipelineStateDesc& GetDesc() const { return desc; }

protected:
    RHIGraphicsPipelineStateDesc desc;
};

// Compute Pipeline
struct RHI_API RHIComputePipelineStateDesc
{
    RHIComputeShader* computeShader;// 指向Compute Shader
    // 可扩展Compute管线特有参数
};

class RHI_API RHIComputePipelineState : public RHIResource
{
public:
    RHIComputePipelineState(const RHIComputePipelineStateDesc& desc) : RHIResource(ERHIResourceType::ComputePipelineState), desc(desc) {}
    ~RHIComputePipelineState() override = default;
    const RHIComputePipelineStateDesc& GetDesc() const { return desc; }
    // 可扩展Compute管线特有参数
protected:
    RHIComputePipelineStateDesc desc;
};

// RayTracing Pipeline
struct RHI_API RHIRayTracingPipelineStateDesc
{
    std::vector<RHIRayTracingShader*> RayGenTable;
    std::vector<RHIRayTracingShader*> MissTable;
    std::vector<RHIRayTracingShader*> HitGroupTable;
    std::vector<RHIRayTracingShader*> CallableTable;
    std::vector<RHIRayTracingShader*> IntersectTable;
};

class RHI_API RHIRayTracingPipelineState : public RHIResource
{
public:
    RHIRayTracingPipelineState(const RHIRayTracingPipelineStateDesc& desc) : RHIResource(ERHIResourceType::RayTracingPipelineState), desc(desc) {}
    ~RHIRayTracingPipelineState() override = default;

    // 可扩展RayTracing管线特有参数
protected:
    RHIRayTracingPipelineStateDesc desc;
};



using RHIGraphicsPipelineStateSP = std::shared_ptr<RHIGraphicsPipelineState>;
using RHIComputePipelineStateSP = std::shared_ptr<RHIComputePipelineState>;
using RHIRayTracingPipelineStateSP = std::shared_ptr<RHIRayTracingPipelineState>;


constexpr int32_t MaxColorAttachments = 8;

struct RHIBoundRenderTargets
{
    struct ColorAttachment
    {
        RHITexture* Texture = nullptr;
        RHITexture* ResolveTarget = nullptr;

        uint8_t  MipIndex = 0;
        int32_t  ArraySlice = 0;
        RHIClearValueBinding ClearBinding;
        ERenderTargetActions Actions = ERenderTargetActions::Load_Store;
        uint32_t SampleCount = 1;
    };

    struct DepthStencilAttachment
    {
        RHITexture* Texture = nullptr;
        RHITexture* ResolveTarget = nullptr;

        uint32_t MipIndex = 0;
        uint32_t ArraySlice = 0;
        RHIClearValueBinding ClearBinding;
        ERenderTargetActions Actions = ERenderTargetActions::Load_Store;
        uint32_t SampleCount = 1;
    };

    // === 核心数据 ===
    ColorAttachment       ColorAttachments[MaxColorAttachments];
    DepthStencilAttachment DepthStencil;

    uint8_t NumColorAttachments = 0;
    Core::Int2 Dimensions{};
    RHIBoundRenderTargets() = default;
    void Bound(RHITexture* colorTexture, ERenderTargetActions colorActions,RHITexture* depthTexture, ERenderTargetActions depthActions,int Width,int Height) {

        // reset
        NumColorAttachments = 1;
        for (uint32_t i = 0; i < MaxColorAttachments; ++i)
        {
            ColorAttachments[i] = {};
        }
        DepthStencil = {};

        // =========================
        // Color Attachments
        // =========================

        assert(colorTexture != nullptr && "Color attachment enabled but colorTexture is null");
        


       ColorAttachment& att = ColorAttachments[0];

       att.Texture = colorTexture;
       att.ResolveTarget = nullptr;
       att.MipIndex = 0;
       att.ArraySlice = 0;
       att.Actions = colorActions;
       // Clear binding
       att.ClearBinding.Binding =
           RHIClearValueBinding::ClearValueBinding::Color;

        


        // =========================
        // Depth / Stencil
        // =========================

        DepthStencil.Texture = depthTexture;
        DepthStencil.ResolveTarget = nullptr;
        DepthStencil.MipIndex = 0;
        DepthStencil.ArraySlice = 0;
        DepthStencil.Actions = depthActions;
        DepthStencil.ClearBinding.Binding =
            RHIClearValueBinding::ClearValueBinding::DepthStencil;

        DepthStencil.ClearBinding.Depth = 1.0f;
        DepthStencil.ClearBinding.Stencil = 0;
        
        Dimensions.x = Width;
        Dimensions.y = Height;
        

    }

    static size_t CalculateHash(const RHIBoundRenderTargets& renderTargets) {
        // FNV-1a 64bit
        uint64_t hash = 14695981039346656037ull;
        auto fnHashBytes = [&](const void* data, size_t size)
            {
                const uint8_t* ptr = reinterpret_cast<const uint8_t*>(data);
                for (size_t i = 0; i < size; ++i)
                {
                    hash ^= static_cast<uint64_t>(ptr[i]);
                    hash *= 1099511628211ull;
                }
            };

        // 1. Color attachments
        fnHashBytes(&renderTargets.NumColorAttachments, sizeof(renderTargets.NumColorAttachments));
        for (uint8_t i = 0; i < renderTargets.NumColorAttachments; ++i)
        {
            const auto& att = renderTargets.ColorAttachments[i];

            uintptr_t texPtr = reinterpret_cast<uintptr_t>(att.Texture);
            uintptr_t resolvePtr = reinterpret_cast<uintptr_t>(att.ResolveTarget);
            fnHashBytes(&texPtr, sizeof(texPtr));
            fnHashBytes(&resolvePtr, sizeof(resolvePtr));

            fnHashBytes(&att.MipIndex, sizeof(att.MipIndex));
            fnHashBytes(&att.ArraySlice, sizeof(att.ArraySlice));
        }

        // 2. Depth/Stencil
        const auto& ds = renderTargets.DepthStencil;
        uintptr_t dsTexPtr = reinterpret_cast<uintptr_t>(ds.Texture);
        uintptr_t dsResolvePtr = reinterpret_cast<uintptr_t>(ds.ResolveTarget);
        fnHashBytes(&dsTexPtr, sizeof(dsTexPtr));
        fnHashBytes(&dsResolvePtr, sizeof(dsResolvePtr));
        fnHashBytes(&ds.MipIndex, sizeof(ds.MipIndex));
        fnHashBytes(&ds.ArraySlice, sizeof(ds.ArraySlice));

        return static_cast<size_t>(hash);
    }

    

    bool HasDepth() const { return DepthStencil.Texture != nullptr; }

    void CalculateDimensions() { 

        // 1. 优先用 color attachment
        for (uint8_t i = 0; i < NumColorAttachments; ++i)
        {
            const auto& att = ColorAttachments[i];
            if (att.Texture)
            {
                Core::Int3 size = att.Texture->GetMipSize(att.MipIndex);
                Dimensions.x = size.x;
                Dimensions.y = size.y;
            }
        }

        // 2. fallback depth
        if (DepthStencil.Texture)
        {
            auto size = DepthStencil.Texture->GetMipSize(DepthStencil.MipIndex);
            Dimensions.x = size.x;
            Dimensions.y = size.y;
        }
        for (uint8_t i = 0; i < NumColorAttachments; ++i)
        {
            const auto& att = ColorAttachments[i];
            if (att.Texture)
            {
                Core::Int3 size = att.Texture->GetMipSize(att.MipIndex);
                if (size.x != Dimensions.x || size.y != Dimensions.y) {
                    assert(0);
                }
            }
        }

        // 2. fallback depth
        if (DepthStencil.Texture)
        {
            auto size = DepthStencil.Texture->GetMipSize(DepthStencil.MipIndex);
            if (size.x != Dimensions.x || size.y != Dimensions.y) {
                assert(0);
            }
        }
    }

};

struct RHIRenderPassInfo {
    RHIBoundRenderTargets RenderTargets;
    RHIRect RenderArea;


};


}