#pragma once
#include "Shader.h"
#include "RHIApi.h"
#include "HashHelper.hpp"
#include "EngineExport.h"
#include <unordered_map>
namespace RenderCore {
    class VertexFactoryType;
}

namespace Engine {
    enum class EBlendMode : uint8_t {
        Opaque,
        Masked,
        Translucent
    };

    enum class EShadingModel : uint8_t {
        Unlit = 0,
        Lit,
        COUNT_MAX
    };

    struct MaterialPermutationParameters
    {
        EShadingModel ShadingModel = EShadingModel::Unlit;

        MaterialPermutationParameters() = default;
        MaterialPermutationParameters(EBlendMode blend, EShadingModel shading)
            : ShadingModel(shading) {}

        bool operator==(const MaterialPermutationParameters& other) const {
            return  ShadingModel == other.ShadingModel;
        }

        MaterialPermutationParameters& operator=(const MaterialPermutationParameters& other) {
            if (this != &other) {
                ShadingModel = other.ShadingModel;
            }
            return *this;
        }
    };

    ENGINE_API void ModifyShaderCompilerEnvironment(
        const MaterialPermutationParameters& parameters,
        RenderCore::ShaderCompilerEnvironment& outEnvironment);

    class MaterialShaderType;

    class MeshMaterialShaderType;

    struct MaterialShaderKey {
        RHI::ERHIShaderPlatform Platform = RHI::GShaderPlatform;
        MaterialShaderType* ShaderType;
        RenderCore::ShaderPermutationId PermutationId;
        MaterialPermutationParameters MaterialParameter;

        MaterialShaderKey(
            MaterialShaderType* shaderType,
            RenderCore::ShaderPermutationId permutationId,
            const MaterialPermutationParameters& matParam)
            : ShaderType(shaderType), PermutationId(permutationId), MaterialParameter(matParam) {}

        // Default constructor
        MaterialShaderKey(
            ) : ShaderType(nullptr), PermutationId(), MaterialParameter(MaterialPermutationParameters()) {}

        bool operator==(const MaterialShaderKey& other) const {
            return Platform == other.Platform && ShaderType == other.ShaderType &&
                PermutationId == other.PermutationId &&
                MaterialParameter == other.MaterialParameter;
        }

        MaterialShaderKey& operator=(const MaterialShaderKey& other) {
            if (this != &other) {
                Platform = other.Platform;
                ShaderType = other.ShaderType;
                PermutationId = other.PermutationId;
                // 注意：引用成员不能重新赋值，只能在构造时初始化
                MaterialParameter = other.MaterialParameter;
            }
            return *this;
        }
    };

    struct MeshMaterialShaderKey {
        RHI::ERHIShaderPlatform Platform = RHI::GShaderPlatform;
        MeshMaterialShaderType* ShaderType;
        RenderCore::VertexFactoryType* VF;
        RenderCore::ShaderPermutationId PermutationId;
        RenderCore::VertexFactoryFeatureFlags VertexFactoryFlags;
        MaterialPermutationParameters MaterialParameter;

        MeshMaterialShaderKey(
            MeshMaterialShaderType* shaderType,
            RenderCore::VertexFactoryType* vf,
            RenderCore::ShaderPermutationId permutationId,
            const RenderCore::VertexFactoryFeatureFlags& vfFlags,
            const MaterialPermutationParameters& matParam)
            : ShaderType(shaderType), VF(vf), PermutationId(permutationId), VertexFactoryFlags(vfFlags), MaterialParameter(matParam) {}

        // Default constructor
        MeshMaterialShaderKey()
            : ShaderType(nullptr), VF(nullptr), PermutationId(), VertexFactoryFlags(0), MaterialParameter(MaterialPermutationParameters()) {
        }

        bool operator==(const MeshMaterialShaderKey& other) const {
            return Platform == other.Platform && ShaderType == other.ShaderType &&
                VF == other.VF &&
                PermutationId == other.PermutationId &&
                VertexFactoryFlags == other.VertexFactoryFlags &&
                MaterialParameter == other.MaterialParameter;
        }

        MeshMaterialShaderKey& operator=(const MeshMaterialShaderKey& other) {
            if (this != &other) {
                Platform = other.Platform;
                ShaderType = other.ShaderType;
                VF = other.VF;
                PermutationId = other.PermutationId;
                // 注意：引用成员不能重新赋值，只能在构造时初始化
                VertexFactoryFlags = other.VertexFactoryFlags;
                MaterialParameter = other.MaterialParameter;
            }
            return *this;
        }
    };
}

namespace std {
    template<>
    struct std::hash<Engine::MaterialPermutationParameters> {
        size_t operator()(const Engine::MaterialPermutationParameters& key) const
        {
            size_t h = 0;
            HashCombine(h, std::hash<uint8_t>()(static_cast<uint8_t>(key.ShadingModel)));
            return h;
        }
    };

    template<>
    struct std::hash<Engine::MaterialShaderKey> {
        size_t operator()(const Engine::MaterialShaderKey& key) const
        {
            size_t h = 0;
            HashCombine(h, static_cast<uint32_t>(key.Platform));
            HashCombine(h, std::hash<void*>()(key.ShaderType));
            HashCombine(h, std::hash<RenderCore::ShaderPermutationId>()(key.PermutationId));
            HashCombine(h, std::hash<Engine::MaterialPermutationParameters>()(key.MaterialParameter));
            return h;
        }
    };
    template<>
    struct std::hash<Engine::MeshMaterialShaderKey> {
        size_t operator()(const Engine::MeshMaterialShaderKey& key) const
        {
            size_t h = 0;
            HashCombine(h, static_cast<uint32_t>(key.Platform));
            HashCombine(h, std::hash<void*>()(key.ShaderType));
            HashCombine(h, std::hash<void*>()(key.VF));
            HashCombine(h, std::hash<RenderCore::ShaderPermutationId>()(key.PermutationId));
            HashCombine(h, std::hash<RenderCore::VertexFactoryFeatureFlags>()(key.VertexFactoryFlags));
            HashCombine(h, std::hash<Engine::MaterialPermutationParameters>()(key.MaterialParameter));
            return h;
        }
    };
}

namespace Engine {


    struct MeshMaterialShaderPermutationParameters : public RenderCore::ShaderPermutationParameters
    {
        MaterialPermutationParameters MaterialParams;
        const RenderCore::VertexFactoryType* VFType;
    };


    /*
    ===============================================================================

        MaterialShaderType

        ShaderType specialization for material-based shaders.

    ===============================================================================
    */
    class ENGINE_API MeshMaterialShaderType : public RenderCore::ShaderType
    {
    public:
        struct ShaderCompiledInitializer : public RenderCore::ShaderCompiledInitializer {
            RenderCore::VertexFactoryType* FactoryType;
			ShaderCompiledInitializer(
				const RenderCore::ShaderType* type,
				const std::vector<char>& code,
				const RenderCore::ShaderParameterAllocationMap& parameterMap,
				uint32_t permutationId,
				RenderCore::VertexFactoryType* inFactoryType)
				: RenderCore::ShaderCompiledInitializer(type, code, parameterMap, permutationId),
				FactoryType(inFactoryType) {
			}
        };
        MeshMaterialShaderType(
            const std::string& inName,
            const std::string& inSourceFile,
            const std::string& inEntryPoint,
            RHI::ERHIShaderFrequency inFrequency,
            RenderCore::ModifyCompilationEnvironmentFuncType inModifyCompilationEnvironment,
            RenderCore::ShouldCompilePermutationFuncType inShouldCompilePermutation,
            RenderCore::ConstructCompiledFuncType inConstructCompiled,
            int32_t inTotalPermutationCount = 1,
            const RenderCore::ShaderParametersMetadata* inRootParametersMetadata = nullptr
        )
            : ShaderType(
                inName,
                inSourceFile,
                inEntryPoint,
                inFrequency,
                inModifyCompilationEnvironment,
                inShouldCompilePermutation,
                inConstructCompiled,
                inTotalPermutationCount,
                inRootParametersMetadata,
                EShaderTypeFlag::MeshMaterial
            )
        {
            
        }

        virtual ~MeshMaterialShaderType() = default;

    public:

    };



    /*
    ===============================================================================

        MaterialShaderMap

        Stores compiled shader permutations for a specific material.

    ===============================================================================
    */
    class ENGINE_API MeshMaterialShaderMap
    {
    public:
        using ShaderMetaType = MeshMaterialShaderType;
        void Initialize();
        void Clear();
    public:

        RenderCore::Shader* GetShader(const MeshMaterialShaderKey& key);
        RenderCore::ShaderSP GetShaderShared(const MeshMaterialShaderKey& key);
        const std::string& GetLastError() const { return m_LastError; }

    private:
        RenderCore::Shader* CompileShader(const MeshMaterialShaderKey& key);


        std::unordered_map<
            MeshMaterialShaderKey,
            RenderCore::ShaderSP
        > m_ShaderMap;
        std::string m_LastError;

    private:

    };

    extern ENGINE_API MeshMaterialShaderMap GMeshMaterialShaderMap;

#define DECLARE_MESH_MATERIAL_SHADER_TYPE(ClassType) \
    DECLARE_SHADER_TYPE(ClassType)\
    ClassType(const ShaderMetaType::ShaderCompiledInitializer& Initializer) : MeshMaterialShader(Initializer) {}\

#define IMPLEMENT_MESH_MATERIAL_SHADER_TYPE(ClassType,ShaderPath,ShaderName,EntryPoint,Frequency ) \
    IMPLEMENT_SHADER_TYPE(ClassType,ShaderPath,ShaderName,EntryPoint,Frequency)


    class Material;
    class MaterialShaderType;
    class RenderCore::VertexFactoryType;

    /*
   ===============================================================================

       MaterialShaderMap

       Stores compiled shader permutations for a specific material.

   ===============================================================================
   */
    class ENGINE_API MaterialShaderMap
    {
    public:
        using ShaderMetaType = MaterialShaderType;
        void Initialize();
        void Clear();
    public:

        RenderCore::Shader* GetShader(const MaterialShaderKey& key);
        const std::string& GetLastError() const { return m_LastError; }


    private:
        RenderCore::Shader* CompileShader(const MaterialShaderKey& key);


        std::unordered_map<
            MaterialShaderKey,
            RenderCore::ShaderSP
        > m_ShaderMap;
        std::string m_LastError;

    private:

    };

    struct MaterialShaderPermutationParameters : public RenderCore::ShaderPermutationParameters
    {
        MaterialPermutationParameters MaterialParams;
    };

    class ENGINE_API MaterialShaderType : public RenderCore::ShaderType
    {
    public:

        using ShaderCompiledInitializer = RenderCore::ShaderCompiledInitializer;
        using Super = RenderCore::ShaderType;

        MaterialShaderType(
            const std::string& inName,
            const std::string& inShaderPath,
            const std::string& inEntryPoint,
            ERHIShaderFrequency inFrequency,
            RenderCore::ModifyCompilationEnvironmentFuncType inModifyCompilationEnvironment,
            RenderCore::ShouldCompilePermutationFuncType inShouldCompilePermutation,
            RenderCore::ConstructCompiledFuncType inConstructInstance,
            int32_t inTotalPermutationCount = 1,
            const RenderCore::ShaderParametersMetadata* inRootParametersMetadata = nullptr
        )
            : ShaderType(
                inName,
                inShaderPath,
                inEntryPoint,
                inFrequency,
                inModifyCompilationEnvironment,
                inShouldCompilePermutation,
                inConstructInstance,
                inTotalPermutationCount,
                inRootParametersMetadata,
                EShaderTypeFlag::Material
            )
        {
        }

        virtual ~MaterialShaderType() = default;

    public:

    };

    extern ENGINE_API MaterialShaderMap GMaterialShaderMap;

    enum class EMaterialCompileState { Unrequested, Ready, Failed };

    // Built-in Surface program resource. Dynamic parameter values are not compilation inputs.
    // ShaderType, permutation and VertexFactory remain selected by Renderer passes.
    class ENGINE_API MaterialResource
    {
    public:
        explicit MaterialResource(EShadingModel shading) : m_ShadingModel(shading) {}
        EShadingModel GetShadingModel() const { return m_ShadingModel; }
        const char* GetProgramName() const { return "BuiltinSurface"; }
        RenderCore::Shader* GetMeshShader(MeshMaterialShaderKey key);
        EMaterialCompileState GetLastCompileState() const { return m_LastCompileState; }
        const std::string& GetLastError() const { return m_LastError; }
    private:
        EShadingModel m_ShadingModel;
        EMaterialCompileState m_LastCompileState = EMaterialCompileState::Unrequested;
        std::string m_LastError;
        // Preserve requested shader lifetimes; lookup/caching belongs to the global ShaderMap.
        std::vector<RenderCore::ShaderSP> m_ShaderReferences;
    };

#define DECLARE_MATERIAL_SHADER_TYPE(ClassType) \
    DECLARE_SHADER_TYPE(ClassType)\
    ClassType(const ShaderMetaType::ShaderCompiledInitializer& Initializer) : MaterialShader(Initializer) {}
#define IMPLEMENT_MATERIAL_SHADER_TYPE(ClassType,ShaderPath,ShaderName,EntryPoint,Frequency ) \
    IMPLEMENT_SHADER_TYPE(ClassType,ShaderPath,ShaderName,EntryPoint,Frequency)

}