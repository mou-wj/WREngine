#include "Shader.h"
#include "RenderGraphBuilder.h"
#include "RHIApi.h"
#include <stdexcept>
#include <iostream>

namespace RenderCore {


Shader::Shader(const ShaderCompiledInitializer& initializer)
{
    _name = initializer.Type->Name;
    _shaderType = initializer.Type->Frequency;
    _shaderSourceCode = initializer.Code;
    auto metadata = initializer.Type->RootParametersMetadata;
    if (metadata) {
		InitShaderBindings(metadata, initializer.ParameterMap);
    }
    InitShaderRHI(initializer.Type->Frequency, initializer.Code);
}

// Destructor
Shader::~Shader()
{
    // Cleanup if necessary
    if (_rhiShader)
	{
		_rhiShader.reset();
		_rhiShader = nullptr;
	}
}

// Compilation
bool Shader::Compile()
{
    try
    {
        // Create RHI Shader using RenderGraphBuilder
        //RHIShader = GRHIApi->CreateShader(ShaderSourceCode, ShaderType);
        if (!_rhiShader)
        {
            throw std::runtime_error("Failed to create RHI shader for: " + _name);
        }


        return true;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Shader compilation error: " << e.what() << std::endl;
        return false;
    }
}

void Shader::ProcessMetadataRecursive(
    const ShaderParametersMetadata& metadata,
    const std::string& prefix,
    const ShaderParameterAllocationMap& parameterMap,
    const uint64_t metadataOffset,
    ShaderParameterBindingInfo& outBindings)
{
    for (const auto& member : metadata.GetMembers())
    {
        std::string name = prefix.empty()
            ? member.Name
            : member.Name;

        // =========================
        // 1. Resource（SRV/UAV/Sampler）
        // =========================
        if (member.IsResource())
        {
            auto allocation = parameterMap.FindParameterAllocation(name);
            if (!allocation.has_value())
                continue; // shader没用到
            ShaderParameterBindingInfo::ShaderResourceBinding binding;
            binding.BaseType = member.BaseType;
            binding.BindSlot = allocation->BaseIndex;
            binding.ArraySize = (member.NumElements > 0) ? member.NumElements : 1;
            binding.Offset = member.Offset + metadataOffset;
            outBindings.AddResourceBinding(name, binding);
        }
        // =========================
        // 2. Struct（递归展开）
        // =========================
        else if (member.IsIncludeStruct() || member.IsReferenceStruct())
        {
            ProcessMetadataRecursive(
                *member.StructMetadata,
                "",
                parameterMap,
                member.Offset + metadataOffset,
                outBindings);
        }
        //else if (Member.IsNestedStruct())
        //{
        //    ProcessMetadataRecursive(
        //        *Member.StructMetadata,
        //        Name,
        //        ParameterMap,
        //        Member.Offset + MetadataOffset,
        //        OutBindings);
        //}
        // =========================
        // 3. Uniform（关键）
        // =========================
        else
        {
            // Uniform 是走 cbuffer 的

            auto allocation = parameterMap.FindParameterAllocation(name);

            if (!allocation.has_value())
                continue; // shader没用到

            if (allocation->Type != EShaderParameterType::LooseData &&
                allocation->Type != EShaderParameterType::UniformBuffer)
            {
                continue;
            }

            ShaderParameterBindingInfo::ShaderUniformBinding binding;
            binding.BaseType = member.BaseType;
            binding.BufferIndex = allocation->BufferIndex;
            binding.BaseIndex = allocation->BaseIndex;
            binding.Offset = member.Offset + metadataOffset;
            binding.Size = allocation->Size;

            outBindings.AddUniformBinding(name, binding);
        }
    }
}

void Shader::InitShaderBindings(const ShaderParametersMetadata* metadata, const ShaderParameterAllocationMap& inParameterMap)
{
    if (!metadata) return;

    _bindings = {}; // 清空

    if (!metadata)
        return;

    ProcessMetadataRecursive(
        *metadata,
        "",                     // prefix
        inParameterMap,
        0,
        _bindings);
}

void Shader::InitShaderRHI(ERHIShaderFrequency frequency, const std::vector<char>& shaderSourceCode)
{
    switch (frequency)
    {
    case RHI::ERHIShaderFrequency::Unknown:
        break;
    case RHI::ERHIShaderFrequency::Vertex:
        _rhiShader = GRHIApi->CreateVertexShader(shaderSourceCode);
        break;
    case RHI::ERHIShaderFrequency::Fragment:
        _rhiShader = GRHIApi->CreateFragmentShader(shaderSourceCode);
        break;
    case RHI::ERHIShaderFrequency::Geometry:
        _rhiShader = GRHIApi->CreateGeometryShader(shaderSourceCode);
        break;
    case RHI::ERHIShaderFrequency::Compute:
        _rhiShader = GRHIApi->CreateComputeShader(shaderSourceCode);
        break;
    case RHI::ERHIShaderFrequency::TessControl:
        _rhiShader = GRHIApi->CreateTessControlShader(shaderSourceCode);
        break;
    case RHI::ERHIShaderFrequency::TessEvaluation:
        _rhiShader = GRHIApi->CreateTessEvalShader(shaderSourceCode);
        break;
    case RHI::ERHIShaderFrequency::Mesh:
        _rhiShader = GRHIApi->CreateMeshShader(shaderSourceCode);
        break;
    case RHI::ERHIShaderFrequency::Task:
        _rhiShader = GRHIApi->CreateTaskShader(shaderSourceCode);
        break;
    case RHI::ERHIShaderFrequency::RayGen:
        _rhiShader = GRHIApi->CreateRayGenShader(shaderSourceCode);
        break;
    case RHI::ERHIShaderFrequency::ClosestHit:
        _rhiShader = GRHIApi->CreateCloseHitShader(shaderSourceCode);
        break;
    case RHI::ERHIShaderFrequency::Miss:
        _rhiShader = GRHIApi->CreateMissShader(shaderSourceCode);
        break;
    case RHI::ERHIShaderFrequency::AnyHit:
        _rhiShader = GRHIApi->CreateAnyHitShader(shaderSourceCode);
        break;
    case RHI::ERHIShaderFrequency::Intersection:
        _rhiShader = GRHIApi->CreateIntersectionShader(shaderSourceCode);
        break;
    case RHI::ERHIShaderFrequency::Callable:
        _rhiShader = GRHIApi->CreateCallableShader(shaderSourceCode);
        break;
    default:
        break;
    }

}


} // namespace RenderCore