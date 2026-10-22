#pragma once
#include <string>
#include <vector>

namespace RenderCore {

    class RENDERCORE_API ShaderCompiledBinaryResultPacker {
    public:
        virtual void Depack(const std::vector<char>& packedResult) = 0;
    protected:
        virtual bool Pack(void* packSource, std::vector<char>& packedResultOut) { return true; };
    };

    class RENDERCORE_API HLSLCompiledBinaryResultPacker : public ShaderCompiledBinaryResultPacker
    {
    public:
        struct Header
        {
            RHI::ERHIShaderFrequency Frequency = RHI::ERHIShaderFrequency::Unknown;
            char EntryPoint[32] = "main";
            uint64_t ShaderHash = 0;
        };

        struct PackedCode
        {
            std::vector<char> DXILCode;
            Header HeaderData;
        } DepackedData;

        void Depack(const std::vector<char>& packedResult) override;

    protected:
        bool Pack(void* packSource, std::vector<char>& packedResultOut) override;
        friend class ShaderCompiler;
    };

    class RENDERCORE_API SPIRVCompiledBinaryResultPacker : public ShaderCompiledBinaryResultPacker {
    public:
        enum class ESPIRVShaderResourceType : uint8_t
        {
            Sampler = 0,
            SampledImage = 1,
            StorageImage = 2,
            UniformBuffer = 3,
            StorageBuffer = 4,
            // Typed buffer resources.
            UniformTexelBuffer = 5,    // tN: Buffer<T>
            StorageTexelBuffer = 6,    // uN: RWBuffer<T>

            // Special resource binding categories.
            PushConstant = 7,          // Push constants use offsets rather than descriptor bindings.

            // Additional API resource types.
            AccelerationStructure = 8, // Ray tracing acceleration structure.
            Num = 9

        };

        struct DescriptorBindingInfo
        {
            uint16_t Set = 0;
            uint16_t Binding = 0;
            uint16_t Count = 1;

            ESPIRVShaderResourceType Type;
            char Name[32] = {};
        };

        struct UniformBufferBindingInfo 
        {
			uint16_t Set = 0;
			uint16_t Binding = 0;
			uint16_t Size = 0;
			char Name[32] = {};
        };

        struct PushConstantInfo
        {
            uint16_t Size = 0;
        };

        struct Header
        {
            RHI::ERHIShaderFrequency Frequency = RHI::ERHIShaderFrequency::Unknown;

            char EntryPoint[32] = "main";

            uint64_t ShaderHash = 0;

            std::vector<DescriptorBindingInfo> DescriptorBindings;
            bool bHasPushConstant = false;
            PushConstantInfo PushConstant;
            std::vector<UniformBufferBindingInfo> UniformBufferBindings;
        };

        struct PackedCode {
            std::vector<uint32_t> SpirvCode;
            Header Header;

        } DepackedData;

        void Depack(const std::vector<char>& packedResult) override;
    protected:
        bool Pack(void* packSource, std::vector<char>& packedResultOut) override;
        friend class ShaderCompiler;
    };

    class RENDERCORE_API GLSLCompiledBinaryResultPacker
        : public ShaderCompiledBinaryResultPacker
    {
    public:

        enum class EGLSLShaderResourceType : uint8_t
        {
            Texture = 0,
            Sampler,

            StorageImage,

            UniformBuffer,
            StorageBuffer,

            UniformTexelBuffer,
            StorageTexelBuffer,

            LooseUniform,

            Num
        };

        struct ResourceBindingInfo
        {
            uint32_t Binding = 0;
            uint32_t Count = 1;

            EGLSLShaderResourceType Type =
                EGLSLShaderResourceType::Texture;

            char Name[64] = {};
        };

        struct CombinedBindingInfo {
            uint32_t SamplerBinding = 0;//
			uint32_t TextureBinding = 0;
            uint32_t CombinedBinding = 0;
        };

        // ============================================================
        // Uniform Buffer
        // ============================================================
        struct UniformBufferBindingInfo
        {
            uint32_t Binding = 0;
            uint32_t Size = 0;

            char Name[64] = {};
        };


        // ============================================================
        // Push Constant
        //
        // OpenGL uses uniform buffers for the push constant representation.
        // The shader compiler assigns its UBO binding.
        // Store the resulting binding information here.
        // ============================================================
        struct PushConstantInfo
        {
            uint32_t Size = 0;

            uint32_t Binding = 0;

            bool bIsMappedToUniformBuffer = false;
        };

        // ============================================================
        // Header
        // ============================================================
        struct Header
        {
            RHI::ERHIShaderFrequency Frequency =
                RHI::ERHIShaderFrequency::Unknown;

            char EntryPoint[32] = "main";

            uint64_t ShaderHash = 0;

            // Texture / Sampler / Image / Buffer
            std::vector<ResourceBindingInfo> Resources;

            // UBO
            std::vector<UniformBufferBindingInfo> UniformBuffers;

            //CombinedBinding
            std::vector<CombinedBindingInfo> CombinedBindings;

            // Push constant mapping.
            bool bHasPushConstant = false;
            PushConstantInfo PushConstant;
        };

        // ============================================================
        // Packed Shader
        // ============================================================
        struct PackedCode
        {
            // OpenGL 4.6 can also consume SPIR-V with the required support.
            // This implementation currently uses the following translation path.
            //
            // HLSL -> SPIR-V -> SPIRV-Cross -> GLSL
            //
            // Store the GLSL generated by that path.
            std::string GLSLCode;

            Header HeaderData;
        };

        PackedCode DepackedData;

        void Depack(const std::vector<char>& packedResult) override;

    protected:

        bool Pack(void* packSource, std::vector<char>& packedResultOut) override;

        friend class ShaderCompiler;
    };
}