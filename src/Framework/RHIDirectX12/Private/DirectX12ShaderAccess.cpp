#include "DirectX12ShaderAccess.h"
#include "DirectX12RHIUtils.h"
#include <regex>
#include <sstream>
#include <unordered_set>

namespace RHIDirectX12
{
    std::string DisassembleDXIL(const std::vector<char>& code)
    {
        Microsoft::WRL::ComPtr<IDxcCompiler3> compiler;
        if (FAILED(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler))))
            return {};
        DxcBuffer buffer{code.data(), code.size(), 0};
        Microsoft::WRL::ComPtr<IDxcResult> result;
        if (FAILED(compiler->Disassemble(&buffer, IID_PPV_ARGS(&result))))
            return {};
        Microsoft::WRL::ComPtr<IDxcBlobUtf8> text;
        if (FAILED(result->GetOutput(DXC_OUT_DISASSEMBLY, IID_PPV_ARGS(&text), nullptr)) || !text)
            return {};
        return {text->GetStringPointer(), text->GetStringLength()};
    }

    bool IsReadOnlyBufferUAV(const std::string& assembly, const D3D12_SHADER_INPUT_BIND_DESC& binding)
    {
        if (assembly.empty() || (binding.Type != D3D_SIT_UAV_RWSTRUCTURED && binding.Type != D3D_SIT_UAV_RWBYTEADDRESS))
            return false;
        // DXC prints one instruction per line. Restrict certification to the known
        // DXIL handle/load forms; this is not a general LLVM optimizer or parser.
        // https://github.com/microsoft/DirectXShaderCompiler/blob/main/docs/DXIL.rst
        static const std::regex value(R"(%[A-Za-z0-9_.$-]+)");
        static const std::regex definition(R"(^\s*(%[A-Za-z0-9_.$-]+)\s*=)");
        static const std::regex legacy(R"(@dx\.op\.createHandle\(i32 57,\s*i8 1,\s*i32 ([0-9]+),)");
        static const std::regex fromBinding(
            R"(@dx\.op\.createHandleFromBinding\(i32 217,\s*%dx\.types\.ResBind\s*\{\s*i32 ([0-9]+),\s*i32 ([0-9]+),\s*i32 ([0-9]+),\s*i8 1\s*\})");
        static const std::regex annotate(R"(@dx\.op\.annotateHandle\(i32 216,)");
        static const std::regex read(
            R"(@dx\.op\.(?:bufferLoad\.[A-Za-z0-9]+\(i32 68|rawBufferLoad\.[A-Za-z0-9]+\(i32 139|getDimensions\(i32 72),)");
        struct Instruction
        {
            std::string Result;
            std::vector<std::string> Operands;
            bool bOrigin = false, bAnnotation = false, bRead = false;
        };
        bool found = false;
        std::vector<Instruction> instructions;
        auto checkFunction = [&]() {
            std::unordered_set<std::string> handles;
            for (const auto& instruction : instructions)
                if (instruction.bOrigin)
                    handles.insert(instruction.Result);
            if (handles.empty())
                return true;
            found = true;
            bool changed;
            do
            {
                changed = false;
                for (const auto& instruction : instructions)
                    if (instruction.bAnnotation && !instruction.Result.empty())
                        for (const auto& operand : instruction.Operands)
                            if (handles.count(operand))
                                changed |= handles.insert(instruction.Result).second;
            } while (changed);
            for (const auto& instruction : instructions)
                for (const auto& operand : instruction.Operands)
                    if (handles.count(operand) && !instruction.bAnnotation && !instruction.bRead)
                        return false; // Store, atomic, counter, phi/select, escape, or unknown use.
            return true;
        };
        std::istringstream source(assembly);
        std::string line;
        bool inFunction = false;
        while (std::getline(source, line))
        {
            line = line.substr(0, line.find(';'));
            if (line.rfind("define ", 0) == 0)
            {
                if (inFunction)
                    return false;
                inFunction = true;
                instructions.clear();
                continue;
            }
            if (!inFunction)
                continue;
            if (line.find_first_not_of(" \t\r") == line.find('}') && line.find('}') != std::string::npos)
            {
                if (!checkFunction())
                    return false;
                inFunction = false;
                continue;
            }
            Instruction instruction;
            std::smatch match;
            if (std::regex_search(line, match, definition))
            {
                instruction.Result = match[1];
                line = line.substr(static_cast<size_t>(match.position() + match.length()));
            }
            if (std::regex_search(line, match, legacy))
                instruction.bOrigin = std::stoull(match[1]) == binding.uID;
            else if (std::regex_search(line, match, fromBinding))
                instruction.bOrigin = std::stoull(match[1]) == binding.BindPoint &&
                                     std::stoull(match[2]) == UINT64(binding.BindPoint) + binding.BindCount - 1 &&
                                     std::stoull(match[3]) == binding.Space;
            if (instruction.Result.empty())
                instruction.bOrigin = false;
            instruction.bAnnotation = std::regex_search(line, annotate);
            instruction.bRead = std::regex_search(line, read);
            for (auto i = std::sregex_iterator(line.begin(), line.end(), value); i != std::sregex_iterator(); ++i)
                instruction.Operands.push_back(i->str());
            instructions.push_back(std::move(instruction));
        }
        return !inFunction && found;
    }
} // namespace RHIDirectX12
