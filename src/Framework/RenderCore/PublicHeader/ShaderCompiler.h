#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <set>
#include <map>
#include <mutex>
#include "Shader.h"
#include "RHIDefine.h"
#include "ShaderParameter.h"
#include "ShaderCore.h"
#include "HashHelper.hpp"

namespace spirv_cross {
    class Compiler;
}

namespace RenderCore{


    // ============================================================
    // Compile Flags
    // ============================================================

    enum class EShaderCompileFlags : uint32_t
    {
        None = 0,
        DebugInfo = 1 << 0,
        DisableOptimize = 1 << 1,
        WarningsAsError = 1 << 2,
    };

    inline EShaderCompileFlags operator|(EShaderCompileFlags a, EShaderCompileFlags b)
    {
        return static_cast<EShaderCompileFlags>(
            static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }


    // ============================================================
    // Shader Compile Input
    // ============================================================

    struct RENDERCORE_API ShaderCompileInput
    {
        // ԭʼ�ļ�·����������־/���ԣ�
        std::string VirtualSourceFilePath;

        std::string EntryPoint = "main";

        RHI::ERHIShaderFrequency Frequency;
        RHI::ERHIShaderPlatform Platform;

        // �����궨�� / include ����·�� / ���� include
        ShaderCompilerEnvironment Environment;

        std::string TargetProfile;     // vs_6_6 / ps_6_6 ��
        EShaderCompileFlags Flags = EShaderCompileFlags::DebugInfo | EShaderCompileFlags::DisableOptimize;

        // �Ƿ����� shader pipeline����ѡ��
        bool bCompilingForPipeline = false;

        bool operator==(const ShaderCompileInput& other) const
        {
            return VirtualSourceFilePath == other.VirtualSourceFilePath &&
                EntryPoint == other.EntryPoint &&
                Frequency == other.Frequency &&
                Platform == other.Platform &&
                Environment == other.Environment &&
                TargetProfile == other.TargetProfile &&
                Flags == other.Flags &&
                bCompilingForPipeline == other.bCompilingForPipeline;
        }
    };

    // ============================================================
    // Hash for ShaderCompileInput
    // ============================================================

} // namespace RenderCore

namespace std
{
    template<>
    struct hash<RenderCore::ShaderCompileInput>
    {
        size_t operator()(const RenderCore::ShaderCompileInput& input) const
        {
            using namespace RenderCore;
            size_t seed = 0;

            HashCombine(seed, std::hash<std::string>()(input.VirtualSourceFilePath));
            HashCombine(seed, std::hash<std::string>()(input.EntryPoint));
            HashCombine(seed, std::hash<int>()(static_cast<int>(input.Frequency)));
            HashCombine(seed, std::hash<int>()(static_cast<int>(input.Platform)));
            HashCombine(seed, std::hash<std::string>()(input.TargetProfile));
            HashCombine(seed, std::hash<uint32_t>()(static_cast<uint32_t>(input.Flags)));
            HashCombine(seed, std::hash<int>()(input.bCompilingForPipeline ? 1 : 0));

            for (const auto& m : input.Environment.Definitions)
            {
                HashCombine(seed, std::hash<std::string>()(m.first));
                HashCombine(seed, std::hash<std::string>()(m.second));
            }
            for (const auto& m : input.Environment.VirtualIncludes)
            {
                HashCombine(seed, std::hash<std::string>()(m.first));
                HashCombine(seed, std::hash<std::string>()(m.second));
            }
            for (const auto& path : input.Environment.IncludePaths)
            {
                HashCombine(seed, std::hash<std::string>()(path));
            }

            return seed;
        }
    };
}

namespace RenderCore
{

    // ============================================================
    // Shader Compilation Output
    // ============================================================

    struct RENDERCORE_API ShaderCompilationOutput
    {
        RHI::ERHIShaderPlatform Platform;

        bool Success = false;
        std::string ErrorMessage;

        std::vector<char> PackedBinaryData;
        ShaderParameterAllocationMap ParameterMap;

#if defined(SHADER_DEBUG)
        std::string PreprocessedSource;
#endif

        // �����ļ������������أ�
        std::vector<std::string> IncludedFiles;
    };

    // ============================================================
    // Shader Compiler
    // ============================================================

    class RENDERCORE_API ShaderCompiler
    {
    public:
            ShaderCompiler();
            ~ShaderCompiler();

            // ʹ�� ShaderCompileInput ������������
            static ShaderCompilationOutput Compile(const ShaderCompileInput& input);

            /**
             * 生成 ShaderParameter 的结构体定义字符串，带全局缓存。
             * 规则：
             * 1. 基本类型（如 float2）生成同名结构体，内部填入对应参数。
             * 2. 资源类型（如 Texture）全部扁平化到最外层结构体。
             * 3. 内嵌结构体：
             *    - 若有资源，资源扁平化到外层，且递归生成 #include 声明。
             *    - 若仅有基本类型，则直接作为类型名成员。
             * 4. 结果缓存到全局 map，key 为参数结构体名。
             */
            static std::string GenerateOrGetShaderPrameterMetaDataSF(const ShaderParametersMetadata& root);

            static std::optional<std::string> GetFileContent(const std::string& Path);
        private:
        static bool LoadShaderSource(
            const ShaderCompileInput& input,
            std::string& outSource);

        static bool PreprocessSource(
            const ShaderCompileInput& input,
            std::string& outSource,
            std::vector<std::string>& outIncludedFiles);

        static bool ExpandIncludes(
            const std::string& source,
            const ShaderCompilerEnvironment& env,
            std::string& outExpanded,
            std::vector<std::string>& outIncludedFiles,
            int depth = 0,
            std::set<std::string>* includeStack = nullptr,
            std::set<std::string>* includeFiles = nullptr);

        static void ApplyMacros(
            std::string& source,
            const std::map<std::string, std::string>& macros);

        static void CompileToSPIRV(
            const std::string& preprocessedSource,
            const ShaderCompileInput& input,
            ShaderCompilationOutput& out);

        static void CompileToDirectX(
            const std::string& preprocessedSource,
            const ShaderCompileInput& input,
            ShaderCompilationOutput& out);

        static void CompileToMetal(
            const std::string& preprocessedSource,
            const ShaderCompileInput& input,
            ShaderCompilationOutput& out);

        static void CompileToOpenGL(
            const std::string& preprocessedSource,
            const ShaderCompileInput& input,
            ShaderCompilationOutput& out);


    private:
        static void CompileHLSLToSPIRV(
            const std::string& preprocessedHLSLSource,
            const ShaderCompileInput& input,
            const std::vector<std::wstring>& compileParams,
            ShaderCompilationOutput& out,
            std::vector<uint32_t>& spirvOut);

        static void ReflectParameterMapFromSPIRV(
            spirv_cross::Compiler* compiler,
            ShaderParameterAllocationMap& out);

        static std::string ShaderSourceDirectory;
        int MaxIncludeDepth = 10;
    };


} // namespace RenderCore