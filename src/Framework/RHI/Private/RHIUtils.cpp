#include "RHIUtils.h"

namespace RHI {

    size_t GetFormatSize(ERHIFormat format) {
        switch (format)
        {
        case ERHIFormat::R8_UNorm:
            return 1;
        case ERHIFormat::R8G8B8A8_UNorm:
        case ERHIFormat::R8G8B8A8_SRGB:
        case ERHIFormat::B8G8R8A8_UNorm:
        case ERHIFormat::B8G8R8A8_SRGB:
            return 4;
        case ERHIFormat::R16G16_Float:
            return 4;
        case ERHIFormat::R16G16B16A16_Float:
            return 8;
        case ERHIFormat::R32_Float:
            return 4;
        case ERHIFormat::R32G32_UInt:
        case ERHIFormat::R32G32_Float:
            return 8;
        case ERHIFormat::R32G32B32A32_Float:
            return 16;
        case ERHIFormat::D24_UNorm_S8_UInt:
            return 4; // 通常为24位深度和8位模板
        case ERHIFormat::D32_Float:
            return 4; // 通常为32位深度
        default:
            return 0; // 未知格式
        }
        return 0; // 默认返回0 
    }

    EVerdorId GetVendorIdFromUint32(uint32_t vendorId)
    {
		return static_cast<EVerdorId>(vendorId);
    }

    EVerdorId GetPreferredVendorId()
    {
		//这里后面实现通过命令行参数或者配置文件来指定首选的VendorId
        return EVerdorId::NVIDIA;
    }



}