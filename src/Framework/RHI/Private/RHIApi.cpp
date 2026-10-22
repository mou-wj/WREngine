#include "RHIApi.h"
#include "RHIDefine.h"

namespace RHI{
    RHIApi* GRHIApi = nullptr;
    ERHIShaderPlatform GShaderPlatform = ERHIShaderPlatform::Unknown;

    const std::unordered_map<ERHIFormat, FormatInfo> GFormatInfoMap = {
    { ERHIFormat::Unknown, {"Unknown", 0, 0, false, false, false, false} },
    { ERHIFormat::R8_UNorm, {"R8_UNorm", 1, 1, false, false, false, false} },
    { ERHIFormat::R8G8B8_UNorm, {"R8G8B8_UNorm", 3, 3, false, false, false, false} },
    { ERHIFormat::R8G8B8_SRGB, {"R8G8B8_SRGB", 3, 3, false, false, true, false} },
    { ERHIFormat::B8G8R8_UNorm, {"B8G8R8_UNorm", 3, 3, false, false, false, false} },
    { ERHIFormat::B8G8R8_SRGB, {"B8G8R8_SRGB", 3, 3, false, false, true, false} },
    { ERHIFormat::R8G8B8A8_UNorm, {"R8G8B8A8_UNorm", 4, 4, false, false, false, false} },
    { ERHIFormat::R8G8B8A8_SRGB, {"R8G8B8A8_SRGB", 4, 4, false, false, true, false} },
    { ERHIFormat::B8G8R8A8_UNorm, {"B8G8R8A8_UNorm", 4, 4, false, false, false, false} },
    { ERHIFormat::B8G8R8A8_SRGB, {"B8G8R8A8_SRGB", 4, 4, false, false, true, false} },
    { ERHIFormat::R16G16_Float, {"R16G16_Float", 4, 2, false, false, false, true } },
    { ERHIFormat::R16G16B16A16_Float, {"R16G16B16A16_Float", 8, 4, false, false, false, true } },
    { ERHIFormat::R32_Float, {"R32_Float", 4, 1, false, false, false, true } },
    { ERHIFormat::R32G32_Float, {"R32G32_Float", 8, 2, false, false, false, true } },
    { ERHIFormat::R32G32B32A32_Float, {"R32G32B32A32_Float",16,4, false, false, false, true } },
    { ERHIFormat::D24_UNorm_S8_UInt, {"D24_UNorm_S8_UInt", 4, 2, true, true, false, false} },
    { ERHIFormat::D32_Float, {"D32_Float", 4, 1, true, false, false, true } },
    // ...可根据需要继续添加
    };

}