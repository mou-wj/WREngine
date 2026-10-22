#pragma once
#include "RHIResource.h"
#include "VulkanDevice.h"
#include "ShaderCompiledDataPacker.h"
#include <vector>
#include <vulkan/vulkan.h>
#include <map>

namespace RHIVulkan {

// ----------------------------
// Vulkan 描述符布局结构体
// ----------------------------
struct VulkanDescriptorSetLayoutInfo
{
    std::vector<VkDescriptorSetLayoutBinding> Bindings;
    bool operator==(const VulkanDescriptorSetLayoutInfo& rhs) const
    {
        if (Bindings.size() != rhs.Bindings.size()) return false;
        for (size_t i = 0; i < Bindings.size(); ++i)
        {
            const auto& a = Bindings[i];
            const auto& b = rhs.Bindings[i];
            if (a.binding != b.binding ||
                a.descriptorType != b.descriptorType ||
                a.descriptorCount != b.descriptorCount ||
                a.stageFlags != b.stageFlags)
                return false;
        }
        return true;
    }
};

// ----------------------------
// VulkanRHIShader Base
// ----------------------------
class VulkanRHIShader
{
public:
    VulkanRHIShader(VulkanDevice* device);
    virtual ~VulkanRHIShader();

    

    bool Initialize(const std::vector<char>& packedCode);
    void Cleanup();
	RenderCore::SPIRVCompiledBinaryResultPacker::Header GetShaderReflection() const { return _reflection; }
	VkShaderModule  GetShaderModule() const { return _shaderModule; }
	const std::string& GetEntryPoint() const { return _entryPoint; }
    VkShaderStageFlagBits GetShaderStage() const { return _shaderStage; }
protected:
	RenderCore::SPIRVCompiledBinaryResultPacker::Header _reflection;
    VkShaderModule _shaderModule = VK_NULL_HANDLE;
    VulkanDevice* _device = nullptr;
    std::string _entryPoint;
    VkShaderStageFlagBits _shaderStage = VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM;
};

// 各具体Shader类型，继承对应RHIShader和VulkanRHIShader
class VulkanRHIVertexShader : public RHIVertexShader, public VulkanRHIShader
{
public:
    VulkanRHIVertexShader(VulkanDevice* device) : VulkanRHIShader(device) {}
    virtual ~VulkanRHIVertexShader() = default;
};

class VulkanRHIFragmentShader : public RHIFragmentShader, public VulkanRHIShader
{
public:
    VulkanRHIFragmentShader(VulkanDevice* device) : VulkanRHIShader(device) {}
    virtual ~VulkanRHIFragmentShader() = default;
};

class VulkanRHIGeometryShader : public RHIGeometryShader, public VulkanRHIShader
{
public:
    VulkanRHIGeometryShader(VulkanDevice* device) : VulkanRHIShader(device) {}
    virtual ~VulkanRHIGeometryShader() = default;
};

class VulkanRHIComputeShader : public RHIComputeShader, public VulkanRHIShader
{
public:
    VulkanRHIComputeShader(VulkanDevice* device) : VulkanRHIShader(device) {}
    virtual ~VulkanRHIComputeShader() = default;
};

class VulkanRHITessControlShader : public RHITessControlShader, public VulkanRHIShader
{
public:
    VulkanRHITessControlShader(VulkanDevice* device) : VulkanRHIShader(device) {}
    virtual ~VulkanRHITessControlShader() = default;
};

class VulkanRHITessEvalShader : public RHITessEvalShader, public VulkanRHIShader
{
public:
    VulkanRHITessEvalShader(VulkanDevice* device) : VulkanRHIShader(device) {}
    virtual ~VulkanRHITessEvalShader() = default;
};

class VulkanRHIMeshShader : public RHIMeshShader, public VulkanRHIShader
{
public:
    VulkanRHIMeshShader(VulkanDevice* device) : VulkanRHIShader(device) {}
    virtual ~VulkanRHIMeshShader() = default;
};

class VulkanRHITaskShader : public RHITaskShader, public VulkanRHIShader
{
public:
    VulkanRHITaskShader(VulkanDevice* device) : VulkanRHIShader(device) {}
    virtual ~VulkanRHITaskShader() = default;
};

// 光线追踪相关着色器
class VulkanRHIRayGenShader : public RHIRayGenShader, public VulkanRHIShader
{
public:
    VulkanRHIRayGenShader(VulkanDevice* device) : VulkanRHIShader(device) {}
    virtual ~VulkanRHIRayGenShader() = default;
};

class VulkanRHICloseHitShader : public RHICloseHitShader, public VulkanRHIShader
{
public:
    VulkanRHICloseHitShader(VulkanDevice* device) : VulkanRHIShader(device) {}
    virtual ~VulkanRHICloseHitShader() = default;
};

class VulkanRHIMissShader : public RHIMissShader, public VulkanRHIShader
{
public:
    VulkanRHIMissShader(VulkanDevice* device) : VulkanRHIShader(device) {}
    virtual ~VulkanRHIMissShader() = default;
};

class VulkanRHIAnyHitShader : public RHIAnyHitShader, public VulkanRHIShader
{
public:
    VulkanRHIAnyHitShader(VulkanDevice* device) : VulkanRHIShader(device) {}
    virtual ~VulkanRHIAnyHitShader() = default;
};

class VulkanRHIIntersectionShader : public RHIIntersectionShader, public VulkanRHIShader
{
public:
    VulkanRHIIntersectionShader(VulkanDevice* device) : VulkanRHIShader(device) {}
    virtual ~VulkanRHIIntersectionShader() = default;
};

class VulkanRHICallableShader : public RHICallableShader, public VulkanRHIShader
{
public:
    VulkanRHICallableShader(VulkanDevice* device) : VulkanRHIShader(device) {}
    virtual ~VulkanRHICallableShader() = default;
};


// hash函数，直接对 std::vector<char> 做 FNV 或 std::hash
inline size_t HashShaderCode(const std::vector<char>& code)
{
    // 简单 FNV-1a 64位
    const uint64_t fnvOffsetBasis = 14695981039346656037ULL;
    const uint64_t fnvPrime = 1099511628211ULL;

    uint64_t hash = fnvOffsetBasis;
    for (auto c : code)
    {
        hash ^= static_cast<uint8_t>(c);
        hash *= fnvPrime;
    }
    return static_cast<size_t>(hash);
}

class VulkanShaderManager
{
public:
    VulkanShaderManager(VulkanDevice* device) : m_Device(device) {}
    ~VulkanShaderManager() { Cleanup(); }

    // -----------------------
    // 通用模板创建接口
    // ShaderType = VulkanRHIVertexShader / VulkanRHIFragmentShader ...
    // -----------------------
    template<typename ShaderType>
    std::shared_ptr<ShaderType> GetOrCreateShader(
        const std::vector<char>& shaderCode)
    {
        const size_t hash = HashShaderCode(shaderCode);

        // 先查缓存
        {
            std::lock_guard<std::mutex> lg(m_ShaderMutex);
            auto it = m_ShaderCache.find(hash);
            if (it != m_ShaderCache.end())
            {
                return std::static_pointer_cast<ShaderType>(it->second);
            }
        }

        // 创建新的 shader
        auto shader = std::make_shared<ShaderType>(m_Device);



        if (!shader->Initialize(shaderCode))
        {
            return nullptr;
        }

        // 加入缓存
        {
            std::lock_guard<std::mutex> lg(m_ShaderMutex);
            m_ShaderCache[hash] = shader;
        }

        return shader;
    }

    void Cleanup()
    {
        std::lock_guard<std::mutex> lg(m_ShaderMutex);
        for (auto& kv : m_ShaderCache)
        {
            if (kv.second)
                kv.second->Cleanup();
        }
        m_ShaderCache.clear();
    }

private:
    VulkanDevice* m_Device = nullptr;

    std::unordered_map<size_t, std::shared_ptr<VulkanRHIShader>> m_ShaderCache;
    std::mutex m_ShaderMutex;
};



} // namespace WR::RHIVulkan
