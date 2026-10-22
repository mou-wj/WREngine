#pragma once

#include "RenderGraphResource.h"

#include <string>
#include <vector>
#include <functional> // For std::function
#include <list>

#include "RHICommandList.h"
#include "ShaderParameter.h"
#include "RHITransition.h"
#include "ShaderParameter.h"

namespace RenderCore {
    // Forward Declarations
class RenderGraph;
class RenderGraphBuilder;

class RENDERCORE_API BarrierBatchBegin {
public:

    void AddTransition(RenderGraphResource* resource,const RHI::RHITransitionInfo& transientInfo) {
        m_Transitions.push_back(transientInfo);
        m_Resources.push_back(resource);
    }
    void Process();
    const std::vector<RHI::RHITransitionInfo>& GetTransitions() const { return m_Transitions; }

private:
    std::vector<RHI::RHITransitionInfo> m_Transitions;
    std::vector<RenderGraphResource*> m_Resources;
};

class RENDERCORE_API BarrierBatchEnd {
public:
    void AddTransition(RenderGraphResource* resource, const RHI::RHITransitionInfo& transientInfo) {
        m_Transitions.push_back(transientInfo);
        m_Resources.push_back(resource);
    }
    void Process();

    const std::vector<RHI::RHITransitionInfo>& GetTransitions() const { return m_Transitions; }

private:
    std::vector<RHI::RHITransitionInfo> m_Transitions;
    std::vector<RenderGraphResource*> m_Resources;
};

enum class EPassFlag {
    Graphic,
    Compute
};

/**
 * 对应 UE 的 FRDGParameterStruct
 * 封装了 Pass 所需的所有数据（常量、RDG资源、RenderTarget 绑定）
 */
class RENDERCORE_API RenderGraphParameterStruct
{
public:
    RenderGraphParameterStruct() = default;

    /**
     * @param InContents 指向由 Builder 分配并由用户填充的内存块
     * @param InMetadata 该内存块对应的反射元数据
     */
    RenderGraphParameterStruct(const void* inContents, const ShaderParametersMetadata* inMetadata)
        : m_Contents(reinterpret_cast<const uint8_t*>(inContents))
        , m_Metadata(inMetadata)
    {
        
    }

    // 获取原始内存指针
    const uint8_t* GetContents() const { return m_Contents; }

    // 获取反射元数据
    const ShaderParametersMetadata* GetMetadata() const { return m_Metadata; }


private:


    const uint8_t* m_Contents = nullptr;
    const ShaderParametersMetadata* m_Metadata = nullptr;
};


//  Render Graph Pass Base Class
// -------------------------------------------------------------------------------------------------
class RENDERCORE_API RenderGraphPass
{
public:
    // Construction/Destruction
    RenderGraphPass(const std::string& name, EPassFlag passFlag, const RenderGraphParameterStruct& parameter) ;
    virtual ~RenderGraphPass() = default;

    EPassFlag GetPassFlag() const { return _passFlag; }

    // Accessors
    const std::string& GetName() const { return _name; }

    // Execution
    virtual void Execute(RHI::RHICommandListBase& commandList) = 0;

    // Pass Info
    const RenderGraphParameterStruct& GetParameterStruct() const { return _parameterStruct; }


protected:
    // Pass Name (for debugging and identification)
    std::string _name;
    EPassFlag _passFlag;
    // Pass Info (metadata about the pass)
    RenderGraphParameterStruct _parameterStruct;
    using PassList = std::list<RenderGraphPass*>;
    PassList _passConsumers;//依赖当前pass的pass集合
    PassList _passProducers;//当前pass依赖的pass集合
    BarrierBatchBegin _beginBarrier;//pass执行开始前的barrier
    BarrierBatchEnd _endBarrier;//pass执行结束后的barrier
    /** 
     * Pass 内部持有的访问意向
     */
    struct RenderGraphTextureIntent
    {
        RenderGraphTexture* Texture = nullptr;

        // 该 Pass 对纹理的访问需求
        RHI::ERHIResourceAccess RequiredAccess = RHI::ERHIResourceAccess::Unknown;

        // 访问的子资源范围（重要：解决你之前担心的 Subresource 问题）
        // 如果是全资源访问，可以使用之前定义的 kAllSubresources
        RHI::RHISubresourceRange SubresourceRange;

        bool operator==(const RenderGraphTextureIntent& other) const {
            return Texture == other.Texture && RequiredAccess == other.RequiredAccess && SubresourceRange == other.SubresourceRange;
        }
        
    };

    /**
     * Pass 内部持有的 Buffer 访问意向
     */
    struct RenderGraphBufferIntent
    {
        RenderGraphBuffer* Buffer = nullptr;

        // 该 Pass 需要的访问权限 (如：ShaderResource, UnorderedAccess, VertexBuffer 等)
        RHI::ERHIResourceAccess RequiredAccess = RHI::ERHIResourceAccess::Unknown;

		bool operator==(const RenderGraphBufferIntent& other) const {
			return Buffer == other.Buffer && RequiredAccess == other.RequiredAccess;
		}

    };
    void AddTextureIntent(const RenderGraphTextureIntent& intent) {
        for (auto i = 0; i < _textureIntents.size(); i++) {
            if (intent == _textureIntents[i]) {
                return;
            }
        }

        _textureIntents.push_back(intent);
    }
    void AddBufferIntent(const RenderGraphBufferIntent& intent) {   
		for (auto i = 0; i < _bufferStates.size(); i++) {
			if (intent == _bufferStates[i]) {
				return;
			}
		}
		_bufferStates.push_back(intent);
	}


    std::vector<RenderGraphTextureIntent> _textureIntents;
    std::vector<RenderGraphBufferIntent>  _bufferStates;
    friend class RenderGraphBuilder;
};

// -------------------------------------------------------------------------------------------------
//  Render Graph Lambda Pass (For simple passes defined inline)
// -------------------------------------------------------------------------------------------------

/**
 * 模板化的 Lambda Pass
 * TParameterStruct: 用户定义的参数结构体类型 (例如 FMyPassParams)
 * TExecuteLambda:   用户编写的执行逻辑 Lambda 类型
 */
template <typename TParameterStruct, typename TExecuteLambda>
class RenderGraphLambdaPass final : public RenderGraphPass
{
public:
    RenderGraphLambdaPass(
        const std::string& inName,
        EPassFlag inPassFlag,
        const RenderGraphParameterStruct& inParams,
        TExecuteLambda&& inExecuteLambda)
        : RenderGraphPass(inName, inPassFlag, inParams)
        , m_ExecuteLambda(std::forward<TExecuteLambda>(inExecuteLambda))
    {
    }

    /**
     * 实现基类的 Execute 接口
     */
    void Execute(RHI::RHICommandListBase& rhiCmdList) override
    {
        m_ExecuteLambda(rhiCmdList);
    }

private:
    // 直接存储 Lambda 对象，避免 std::function 的堆分配和虚函数调用开销
    TExecuteLambda m_ExecuteLambda;
};


using RenderGraphPassRef = RenderGraphPass*;


} // namespace WR::RenderCore