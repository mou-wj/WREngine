#pragma once
#include <d3d12shader.h>
#include <string>
#include <vector>

namespace RHIDirectX12
{
    std::string DisassembleDXIL(const std::vector<char>& code);
    // A UAV register is a native binding category, not proof that this shader writes.
    // Unknown handle use is conservatively treated as potentially writable.
    bool IsReadOnlyBufferUAV(const std::string& assembly, const D3D12_SHADER_INPUT_BIND_DESC& binding);
} // namespace RHIDirectX12
