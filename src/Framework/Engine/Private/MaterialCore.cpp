#include "MaterialCore.h"
#include "VertexFactory.h"
#include "RHIApi.h"
#include "ShaderCompiler.h"
#include <stdexcept>
#include <algorithm>
using namespace RenderCore;
namespace Engine {

    void ModifyShaderCompilerEnvironment(
        const MaterialPermutationParameters& parameters,
        RenderCore::ShaderCompilerEnvironment& outEnvironment) {

        outEnvironment.SetDefine(
            "MATERIAL_SHADING_MODEL",
            static_cast<int32_t>(parameters.ShadingModel));
    }

    void MeshMaterialShaderMap::Initialize() {
        auto meshShaderTypes = ShaderType::GetRegisterMap()[ShaderType::EShaderTypeFlag::MeshMaterial];
        auto vfTypes = VertexFactoryType::GetRegisterMap();
        for(auto& meshShaderType : meshShaderTypes) {
            auto st = static_cast<MeshMaterialShaderType*>(meshShaderType.second);
            for (auto& vfType : vfTypes) {
                auto vft = vfType.second;
                for (int i = 0; i < static_cast<int>(EShadingModel::COUNT_MAX); i++) {
                    for (int j = 0; j < st->TotalPermutationCount; j++)
                    {
                        for (int k = 0; k < vft->PermutationTotalCount; k++) {
                            MaterialPermutationParameters mParameter;
                            mParameter.ShadingModel = static_cast<EShadingModel>(i);
                            MeshMaterialShaderKey key(st, vft, j, k , mParameter);
                            CompileShader(key);
                        }
                    }
                }
            }
        }
    }
    void MeshMaterialShaderMap::Clear() {
        m_ShaderMap.clear();
    }
    RenderCore::Shader* MeshMaterialShaderMap::GetShader(
        const MeshMaterialShaderKey& key
    ) {
        m_LastError.clear();
        auto it = m_ShaderMap.find(key);
        if (it != m_ShaderMap.end()) {
            return it->second.get();
        }
        return CompileShader(key);
    }

    RenderCore::Shader* MeshMaterialShaderMap::CompileShader(const MeshMaterialShaderKey& key)
    {
        auto* st =
            key.ShaderType;

        if (!st)
        {
            m_LastError = "Material shader type is null";
            return nullptr;
        }

        RHI::ERHIShaderPlatform platform =
            key.Platform;
        auto vfType = key.VF;


        // 2. 遍历该 ShaderType 的所有变体组合 (由 IMPLEMENT_SHADER_TYPE 宏自动提取)

        // 3. 检查当前变体是否需要编译
        MeshMaterialShaderPermutationParameters permParams;
        permParams.MaterialParams.ShadingModel = key.MaterialParameter.ShadingModel;
        permParams.Platform = static_cast<uint32_t>(platform);
        permParams.PermutationId = static_cast<uint32_t>(key.PermutationId);
        permParams.VFType = vfType;
        if (st->ShouldCompilePermutation && !st->ShouldCompilePermutation(permParams))
        {
            m_LastError = "Material shader permutation is unsupported";
            return nullptr;
        }

        // 4. 准备编译环境 (Environment)
        ShaderCompilerEnvironment env;
        env.IncludePaths.push_back(Core::GetShaderFilesRootDir());
        if (vfType) {
            VertexFactoryShaderPermutationParameters vfPermParams;
            vfPermParams.VertexFactoryFlags = key.VertexFactoryFlags;
            vfPermParams.Platform = platform;
            if (!vfType->ShouldCompile(vfPermParams))
            {
                m_LastError = "VertexFactory permutation is unsupported";
                return nullptr;
            }
            auto parameterInfos = ShaderCompiler::GenerateOrGetShaderPrameterMetaDataSF(*(vfType->RootParametersMetadata));
            vfType->ModifyCompilationEnvironment(vfPermParams, env);

        }

        // A. 处理 Shader 参数 (VFS 注册)
        if (st->RootParametersMetadata)
        {
            // 生成 HLSL 并注册到 GShaderVirtualFileSystem
            auto parameterInfos = ShaderCompiler::GenerateOrGetShaderPrameterMetaDataSF(*(st->RootParametersMetadata));
        }

        // B. 处理变体宏注入 (调用子类静态方法)
        if (st->ModifyCompilationEnvironment)
        {
            // 内部会调用 Domain.SetFromId(permId) 和 Domain.ModifyCompilationEnvironment(env)
            st->ModifyCompilationEnvironment(permParams, env);
        }



        // 5. 构造编译输入并调用编译器
        ShaderCompileInput input;
        input.VirtualSourceFilePath = st->SourceFile;
        input.EntryPoint = st->EntryPoint;
        input.Frequency = st->Frequency;
        input.Platform = platform;
        input.Environment = std::move(env); // 移动环境数据

        ShaderCompilationOutput output = ShaderCompiler::Compile(input);

        if (!output.bSuccess)
        {
            m_LastError = output.ErrorMessage;
            // 记录错误并跳过此变体
            // LOG_ERROR("Failed to compile %s (Permutation %d)", st->Name.c_str(), permId);
            return nullptr;
        }

        // 6. 构造 RHI Shader 对象
        MeshMaterialShaderType::ShaderCompiledInitializer initializer(st, output.PackedBinaryData, output.ParameterMap, key.PermutationId,key.VF);

        // 7. 实例化并缓存
        ShaderSP shaderInstance = std::shared_ptr<Shader>(st->ConstructCompiled(initializer));

        // ShaderMap 结构: std::unordered_map<ShaderType*, std::map<ShaderPermutationId, ShaderSP>>

        if (!shaderInstance)
        {
            m_LastError = "Material shader construction returned null";
            return nullptr;
        }
        m_ShaderMap[key] = shaderInstance;
        return shaderInstance.get();


    }

    void MaterialShaderMap::Initialize() {
        auto meshShaderTypes = ShaderType::GetRegisterMap()[ShaderType::EShaderTypeFlag::Material];
        auto vfTypes = VertexFactoryType::GetRegisterMap();
        for (auto& meshShaderType : meshShaderTypes) {
            auto st = static_cast<MaterialShaderType*>(meshShaderType.second);

            for (int i = 0; i < static_cast<int>(EShadingModel::COUNT_MAX); i++) {
                for (int j = 0; j < st->TotalPermutationCount; j++)
                {

                    MaterialPermutationParameters mParameter;
                    mParameter.ShadingModel = static_cast<EShadingModel>(i);
                    MaterialShaderKey key(st, j, mParameter);
                    CompileShader(key);

                }
            }
            
        }
    }
    void MaterialShaderMap::Clear() {
        m_ShaderMap.clear();
    }
    RenderCore::Shader* MaterialShaderMap::GetShader(
        const MaterialShaderKey& key
    ) {
        m_LastError.clear();
        auto it = m_ShaderMap.find(key);
        if (it != m_ShaderMap.end()) {
            return it->second.get();
        }
        return CompileShader(key);
    }

    RenderCore::Shader* MaterialShaderMap::CompileShader(const MaterialShaderKey& key)
    {
        auto* st =
            key.ShaderType;

        if (!st)
        {
            m_LastError = "Material shader type is null";
            return nullptr;
        }

        RHI::ERHIShaderPlatform platform =
            key.Platform;


        // 2. 遍历该 ShaderType 的所有变体组合 (由 IMPLEMENT_SHADER_TYPE 宏自动提取)

        // 3. 检查当前变体是否需要编译
        MaterialShaderPermutationParameters permParams;
        permParams.Platform = static_cast<uint32_t>(platform);
        permParams.PermutationId = static_cast<uint32_t>(key.PermutationId);
        permParams.MaterialParams = key.MaterialParameter;
        if (st->ShouldCompilePermutation && !st->ShouldCompilePermutation(permParams))
        {
            m_LastError = "Material shader permutation is unsupported";
            return nullptr;
        }

        // 4. 准备编译环境 (Environment)
        ShaderCompilerEnvironment env;
        env.IncludePaths.push_back(Core::GetShaderFilesRootDir());
        // A. 处理 Shader 参数 (VFS 注册)
        if (st->RootParametersMetadata)
        {
            // 生成 HLSL 并注册到 GShaderVirtualFileSystem
            auto parameterInfos = ShaderCompiler::GenerateOrGetShaderPrameterMetaDataSF(*(st->RootParametersMetadata));
        }

        // B. 处理变体宏注入 (调用子类静态方法)
        if (st->ModifyCompilationEnvironment)
        {
            // 内部会调用 Domain.SetFromId(permId) 和 Domain.ModifyCompilationEnvironment(env)
            st->ModifyCompilationEnvironment(permParams, env);
        }



        // 5. 构造编译输入并调用编译器
        ShaderCompileInput input;
        input.VirtualSourceFilePath = st->SourceFile;
        input.EntryPoint = st->EntryPoint;
        input.Frequency = st->Frequency;
        input.Platform = platform;
        input.Environment = std::move(env); // 移动环境数据

        ShaderCompilationOutput output = ShaderCompiler::Compile(input);

        if (!output.bSuccess)
        {
            m_LastError = output.ErrorMessage;
            // 记录错误并跳过此变体
            // LOG_ERROR("Failed to compile %s (Permutation %d)", st->Name.c_str(), permId);
            return nullptr;
        }

        // 6. 构造 RHI Shader 对象
        MaterialShaderType::ShaderCompiledInitializer initializer(st, output.PackedBinaryData, output.ParameterMap, key.PermutationId);

        // 7. 实例化并缓存
        ShaderSP shaderInstance = std::shared_ptr<Shader>(st->ConstructCompiled(initializer));

        // ShaderMap 结构: std::unordered_map<ShaderType*, std::map<ShaderPermutationId, ShaderSP>>

        if (!shaderInstance)
        {
            m_LastError = "Material shader construction returned null";
            return nullptr;
        }
        m_ShaderMap[key] = shaderInstance;
        return shaderInstance.get();
    }

    RenderCore::ShaderSP MeshMaterialShaderMap::GetShaderShared(const MeshMaterialShaderKey& key)
    {
        if (!GetShader(key)) return nullptr;
        return m_ShaderMap.at(key);
    }

    RenderCore::Shader* MaterialResource::GetMeshShader(MeshMaterialShaderKey key)
    {
        key.MaterialParameter.ShadingModel = m_ShadingModel;
        auto shader = GMeshMaterialShaderMap.GetShaderShared(key);
        if (!shader)
        {
            m_LastCompileState = EMaterialCompileState::Failed;
            m_LastError = GMeshMaterialShaderMap.GetLastError();
            if (m_LastError.empty()) m_LastError = "Material shader is unavailable";
            throw std::runtime_error(m_LastError);
        }
        m_LastCompileState = EMaterialCompileState::Ready;
        m_LastError.clear();
        auto* result = shader.get();
        if (std::find(m_ShaderReferences.begin(),m_ShaderReferences.end(),shader) == m_ShaderReferences.end())
            m_ShaderReferences.push_back(std::move(shader));
        return result;
    }

}