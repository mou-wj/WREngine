#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>
#include "RHIResource.h"
#include "RHIApi.h"

namespace RHI {

    using ShaderKeyHash = size_t;

struct RHI_API ShaderBinary {
    std::string Name;
    ERHIShaderFrequency Type;
    std::string EntryPoint;
    std::vector<uint8_t> BinaryCode;
    std::string SourceCode;
};
class RHI_API RHIShaderLibrary {
public:
    RHIShaderLibrary(const std::string& name, ERHIShaderPlatform platform);
    virtual ~RHIShaderLibrary() = default;

    virtual RHIShaderSP CreateShader(ShaderKeyHash key) = 0;

    ERHIShaderPlatform GetPlatform() const { return _platform; }

    const std::string& GetName() const { return _name; }
protected:
    std::string _name;
    ERHIShaderPlatform _platform;
    mutable std::mutex _shaderCacheMutex;
    std::unordered_map<ShaderKeyHash, std::shared_ptr<RHIShader>> _shaderCache;

};

} // namespace RHI