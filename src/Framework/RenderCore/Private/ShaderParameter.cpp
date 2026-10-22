#include "ShaderParameter.h"
#include "Shader.h"
#include "RenderGraphResource.h"
namespace RenderCore {


    class RENDERCORE_API ShaderParameterReader
    {
    public:
        // Read values from the supplied C++ parameter structure.
        ShaderParameterReader(const void* inData, uint32_t inSize)
            : m_Data(reinterpret_cast<const uint8_t*>(inData))
            , m_DataSize(inSize)
        {
        }

        /**
         * Read the value described by a resolved binding from the parameter memory.
         */
        template<typename T>
        const T& Read(const ShaderParameterBindingInfo::ShaderUniformBinding& binding) const
        {
            // Check that the requested value fits in the parameter structure.
            // Binding.Offset is the byte offset in the C++ structure.
            assert(binding.Offset + sizeof(T) <= m_DataSize);

            return *reinterpret_cast<const T*>(m_Data + binding.Offset);
        }

        // Return a pointer for bulk data copies.
        const void* GetRawPointer(uint32_t offset) const
        {
            assert(offset <= m_DataSize);
            return m_Data + offset;
        }

    private:
        const uint8_t* m_Data;
        const uint32_t m_DataSize;
    };


    void SetShaderParameters(
        RHI::RHICommandListBase& cmdList,
        const Shader* shader,
        const ShaderParametersMetadata* parametersMetaData,
        void* parametersData) {
        if (!shader || !parametersData) return;

        if (!shader || !parametersData) return;

        // Create a reader for the supplied parameter structure.
        // Use the size recorded by the parameter metadata.
        ShaderParameterReader reader(parametersData, parametersMetaData->GetSize());

        // Prepare the batched RHI parameter container.
        RHI::RHIBatchedShaderParameters batchedParams;

        // Visit the bindings resolved for this shader.
        // Iterate shader bindings rather than every metadata member.
        const ShaderParameterBindingInfo& bindingInfo = shader->GetParameterBindings();

        // Collect uniform values.
        size_t uniformDataOffset = 0;
        for (const auto& pair : bindingInfo.UniformBindings)
        {
            const std::string& name = pair.first;
            const auto& binding = pair.second;

            // 组装Uniform参数描述
            RHI::RHIShaderUniformParameter param;
            param.BufferIndex = binding.BufferIndex; // 若有多UBO可扩展
            param.BaseIndex = binding.BaseIndex; // 这里用Offset或BindSlot视RHI实现
            param.Offset = uniformDataOffset;
            param.Size = binding.Size;
            batchedParams.UniformParameters.push_back(param);

            // 拷贝数据到Data
            const uint8_t* src = reinterpret_cast<const uint8_t*>(reader.GetRawPointer(binding.Offset));
            batchedParams.Data.insert(batchedParams.Data.end(), src, src + binding.Size);
            uniformDataOffset += binding.Size;
        }

        // --- 资源参数组装 ---
        for (const auto& pair : bindingInfo.ResourceBindings)
        {
            const std::string& name = pair.first;
            const auto& binding = pair.second;
            const void* resourcePtrLocation = reader.GetRawPointer(binding.Offset);
            RHI::RHIResource* resource = nullptr;
            auto rdgr = *(RenderGraphResource**)resourcePtrLocation;
            RHI::RHIShaderResourceParameter param;
            
            std::vector<const void*> resourcePtrs;
            std::vector<uint32_t> arrayIndexs;
            if (binding.ArraySize > 1) {
                auto* array = reinterpret_cast<const ShaderParameterElementAccessor*>(resourcePtrLocation);
                resourcePtrs.resize(array->GetElementCount());
                for (uint32_t i = 0; i < array->GetElementCount(); ++i) {
                    resourcePtrs[i] = array->GetElementOffset(i);
                    arrayIndexs.push_back(i);
                    
                }
            }
            else {
                resourcePtrs.push_back(resourcePtrLocation);
                arrayIndexs.push_back(0);
            }
            uint32_t innerIndex = 0;
            for (auto resourcePtrLocation : resourcePtrs) {
                param.Index = binding.BindSlot;
                param.ArrayIndex = arrayIndexs[innerIndex];
                innerIndex++;
                // 根据BaseType类型组装参数
                switch (binding.BaseType)
                {
                case EShaderParameterBaseType::RDGTexture:
                    if (rdgr == nullptr) continue;
                    param.Type = RHI::RHIShaderResourceParameter::EType::Texture;
                    resource = (*(RenderGraphResource**)resourcePtrLocation)->GetRHIResource();
                    break;
                case EShaderParameterBaseType::RDGTexture_UAV:
                case EShaderParameterBaseType::RDGBuffer_UAV:
                    if (rdgr == nullptr) continue;
                    param.Type = RHI::RHIShaderResourceParameter::EType::UAV;
                    resource = (*(RenderGraphResource**)resourcePtrLocation)->GetRHIResource();
                    break;
                case EShaderParameterBaseType::RDGBuffer_SRV:
                    if (rdgr == nullptr) continue;
                    param.Type = RHI::RHIShaderResourceParameter::EType::SRV;
                    resource = (*(RenderGraphResource**)resourcePtrLocation)->GetRHIResource();
                    break;
                case EShaderParameterBaseType::RHISampler:
                    param.Type = RHI::RHIShaderResourceParameter::EType::Sampler;
                    resource = *(RHIResource**)resourcePtrLocation;
                    break;
                case EShaderParameterBaseType::RHITexture:
                    param.Type = RHI::RHIShaderResourceParameter::EType::Texture;
                    resource = *(RHIResource**)resourcePtrLocation;
                    break;
                case EShaderParameterBaseType::RHI_SRV:
                    param.Type = RHI::RHIShaderResourceParameter::EType::SRV;
                    resource = *(RHIResource**)resourcePtrLocation;
                    break;
                case EShaderParameterBaseType::RHI_UAV:
                    param.Type = RHI::RHIShaderResourceParameter::EType::UAV;
                    resource = *(RHIResource**)resourcePtrLocation;
                    break;
                case EShaderParameterBaseType::StructNested:
                    param.Type = RHI::RHIShaderResourceParameter::EType::UniformBuffer;
                    break;
                default:
                    assert(false);
                    break;
                }
                param.Resource = resource;
                batchedParams.ResourceParameters.push_back(param);
            }
            
        }

        // Submit the batched parameters to the command list.
        const auto shaderFrequency = shader->GetShaderFrequency();
        if (shaderFrequency == ERHIShaderFrequency::Compute)
        {
            auto& computeCmdList = static_cast<RHI::RHIComputeCommandList&>(cmdList);
            computeCmdList.SetBatchedShaderParameters(
                static_cast<RHI::RHIComputeShader*>(shader->GetRHIShader()),
                batchedParams
            );
        }
        else if (
            shaderFrequency == ERHIShaderFrequency::RayGen ||
            shaderFrequency == ERHIShaderFrequency::ClosestHit ||
            shaderFrequency == ERHIShaderFrequency::Miss ||
            shaderFrequency == ERHIShaderFrequency::AnyHit ||
            shaderFrequency == ERHIShaderFrequency::Intersection ||
            shaderFrequency == ERHIShaderFrequency::Callable)
        {
            auto& graphicCmdList = static_cast<RHI::RHIGraphicCommandList&>(cmdList);
            graphicCmdList.SetBatchedShaderParameters(
                static_cast<RHI::RHIRayTracingShader*>(shader->GetRHIShader()),
                batchedParams
            );
        }
        else
        {
            auto& graphicCmdList = static_cast<RHI::RHIGraphicCommandList&>(cmdList);
            graphicCmdList.SetBatchedShaderParameters(
                static_cast<RHI::RHIGraphicShader*>(shader->GetRHIShader()),
                batchedParams
            );
        }

    }


}
