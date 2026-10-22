#pragma once

#include "RHIResource.h" // For RHIShaderSP, ERHIResourceType
#include "ShaderCore.h"
#include "VertexFactory.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <optional>

using namespace RHI;

namespace RenderCore {
    class ShaderParametersMetadata;
    enum class EShaderParameterType : uint8_t
    {
        LooseData,
        UniformBuffer,
        Sampler,
        SRV,
        UAV,
        BindlessResourceIndex,
        BindlessSamplerIndex,
        AccelerationStructure,
        Num
    };

    // 单个参数在 CPU/GPU 中的分配信息
    struct ShaderParameterAllocation
    {
        uint16_t BufferIndex = 0;   // 所属 UniformBuffer 索引
        uint16_t BaseIndex = 0;     // GPU binding slot
        uint16_t Size = 0;          // 字节大小
        EShaderParameterType Type = EShaderParameterType::Num;
        mutable bool bBound = false;

        ShaderParameterAllocation() = default;

        ShaderParameterAllocation(uint16_t inBufferIndex, uint16_t inBaseIndex, uint16_t inSize, EShaderParameterType inType)
            : BufferIndex(inBufferIndex)
            , BaseIndex(inBaseIndex)
            , Size(inSize)
            , Type(inType)
        {
        }
    };

    // ShaderParameterMap：管理 Shader 参数名 → 分配信息
    class ShaderParameterAllocationMap
    {
    public:
        ShaderParameterAllocationMap() = default;

        // 查找参数
        std::optional<ShaderParameterAllocation> FindParameterAllocation(const std::string& name) const
        {
            auto it = m_ParameterMap.find(name);
            if (it != m_ParameterMap.end())
                return it->second;
            return std::nullopt;
        }

        // 添加参数
        void AddParameterAllocation(const std::string& name, uint16_t bufferIndex, uint16_t baseIndex, uint16_t size, EShaderParameterType type)
        {
            m_ParameterMap[name] = ShaderParameterAllocation(bufferIndex, baseIndex, size, type);
        }

        // 移除参数
        void RemoveParameterAllocation(const std::string& name)
        {
            m_ParameterMap.erase(name);
        }

        // 判断参数是否存在
        bool ContainsParameterAllocation(const std::string& name) const
        {
            return m_ParameterMap.find(name) != m_ParameterMap.end();
        }

        // 获取同类型参数列表
        std::vector<std::string> GetAllParameterNamesOfType(EShaderParameterType type) const
        {
            std::vector<std::string> names;
            for (const auto& kv : m_ParameterMap)
            {
                if (kv.second.Type == type)
                    names.push_back(kv.first);
            }
            return names;
        }

        // 获取所有参数名
        std::vector<std::string> GetAllParameterNames() const
        {
            std::vector<std::string> names;
            for (const auto& kv : m_ParameterMap)
                names.push_back(kv.first);
            return names;
        }

        const std::unordered_map<std::string, ShaderParameterAllocation>& GetParameterMap() const {
            return m_ParameterMap;
        }

    private:
        std::unordered_map<std::string, ShaderParameterAllocation> m_ParameterMap;
    };




    // 上层 RenderCore 的绑定信息
    struct RENDERCORE_API ShaderParameterBindingInfo
    {
        // --------- RenderCore 层的 Shader Parameter Binding ---------
        struct RENDERCORE_API ShaderUniformBinding
        {
            EShaderParameterBaseType BaseType; // Float, Int, Bool 等基础类型
            uint32_t BufferIndex;              // 所属 UniformBuffer 索引
            uint32_t BaseIndex;                // GPU binding 中的索引
            uint32_t Offset;                 // 在统一 CPU buffer 中的偏移
            uint32_t Size;                   // 字节大小
        };

        struct RENDERCORE_API ShaderResourceBinding
        {
            EShaderParameterBaseType BaseType; // Texture / Buffer / UAV / Sampler
            uint16_t BindSlot;               // GPU绑定槽
            uint16_t ArraySize = 1;          // 支持数组
            uint32_t Offset;
        };
        // 名字到 Uniform 参数的映射
        std::unordered_map<std::string, ShaderUniformBinding> UniformBindings;

        // 名字到 Resource 参数的映射
        std::unordered_map<std::string, ShaderResourceBinding> ResourceBindings;

        // 添加 Uniform
        void AddUniformBinding(const std::string& name, const ShaderUniformBinding& binding)
        {
            UniformBindings[name] = binding;
        }

        // 添加 Resource
        void AddResourceBinding(const std::string& name, const ShaderResourceBinding& binding)
        {
            ResourceBindings[name] = binding;
        }

        // 获取 Uniform
        const ShaderUniformBinding* GetUniformBinding(const std::string& name) const
        {
            auto it = UniformBindings.find(name);
            if (it != UniformBindings.end())
                return &it->second;
            return nullptr;
        }

        // 获取 Resource
        const ShaderResourceBinding* GetResourceBinding(const std::string& name) const
        {
            auto it = ResourceBindings.find(name);
            if (it != ResourceBindings.end())
                return &it->second;
            return nullptr;
        }
    };
    struct ShaderType;
    class Shader;
    using ModifyCompilationEnvironmentFuncType = std::function< void(const ShaderPermutationParameters&, ShaderCompilerEnvironment&)>;
    using ShouldCompilePermutationFuncType = std::function< bool(const ShaderPermutationParameters&)>;
    struct ShaderCompiledInitializer;
    using ConstructCompiledFuncType = std::function< Shader* (const ShaderCompiledInitializer&)>;
    struct ShaderCompiledInitializer
    {
        const ShaderType* Type;
        const std::vector<char>& Code;
        const ShaderParameterAllocationMap& ParameterMap;
        uint32_t PermutationId;
        ShaderCompiledInitializer(const ShaderType* type,
            const std::vector<char>& code,
            const ShaderParameterAllocationMap& parameterMap,
            uint32_t permutationId):Type(type) , Code(code), ParameterMap(parameterMap), PermutationId(permutationId) {}
    };

    // Shader type description and registration.
    struct RENDERCORE_API ShaderType
    {
public:
    enum class EShaderTypeFlag : uint8_t
    {
        Global = 0,
        Material = 1 << 0, // 是否为全局 Shader（不依赖 VertexFactory）
        MeshMaterial = 1 << 1, // 是否为 MeshMaterial Shader（依赖 VertexFactory）
        // Future flags can be added here (e.g., for editor-only shaders, mobile shaders, etc.)
    };

    std::string Name;             // Shader 名称，如 BasePassVS
    std::string SourceFile;       // USF 源文件路径
    std::string EntryPoint;       // 入口函数名
    RHI::ERHIShaderFrequency Frequency;   // VS / PS / CS

    ModifyCompilationEnvironmentFuncType ModifyCompilationEnvironment;
    ShouldCompilePermutationFuncType ShouldCompilePermutation;
    ConstructCompiledFuncType ConstructCompiled;
    int32_t TotalPermutationCount = 1;
    const ShaderParametersMetadata* RootParametersMetadata = nullptr;
    EShaderTypeFlag Flag = EShaderTypeFlag::Global;

    ShaderType(
        const std::string& inName,
        const std::string& inSourceFile,
        const std::string& inEntryPoint,
        RHI::ERHIShaderFrequency inFrequency,
        ModifyCompilationEnvironmentFuncType inModifyCompilationEnvironment,
        ShouldCompilePermutationFuncType inShouldCompilePermutation,
        ConstructCompiledFuncType inConstructCompiled,
        int32_t inTotalPermutationCount,
        const ShaderParametersMetadata* inRootParametersMetadata,
        EShaderTypeFlag inFlag
    )
        :
        Name(inName),
        SourceFile(inSourceFile),
        EntryPoint(inEntryPoint),
        Frequency(inFrequency),
        ModifyCompilationEnvironment(inModifyCompilationEnvironment),
        ShouldCompilePermutation(inShouldCompilePermutation),
        ConstructCompiled(inConstructCompiled),
        TotalPermutationCount(inTotalPermutationCount),
		RootParametersMetadata(inRootParametersMetadata),
        Flag(inFlag)
    {
        Register(this);
    }

    // 全局注册表相关静态方法
    // flag -> (name -> ShaderType*)
    static std::unordered_map<EShaderTypeFlag, std::unordered_map<std::string, ShaderType*>>& GetRegisterMap() {
        static std::unordered_map<EShaderTypeFlag, std::unordered_map<std::string, ShaderType*>> map;
        return map;
    }
    // 注册到指定 flag 分组
    static void Register(const std::string& name, ShaderType* type, EShaderTypeFlag flag) {
        type->Flag = flag;
        GetRegisterMap()[flag][name] = type;
    }
    // 按 flag+name 查找
    static ShaderType* Find(const std::string& name, EShaderTypeFlag flag) {
        auto& map = GetRegisterMap();
        auto fit = map.find(flag);
        if (fit != map.end()) {
            auto nit = fit->second.find(name);
            if (nit != fit->second.end())
                return nit->second;
        }
        return nullptr;
    }
    // 获取某 flag 下所有 ShaderType
    static std::vector<ShaderType*> GetAllOfFlag(EShaderTypeFlag flag) {
        std::vector<ShaderType*> result;
        auto& map = GetRegisterMap();
        auto fit = map.find(flag);
        if (fit != map.end()) {
            for (const auto& kv : fit->second) {
                result.push_back(kv.second);
            }
        }
        return result;
    }
    private:
        static void Register(ShaderType* type)
        {
            GetRegisterMap()[type->Flag][type->Name] = type;
        }
};


    class VertexFactoryType;

    // ShaderKey
    struct RENDERCORE_API ShaderKey
    {
        // Shader type used by this key.
        ShaderType* ShaderTypeP;

        // Target shader platform.
        RHI::ERHIShaderPlatform Platform;

        // Shader permutation identifier.
        ShaderPermutationId PermutationId;

        // Vertex factory type for mesh material shaders.
        VertexFactoryType* VFType = nullptr;

        // Construct a shader lookup key.
        ShaderKey(
            ShaderType* shaderType,
            RHI::ERHIShaderPlatform platform,
            ShaderPermutationId permId,
            VertexFactoryType* vfType = nullptr)
            : ShaderTypeP(shaderType)
            , Platform(platform)
            , PermutationId(permId)
            , VFType(vfType)
        {
        }

        // Compare complete keys for map and set lookup.
        bool operator==(const ShaderKey& other) const
        {
            return ShaderTypeP == other.ShaderTypeP &&
                Platform == other.Platform &&
                PermutationId == other.PermutationId &&
                VFType == other.VFType;
        }

        // Hash the key for unordered containers.
        size_t GetHash() const
        {
            size_t hash = std::hash<int>()(static_cast<int>(Platform));
            hash ^= std::hash<int>()(static_cast<int>(Platform)) << 1;
            hash ^= std::hash<ShaderPermutationId>()(PermutationId) << 2;
            hash ^= std::hash<VertexFactoryType*>()(VFType) << 3;
            return hash;
        }
    };


// -------------------------------------------------------------------------------------------------
//  Shader Base Class (for Render Passes) - Mimicking UE's Approach
// -------------------------------------------------------------------------------------------------
class RENDERCORE_API Shader
{
public:
    using PermutationDomain = ShaderPermutationDomain<>;
    // Construction/Destruction
    Shader(const ShaderCompiledInitializer& initializer);
    virtual ~Shader();

    // Accessors
    const std::string& GetName() const { return _name; }
    const std::vector<char>& GetShaderSourceCode() const { return _shaderSourceCode; }
    ERHIShaderFrequency GetShaderFrequency() const { return _shaderType; }

    template<typename T>
    const T* CastGetRHIShader() const
    {
        return static_cast<const T*>(GetRHIShader());
    }
    // RHI Resource
    RHIShader* GetRHIShader() const { return _rhiShader.get(); }
    void SetRHIShader(RHIShaderSP shader) { _rhiShader = shader; }

    // Compilation (Called by the RenderGraphBuilder)
    bool Compile();



    static void ModifyShaderCompilerEnvironment(const ShaderPermutationParameters& ,ShaderCompilerEnvironment& env) {}

    static bool ShouldCompilePermutation(const ShaderPermutationParameters& param) { return true; }

    const ShaderParameterBindingInfo& GetParameterBindings() const { return _bindings; }

protected:
    void InitShaderBindings(const ShaderParametersMetadata* metadata,
        const ShaderParameterAllocationMap& inParameterMap);
    void ProcessMetadataRecursive(
        const ShaderParametersMetadata& metadata,
        const std::string& prefix,
        const ShaderParameterAllocationMap& parameterMap,
        const uint64_t metadataOffset,
        ShaderParameterBindingInfo& outBindings);

    void InitShaderRHI(ERHIShaderFrequency frequency, const std::vector<char>& shaderSourceCode);
    // Shader Name (for debugging and identification)
    std::string _name;

    // Shader Source Code
    std::vector<char> _shaderSourceCode;

    // Shader Type (Vertex, Fragment, Compute, etc.)
    ERHIShaderFrequency _shaderType;

    // RHI Shader Resource
    RHIShaderSP _rhiShader;

    ShaderParameterBindingInfo _bindings;

public:

};


using ShaderSP = std::shared_ptr<Shader>;
} // namespace RenderCore


#define DECLARE_SHADER_TYPE(ShaderClass) \
public: \
    static ShaderMetaType StaticType; \
    static RenderCore::ShaderType* GetStaticType() \
    { \
        return &StaticType; \
    }


// 注册宏：支持传入 flag，自动注册到对应分组
#define IMPLEMENT_SHADER_TYPE( \
    ShaderClass, \
    ShaderName, \
    ShaderPath, \
    EntryPoint, \
    Frequency\
) \
    ShaderClass::ShaderMetaType ShaderClass::StaticType( \
        ShaderName, \
        ShaderPath, \
        EntryPoint, \
        Frequency, \
        &ShaderClass::ModifyShaderCompilerEnvironment, \
        &ShaderClass::ShouldCompilePermutation, \
        [](const RenderCore::ShaderCompiledInitializer& initializer){return new ShaderClass(static_cast<const ShaderClass::ShaderMetaType::ShaderCompiledInitializer&>(initializer));}, \
        ShaderClass::PermutationDomain::TotalCount, \
        ShaderClass::GetShaderParameterMetadata()\
    );

