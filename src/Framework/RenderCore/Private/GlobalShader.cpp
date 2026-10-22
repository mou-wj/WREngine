#include "GlobalShader.h"
#include "ShaderCompiler.h"
#include "RHIApi.h"
#include "PathInfo.h"
#include "Log.h"
#include <functional>
#include <sstream>

namespace RenderCore{

    // static members

    GlobalShaderMap GShaderMap;

    Shader* GlobalShaderMap::GetShader(ShaderType* shaderType, ShaderPermutationId id)
    {
        auto it = m_ShaderMap.find(shaderType);
        if (it != m_ShaderMap.end()) {
            auto shaderIt = it->second.find(id);
            if (shaderIt != it->second.end()) {
                return shaderIt->second.get();
            }
        }
        return nullptr;
    }

    // Helper: build a simple stable key string for a ShaderType + platform + perm
    static std::string BuildShaderUniqueString(const ShaderType* st, RHI::ERHIShaderPlatform platform, ShaderPermutationId perm)
    {
        std::ostringstream oss;
        oss << st->Name << "|" << st->SourceFile << "|" << st->EntryPoint << "|" << static_cast<int>(platform) << "|" << perm;
        return oss.str();
    }

    bool GlobalShaderMap::Initialize()
    {
        if (m_bIsInitialized) {
            return true;
        }

        // 获取当前运行平台
        RHI::ERHIShaderPlatform platform = GShaderPlatform;

        // 1. 获取所有注册为 Global 类型的 Shader
        const auto& globalTypes = ShaderType::GetRegisterMap()[ShaderType::EShaderTypeFlag::Global];

        for (const auto& kv : globalTypes)
        {
            ShaderType* st = kv.second;
            if (!st) continue;

            // 2. 遍历该 ShaderType 的所有变体组合 (由 IMPLEMENT_SHADER_TYPE 宏自动提取)
            for (int32_t permId = 0; permId < st->TotalPermutationCount; ++permId)
            {
                // 3. 检查当前变体是否需要编译
                ShaderPermutationParameters permParams;
                permParams.Platform = static_cast<uint32_t>(platform);
                permParams.PermutationId = static_cast<uint32_t>(permId);

                if (st->ShouldCompilePermutation && !st->ShouldCompilePermutation(permParams))
                {
                    continue;
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
                    st->ModifyCompilationEnvironment(permParams,env);
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
                    // 记录错误并跳过此变体
                    LOG_ERROR("Failed to compile %s (Permutation %d)", st->Name.c_str(), permId);
                    continue;
                }

                // 6. 构造 RHI Shader 对象
                ShaderCompiledInitializer initializer(st,output.PackedBinaryData,output.ParameterMap,permId);

                // 7. 实例化并缓存
                ShaderSP shaderInstance = std::shared_ptr<Shader>(st->ConstructCompiled(initializer));

                // ShaderMap 结构: std::unordered_map<ShaderType*, std::map<ShaderPermutationId, ShaderSP>>
                m_ShaderMap[st][permId] = shaderInstance;
            }
        }

        m_bIsInitialized = true;
        return true;
    }

    void GlobalShaderMap::Clear()
    {
        m_ShaderMap.clear();
    }

} // namespace RenderCore

