#pragma once
#include <map>
#include <string>
#include <vector>
#include "RHIDefine.h"

namespace RenderCore {


    // ============================================================
    // Shader Compiler Environment
    // ============================================================

    struct ShaderCompilerEnvironment
    {

        std::map<std::string, std::string> Definitions;

        std::map<std::string, std::string> VirtualIncludes;

        std::vector<std::string> IncludePaths;


        bool bFullPrecisionInPS = false;

        void Merge(const ShaderCompilerEnvironment& other)
        {
            Definitions.insert(other.Definitions.begin(), other.Definitions.end());
            for (const auto& it : other.VirtualIncludes)
            {
                auto existing = VirtualIncludes.find(it.first);
                if (existing != VirtualIncludes.end())
                    existing->second.append(it.second);
                else
                    VirtualIncludes[it.first] = it.second;
            }
            IncludePaths.insert(IncludePaths.end(), other.IncludePaths.begin(), other.IncludePaths.end());
            bFullPrecisionInPS |= other.bFullPrecisionInPS;
        }
        bool operator==(const ShaderCompilerEnvironment& other) const
        {
            return Definitions == other.Definitions &&
                VirtualIncludes == other.VirtualIncludes &&
                IncludePaths == other.IncludePaths &&
                bFullPrecisionInPS == other.bFullPrecisionInPS;
        }
        void SetDefine(const std::string& name, const std::string& value) { Definitions[name] = value; }
        void SetDefine(const std::string& name, int32_t value) { Definitions[name] = std::to_string(value); }
        void SetDefine(const std::string& name, uint64_t value) { Definitions[name] = std::to_string(value); }
        void SetDefine(const std::string& name, bool value) { Definitions[name] = value ? "1" : "0"; }
        void SetDefine(const std::string& name, float value) { Definitions[name] = std::to_string(value); }
    };

    // 所有 Shader 通用的最小参数
    struct ShaderPermutationParameters
    {
        uint32_t Platform;        // Vulkan / DX12 / Metal ...
        uint32_t PermutationId;   // Bitmask（功能组合）
        size_t GetHash() const {
            size_t hash = std::hash<uint32_t>()(Platform);
            hash ^= std::hash<uint32_t>()(PermutationId) << 1;
            return hash;
        }
    };

    using ShaderPermutationId = size_t;


    // 基础布尔维度
    template<const char* MacroName>
    struct FPermutationDimensionBool
    {
        static constexpr const char* Name = MacroName;
        static constexpr uint32_t Count = 2; // 固定为 0 和 1
        uint32_t Value = 0; // 内部统一用 uint32 存储
    };

    // 基础枚举/整数维度
    template<const char* MacroName, uint32_t InCount>
    struct FPermutationDimensionEnum
    {
        static constexpr const char* Name = MacroName;
        static constexpr uint32_t Count = InCount;
        uint32_t Value = 0;
    };
   // ------------------- 增强型 ShaderPermutationDomain -------------------
    template<typename... Dimensions>
    class ShaderPermutationDomain
    {
    public:
        // 1. 编译期计算总变体数量 (所有维度 Count 的乘积)
        static constexpr uint32_t TotalCount = (Dimensions::Count * ... * 1);

        std::tuple<Dimensions...> DimensionValues;

        ShaderPermutationDomain() = default;

        // --- 核心接口 1: 从 PermutationId 还原所有维度的状态 ---
        void SetFromId(uint32_t id)
        {
            SetFromIdImpl(id, std::make_index_sequence<sizeof...(Dimensions)>{});
        }

        // --- 核心接口 2: 将当前维度状态打包为单一 ID ---
        uint32_t GetPermutationId() const
        {
            return GetPermutationIdImpl(std::make_index_sequence<sizeof...(Dimensions)>{});
        }

        // --- 核心接口 3: 注入到编译环境 ---
        void ModifyCompilationEnvironment(ShaderCompilerEnvironment& outEnv) const
        {
            ModifyEnvImpl(outEnv, std::make_index_sequence<sizeof...(Dimensions)>{});
        }

        // --- 辅助接口: 获取特定维度的值 ---
        template<typename T>
        uint32_t GetFieldValue() const
        {
            return std::get<T>(DimensionValues).Value;
        }

        // --- 辅助接口: 设置特定维度的值 ---
        template<typename T>
        void SetFieldValue(uint32_t inValue)
        {
            std::get<T>(DimensionValues).Value = inValue;
        }

    private:
        // 辅助工具：计算第 I 个维度之前的“进制权重”
        // 例如：维度为 [Count=3, Count=2, Count=4]，则权重分别为 [1, 3, 6]
        template<std::size_t I>
        static constexpr uint32_t GetDimensionMultiplier()
        {
            return GetMultiplierImpl<I>(std::make_index_sequence<I>{});
        }

        template<std::size_t I, std::size_t... J>
        static constexpr uint32_t GetMultiplierImpl(std::index_sequence<J...>)
        {
            // 展开计算前面的 Dimensions::Count 乘积
            uint32_t res = 1;
            auto helper = [&res](uint32_t count) { res *= count; };

            // 这里的技巧是利用 tuple_element 拿到前面维度的 Count
            using TDimensions = std::tuple<Dimensions...>;
            (helper(std::tuple_element_t<J, TDimensions>::Count), ...);

            return res;
        }

        // --- 解码实现 (Id -> Values) ---
        template<std::size_t... I>
        void SetFromIdImpl(uint32_t id, std::index_sequence<I...>)
        {
            // 公式：Value = (Id / Multiplier) % Count
            ((std::get<I>(DimensionValues).Value = (id / GetDimensionMultiplier<I>()) % std::get<I>(DimensionValues).Count), ...);
        }

        // --- 编码实现 (Values -> Id) ---
        template<std::size_t... I>
        uint32_t GetPermutationIdImpl(std::index_sequence<I...>) const
        {
            // 公式：Id = Sum(Value * Multiplier)
            uint32_t id = 0;
            ((id += std::get<I>(DimensionValues).Value * GetDimensionMultiplier<I>()), ...);
            return id;
        }

        // --- 注入宏实现 ---
        template<std::size_t... I>
        void ModifyEnvImpl(ShaderCompilerEnvironment& outEnv, std::index_sequence<I...>) const
        {
            ((outEnv.SetDefine(Dimensions::Name, (int32_t)std::get<I>(DimensionValues).Value)), ...);
        }
    };


    enum class EShaderParameterBaseType {
        Unknown = 0,
        Float32,
        Int32,
        UInt32,
        Bool,
        StructNested,//结构体类型，内部可以含有其他类型数据
        StructInclude,
        StructReference,//结构体引用，在metadata中不展开

        RenderTargetSlots,
        // 可根据需要扩展更多类型
        RDGTexture,
        RDGTexture_SRV,
        RDGTexture_UAV,
        RDGTexture_SRV_ARRAY,
        RDGTexture_UAV_ARRAY,
        RDGBuffer,
        RDGBuffer_SRV,
        RDGBuffer_UAV,
        RHISampler,
        RHITexture,
        RHI_SRV,
        RHI_UAV,
        ColorBindings,

        //

    };
    
}
