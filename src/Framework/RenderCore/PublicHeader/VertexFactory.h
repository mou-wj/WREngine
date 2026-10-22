#pragma once

#include <vector>
#include <string>
#include <memory>
#include <cstdint>
#include <unordered_map>
#include <functional>
#include "RHIResource.h"
#include "ShaderCore.h"
#include "RHIDefine.h"
#include "RHICommandList.h"
namespace RenderCore {
	

// 前向声明
class VertexFactory;
class ShaderType;
class VertexFactoryType;
class ShaderCompilerEnvironment;
using VertexFactoryFeatureFlags = uint64_t;
class ShaderParametersMetadata;
struct VertexFactoryShaderPermutationParameters
{
    const VertexFactoryType* VFType;
    const ShaderType* ShaderType;
    VertexFactoryFeatureFlags VertexFactoryFlags;
    RHI::ERHIShaderPlatform Platform;
};

// 顶点工厂类型// 声明函数原型
using VFShouldCompileFunc = std::function<bool(const VertexFactoryShaderPermutationParameters&)>;
using VFModifyEnvFunc = std::function<void(const VertexFactoryShaderPermutationParameters&, ShaderCompilerEnvironment&)>;

class RENDERCORE_API VertexFactoryType
{
public:
    static std::unordered_map<std::string, VertexFactoryType*>& GetRegisterMap() {
        static std::unordered_map<std::string, VertexFactoryType*> map;
        return map;
    }

    VertexFactoryType(
        std::string inName,
        std::string inShaderPath,
        uint32_t inPermutationTotalCount,
        VFShouldCompileFunc inShouldCompile,
        VFModifyEnvFunc inModifyEnv,
        const ShaderParametersMetadata* inRootParametersMetadata
    )
        : Name(std::move(inName))
        , ShaderPath(std::move(inShaderPath))
        , PermutationTotalCount (inPermutationTotalCount)
        , ShouldCompileFunc(std::move(inShouldCompile))
        , ModifyEnvFunc(std::move(inModifyEnv))
        , RootParametersMetadata(inRootParametersMetadata)
    {
        GetRegisterMap()[Name] = this;
    }

    // 暴露给编译器的接口
    bool ShouldCompile(const VertexFactoryShaderPermutationParameters& params) const
    {
        return ShouldCompileFunc(params);
    }

    void ModifyCompilationEnvironment(const VertexFactoryShaderPermutationParameters& params, ShaderCompilerEnvironment& outEnv) const
    {

		std::string virtualIncludePath = "#include \"" + ShaderPath + "\"";
        outEnv.VirtualIncludes["/Generated/VertexFactory.sf"] = virtualIncludePath;
        ModifyEnvFunc(params, outEnv);
    }

    const std::string& GetName() const { return Name; }
    const std::string& GetShaderPath() const { return ShaderPath; }

    std::string Name;
    std::string ShaderPath;
    VFShouldCompileFunc ShouldCompileFunc;
    VFModifyEnvFunc ModifyEnvFunc;
    uint32_t PermutationTotalCount;
    const ShaderParametersMetadata* RootParametersMetadata;
};


struct VertexStreamComponent
{
    RHI::RHIBuffer* Buffer = nullptr;

    uint32_t ComponentOffset = 0;

    uint32_t Stride = 0;

    RHI::ERHIFormat Format = RHI::ERHIFormat::Unknown;

    RHI::ERHIInputRate InputRate = RHI::ERHIInputRate::PerVertex;
};

// 顶点工厂基类
class RENDERCORE_API VertexFactory
{
public:
    VertexFactory() = default;
    virtual ~VertexFactory() = default;
    virtual RenderCore::VertexFactoryType* GetType() const { return nullptr; }
    RHI::RHIVertexDescState* GetRHIVertexDescState() const;
    VertexFactoryFeatureFlags GetVertexFactoryFlags() const { return _vertexFactoryFlags; }
    static bool ShouldCompilePermutation(const VertexFactoryShaderPermutationParameters& parameters) {
        return false;
    }
    static void ModifyCompilationEnvironment(const VertexFactoryShaderPermutationParameters& parameters, ShaderCompilerEnvironment& outEnvironment) {

    }

    void Bind(RHI::RHIGraphicCommandList& rhiCmdList) const;


protected:
    struct VertexStream
    {
        RHI::RHIBuffer* Buffer = nullptr;

        uint32_t Offset = 0;
        uint32_t Stride = 0;

        uint32_t Binding = 0;
		RHI::ERHIInputRate InputRate = RHI::ERHIInputRate::PerVertex;

        bool operator==(const VertexStream& rhs) const
        {
            return Buffer == rhs.Buffer &&
                Offset == rhs.Offset &&
                Binding == rhs.Binding;
        }
    };


    struct VertexElement
    {
        uint32_t Binding = 0;

        uint32_t Location = 0;

        uint32_t ElementOffset = 0;

        RHI::ERHIFormat Format = RHI::ERHIFormat::Unknown;
    };
protected:

    //
    // 自动生成 Binding + Attribute
    //
    VertexElement AccessStreamComponent(
        const VertexStreamComponent& component,
        uint32_t attributeIndex);

    //
    // 创建最终 VertexInputState
    //
    void InitDeclaration(
        const std::vector<VertexElement>& elements, RHI::RHIVertexDescState** outVertexDescState);


    std::vector<VertexStream> _streams;
    RHI::RHIVertexDescState* _rhiVertexDescState;
    VertexFactoryFeatureFlags _vertexFactoryFlags;
};


} // namespace RenderCore
#define DECLARE_VERTEX_FACTORY_TYPE(FactoryClass) \
public: \
    static RenderCore::VertexFactoryType StaticType; \
    virtual RenderCore::VertexFactoryType* GetType() const override;\
    static uint32_t PermutationTotalCount;

#define IMPLEMENT_VERTEX_FACTORY_TYPE(FactoryClass, ShaderFile,TotalCount)\
RenderCore::VertexFactoryType FactoryClass::StaticType(\
    #FactoryClass, \
    ShaderFile, \
    FactoryClass::PermutationTotalCount,\
    & FactoryClass::ShouldCompilePermutation, \
    & FactoryClass::ModifyCompilationEnvironment ,\
    FactoryClass::GetShaderParameterMetadata() \
); \
\
RenderCore::VertexFactoryType* FactoryClass::GetType() const \
{ \
return &StaticType; \
}\
uint32_t FactoryClass::PermutationTotalCount = TotalCount;

#define IMPLEMENT_VERTEX_FACTORY_TYPE_DEFAULT(FactoryClass, ShaderFile) \
    IMPLEMENT_VERTEX_FACTORY_TYPE(FactoryClass, ShaderFile, 1)
