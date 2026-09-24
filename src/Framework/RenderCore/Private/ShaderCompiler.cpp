#include "ShaderCompiler.h"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <regex>
#include <algorithm>
#include <iostream>
#include <atomic>
#include <wrl/client.h>
#include <stack>
#include <string>

// Include platform-specific compilation libraries
// For DXC (DirectX Shader Compiler)
// #include <dxcapi.h>

// For glslang
#include "spirv_cross.hpp"
#include "spirv_hlsl.hpp"

#include "dxc/dxcapi.h"
#include "PathInfo.h"
#include "ShaderCompiledDataPacker.h"
#include "Log.h"

namespace RenderCore {

    namespace
    {
        std::string GetPathFileNameWithoutSuffix(const std::string& Path)
        {
            auto fileName = std::filesystem::path(Path).filename().string();
            auto pos = fileName.find_last_of('.');
			if (pos != std::string::npos)
			{
				return fileName.substr(0, pos);
			}
            return fileName;
        }
        std::wstring ToWide(const std::string& text)
        {
            return std::wstring(text.begin(), text.end());
        }

        std::string NormalizePath(const std::string& path)
        {
            std::string result = path;
            std::replace(result.begin(), result.end(), '\\', '/');
            while (result.starts_with("./"))
            {
                result.erase(0, 1);
            }
            return result;
        }

        std::string PathStem(const std::string& path)
        {
			auto pos = path.find_last_of("/");
            if (pos != std::string::npos) {
                return path.substr(0,pos);
            }
            return "";
        }

        bool ReadTextFile(const std::string& filePath, std::string& outText)
        {
            std::ifstream file(filePath, std::ios::in | std::ios::binary);
            if (!file.is_open())
            {
                return false;
            }

            std::ostringstream ss;
            ss << file.rdbuf();
            outText = ss.str();
            return true;
        }
    }

    class ShaderVirtualFileSystem {
    public:
        // 1. 注册虚拟文件（例如由 Generator 生成的参数代码）
        void RegisterVirtualFile(const std::string& VirtualPath, std::string Content) {
            VirtualFiles[VirtualPath] = std::move(Content);
        }

        // 2. 注册物理路径映射（将 /Engine/ 映射到磁盘实际路径）
        void RegisterMountPoint(const std::string& VirtualDir, const std::string& PhysicalDir) {
            MountPoints[VirtualDir] = PhysicalDir;
        }

        // 3. 核心接口：获取文件内容（供编译器后端调用）
        std::optional<std::string> GetFileContent(const std::string& Path) const {
            // A. 先检查是否是内存中的虚拟文件
            auto it = VirtualFiles.find(Path);
            if (it != VirtualFiles.end()) {
                return it->second;
            }

            // B. 检查物理挂载点
            for (const auto& [VirtualPrefix, PhysicalBase] : MountPoints) {
                if (Path.find(VirtualPrefix) == 0) {
                    std::string RelativePath = Path.substr(VirtualPrefix.length());
                    std::string FullPhysicalPath = PhysicalBase + "/" + RelativePath;
                    return ReadPhysicalFile(FullPhysicalPath);
                }
            }

            return std::nullopt; // 未找到
        }


    private:
        std::map<std::string, std::string> VirtualFiles;
        std::map<std::string, std::string> MountPoints;

        std::optional<std::string> ReadPhysicalFile(const std::string& FullPath) const {
            std::ifstream File(FullPath);
            if (!File.is_open()) return std::nullopt;
            return std::string((std::istreambuf_iterator<char>(File)), std::istreambuf_iterator<char>());
        }
    };
    ShaderVirtualFileSystem GShaderVirtualFileSystem;


    class ShaderParameterSFGenerator {
    public:
        // 统一定义虚拟路径的前缀
        static std::string GetVirtualPath(const ShaderParametersMetadata& root) {
            return std::string("/Generated/ShaderParameters/") + root.GetStructName() + ".sf";
        }

        // 用户调用的接口：只返回字符串
        std::string GenerateOrGetShaderParameterMetaDataSF(const ShaderParametersMetadata& root) {
            std::string VirtualPath = GetVirtualPath(root);

            auto CachedContent = GShaderVirtualFileSystem.GetFileContent(VirtualPath);
            if (CachedContent.has_value()) {
                return CachedContent.value();
            }

            std::stringstream Code;

            uint32_t TSlot = 0;
            uint32_t SSlot = 0;
            uint32_t USlot = 0;
            uint32_t BSlot = 0;

            Code << "// Generated for " << root.GetStructName() << "\n\n";

            EmitIncludes(
                root,
                Code);

            EmitNesteds(
                root,
                Code);


            // ================================
            // ⭐ 1. 生成 cbuffer（Uniform）
            // ================================
            //Code << "cbuffer " << root.GetStructName()
            //    << " : register(b" << BSlot++ << ")\n{\n";
            

            std::stringstream cbufferCode;
            EmitUniformMembers(root, "", cbufferCode);
            std::string cbufferMemberResult = cbufferCode.str();
            if (!cbufferMemberResult.empty()) {
                Code << "cbuffer " << root.GetStructName()
                    << "\n{\n";
                Code << cbufferMemberResult;
                Code << "};\n\n";
            }
            // ================================
            // ⭐ 2. 生成 Resource（SRV/UAV/Sampler）
            // ================================
            EmitResources(root, "", Code);

            std::string Result = Code.str();

            //LOG_INFO("%s", Result.c_str());
            GShaderVirtualFileSystem.RegisterVirtualFile(VirtualPath, Result);

            return Result;
        }


    private:
        void EmitIncludes(
            const ShaderParametersMetadata& Root,
            std::stringstream& OutCode)
        {
            std::unordered_set<std::string>
                IncludedFiles;
            for (auto& Member : Root.Members)
            {
                auto* StructMeta =
                    Member.StructMetadata;
                if (!StructMeta)
                    continue;
                if (Member.IsIncludeStruct()) {
                    EmitIncludes(*StructMeta, OutCode);
                }
                if (!Member.IsReferenceStruct())
                    continue;

                std::string Path =
                    GetVirtualPath(
                        *StructMeta);

                if (IncludedFiles.contains(
                    Path))
                {
                    return;
                }

                IncludedFiles.insert(
                    Path);

                GenerateOrGetShaderParameterMetaDataSF(
                    *StructMeta);

                //OutCode
                //    << "#include \""
                //    << Path
                //    << "\"\n";
            }
            //OutCode << "\n";
        }
        void EmitNesteds(
            const ShaderParametersMetadata& Root,
            std::stringstream& OutCode)
        {
            std::unordered_set<std::string>
                EmittedStructs;

            EmitNestedRecursive(
                Root,
                OutCode,
                EmittedStructs);

            OutCode << "\n";
        }

        void EmitNestedRecursive(
            const ShaderParametersMetadata& Root,
            std::stringstream& OutCode,
            std::unordered_set<std::string>&
            Emitted)
        {
            for (const auto& Member :
                Root.Members)
            {
                auto* StructMeta =
                    Member.StructMetadata;

                if (!StructMeta || Member.IsReferenceStruct())
                {
                    continue;
                }

                // include 只递归
                if (Member.IsIncludeStruct())
                {
                    EmitNestedRecursive(
                        *StructMeta,
                        OutCode,
                        Emitted);

                    continue;
                }

                // 这里只剩 nested
                if (!(Member.IsNestedStruct() || Member.BaseType == EShaderParameterBaseType::RDGBuffer_SRV
                    || Member.BaseType == EShaderParameterBaseType::RHI_SRV || Member.BaseType == EShaderParameterBaseType::RHI_UAV))
                    
                {
                    continue;
                }

                const std::string
                    StructName =
                    StructMeta
                    ->GetStructName();

                if (Emitted.contains(
                    StructName))
                {
                    continue;
                }

                // ==================
                // 1. 先展开依赖
                // ==================
                EmitNestedRecursive(
                    *StructMeta,
                    OutCode,
                    Emitted);

                // ==================
                // 2. 再输出自己
                // ==================
                Emitted.insert(
                    StructName);
                OutCode
                    << "struct "
                    << StructName
                    << "\n{\n";
                EmitUniformMembers(
                    *StructMeta,"",
                    OutCode);
                OutCode << "};\n\n";
            }
        }
       
        // ============================================
   // ⭐ Uniform flatten（核心）
   // ============================================
        void EmitUniformMembers(
            const ShaderParametersMetadata& Metadata,
            const std::string& Prefix,
            std::stringstream& OutCode)
        {
            for (const auto& Member : Metadata.GetMembers())
            {
                if (Member.IsResource())
                    continue;

                std::string Name = Member.Name;

                if (Member.IsIncludeStruct())
                {
                    // ⭐递归展开 struct
                    EmitUniformMembers(*Member.StructMetadata, Name, OutCode);
                }
                else if(Member.IsUniformDataMember())
                {
                    std::string TypeName = MapNumericType(Member);

                    std::string ArraySuffix;
                    if (Member.NumElements > 0)
                    {
                        ArraySuffix = "[" + std::to_string(Member.NumElements) + "]";
                    }

                    OutCode << "    " << TypeName << " " << Name << ArraySuffix << ";\n";
                }
            }
        }

        // ============================================
        // ⭐ Resource flatten（SRV/UAV/Sampler）
        // ============================================
        void EmitResources(
            const ShaderParametersMetadata& Metadata,
            const std::string& Prefix,
            std::stringstream& OutCode)
        {
            for (const auto& Member : Metadata.GetMembers())
            {
                std::string Name = Member.Name;

                if (Member.IsResource())
                {

                    std::string ArraySuffix;
                    if (Member.NumElements > 0)
                    {
                        ArraySuffix = "[" + std::to_string(Member.NumElements) + "]";
                    }
                    OutCode << MapResourceType(Member)
                        << " " << Name << ArraySuffix
                        << ";\n";
                }
                else if (Member.IsIncludeStruct())
                {
                    // ⭐递归展开资源
                    EmitResources(*Member.StructMetadata, Name, OutCode);
                }
            }
        }

        // ============================================
        // ⭐ 类型映射
        // ============================================
        std::string MapNumericType(const ShaderParametersMetadata::Member& member)
        {
            std::string base;

            switch (member.BaseType)
            {
            case EShaderParameterBaseType::Float32: base = "float"; break;
            case EShaderParameterBaseType::Int32:   base = "int";   break;
            case EShaderParameterBaseType::UInt32:  base = "uint";  break;
            case EShaderParameterBaseType::Bool:    base = "bool";  break;
            case EShaderParameterBaseType::StructNested: base = member.StructMetadata->GetStructName(); return base;
            default: return "float";
            }

            if (member.NumRows == 1 && member.NumColumns == 1)
                return base;

            if (member.NumRows == 1)
                return base + std::to_string(member.NumColumns);
            //如果是mat就设置为行主序
            return std::string("row_major ") + base + std::to_string(member.NumRows) + "x" + std::to_string(member.NumColumns);
        }

        std::string MapResourceType(const ShaderParametersMetadata::Member& Member)
        {
            return Member.TypeName;
        }

    };

    ShaderParameterSFGenerator GShaderParameterSFGenerator;
struct SPIRVPackSource {
    std::vector<uint32_t> *spirvCode;
	spirv_cross::Compiler *compiler;
    ERHIShaderFrequency frequency;
    std::string entryPoint;
};
struct GLSLPackSource
{
    std::string GLSLCode;
    spirv_cross::CompilerGLSL* compiler = nullptr;
    RHI::ERHIShaderFrequency frequency = RHI::ERHIShaderFrequency::Unknown;
    std::string entryPoint;
};
std::string ShaderCompiler::ShaderSourceDirectory = "";

ShaderCompiler::ShaderCompiler()
{

}

ShaderCompiler::~ShaderCompiler()
{

}


ShaderCompilationOutput ShaderCompiler::Compile(const ShaderCompileInput& input)
{
    ShaderCompilationOutput output;
    output.Platform = input.Platform;

    std::string source;
    std::vector<std::string> includedFiles;
    if (PreprocessSource(input, source, includedFiles)) {
        switch (input.Platform)
        {
        case ERHIShaderPlatform::Vulkan:
#if defined(SHADER_DEBUG)
            output.PreprocessedSource = source;
#endif
            CompileToSPIRV(source, input, output);
            break;
        case ERHIShaderPlatform::OpenGL:
#if defined(SHADER_DEBUG)
            output.PreprocessedSource = source;
#endif
            CompileToOpenGL(source, input, output);
            break;
        default:
#if defined(SHADER_DEBUG)
            output.PreprocessedSource = source;
#endif
            output.IncludedFiles = includedFiles;
            output.ErrorMessage = "Platform not implemented!";
            break;
        }
    }
    return output;
}


std::string ShaderCompiler::GenerateOrGetShaderPrameterMetaDataSF(const ShaderParametersMetadata& root)
{
    return GShaderParameterSFGenerator.GenerateOrGetShaderParameterMetaDataSF(root);
}


std::optional<std::string> ShaderCompiler::GetFileContent(const std::string& Path)
{
    return GShaderVirtualFileSystem.GetFileContent(Path);
}

bool ShaderCompiler::LoadShaderSource(const ShaderCompileInput& input, std::string& outSource)
{
    // �ȼ�� Environment.VirtualIncludes
    auto it = input.Environment.VirtualIncludes.find(input.VirtualSourceFilePath);
    if (it != input.Environment.VirtualIncludes.end())
    {
        outSource = it->second;
        return true;
    }

    // ���Դ� ShaderSourceDirectory + VirtualSourceFilePath ��ȡ
    std::string fullPath = Core::GetShaderFilesRootDir() + input.VirtualSourceFilePath;
    std::ifstream file(fullPath, std::ios::in | std::ios::binary);
    if (!file.is_open())
        return false;

    std::ostringstream ss;
    ss << file.rdbuf();
    outSource = ss.str();
    return true;
}

bool ShaderCompiler::PreprocessSource(const ShaderCompileInput& input, std::string& outSource, std::vector<std::string>& outIncludedFiles)
{
    std::string src;
    // 1. ��ȡ shader Դ
    if (!LoadShaderSource(input, src))
        return false;

    // 2. ʹ�� ExpandIncludes չ������ include
    std::set<std::string> includeStack; // ����ѭ�� include ���
    std::set<std::string> includeFiles;
    if (!ExpandIncludes(src, input.Environment, outSource, outIncludedFiles, 16, &includeStack,&includeFiles))
        return false;

    // 3. Ӧ�ú궨��
    ApplyMacros(outSource, input.Environment.Definitions);
    //LOG_INFO("Preprocess shader source: %s", outSource.c_str());
    return true;
}

bool ShaderCompiler::ExpandIncludes(const std::string& source, const ShaderCompilerEnvironment& env, std::string& outExpanded, std::vector<std::string>& outIncludedFiles, int depth, std::set<std::string>* includeStack, std::set<std::string>* includedFiles)
{
    // 1. 深度限制，防止恶意递归
    if (depth > 32) return false;

    bool bIsRoot = (includeStack == nullptr);
    std::set<std::string> localStack;
    std::set<std::string> localIncludedFiles;
    if (bIsRoot) {
        includeStack = &localStack;
        includedFiles = &localIncludedFiles;
    }

    std::istringstream stream(source);
    std::ostringstream result;
    std::string line;

    while (std::getline(stream, line))
    {
        // 匹配 #include "..." 或 #include <...>
        // 建议增加对宏定义 include 的支持处理（可选）
        size_t includePos = line.find("#include");
        if (includePos != std::string::npos)
        {
            size_t startQuote = line.find_first_of("\"<", includePos + 8);
            size_t endQuote = line.find_first_of("\">", startQuote + 1);

            if (startQuote != std::string::npos && endQuote != std::string::npos)
            {
                std::string includePath = line.substr(startQuote + 1, endQuote - startQuote - 1);

                // 防止循环引用
                if (includeStack->count(includePath)) continue;
                if (includedFiles->contains(includePath)) continue;
                std::string includedSource;
                bool bFound = false;

                // --- 优先级 A: 从环境私有虚拟内容中查找 ---
                auto it = env.VirtualIncludes.find(includePath);
                if (it != env.VirtualIncludes.end())
                {
                    includedSource = it->second;
                    bFound = true;
                }

                // --- 优先级 B: 从全局虚拟文件系统 (GShaderVirtualFileSystem) 查找 ---
                // 这一点很重要，因为自动生成的 .sf 文件注册在这里
                if (!bFound)
                {
                    auto vfsContent = GShaderVirtualFileSystem.GetFileContent(includePath);
                    if (vfsContent.has_value())
                    {
                        includedSource = vfsContent.value();
                        bFound = true;
                    }
                }

                // --- 优先级 C: 磁盘文件系统 ---
                if (!bFound)
                {
                    for (const auto& incDir : env.IncludePaths)
                    {
                        std::string fullPath = incDir + includePath;
                        std::ifstream file(fullPath, std::ios::in | std::ios::binary);
                        if (file.is_open())
                        {
                            std::ostringstream ss;
                            ss << file.rdbuf();
                            includedSource = ss.str();
                            bFound = true;
                            break;
                        }
                    }
                }


                if (!bFound)
                {
                    // 记录未找到的文件日志...
                    LOG_ERROR("Include file not found: %s", includePath.c_str());
                    return false;
                }

                // 递归展开
                includeStack->insert(includePath);
                includedFiles->insert(includePath);
                std::string expandedInclude;
                if (!ExpandIncludes(includedSource, env, expandedInclude, outIncludedFiles, depth + 1, includeStack, includedFiles))
                {
                    return false;
                }

                result << "// Start Include: " << includePath << "\n";
                result << expandedInclude << "\n";
                result << "// End Include: " << includePath << "\n";
                outIncludedFiles.push_back(includePath);
                includeStack->erase(includePath);
                continue;
            }
        }

        result << line << "\n";
    }

    outExpanded = result.str();
    return true;
}

void ShaderCompiler::ApplyMacros(std::string& source, const std::map<std::string, std::string>& macros)
{
    for (const auto& m : macros)
    {
        std::string define = "#define " + m.first + " " + m.second + "\n";
        source = define + source;
    }
}

void ShaderCompiler::CompileToSPIRV(const std::string& preprocessedSource, const ShaderCompileInput& input, ShaderCompilationOutput& out)
{
    out.Platform = ERHIShaderPlatform::Vulkan;
    out.Success = false;
    out.PackedBinaryData.clear();

    std::string setId = "0";
    switch (input.Frequency)
    {
    case ERHIShaderFrequency::Compute:         setId = "0";  break;
    case ERHIShaderFrequency::Vertex:          setId = "0"; break;
    case ERHIShaderFrequency::Fragment:        setId = "1"; break;
    case ERHIShaderFrequency::Geometry:        setId = "2"; break;
    case ERHIShaderFrequency::TessControl:     setId = "3"; break;
    case ERHIShaderFrequency::TessEvaluation:  setId = "4"; break;
    case ERHIShaderFrequency::Task:            setId = "0"; break;
    case ERHIShaderFrequency::Mesh:            setId = "1"; break;
    case ERHIShaderFrequency::RayGen:           setId = "0"; break;
    case ERHIShaderFrequency::ClosestHit:       setId = "1"; break;
    case ERHIShaderFrequency::Miss:             setId = "2"; break;
    case ERHIShaderFrequency::AnyHit:           setId = "1"; break;
    case ERHIShaderFrequency::Intersection:     setId = "3"; break;
    case ERHIShaderFrequency::Callable:         setId = "4"; break;
    default:
        out.ErrorMessage = "Unsupported shader stage";
        return;
    }

    auto HasFlag = [&](EShaderCompileFlags flag)
        {
            return (static_cast<uint32_t>(input.Flags) & static_cast<uint32_t>(flag)) != 0;
        };

    Microsoft::WRL::ComPtr<IDxcCompiler3> dxcCompiler;
    if (FAILED(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler))))
    {
        out.ErrorMessage = "Failed to initialize DXC compiler.";
        return;
    }

    std::vector<std::wstring> argStorage;
    argStorage.reserve(64);
    std::vector<LPCWSTR> dxcArgs;
    auto AddArg = [&](const std::wstring& arg)
        {
            argStorage.push_back(arg);
            dxcArgs.push_back(argStorage.back().c_str());
        };

    AddArg(L"-spirv");
    AddArg(L"-fspv-target-env=vulkan1.3");
    AddArg(L"-fvk-use-dx-layout");
    AddArg(L"-fvk-auto-shift-bindings");
    AddArg(L"-fvk-bind-globals"); AddArg(L"10"); AddArg(ToWide(setId));
    AddArg(L"-fvk-b-shift"); AddArg(L"100"); AddArg(ToWide(setId));
    AddArg(L"-fvk-t-shift"); AddArg(L"200"); AddArg(ToWide(setId));
    AddArg(L"-fvk-s-shift"); AddArg(L"300"); AddArg(ToWide(setId));
    AddArg(L"-fvk-u-shift"); AddArg(L"400"); AddArg(ToWide(setId));

    std::vector<uint32_t> spirv;
	CompileHLSLToSPIRV(preprocessedSource, input, argStorage, out, spirv);
    if (spirv.empty()) {
        return;
    }

    spirv_cross::Compiler compiler(spirv);
    // 6. ʹ�� SPIRV-Cross ������Դ
    ReflectParameterMapFromSPIRV(&compiler, out.ParameterMap);


    // 6. ������
    out.PackedBinaryData.resize(spirv.size() * sizeof(uint32_t));
    memcpy(out.PackedBinaryData.data(), spirv.data(), spirv.size() * sizeof(uint32_t));

    SPIRVPackSource packSource;
    packSource.compiler = &compiler;
    packSource.spirvCode = &spirv;
    packSource.frequency = input.Frequency;
    packSource.entryPoint = input.EntryPoint;

    SPIRVCompiledBinaryResultPacker packer;
    std::vector<char> packedData;

    if (packer.Pack(&packSource, packedData))
    {
        out.PackedBinaryData = std::move(packedData);
        out.Success = true;
    }
}

void ShaderCompiler::CompileToDirectX(const std::string& preprocessedSource, const ShaderCompileInput& input, ShaderCompilationOutput& out)
{

}


void ShaderCompiler::CompileToMetal(const std::string& preprocessedSource, const ShaderCompileInput& input, ShaderCompilationOutput& out)
{

}

void ShaderCompiler::CompileToOpenGL(const std::string& preprocessedSource, const ShaderCompileInput& input, ShaderCompilationOutput& out)
{
    out.Platform = ERHIShaderPlatform::OpenGL;
    out.Success = false;
    out.PackedBinaryData.clear();

    int setId = 0;
    int shaderStageMaxBinding = 500;
    switch (input.Frequency)
    {
    case ERHIShaderFrequency::Compute:         setId = 0;  break;
    case ERHIShaderFrequency::Vertex:          setId = 0; break;
    case ERHIShaderFrequency::Fragment:        setId = 1; break;
    case ERHIShaderFrequency::Geometry:        setId = 2; break;
    case ERHIShaderFrequency::TessControl:     setId = 3; break;
    case ERHIShaderFrequency::TessEvaluation:  setId = 4; break;
    case ERHIShaderFrequency::Task:            setId = 0; break;
    case ERHIShaderFrequency::Mesh:            setId = 1; break;
    case ERHIShaderFrequency::RayGen:           setId = 0; break;
    case ERHIShaderFrequency::ClosestHit:       setId = 1; break;
    case ERHIShaderFrequency::Miss:             setId = 2; break;
    case ERHIShaderFrequency::AnyHit:           setId = 1; break;
    case ERHIShaderFrequency::Intersection:     setId = 3; break;
    case ERHIShaderFrequency::Callable:         setId = 4; break;
    default:
        out.ErrorMessage = "Unsupported shader stage";
        return;
    }
    int shaderBindingOffset = setId * shaderStageMaxBinding;
    auto HasFlag = [&](EShaderCompileFlags flag)
        {
            return (static_cast<uint32_t>(input.Flags) & static_cast<uint32_t>(flag)) != 0;
        };

    Microsoft::WRL::ComPtr<IDxcCompiler3> dxcCompiler;
    if (FAILED(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler))))
    {
        out.ErrorMessage = "Failed to initialize DXC compiler.";
        return;
    }

    std::vector<std::wstring> argStorage;
    argStorage.reserve(64);
    std::vector<LPCWSTR> dxcArgs;
    auto AddArg = [&](const std::wstring& arg)
        {
            argStorage.push_back(arg);
            dxcArgs.push_back(argStorage.back().c_str());
        };

    AddArg(L"-spirv");
    AddArg(L"-fspv-target-env=vulkan1.3");
    AddArg(L"-fvk-use-dx-layout");
    AddArg(L"-fvk-auto-shift-bindings");
    AddArg(L"-fvk-bind-globals"); AddArg(ToWide(std::to_string(shaderBindingOffset + 10))); AddArg(ToWide("0"));
    AddArg(L"-fvk-b-shift"); AddArg(ToWide(std::to_string(shaderBindingOffset + 100))); AddArg(ToWide("0"));
    AddArg(L"-fvk-t-shift"); AddArg(ToWide(std::to_string(shaderBindingOffset + 200))); AddArg(ToWide("0"));
    AddArg(L"-fvk-s-shift"); AddArg(ToWide(std::to_string(shaderBindingOffset + 300))); AddArg(ToWide("0"));
    AddArg(L"-fvk-u-shift"); AddArg(ToWide(std::to_string(shaderBindingOffset + 400))); AddArg(ToWide("0"));

    std::vector<uint32_t> spirv;
    CompileHLSLToSPIRV(preprocessedSource, input, argStorage, out, spirv);
    if (spirv.empty()) {
        return;
    }
    spirv_cross::CompilerGLSL Compiler(spirv);
    // 6. ʹ�� SPIRV-Cross ������Դ
    ReflectParameterMapFromSPIRV(&Compiler, out.ParameterMap);

    
    spirv_cross::CompilerGLSL::Options Options;
    Options.version = 430;
    Options.es = false;

    Compiler.set_common_options(Options);
    Compiler.build_dummy_sampler_for_combined_images();
    Compiler.build_combined_image_samplers();
    std::string GLSL = Compiler.compile();
    

    // 6. ������
    out.PackedBinaryData.resize(GLSL.size() * sizeof(char));
    memcpy(out.PackedBinaryData.data(), GLSL.data(), GLSL.size() * sizeof(char));
    GLSLPackSource packSource;
    packSource.compiler = &Compiler;
    packSource.GLSLCode = GLSL;
    packSource.frequency = input.Frequency;
    packSource.entryPoint = input.EntryPoint;

    GLSLCompiledBinaryResultPacker packer;
    std::vector<char> packedData;

    if (packer.Pack(&packSource, packedData))
    {
        out.PackedBinaryData = std::move(packedData);
        out.Success = true;
    }
}


void ShaderCompiler::ReflectParameterMapFromSPIRV(spirv_cross::Compiler* compilerIn, ShaderParameterAllocationMap& out)
{
    // 6. ʹ�� SPIRV-Cross ������Դ
    spirv_cross::Compiler &compiler = *compilerIn;
    spirv_cross::ShaderResources resourcesSC = compiler.get_shader_resources();

    // ---------- Uniform Buffers ----------
    for (const auto& ub : resourcesSC.uniform_buffers)
    {
        std::string bufferName = compiler.get_name(ub.id);
        if (bufferName.empty()) {
            bufferName = compiler.get_name(ub.base_type_id); // fallback ($Globals)
        }

        uint32_t binding = compiler.get_decoration(ub.id, spv::DecorationBinding);
        uint32_t set = compiler.get_decoration(ub.id, spv::DecorationDescriptorSet);

        auto& type = compiler.get_type(ub.base_type_id);
        uint32_t bufferSize = static_cast<uint32_t>(compiler.get_declared_struct_size(type));

        bool bIsLooseDataBlock = (bufferName.find("$Global") != std::string::npos);

        // ============================================================
        // ⭐ 1. 记录整个 UniformBuffer（Descriptor 用）
        // ============================================================
        out.AddParameterAllocation(
            bufferName,
            static_cast<uint16_t>(set),       // UE语义：BufferIndex=Set
            static_cast<uint16_t>(binding),   // BaseIndex=Binding
            static_cast<uint16_t>(bufferSize),
            EShaderParameterType::UniformBuffer
        );

        // ============================================================
        // ⭐ 2. 展开所有成员（关键！！！）
        // ============================================================
        uint32_t memberCount = static_cast<uint32_t>(type.member_types.size());

        for (uint32_t i = 0; i < memberCount; i++)
        {
            std::string memberName = compiler.get_member_name(ub.base_type_id, i);

            uint32_t memberOffset = compiler.type_struct_member_offset(type, i);

            uint32_t memberSize = static_cast<uint32_t>(
                compiler.get_declared_struct_member_size(type, i));

            // ========================================================
            // ⭐ 构造“扁平路径名”（必须和你 HLSL 完全一致）
            // ========================================================
            std::string fullName;

            if (bIsLooseDataBlock)
            {
                // $Globals → 直接用成员名
                fullName = memberName;
            }
            else
            {
                // 普通UB → BufferName_Member
                fullName = memberName;
            }

            // ========================================================
            // ⭐ 写入 ParameterMap（成员级）
            // ========================================================
            out.AddParameterAllocation(
                fullName,
                static_cast<uint16_t>(binding),       // ⭐ 指向所属UB binding
                static_cast<uint16_t>(memberOffset),  // ⭐ offset（关键）
                static_cast<uint16_t>(memberSize),
                bIsLooseDataBlock
                ? EShaderParameterType::LooseData
                : EShaderParameterType::UniformBuffer
            );
        }
    }

    // ---------- Sampled Images (Texture + Sampler) ----------
    for (const auto& img : resourcesSC.sampled_images)
    {
        std::string name = compiler.get_name(img.id);

        uint32_t binding = compiler.get_decoration(img.id, spv::DecorationBinding);
        uint32_t set = compiler.get_decoration(img.id, spv::DecorationDescriptorSet);
        out.AddParameterAllocation(
            name,
            static_cast<uint32_t>(set),
            static_cast<uint32_t>(binding),
            1,
            EShaderParameterType::SRV
        );
    }

    // ---------- Separate Samplers ----------
    for (const auto& sb : resourcesSC.separate_samplers)
    {
        std::string name = compiler.get_name(sb.id);

        if (name.empty())
            continue;

        uint32_t binding = compiler.get_decoration(sb.id, spv::DecorationBinding);
        uint32_t set = compiler.get_decoration(sb.id, spv::DecorationDescriptorSet);
        auto& type = compiler.get_type(sb.type_id);

        uint32_t arraySize = 1;
        if (!type.array.empty())
            arraySize = type.array[0];

        out.AddParameterAllocation(
            name,
            static_cast<uint16_t>(set),
            static_cast<uint16_t>(binding),
            static_cast<uint16_t>(arraySize),   // ⭐数组支持
            EShaderParameterType::Sampler
        );
    }
    // ---------- Separate Images ----------
    for (const auto& img : resourcesSC.separate_images)
    {
        std::string name = compiler.get_name(img.id);
        uint32_t binding = compiler.get_decoration(img.id, spv::DecorationBinding);
        uint32_t set = compiler.get_decoration(img.id, spv::DecorationDescriptorSet);
        out.AddParameterAllocation(
            name, (uint32_t)set, (uint32_t)binding, 1, EShaderParameterType::SRV
        );
    }

    // ---------- Storage Images ----------
    for (const auto& img : resourcesSC.storage_images)
    {
        std::string name = compiler.get_name(img.id);

        uint32_t binding = compiler.get_decoration(img.id, spv::DecorationBinding);
        uint32_t set = compiler.get_decoration(img.id, spv::DecorationDescriptorSet);
        out.AddParameterAllocation(
            name,
            static_cast<uint32_t>(set),
            static_cast<uint32_t>(binding),
            1,
            EShaderParameterType::UAV
        );
    }

    // ---------- Storage Buffers ----------
    for (const auto& sb : resourcesSC.storage_buffers)
    {
        std::string name = compiler.get_name(sb.id);
        bool hasBinding = compiler.has_decoration(sb.id, spv::DecorationBinding);
        uint32_t binding = compiler.get_decoration(sb.id, spv::DecorationBinding);
        uint32_t set = compiler.get_decoration(sb.id, spv::DecorationDescriptorSet);
        auto& type = compiler.get_type(sb.base_type_id);
        uint32_t size = static_cast<uint32_t>(compiler.get_declared_struct_size(type));

        out.AddParameterAllocation(
            name,
            static_cast<uint32_t>(set),
            static_cast<uint32_t>(binding),
            1,
            EShaderParameterType::UAV
        );
    }

    // ���� Push Constants �ķ����߼�
    for (const auto& pc : resourcesSC.push_constant_buffers)
    {
        // ��ȡ��� PC ���������Ϣ
        auto& type = compiler.get_type(pc.base_type_id);

        // �����ڲ���Ա������Щ��������ѡ�е� Loose Data��
        for (uint32_t i = 0; i < type.member_types.size(); i++)
        {
            std::string name = compiler.get_member_name(pc.base_type_id, i);
            uint32_t offset = compiler.type_struct_member_offset(type, i);
            uint32_t size = static_cast<uint32_t>(compiler.get_declared_struct_member_size(type, i));

            // ������� ParameterMap
            out.AddParameterAllocation(
                name,
                0,      // BufferIndex �� PC ͨ��û���壬����Ϊ�ض���ʶ
                static_cast<uint16_t>(offset), // BaseIndex ���� PC �� Offset
                static_cast<uint16_t>(size),
                EShaderParameterType::LooseData // �������
            );
        }
    }

    // ---------- Accelerations Structures ----------
    for (const auto& sb : resourcesSC.acceleration_structures)
    {
        std::string name = compiler.get_name(sb.id);
        bool hasBinding = compiler.has_decoration(sb.id, spv::DecorationBinding);
        uint32_t binding = compiler.get_decoration(sb.id, spv::DecorationBinding);
        uint32_t set = compiler.get_decoration(sb.id, spv::DecorationDescriptorSet);
        auto& type = compiler.get_type(sb.base_type_id);


        out.AddParameterAllocation(
            name,
            static_cast<uint32_t>(set),
            static_cast<uint32_t>(binding),
            1,
            EShaderParameterType::AccelerationStructure
        );
    }
}

void ShaderCompiler::CompileHLSLToSPIRV(const std::string& preprocessedSource, const ShaderCompileInput& input,
    const std::vector<std::wstring>& compileParams, ShaderCompilationOutput& out, std::vector<uint32_t>& spirvOut)
{

    std::string outProfile = "";
    switch (input.Frequency)
    {
    case ERHIShaderFrequency::Compute:        outProfile = "cs_6_5";     break;
    case ERHIShaderFrequency::Vertex:         outProfile = "vs_6_5";   break;
    case ERHIShaderFrequency::Fragment:       outProfile = "ps_6_5";   break;
    case ERHIShaderFrequency::Geometry:       outProfile = "gs_6_5";   break;
    case ERHIShaderFrequency::TessControl:    outProfile = "hs_6_5";   break;
    case ERHIShaderFrequency::TessEvaluation: outProfile = "ds_6_5";   break;
    case ERHIShaderFrequency::Task:           outProfile = "as_6_5";   break;
    case ERHIShaderFrequency::Mesh:           outProfile = "ms_6_5";   break;
    case ERHIShaderFrequency::RayGen:         outProfile = "lib_6_5";   break;
    case ERHIShaderFrequency::ClosestHit:     outProfile = "lib_6_5";   break;
    case ERHIShaderFrequency::Miss:           outProfile = "lib_6_5";   break;
    case ERHIShaderFrequency::AnyHit:         outProfile = "lib_6_5";   break;
    case ERHIShaderFrequency::Intersection:   outProfile = "lib_6_5";   break;
    case ERHIShaderFrequency::Callable:       outProfile = "lib_6_5";   break;
    default:
        out.ErrorMessage = "Unsupported shader stage";
        return;
    }

    std::string targetProfile = outProfile;
    if (targetProfile.empty())
    {
        out.ErrorMessage = "Unsupported shader stage for DXC.";
        return;
    }

    auto HasFlag = [&](EShaderCompileFlags flag)
        {
            return (static_cast<uint32_t>(input.Flags) & static_cast<uint32_t>(flag)) != 0;
        };

    Microsoft::WRL::ComPtr<IDxcCompiler3> dxcCompiler;
    if (FAILED(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler))))
    {
        out.ErrorMessage = "Failed to initialize DXC compiler.";
        return;
    }

    std::vector<std::wstring> argStorage;
    argStorage.reserve(64);
    std::vector<LPCWSTR> dxcArgs;
    auto AddArg = [&](const std::wstring& arg)
        {
            argStorage.push_back(arg);
            dxcArgs.push_back(argStorage.back().c_str());
        };

    for (auto& arg : compileParams) {
		AddArg(arg);
    }

    AddArg(L"-E");
    AddArg(ToWide(input.EntryPoint));

    AddArg(L"-T");
    AddArg(ToWide(targetProfile));

    if (HasFlag(EShaderCompileFlags::DebugInfo))
    {
        AddArg(L"-Zi");
        AddArg(L"-Qembed_debug");
    }

    if (HasFlag(EShaderCompileFlags::DisableOptimize))
    {
        AddArg(L"-Od");
    }
    else
    {
        AddArg(L"-O3");
    }

    if (HasFlag(EShaderCompileFlags::WarningsAsError))
    {
        AddArg(L"-WX");
    }

    for (const auto& define : input.Environment.Definitions)
    {
        std::string macro = "-D" + define.first + "=" + define.second;
        AddArg(ToWide(macro));
    }
    DxcBuffer sourceBuffer = {};
    sourceBuffer.Ptr = preprocessedSource.data();
    sourceBuffer.Size = preprocessedSource.size();
    sourceBuffer.Encoding = DXC_CP_UTF8;

    Microsoft::WRL::ComPtr<IDxcResult> result;
    HRESULT compileHr = dxcCompiler->Compile(
        &sourceBuffer,
        dxcArgs.data(),
        static_cast<uint32_t>(dxcArgs.size()),
        nullptr,
        IID_PPV_ARGS(&result));

    if (FAILED(compileHr) || result == nullptr)
    {
        out.ErrorMessage = "DXC compile invocation failed.";
        return;
    }

    Microsoft::WRL::ComPtr<IDxcBlobUtf8> errorBlob;
    result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errorBlob), nullptr);

    HRESULT compileStatus = S_OK;
    result->GetStatus(&compileStatus);
    if (FAILED(compileStatus))
    {

        if (errorBlob && errorBlob->GetStringLength() > 0)
        {
            out.ErrorMessage = errorBlob->GetStringPointer();
        }
        else
        {
            out.ErrorMessage = "DXC failed with no detailed error output.";
        }
        //写一个写出到文档逻辑
        std::ofstream fout("error.txt");
        fout << preprocessedSource;
        fout.close();
        return;
    }



    Microsoft::WRL::ComPtr<IDxcBlob> spirvBlob;
    if (FAILED(result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&spirvBlob), nullptr)) || !spirvBlob)
    {
        out.ErrorMessage = "DXC did not return SPIR-V object blob.";
        return;
    }

    if ((spirvBlob->GetBufferSize() % sizeof(uint32_t)) != 0)
    {
        out.ErrorMessage = "DXC SPIR-V blob size is invalid.";
        return;
    }

    spirvOut.resize(spirvBlob->GetBufferSize() / sizeof(uint32_t));
    memcpy(spirvOut.data(), spirvBlob->GetBufferPointer(), spirvBlob->GetBufferSize());

    const uint32_t OpTypeImage = 25;
    auto StripImageFormat = [](std::vector<uint32_t>& spirv)
        {
            if (spirv.size() <= 5)
                return;

            //
            const uint32_t OpCapability = 17;

            // 在 header 后插入（第5个word后）
            size_t insertPos = 5;

            std::vector<uint32_t> capInst = {
                (2 << 16) | OpCapability,
                spv::CapabilityStorageImageWriteWithoutFormat // StorageImageWriteWithoutFormat enum
            };
            ;
            spirv.insert(spirv.begin() + insertPos, capInst.begin(), capInst.end());

            // SPIR-V binary header is 5 words long:
            // magic, version, generator, bound, schema
            for (size_t i = 5; i < spirv.size(); )
            {
                uint32_t opword = spirv[i];
                uint32_t opcode = opword & 0xFFFF;
                uint32_t wc = opword >> 16;

                // 防止无效 SPIR-V 数据导致越界或死循环
                if (wc == 0)
                    break;

                if (i + wc > spirv.size())
                    break;

                if (opcode == OpTypeImage && wc >= 9)
                {
                    if (spirv[i + 7] == 2) {
                        spirv[i + 8] = 0; // Unknown
                    }
                }

                i += wc;
            }
        };
    StripImageFormat(spirvOut);
    auto WriteSPIRVToFile = [](const std::string& filePath, const std::vector<uint32_t>& spirvData) {
        if (spirvData.empty()) {
            std::cerr << "Error: SPIR-V data is empty." << std::endl;
            return false;
        }

        // 1. 以二进制覆盖模式打开文件
        std::ofstream outFile(filePath, std::ios::out | std::ios::binary);

        if (!outFile.is_open()) {
            std::cerr << "Error: Failed to open file for writing: " << filePath << std::endl;
            return false;
        }

        // 2. 将 uint32_t 指针强转为 char*，并计算字节总数 (size * 4)
        outFile.write(reinterpret_cast<const char*>(spirvData.data()), spirvData.size() * sizeof(uint32_t));

        // 3. 检查写入状态并关闭
        outFile.close();

        if (outFile.good()) {
            std::cout << "Successfully wrote SPIR-V to: " << filePath << " (" << (spirvData.size() * 4) << " bytes)" << std::endl;
            return true;
        }
        else {
            std::cerr << "Error occurred during writing." << std::endl;
            return false;
        }
        };
    WriteSPIRVToFile("test.spv", spirvOut);

}


void SPIRVCompiledBinaryResultPacker::Depack(const std::vector<char>& packedResult)
{
    size_t offset = 0;

    auto read = [&](void* dst, size_t size)
        {
            memcpy(dst, packedResult.data() + offset, size);
            offset += size;
        };

    // =========================
    // SPIR-V code
    // =========================

    uint32_t codeSize = 0;
    read(&codeSize, sizeof(uint32_t));

    DepackedData.SpirvCode.resize(codeSize);

    read(DepackedData.SpirvCode.data(), codeSize * sizeof(uint32_t));

    // =========================
    // Header basic info
    // =========================

    read(&DepackedData.header.Frequency, sizeof(ERHIShaderFrequency));

    read(DepackedData.header.EntryPoint, sizeof(DepackedData.header.EntryPoint));

    read(&DepackedData.header.ShaderHash, sizeof(uint64_t));

    // =========================
    // Descriptor bindings
    // =========================

    uint32_t bindingCount = 0;
    read(&bindingCount, sizeof(uint32_t));

    DepackedData.header.DescriptorBindings.resize(bindingCount);

    for (uint32_t i = 0; i < bindingCount; ++i)
    {
        read(&DepackedData.header.DescriptorBindings[i], sizeof(DescriptorBindingInfo));
    }

    // =========================
    // Push constant
    // =========================

    read(&DepackedData.header.HasPushConstant, sizeof(bool));

    if (DepackedData.header.HasPushConstant)
    {
        read(&DepackedData.header.PushConstant, sizeof(PushConstantInfo));
    }
    uint32_t uniformBufferCount = 0;
    read(&uniformBufferCount, sizeof(uint32_t));
    DepackedData.header.UniformBufferBindings.resize(uniformBufferCount);
    for (uint32_t i = 0; i < uniformBufferCount; ++i)
    {
        read(&DepackedData.header.UniformBufferBindings[i], sizeof(UniformBufferBindingInfo));
    }
}
bool SPIRVCompiledBinaryResultPacker::Pack(void* packSource, std::vector<char>& packedResultOut)
{
    if (!packSource)
        return false;

    SPIRVPackSource* src = reinterpret_cast<SPIRVPackSource*>(packSource);

    DepackedData.SpirvCode = *(src->spirvCode);

    spirv_cross::Compiler* compiler = src->compiler;

    DepackedData.header.Frequency = src->frequency;

    strncpy(
        DepackedData.header.EntryPoint,
        src->entryPoint.c_str(),
        sizeof(DepackedData.header.EntryPoint));

    // �� hash
    DepackedData.header.ShaderHash =
        std::hash<std::string>()(
            std::string(
                reinterpret_cast<char*>(DepackedData.SpirvCode.data()),
                DepackedData.SpirvCode.size() * sizeof(uint32_t)));

    auto resources = compiler->get_shader_resources();

    DepackedData.header.DescriptorBindings.clear();


    auto addBinding =
        [&](const spirv_cross::Resource& res, ESPIRVShaderResourceType type)
        {
            DescriptorBindingInfo info{};

            info.Binding =
                (uint16_t)compiler->get_decoration(res.id, spv::DecorationBinding);
            if (info.Binding == 0)
            {
                auto name = compiler->get_name(res.id);
                //return;//binding为0表示没有使用，直接跳过
            }
            info.Set =
                (uint16_t)compiler->get_decoration(res.id, spv::DecorationDescriptorSet);

            auto& typeInfo = compiler->get_type(res.type_id);

            if (!typeInfo.array.empty())
                info.Count = (uint16_t)typeInfo.array[0];
            else
                info.Count = 1;

            info.Type = type;

            std::string name = compiler->get_name(res.id);

            if (!name.empty())
            {
                strncpy(info.Name, name.c_str(), sizeof(info.Name));
            }
            if (type != ESPIRVShaderResourceType::UniformBuffer) {
                DepackedData.header.DescriptorBindings.push_back(info);
            }
            else {
                UniformBufferBindingInfo uniBinding;
                uniBinding.Binding = info.Binding;
                uniBinding.Set = info.Set;
                if (name.empty()) {
                    name = compiler->get_name(res.base_type_id); // fallback ($Globals)
                }
                strncpy(uniBinding.Name, name.c_str(), sizeof(uniBinding.Name));
                const spirv_cross::SPIRType& type =
                    compiler->get_type(res.base_type_id);

                uniBinding.Size =
                    (uint32_t)compiler->get_declared_struct_size(type);
				DepackedData.header.UniformBufferBindings.push_back(uniBinding);
            }

        };

    // =========================
    // Uniform buffers
    // =========================

    for (auto& ub : resources.uniform_buffers)
    {
        addBinding(ub, ESPIRVShaderResourceType::UniformBuffer);
    }

    // =========================
    // Storage buffers
    // =========================

    for (auto& sb : resources.storage_buffers)
    {
        addBinding(sb, ESPIRVShaderResourceType::StorageBuffer);
    }

    // =========================
    // Acceleration structures
    // =========================

    for (auto& as : resources.acceleration_structures)
    {
        addBinding(as, ESPIRVShaderResourceType::AccelerationStructure);
    }

    // =========================
    // Sampled images
    // =========================

    for (auto& img : resources.sampled_images)
    {
        addBinding(img, ESPIRVShaderResourceType::SampledImage);
    }
    // =========================
    // Separated images
    // =========================
    for (auto& img : resources.separate_images)
    {
        addBinding(img, ESPIRVShaderResourceType::SampledImage);
    }

    // =========================
    // Storage images
    // =========================

    for (auto& img : resources.storage_images)
    {
        addBinding(img, ESPIRVShaderResourceType::StorageImage);
    }

    // =========================
	// Samplers
    // =========================
    for (auto& sampler : resources.separate_samplers)
	{
		addBinding(sampler, ESPIRVShaderResourceType::Sampler);
	}

    // =========================
    // Push constants
    // =========================

    DepackedData.header.HasPushConstant = false;

    if (!resources.push_constant_buffers.empty())
    {
        auto& pcb = resources.push_constant_buffers[0];

        auto& type = compiler->get_type(pcb.base_type_id);

        DepackedData.header.PushConstant.Size =
            (uint16_t)compiler->get_declared_struct_size(type);
        DepackedData.header.HasPushConstant = true;
    }

    // =========================
    // Descriptor sort���ǳ���Ҫ��
    // =========================

    std::sort(
        DepackedData.header.DescriptorBindings.begin(),
        DepackedData.header.DescriptorBindings.end(),
        [](const DescriptorBindingInfo& a, const DescriptorBindingInfo& b)
        {
            if (a.Set != b.Set)
                return a.Set < b.Set;

            return a.Binding < b.Binding;
        });

    // =========================
    // Serialize
    // =========================

    packedResultOut.clear();

    auto write = [&](const void* data, size_t size)
        {
            const char* c = reinterpret_cast<const char*>(data);
            packedResultOut.insert(packedResultOut.end(), c, c + size);
        };

    // SPIRV

    uint32_t codeSize = (uint32_t)DepackedData.SpirvCode.size();

    write(&codeSize, sizeof(uint32_t));

    write(
        DepackedData.SpirvCode.data(),
        codeSize * sizeof(uint32_t));

    // Header basic

    write(&DepackedData.header.Frequency, sizeof(ERHIShaderFrequency));

    write(
        DepackedData.header.EntryPoint,
        sizeof(DepackedData.header.EntryPoint));

    write(&DepackedData.header.ShaderHash, sizeof(uint64_t));

    // Descriptor bindings

    uint32_t bindingCount =
        (uint32_t)DepackedData.header.DescriptorBindings.size();

    write(&bindingCount, sizeof(uint32_t));

    for (auto& b : DepackedData.header.DescriptorBindings)
    {
        write(&b, sizeof(DescriptorBindingInfo));
    }

    // Push constant

    write(&DepackedData.header.HasPushConstant, sizeof(bool));

    if (DepackedData.header.HasPushConstant)
    {
        write(&DepackedData.header.PushConstant, sizeof(PushConstantInfo));
    }
    uint32_t uniformBufferCount =
        (uint32_t)DepackedData.header.UniformBufferBindings.size();
    write(&uniformBufferCount, sizeof(uint32_t));
    for (auto& b : DepackedData.header.UniformBufferBindings)
    {
        write(&b, sizeof(UniformBufferBindingInfo));
    }
    return true;
}

void GLSLCompiledBinaryResultPacker::Depack(const std::vector<char>& packedResult)
{
    size_t offset = 0;

    auto read = [&](void* dst, size_t size) -> bool
        {
            if (offset + size > packedResult.size()) return false;
            memcpy(dst, packedResult.data() + offset, size);
            offset += size;
            return true;
        };

    uint32_t glslSize = 0;
    if (!read(&glslSize, sizeof(uint32_t))) return;

    DepackedData.GLSLCode.resize(glslSize);
    if (glslSize > 0 && !read(DepackedData.GLSLCode.data(), glslSize))
    {
        DepackedData.GLSLCode.clear();
        return;
    }

    if (!read(&DepackedData.HeaderData.Frequency, sizeof(RHI::ERHIShaderFrequency))) return;
    if (!read(DepackedData.HeaderData.EntryPoint, sizeof(DepackedData.HeaderData.EntryPoint))) return;
    if (!read(&DepackedData.HeaderData.ShaderHash, sizeof(uint64_t))) return;

    uint32_t resourceCount = 0;
    if (!read(&resourceCount, sizeof(uint32_t))) return;

    DepackedData.HeaderData.Resources.resize(resourceCount);
    for (uint32_t i = 0; i < resourceCount; ++i)
    {
        if (!read(&DepackedData.HeaderData.Resources[i], sizeof(ResourceBindingInfo)))
        {
            DepackedData.HeaderData.Resources.clear();
            return;
        }
    }

    uint32_t uniformBufferCount = 0;
    if (!read(&uniformBufferCount, sizeof(uint32_t))) return;

    DepackedData.HeaderData.UniformBuffers.resize(uniformBufferCount);
    for (uint32_t i = 0; i < uniformBufferCount; ++i)
    {
        if (!read(&DepackedData.HeaderData.UniformBuffers[i], sizeof(UniformBufferBindingInfo)))
        {
            DepackedData.HeaderData.UniformBuffers.clear();
            return;
        }
    }
	uint32_t combinedBindingCount = 0;
    if (!read(&combinedBindingCount, sizeof(uint32_t))) return;
    DepackedData.HeaderData.CombinedBindings.resize(combinedBindingCount);
    for (uint32_t i = 0; i < combinedBindingCount; ++i)
	{
		if (!read(&DepackedData.HeaderData.CombinedBindings[i], sizeof(CombinedBindingInfo)))
		{
			DepackedData.HeaderData.CombinedBindings.clear();
			return;
		}
	}

    if (!read(&DepackedData.HeaderData.HasPushConstant, sizeof(bool))) return;

    if (DepackedData.HeaderData.HasPushConstant)
    {
        if (!read(&DepackedData.HeaderData.PushConstant, sizeof(PushConstantInfo)))
        {
            DepackedData.HeaderData.HasPushConstant = false;
            return;
        }
    }
}
bool GLSLCompiledBinaryResultPacker::Pack(void* packSource, std::vector<char>& packedResultOut)
{
    if (!packSource) return false;

    GLSLPackSource* src = reinterpret_cast<GLSLPackSource*>(packSource);
    if (!src->compiler) return false;

    DepackedData.HeaderData.Frequency = src->frequency;
    strncpy(DepackedData.HeaderData.EntryPoint, src->entryPoint.c_str(), sizeof(DepackedData.HeaderData.EntryPoint) - 1);
    DepackedData.HeaderData.EntryPoint[sizeof(DepackedData.HeaderData.EntryPoint) - 1] = '\0';
    DepackedData.GLSLCode = src->GLSLCode;
    DepackedData.HeaderData.ShaderHash = std::hash<std::string>()(DepackedData.GLSLCode);

    spirv_cross::Compiler* compiler = src->compiler;
    auto resources = compiler->get_shader_resources();

    DepackedData.HeaderData.Resources.clear();
    DepackedData.HeaderData.UniformBuffers.clear();
    DepackedData.HeaderData.HasPushConstant = false;

    auto addResource = [&](const spirv_cross::Resource& res, EGLSLShaderResourceType resourceType)
        {
            ResourceBindingInfo info{};
            if (compiler->has_decoration(res.id, spv::DecorationBinding))
                info.Binding = compiler->get_decoration(res.id, spv::DecorationBinding);
            else
                info.Binding = 0;

            const auto& typeInfo = compiler->get_type(res.type_id);
            info.Count = !typeInfo.array.empty() ? static_cast<uint32_t>(typeInfo.array[0]) : 1;
            info.Type = resourceType;

            std::string name = compiler->get_name(res.id);
            if (name.empty()) name = compiler->get_name(res.base_type_id);
            if (!name.empty()) strncpy_s(info.Name, name.c_str(), sizeof(info.Name) - 1);

            DepackedData.HeaderData.Resources.push_back(info);
        };

    for (const auto& ub : resources.uniform_buffers)
    {
        addResource(ub, EGLSLShaderResourceType::UniformBuffer);

        UniformBufferBindingInfo info{};
        if (compiler->has_decoration(ub.id, spv::DecorationBinding))
            info.Binding = compiler->get_decoration(ub.id, spv::DecorationBinding);

        std::string name = compiler->get_name(ub.id);
        if (name.empty()) name = compiler->get_name(ub.base_type_id);
        if (!name.empty()) strncpy_s(info.Name, name.c_str(), sizeof(info.Name) - 1);

        const auto& type = compiler->get_type(ub.base_type_id);
        info.Size = static_cast<uint32_t>(compiler->get_declared_struct_size(type));

        DepackedData.HeaderData.UniformBuffers.push_back(info);
    }

    for (const auto& sb : resources.storage_buffers)
        addResource(sb, EGLSLShaderResourceType::StorageBuffer);

    for (const auto& img : resources.sampled_images)
        addResource(img, EGLSLShaderResourceType::Texture);

    for (const auto& img : resources.separate_images)
        addResource(img, EGLSLShaderResourceType::Texture);

    for (const auto& sampler : resources.separate_samplers)
        addResource(sampler, EGLSLShaderResourceType::Sampler);

    for (const auto& img : resources.storage_images)
        addResource(img, EGLSLShaderResourceType::StorageImage);


    auto combined_image_samplers = compiler->get_combined_image_samplers();
    for (const auto& combined : combined_image_samplers) {
        CombinedBindingInfo info{};
        info.TextureBinding = compiler->get_decoration(combined.image_id, spv::DecorationBinding); 
        info.SamplerBinding = compiler->get_decoration(combined.sampler_id, spv::DecorationBinding);
        info.CombinedBinding = compiler->get_decoration(combined.combined_id, spv::DecorationBinding);
        DepackedData.HeaderData.CombinedBindings.push_back(info);
        
    }


    if (!resources.push_constant_buffers.empty())
    {
        auto& pcb = resources.push_constant_buffers[0];
        const auto& type = compiler->get_type(pcb.base_type_id);

        DepackedData.HeaderData.HasPushConstant = true;
        DepackedData.HeaderData.PushConstant.Size = static_cast<uint32_t>(compiler->get_declared_struct_size(type));
        DepackedData.HeaderData.PushConstant.Binding = 0;
        DepackedData.HeaderData.PushConstant.IsMappedToUniformBuffer = false;
    }

    std::sort(DepackedData.HeaderData.Resources.begin(), DepackedData.HeaderData.Resources.end(), [](const ResourceBindingInfo& a, const ResourceBindingInfo& b) { return a.Binding < b.Binding; });

    packedResultOut.clear();

    auto write = [&](const void* data, size_t size) { const char* c = reinterpret_cast<const char*>(data); packedResultOut.insert(packedResultOut.end(), c, c + size); };

    uint32_t glslSize = static_cast<uint32_t>(DepackedData.GLSLCode.size());
    write(&glslSize, sizeof(uint32_t));
    if (glslSize > 0) write(DepackedData.GLSLCode.data(), glslSize);

    write(&DepackedData.HeaderData.Frequency, sizeof(RHI::ERHIShaderFrequency));
    write(DepackedData.HeaderData.EntryPoint, sizeof(DepackedData.HeaderData.EntryPoint));
    write(&DepackedData.HeaderData.ShaderHash, sizeof(uint64_t));

    uint32_t resourceCount = static_cast<uint32_t>(DepackedData.HeaderData.Resources.size());
    write(&resourceCount, sizeof(uint32_t));
    for (const auto& resource : DepackedData.HeaderData.Resources)
        write(&resource, sizeof(ResourceBindingInfo));

    uint32_t uniformBufferCount = static_cast<uint32_t>(DepackedData.HeaderData.UniformBuffers.size());
    write(&uniformBufferCount, sizeof(uint32_t));
    for (const auto& buffer : DepackedData.HeaderData.UniformBuffers)
        write(&buffer, sizeof(UniformBufferBindingInfo));

    uint32_t combinedBindingCount = static_cast<uint32_t>(DepackedData.HeaderData.CombinedBindings.size());
	write(&combinedBindingCount, sizeof(uint32_t));
    for (const auto& binding : DepackedData.HeaderData.CombinedBindings)
		write(&binding, sizeof(CombinedBindingInfo));

    write(&DepackedData.HeaderData.HasPushConstant, sizeof(bool));
    if (DepackedData.HeaderData.HasPushConstant)
        write(&DepackedData.HeaderData.PushConstant, sizeof(PushConstantInfo));
    return true;
}

} // namespace RenderCore