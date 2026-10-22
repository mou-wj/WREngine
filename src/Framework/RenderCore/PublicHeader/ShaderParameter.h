#pragma once

#include "RenderGraphResource.h"
#include "Math.hpp" // For Float2, Float3, Float4, etc.
#include "RHIDefine.h"
#include "RHICommandList.h"
#include "ShaderCore.h"
#include <string>
#include <memory>
#include <vector>
#include <unordered_map>
#include <type_traits>
#include <iostream>

#define SHADER_PARAMETER_ALIGNMENT 16

namespace RenderCore {
 

    class ShaderParametersMetadata;




    struct RenderTargetBinding
    {
        RenderGraphTexture* Texture = nullptr;

        RenderGraphTexture* ResolveTexture = nullptr;

        RHI::ERenderTargetActions Action =
            RHI::ERenderTargetActions::Clear_Store;

        uint8_t MipIndex = 0;

        int16_t ArraySlice = 0;

        uint32_t SampleCount = 1;

        Core::Float4 ClearValue;

        bool IsValid() const
        {
            return Texture != nullptr;
        }
    };

    struct DepthStencilBinding
    {
        RenderGraphTexture* Texture = nullptr;

        RHI::ERenderTargetActions DepthAction =
            RHI::ERenderTargetActions::Clear_Store;

        RHI::ERenderTargetActions StencilAction =
            RHI::ERenderTargetActions::Clear_Store;

        bool bWriteDepth = true;

        bool bWriteStencil = false;

        uint8_t MipIndex = 0;

        int16_t ArraySlice = 0;

        uint32_t SampleCount = 1;

        bool IsValid() const
        {
            return Texture != nullptr;
        }
    };

    struct RenderTargetBindingSlots
    {
        static constexpr uint32_t MaxRenderTargets = 8;

        RenderTargetBinding
            ColorRenderTargets[MaxRenderTargets];

        uint32_t NumColorRenderTargets = 0;

        DepthStencilBinding DepthStencil;

    public:

        RenderTargetBinding&
            operator[](uint32_t Index)
        {
            return ColorRenderTargets[Index];
        }

        const RenderTargetBinding&
            operator[](uint32_t Index) const
        {
            return ColorRenderTargets[Index];
        }
        RHI::RHIBoundRenderTargets GetBoundRenderTarget() const
        {
            RHI::RHIBoundRenderTargets BoundRTs;

            // 1. 填充颜色附件
            // 外部可能没有紧凑地排列 ColorRenderTargets，所以我们通过 NumColorRenderTargets 
            // 或者显式遍历有效插槽来保证数据的正确性。
            BoundRTs.NumColorAttachments = static_cast<uint8_t>(NumColorRenderTargets);

            for (uint32_t i = 0; i < NumColorRenderTargets; ++i)
            {
                const RenderTargetBinding& SourceSlot = ColorRenderTargets[i];
                auto& DestSlot = BoundRTs.ColorAttachments[i];

                if (SourceSlot.IsValid())
                {
                    // 【核心】从 RDG Texture 提取底层物理 RHI 纹理指针
                    // 请根据你引擎中 RenderGraphTexture 的实际提取函数名修改（如 ->GetRHITexture() 或 ->RHIResource）
                    DestSlot.Texture = SourceSlot.Texture->GetRHITexture();
                    DestSlot.ResolveTarget = SourceSlot.ResolveTexture ? SourceSlot.ResolveTexture->GetRHITexture() : nullptr;

                    DestSlot.MipIndex = SourceSlot.MipIndex;
                    DestSlot.ArraySlice = static_cast<int32_t>(SourceSlot.ArraySlice);
                    DestSlot.Actions = SourceSlot.Action;
                    DestSlot.SampleCount = SourceSlot.SampleCount;
                    DestSlot.Actions = SourceSlot.Action;
                    // 设置清除标记类型
                    DestSlot.ClearBinding.Binding = RHI::RHIClearValueBinding::ClearValueBinding::Color;
                    DestSlot.ClearBinding.Color[0] = SourceSlot.ClearValue.X;
                    DestSlot.ClearBinding.Color[1] = SourceSlot.ClearValue.Y;
                    DestSlot.ClearBinding.Color[2] = SourceSlot.ClearValue.Z;
                    DestSlot.ClearBinding.Color[3] = SourceSlot.ClearValue.W;
                }
            }

            // 2. 填充深度模板附件
            if (DepthStencil.IsValid())
            {
                auto& DestDepth = BoundRTs.DepthStencil;

                // 【核心】提取底层物理 RHI 深度纹理指针
                DestDepth.Texture = DepthStencil.Texture->GetRHITexture();
                DestDepth.ResolveTarget = nullptr; // 深度通常不直接在这做 Resolve

                DestDepth.MipIndex = DepthStencil.MipIndex;
                DestDepth.ArraySlice = static_cast<uint32_t>(DepthStencil.ArraySlice < 0 ? 0 : DepthStencil.ArraySlice);
                DestDepth.Actions = DepthStencil.DepthAction; // 默认或者以 DepthAction 为主
                DestDepth.SampleCount = DepthStencil.SampleCount;

                DestDepth.ClearBinding.Binding = RHI::RHIClearValueBinding::ClearValueBinding::DepthStencil;
                DestDepth.ClearBinding.Depth = 1.0f;
                DestDepth.ClearBinding.Stencil = 0;
            }

            // 3. 触发物理尺寸的重新计算与对齐
            // 由于 Bound 逻辑移到了这里，我们需要手动调用转换后结构的内部计算
            // 如果之前把 CalculateDimensions 方法设为了 protected/private，需要把它改成 public
            BoundRTs.CalculateDimensions();

            return BoundRTs;
        }

    };



    class ShaderParametersMetadata
    {
    public:
        enum class EUseCase : uint8_t
        {
            ShaderParameterStruct,
            UniformBuffer
        };

        struct Member
        {
            const char* TypeName;
            const char* Name;
            uint32_t Offset;
            EShaderParameterBaseType BaseType;
            uint32_t NumRows = 1;
            uint32_t NumColumns = 1;
            uint32_t NumElements = 0;
            const ShaderParametersMetadata* StructMetadata = nullptr;
            Member(
            const char* InTypeName,
            const char* InName,
            uint32_t InOffset,
            EShaderParameterBaseType InBaseType,
            uint32_t InNumRows,
            uint32_t InNumColumns,
            uint32_t InNumElements,
            const ShaderParametersMetadata* InStructMetadata
            ):
				TypeName(InTypeName),
				Name(InName), 
                Offset(InOffset), 
                BaseType(InBaseType),
                NumRows(InNumRows), 
                NumColumns(InNumColumns), 
                NumElements(InNumElements), 
                StructMetadata(InStructMetadata)
            
            {

            }
            bool IsUniformDataMember() const { return StructMetadata == nullptr && BaseType <= EShaderParameterBaseType::StructNested; }
            bool IsIncludeStruct() const { return StructMetadata != nullptr && BaseType == EShaderParameterBaseType::StructInclude; }
            bool IsReferenceStruct() const { return StructMetadata != nullptr && BaseType == EShaderParameterBaseType::StructReference; }
            bool IsNestedStruct() const { return StructMetadata != nullptr && BaseType == EShaderParameterBaseType::StructNested; }
            bool IsResource() const { return (BaseType >= EShaderParameterBaseType::RDGTexture); }
            bool IsArray() const { return (NumElements > 1); }
            bool IsRenderTargetSlots() const { return BaseType == EShaderParameterBaseType::RenderTargetSlots; }
        };
		bool InitFlag = false;
    public:

        ShaderParametersMetadata(
            const char* InStructName,
            uint32_t InSize,
            std::vector<Member> InMembers,
            EUseCase InUseCase = EUseCase::ShaderParameterStruct)
            : StructName(InStructName)
            , Size(InSize)
            , Members(std::move(InMembers))
            , UseCase(InUseCase)
        {
        }

        const char* GetStructName() const { return StructName; }
        uint32_t GetSize() const { return Size; }
        const std::vector<Member>& GetMembers() const { return Members; }
        EUseCase GetUseCase() const { return UseCase; }
        bool HasResource() const
        {
            for (const auto& Member : Members)
            {
                if (Member.IsResource())
                    return true;

                if (Member.IsNestedStruct())
                {
                    if (Member.StructMetadata &&
                        Member.StructMetadata
                        ->HasResource())
                    {
                        return true;
                    }
                }
            }
            return false;
        }

        template<typename Fn>
        void IterateStructureMetadataDependencies(
            Fn Lambda) const
        {
            for (auto& Member : Members)
            {
                auto* StructMeta =
                    Member.GetStructMetadata();

                if (!StructMeta || Member.IsIncludeStruct())
                    continue;

                StructMeta
                    ->IterateStructureMetadataDependencies(
                        Lambda);
            }

            Lambda(this);
        }

        std::vector<Member> Members;
    private:
        const char* StructName;
        uint32_t Size;
        
        EUseCase UseCase;

    };
    



class Shader;
RENDERCORE_API void SetShaderParameters(
    RHI::RHICommandListBase& cmdList,
    const Shader* shader,
    const ShaderParametersMetadata* ParametersMetaData,
    void* ParametersData);


/**
 * RenderCore 层函数：将 C++ 结构体参数提交至 RHI 指令流
 * @param cmdList  RHI 指令列表
 * @param shader   Shader 对象（持有 Metadata 和 Binding 映射）
 * @param ParametersData C++ 结构体实例指针
 */
template<typename TParameters>
void SetShaderParameters(
    RHI::RHICommandListBase& cmdList,
    const Shader* shader,
    const TParameters* ParametersData) {
    SetShaderParameters(cmdList, shader, ParametersData->GetMetaData(), (void*)ParametersData);
}




} // namespace WR::RenderCore


template<typename T>
struct ShaderParameterTypeInfo
{
    static constexpr RenderCore::EShaderParameterBaseType BaseType = RenderCore::EShaderParameterBaseType::Unknown;
    static constexpr uint32_t NumRows = 1;
    static constexpr uint32_t NumColumns = 1;
    static constexpr uint32_t NumElements = 0;

    static constexpr uint32_t Alignment = SHADER_PARAMETER_ALIGNMENT;
    static constexpr bool bIsStoredInConstantBuffer = true;

    using TAlignedType = T;

    static const RenderCore::ShaderParametersMetadata* GetStructMetadata()
    {
        return nullptr;
    }
};
class ShaderParameterElementAccessor {
public:
    virtual const uint8_t* GetElementOffset(uint32_t Index) const = 0;
    virtual size_t GetElementCount() const = 0;
};
template<typename T, size_t InNumElements>
class ShaderParameterArray : public ShaderParameterElementAccessor,public std::array<T, InNumElements> {
public:
    const uint8_t* GetElementOffset(uint32_t Index) const override {
        return (uint8_t*)&(*this)[Index];
    }
	size_t GetElementCount() const override {
		return InNumElements;
	}
};

template<typename T,size_t InNumElements, RenderCore::EShaderParameterBaseType InBaseType>
struct ShaderParameterArrayInfo
{
    static constexpr RenderCore::EShaderParameterBaseType BaseType = InBaseType;
    static constexpr uint32_t NumRows = 1;
    static constexpr uint32_t NumColumns = 1;
    static constexpr uint32_t NumElements = InNumElements;

    static constexpr uint32_t Alignment = SHADER_PARAMETER_ALIGNMENT;
    static constexpr bool bIsStoredInConstantBuffer = true;

    using TAlignedType = ShaderParameterArray<T, InNumElements>;

    static const RenderCore::ShaderParametersMetadata* GetStructMetadata()
    {
        return nullptr;
    }
};
template<>
struct ShaderParameterTypeInfo<RenderCore::RenderGraphTexture>
{
    static constexpr RenderCore::EShaderParameterBaseType BaseType = RenderCore::EShaderParameterBaseType::RDGTexture;
    static constexpr uint32_t NumRows = 1;
    static constexpr uint32_t NumColumns = 1;
    static constexpr uint32_t NumElements = 0;
    static constexpr uint32_t Alignment = 0; // 资源通常不占 ConstantBuffer 空间
    static constexpr bool bIsStoredInConstantBuffer = false; // 关键：标记为非 CBuffer 成员

    using TAlignedType = RenderCore::RenderGraphTexture*;

    static const RenderCore::ShaderParametersMetadata* GetStructMetadata() { return nullptr; }
};
template<size_t Count>
using ShaderParameterTypeRDGTextureArray = ShaderParameterArrayInfo<RenderCore::RenderGraphTexture*, Count, RenderCore::EShaderParameterBaseType::RDGTexture>;

template<>
struct ShaderParameterTypeInfo<RenderCore::RenderGraphTextureUAV>
{
    static constexpr RenderCore::EShaderParameterBaseType BaseType = RenderCore::EShaderParameterBaseType::RDGTexture_UAV;
    static constexpr uint32_t NumRows = 1;
    static constexpr uint32_t NumColumns = 1;
    static constexpr uint32_t NumElements = 0;
    static constexpr uint32_t Alignment = 0; // 资源通常不占 ConstantBuffer 空间
    static constexpr bool bIsStoredInConstantBuffer = false; // 关键：标记为非 CBuffer 成员

    using TAlignedType = RenderCore::RenderGraphTextureUAV*;

    static const RenderCore::ShaderParametersMetadata* GetStructMetadata() { return nullptr; }
};
template<>
struct ShaderParameterTypeInfo<RenderCore::RenderGraphBufferSRV>
{
    static constexpr RenderCore::EShaderParameterBaseType BaseType = RenderCore::EShaderParameterBaseType::RDGTexture_UAV;
    static constexpr uint32_t NumRows = 1;
    static constexpr uint32_t NumColumns = 1;
    static constexpr uint32_t NumElements = 0;
    static constexpr uint32_t Alignment = 0; // 资源通常不占 ConstantBuffer 空间
    static constexpr bool bIsStoredInConstantBuffer = false; // 关键：标记为非 CBuffer 成员

    using TAlignedType = RenderCore::RenderGraphBufferSRV*;

    static const RenderCore::ShaderParametersMetadata* GetStructMetadata() { return nullptr; }
};
template<>
struct ShaderParameterTypeInfo<RHI::RHISampler>
{
    static constexpr RenderCore::EShaderParameterBaseType BaseType = RenderCore::EShaderParameterBaseType::RHISampler;
    static constexpr uint32_t NumRows = 1;
    static constexpr uint32_t NumColumns = 1;
    static constexpr uint32_t NumElements = 0;
    static constexpr uint32_t Alignment = 0; // 资源通常不占 ConstantBuffer 空间
    static constexpr bool bIsStoredInConstantBuffer = false; // 关键：标记为非 CBuffer 成员

    using TAlignedType = RHI::RHISampler*;

    static const RenderCore::ShaderParametersMetadata* GetStructMetadata() { return nullptr; }
};

template<>
struct ShaderParameterTypeInfo<RHI::RHITexture>
{
    static constexpr RenderCore::EShaderParameterBaseType BaseType = RenderCore::EShaderParameterBaseType::RHITexture;
    static constexpr uint32_t NumRows = 1;
    static constexpr uint32_t NumColumns = 1;
    static constexpr uint32_t NumElements = 0;
    static constexpr uint32_t Alignment = 0; // 资源通常不占 ConstantBuffer 空间
    static constexpr bool bIsStoredInConstantBuffer = false; // 关键：标记为非 CBuffer 成员

    using TAlignedType = RHI::RHITexture*;

    static const RenderCore::ShaderParametersMetadata* GetStructMetadata() { return nullptr; }
};

template<>
struct ShaderParameterTypeInfo<RHI::RHIShaderResourceView>
{
    static constexpr RenderCore::EShaderParameterBaseType BaseType = RenderCore::EShaderParameterBaseType::RHI_SRV;
    static constexpr uint32_t NumRows = 1;
    static constexpr uint32_t NumColumns = 1;
    static constexpr uint32_t NumElements = 0;
    static constexpr uint32_t Alignment = 0; // 资源通常不占 ConstantBuffer 空间
    static constexpr bool bIsStoredInConstantBuffer = false; // 关键：标记为非 CBuffer 成员

    using TAlignedType = RHI::RHIShaderResourceView*;

    static const RenderCore::ShaderParametersMetadata* GetStructMetadata() { return nullptr; }
};
template<>
struct ShaderParameterTypeInfo<RHI::RHIUnorderedAccessView>
{
    static constexpr RenderCore::EShaderParameterBaseType BaseType = RenderCore::EShaderParameterBaseType::RHI_UAV;
    static constexpr uint32_t NumRows = 1;
    static constexpr uint32_t NumColumns = 1;
    static constexpr uint32_t NumElements = 0;
    static constexpr uint32_t Alignment = 0; // 资源通常不占 ConstantBuffer 空间
    static constexpr bool bIsStoredInConstantBuffer = false; // 关键：标记为非 CBuffer 成员

    using TAlignedType = RHI::RHIUnorderedAccessView*;

    static const RenderCore::ShaderParametersMetadata* GetStructMetadata() { return nullptr; }
};


template<>
struct ShaderParameterTypeInfo<uint32_t>
{
    static constexpr RenderCore::EShaderParameterBaseType BaseType = RenderCore::EShaderParameterBaseType::UInt32;
    static constexpr uint32_t NumRows = 1;
    static constexpr uint32_t NumColumns = 1;
    static constexpr uint32_t NumElements = 0;
    static constexpr uint32_t Alignment = 0; // 资源通常不占 ConstantBuffer 空间
    static constexpr bool bIsStoredInConstantBuffer = false; // 关键：标记为非 CBuffer 成员

    using TAlignedType = uint32_t;

    static const RenderCore::ShaderParametersMetadata* GetStructMetadata() { return nullptr; }
};
template<>
struct ShaderParameterTypeInfo<Core::Int2>
{
    static constexpr RenderCore::EShaderParameterBaseType BaseType = RenderCore::EShaderParameterBaseType::Int32;
    static constexpr uint32_t NumRows = 1;
    static constexpr uint32_t NumColumns = 2;
    static constexpr uint32_t NumElements = 0;
    static constexpr uint32_t Alignment = 0; // 资源通常不占 ConstantBuffer 空间
    static constexpr bool bIsStoredInConstantBuffer = false; // 关键：标记为非 CBuffer 成员

    using TAlignedType = Core::Int2;

    static const RenderCore::ShaderParametersMetadata* GetStructMetadata() { return nullptr; }
};
template<>
struct ShaderParameterTypeInfo<Core::Int3>
{
    static constexpr RenderCore::EShaderParameterBaseType BaseType = RenderCore::EShaderParameterBaseType::Int32;
    static constexpr uint32_t NumRows = 1;
    static constexpr uint32_t NumColumns = 3;
    static constexpr uint32_t NumElements = 0;
    static constexpr uint32_t Alignment = 0; // 资源通常不占 ConstantBuffer 空间
    static constexpr bool bIsStoredInConstantBuffer = false; // 关键：标记为非 CBuffer 成员

    using TAlignedType = Core::Int3;

    static const RenderCore::ShaderParametersMetadata* GetStructMetadata() { return nullptr; }
};

template<>
struct ShaderParameterTypeInfo<Core::UInt3>
{
    static constexpr RenderCore::EShaderParameterBaseType BaseType = RenderCore::EShaderParameterBaseType::UInt32;
    static constexpr uint32_t NumRows = 1;
    static constexpr uint32_t NumColumns = 3;
    static constexpr uint32_t NumElements = 0;
    static constexpr uint32_t Alignment = 0; // 资源通常不占 ConstantBuffer 空间
    static constexpr bool bIsStoredInConstantBuffer = false; // 关键：标记为非 CBuffer 成员

    using TAlignedType = Core::UInt3;

    static const RenderCore::ShaderParametersMetadata* GetStructMetadata() { return nullptr; }
};

template<>
struct ShaderParameterTypeInfo<float>
{
    static constexpr RenderCore::EShaderParameterBaseType BaseType = RenderCore::EShaderParameterBaseType::Float32;
    static constexpr uint32_t NumRows = 1;
    static constexpr uint32_t NumColumns = 1;
    static constexpr uint32_t NumElements = 0;
    static constexpr uint32_t Alignment = 0; // 资源通常不占 ConstantBuffer 空间
    static constexpr bool bIsStoredInConstantBuffer = false; // 关键：标记为非 CBuffer 成员

    using TAlignedType = float;

    static const RenderCore::ShaderParametersMetadata* GetStructMetadata() { return nullptr; }
};
template<>
struct ShaderParameterTypeInfo<Core::Float2>
{
    static constexpr RenderCore::EShaderParameterBaseType BaseType = RenderCore::EShaderParameterBaseType::Float32;
    static constexpr uint32_t NumRows = 1;
    static constexpr uint32_t NumColumns = 2;
    static constexpr uint32_t NumElements = 0;
    static constexpr uint32_t Alignment = 0; // 资源通常不占 ConstantBuffer 空间
    static constexpr bool bIsStoredInConstantBuffer = false; // 关键：标记为非 CBuffer 成员

    using TAlignedType = Core::Float2;

    static const RenderCore::ShaderParametersMetadata* GetStructMetadata() { return nullptr; }
};
template<>
struct ShaderParameterTypeInfo<Core::Float3>
{
    static constexpr RenderCore::EShaderParameterBaseType BaseType = RenderCore::EShaderParameterBaseType::Float32;
    static constexpr uint32_t NumRows = 1;
    static constexpr uint32_t NumColumns = 3;
    static constexpr uint32_t NumElements = 0;
    static constexpr uint32_t Alignment = 0; // 资源通常不占 ConstantBuffer 空间
    static constexpr bool bIsStoredInConstantBuffer = false; // 关键：标记为非 CBuffer 成员

    using TAlignedType = Core::Float3;

    static const RenderCore::ShaderParametersMetadata* GetStructMetadata() { return nullptr; }
};
template<>
struct ShaderParameterTypeInfo<Core::Float4>
{
    static constexpr RenderCore::EShaderParameterBaseType BaseType = RenderCore::EShaderParameterBaseType::Float32;
    static constexpr uint32_t NumRows = 1;
    static constexpr uint32_t NumColumns = 4;
    static constexpr uint32_t NumElements = 0;
    static constexpr uint32_t Alignment = 0; // 资源通常不占 ConstantBuffer 空间
    static constexpr bool bIsStoredInConstantBuffer = false; // 关键：标记为非 CBuffer 成员

    using TAlignedType = Core::Float4;

    static const RenderCore::ShaderParametersMetadata* GetStructMetadata() { return nullptr; }
};
template<>
struct ShaderParameterTypeInfo<Core::Float4x4>
{
    static constexpr RenderCore::EShaderParameterBaseType BaseType = RenderCore::EShaderParameterBaseType::Float32;
    static constexpr uint32_t NumRows = 4;
    static constexpr uint32_t NumColumns = 4;
    static constexpr uint32_t NumElements = 0;
    static constexpr uint32_t Alignment = 0; // 资源通常不占 ConstantBuffer 空间
    static constexpr bool bIsStoredInConstantBuffer = false; // 关键：标记为非 CBuffer 成员

    using TAlignedType = Core::Float4x4;

    static const RenderCore::ShaderParametersMetadata* GetStructMetadata() { return nullptr; }
};

template<>
struct ShaderParameterTypeInfo<RenderCore::RenderTargetBindingSlots>
{
    static constexpr RenderCore::EShaderParameterBaseType BaseType = RenderCore::EShaderParameterBaseType::RenderTargetSlots;
    static constexpr uint32_t NumRows = 1;
    static constexpr uint32_t NumColumns = 1;
    static constexpr uint32_t NumElements = 0;
    static constexpr uint32_t Alignment = 0; // 资源通常不占 ConstantBuffer 空间
    static constexpr bool bIsStoredInConstantBuffer = false; // 关键：标记为非 CBuffer 成员

    using TAlignedType = RenderCore::RenderTargetBindingSlots;

    static const RenderCore::ShaderParametersMetadata* GetStructMetadata() { return nullptr; }
};

template<typename T>
struct StructNestedShaderParameterTypeInfo
{
    static constexpr RenderCore::EShaderParameterBaseType BaseType = RenderCore::EShaderParameterBaseType::StructNested;
    static constexpr uint32_t NumRows = 1;
    static constexpr uint32_t NumColumns = 1;
    static constexpr uint32_t NumElements = 0;
    static constexpr uint32_t Alignment = 0; // 资源通常不占 ConstantBuffer 空间
    static constexpr bool bIsStoredInConstantBuffer = false; // 关键：标记为非 CBuffer 成员

    using TAlignedType = T;

    static const RenderCore::ShaderParametersMetadata* GetStructMetadata() { return T::GetMetaData(); }
};
template<typename T>
struct StructReferenceShaderParameterTypeInfo
{
    static constexpr RenderCore::EShaderParameterBaseType BaseType = RenderCore::EShaderParameterBaseType::StructReference;
    static constexpr uint32_t NumRows = 1;
    static constexpr uint32_t NumColumns = 1;
    static constexpr uint32_t NumElements = 0;
    static constexpr uint32_t Alignment = 0; // 资源通常不占 ConstantBuffer 空间
    static constexpr bool bIsStoredInConstantBuffer = false; // 关键：标记为非 CBuffer 成员

    using TAlignedType = T;

    static const RenderCore::ShaderParametersMetadata* GetStructMetadata() { return T::GetMetaData(); }
};

template<typename T>
struct StructIncludeShaderParameterTypeInfo
{
    static constexpr RenderCore::EShaderParameterBaseType BaseType = RenderCore::EShaderParameterBaseType::StructInclude;
    static constexpr uint32_t NumRows = 1;
    static constexpr uint32_t NumColumns = 1;
    static constexpr uint32_t NumElements = 0;
    static constexpr uint32_t Alignment = 0; // 资源通常不占 ConstantBuffer 空间
    static constexpr bool bIsStoredInConstantBuffer = false; // 关键：标记为非 CBuffer 成员

    using TAlignedType = T;

    static const RenderCore::ShaderParametersMetadata* GetStructMetadata() { return T::GetMetaData(); }
};

template<typename ElementType,typename T, RenderCore::EShaderParameterBaseType InBaseType>
struct StructNestedBufferShaderParameterTypeInfo
{
    static constexpr RenderCore::EShaderParameterBaseType BaseType = InBaseType;
    static constexpr uint32_t NumRows = 1;
    static constexpr uint32_t NumColumns = 1;
    static constexpr uint32_t NumElements = 0;
    static constexpr uint32_t Alignment = 0; // 资源通常不占 ConstantBuffer 空间
    static constexpr bool bIsStoredInConstantBuffer = false; // 关键：标记为非 CBuffer 成员

    using TAlignedType = T;

    static const RenderCore::ShaderParametersMetadata* GetStructMetadata() { return ElementType::GetMetaData(); }
};


#define TEXT(T) #T


#define STRUCT_OFFSET(StructType, Member) offsetof(StructType, Member)
// Begin shader parameter struct
#define BEGIN_SHADER_PARAMETER_STRUCT(StructClass) struct  StructClass{ \
    struct FirstIdType{}; \
    using PrevMemberIdType = FirstIdType; \
    using ThisStructType = StructClass; \
    using FuncPtr = void*;\
    typedef FuncPtr(*MemberFuncType)(PrevMemberIdType, std::vector<RenderCore::ShaderParametersMetadata::Member>*);\
    static FuncPtr sAppendMemberGetPrev(PrevMemberIdType, std::vector<RenderCore::ShaderParametersMetadata::Member>*) \
		{ \
			return nullptr; \
		} \
	typedef PrevMemberIdType
// End shader parameter struct
#define END_SHADER_PARAMETER_STRUCT(StructClass) \
    LastIdType;\
public:\
    static const RenderCore::ShaderParametersMetadata* GetMetaData() \
    {\
        static RenderCore::ShaderParametersMetadata sMetaData(#StructClass,sizeof(StructClass),{});\
        if(!sMetaData.InitFlag){\
        std::vector<RenderCore::ShaderParametersMetadata::Member>& Members = sMetaData.Members;\
        FuncPtr(*PrevFunc)(LastIdType, std::vector<RenderCore::ShaderParametersMetadata::Member>*);            \
        PrevFunc = sAppendMemberGetPrev; \
        FuncPtr func = (FuncPtr)PrevFunc; \
        do{\
            func = reinterpret_cast<MemberFuncType>(func)(LastIdType(), &Members);\
	    } while (func != nullptr); \
        std::reverse(Members.begin(), Members.end());\
        sMetaData.InitFlag = true;\
        }\
        return &sMetaData;\
    }\
};

#define SHADER_PARAMETER_INTERNAL(MemberTypeName,MemberName,TypeInfo)\
    PrevType##MemberName;\
    struct CurMember##MemberName : PrevType##MemberName{};\
    using CurMemberIdType##MemberName = CurMember##MemberName;\
public:\
    TypeInfo::TAlignedType MemberName;\
private:\
    static FuncPtr sAppendMemberGetPrev(CurMemberIdType##MemberName, std::vector<RenderCore::ShaderParametersMetadata::Member>* Members) \
		{ \
            Members->push_back(RenderCore::ShaderParametersMetadata::Member(\
            MemberTypeName,\
            #MemberName,\
            STRUCT_OFFSET(ThisStructType, MemberName),\
            TypeInfo::BaseType,\
            TypeInfo::NumRows,\
            TypeInfo::NumColumns,\
            TypeInfo::NumElements,\
            TypeInfo::GetStructMetadata()));\
			FuncPtr(*PrevFunc)(PrevType##MemberName, std::vector<RenderCore::ShaderParametersMetadata::Member>*); \
			PrevFunc = sAppendMemberGetPrev; \
            return (FuncPtr)PrevFunc; \
		} \
	typedef CurMemberIdType##MemberName

// Define a texture parameter
#define SHADER_PARAMETER(ClassType,Name) \
SHADER_PARAMETER_INTERNAL("",Name,ShaderParameterTypeInfo<ClassType>)

    // 定义纹理参数宏
#define SHADER_PARAMETER_RDG_TEXTURE(MemberTypeName,MemberName) \
    SHADER_PARAMETER_INTERNAL( \
        TEXT(MemberTypeName),\
        MemberName, \
        ShaderParameterTypeInfo<RenderCore::RenderGraphTexture>)

#define SHADER_PARAMETER_RDG_TEXTURE_ARRAY(MemberTypeName,MemberName,Count) \
    SHADER_PARAMETER_INTERNAL( \
        TEXT(MemberTypeName),\
        MemberName, \
        ShaderParameterTypeRDGTextureArray<Count>)

#define SHADER_PARAMETER_SAMPLER(MemberName) \
    SHADER_PARAMETER_INTERNAL( \
        "SamplerState",\
        MemberName, \
        ShaderParameterTypeInfo<RHI::RHISampler>)

#define SHADER_PARAMETER_RDG_TEXTURE_UAV(MemberTypeName,MemberName) \
    SHADER_PARAMETER_INTERNAL( \
        TEXT(MemberTypeName),\
        MemberName, \
        ShaderParameterTypeInfo<RenderCore::RenderGraphTextureUAV>)

#define SHADER_PARAMETER_RDG_BUFFER_SRV(MemberTypeName,MemberName) \
    SHADER_PARAMETER_INTERNAL( \
        TEXT(MemberTypeName),\
        MemberName, \
        ShaderParameterTypeInfo<RenderCore::RenderGraphBufferSRV>)

#define SHADER_PARAMETER_RENDER_TARGET_BINDING_SLOTS(MemberName) \
    SHADER_PARAMETER_INTERNAL( \
        "",\
        MemberName, \
        ShaderParameterTypeInfo<RenderCore::RenderTargetBindingSlots>)

#define SHADER_PARAMETER_STRUCT_NESTED(StructType, MemberName) \
    SHADER_PARAMETER_INTERNAL( \
        TEXT(StructType),\
        MemberName, \
        StructNestedShaderParameterTypeInfo<StructType>)

#define SHADER_PARAMETER_STRUCT_REFERENCE(StructType, MemberName) \
    SHADER_PARAMETER_INTERNAL( \
        "",\
        MemberName, \
        StructReferenceShaderParameterTypeInfo<StructType>)

#define SHADER_PARAMETER_STRUCT_INCLUDE(StructType, MemberName) \
    SHADER_PARAMETER_INTERNAL( \
        "",\
        MemberName, \
        StructIncludeShaderParameterTypeInfo<StructType>)
template<typename ElementType>
using RDGStructuredBufferTemplate = StructNestedBufferShaderParameterTypeInfo<ElementType, RenderCore::RenderGraphBufferSRVRef, RenderCore::EShaderParameterBaseType::RDGBuffer_SRV>;

#define SHADER_PARAMETER_RDG_STRUCTURED_BUFFER( \
    ElementType, Name) \
SHADER_PARAMETER_INTERNAL( \
    "StructuredBuffer<"##TEXT(ElementType)##">",\
    Name, \
    RDGStructuredBufferTemplate<ElementType>)

template<typename ElementType>
using RDGRWStructuredBufferTemplate = StructNestedBufferShaderParameterTypeInfo<ElementType, RenderCore::RenderGraphBufferUAVRef, RenderCore::EShaderParameterBaseType::RDGBuffer_UAV>;

#define SHADER_PARAMETER_RDG_RWSTRUCTURED_BUFFER( \
    ElementType, Name) \
SHADER_PARAMETER_INTERNAL( \
    "RWStructuredBuffer<"##TEXT(ElementType)##">",\
    Name, \
    RDGRWStructuredBufferTemplate<ElementType>)

#define SHADER_PARAMETER_RHI_TEXTURE(MemberTypeName,MemberName) \
    SHADER_PARAMETER_INTERNAL( \
        TEXT(MemberTypeName),\
        MemberName, \
        ShaderParameterTypeInfo<RHI::RHITexture>)

#define SHADER_PARAMETER_RHI_SRV(MemberTypeName,MemberName) \
    SHADER_PARAMETER_INTERNAL( \
        TEXT(MemberTypeName),\
        MemberName, \
        ShaderParameterTypeInfo<RHI::RHIShaderResourceView>)

#define SHADER_PARAMETER_RHI_UAV(MemberTypeName,MemberName) \
    SHADER_PARAMETER_INTERNAL( \
        TEXT(MemberTypeName),\
        MemberName, \
        ShaderParameterTypeInfo<RHI::RHIUnorderedAccessView>)


template<typename ElementType>
using RHIStructuredBufferTemplate = StructNestedBufferShaderParameterTypeInfo<ElementType, RHI::RHIShaderResourceView*, RenderCore::EShaderParameterBaseType::RHI_SRV>;

#define SHADER_PARAMETER_RHI_STRUCTURED_BUFFER( \
    ElementType, Name) \
SHADER_PARAMETER_INTERNAL( \
    "StructuredBuffer<"##TEXT(ElementType)##">",\
    Name, \
    RHIStructuredBufferTemplate<ElementType>)

template<typename ElementType>
using RHIRWStructuredBufferTemplate = StructNestedBufferShaderParameterTypeInfo<ElementType, RHI::RHIUnorderedAccessView*, RenderCore::EShaderParameterBaseType::RHI_UAV>;

#define SHADER_PARAMETER_RHI_RWSTRUCTURED_BUFFER( \
    ElementType, Name) \
SHADER_PARAMETER_INTERNAL( \
    "RWStructuredBuffer<"##TEXT(ElementType)##">",\
    Name, \
    RHIRWStructuredBufferTemplate<ElementType>)

BEGIN_SHADER_PARAMETER_STRUCT(A)
    SHADER_PARAMETER(Core::Int2, Int2Parameter)
END_SHADER_PARAMETER_STRUCT(A)