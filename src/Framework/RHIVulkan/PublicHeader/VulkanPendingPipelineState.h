#pragma once
#include "VulkanDevice.h"
#include <vector>
#include <unordered_map>
#include <memory>
#include <string>
#include "VulkanDescriptorSets.h"
#include "VulkanRenderPass.h"
#include "VulkanResource.h"
#include "VulkanPipelineState.h"
#include "VulkanCommandBuffer.h"
namespace RHIVulkan {
    class VulkanCommandContext;
    struct DescriptorBindingState
    {
        VkDescriptorType Type;
        uint64_t ResourceHash = 0; // Unique resource identity for SRV, UAV and sampler bindings.
        bool bDirty = true;
    };

    struct VulkanPerSetDescriptorState
    {
        VulkanDescriptorWriter Writer;

        VkDescriptorSet DescriptorSet = VK_NULL_HANDLE;

        bool bDirty = true;

        void Reset()
        {
            Writer.Reset();
            DescriptorSet = VK_NULL_HANDLE;
            bDirty = true;
        }
    };

    struct ShaderParameterKey
    {
        ERHIShaderFrequency Frequency;
        uint32_t ParameterId;

        bool operator==(const ShaderParameterKey& other) const
        {
            return Frequency == other.Frequency &&
                ParameterId == other.ParameterId;
        }
    };

    struct ShaderParameterKeyHash
    {
        size_t operator()(const ShaderParameterKey& k) const
        {
            return (size_t)k.Frequency ^ ((size_t)k.ParameterId << 8);
        }
    };

    struct ShaderParameterBinding
    {
        uint32_t SetIndex = 0;
        uint32_t Binding = 0;
    };

    struct PackedUniformBuffer
    {
        std::vector<uint8_t> Data;
        uint64_t GPUBufferOffset = 0;  // GPU缓冲区中的偏移
        bool bDirty = true;  // 是否有待更新的数据
        struct PackedUniformBinding
        {
            uint32_t SetIndex = 0;
            uint32_t Binding = 0;

            uint32_t CPUOffset = 0;
            uint32_t GPUOffset = 0;

            uint32_t Size = 0;
            bool bUseExternalBuffer = false;
        };
        std::vector<PackedUniformBinding> Bindings;
		int GetBindingInfoIndex(uint32_t bufferIndex) const
		{
			for (size_t i = 0; i < Bindings.size(); i++)
			{
				if (Bindings[i].Binding == bufferIndex)
				{
					return static_cast<int>(i);
				}
			}
			return -1; // 没有找到
		}
		PackedUniformBuffer() = default;
        PackedUniformBuffer(size_t inSize)
            : Data(inSize, 0), GPUBufferOffset(0), bDirty(true)
        {
        }
        PackedUniformBuffer(const std::vector<PackedUniformBinding>& bindingInfo)
            : GPUBufferOffset(0), bDirty(true)
        {
			Bindings = bindingInfo;
			size_t totalSize = 0;
			for (auto& binding : Bindings)
			{
                binding.CPUOffset = totalSize;
                binding.GPUOffset = totalSize;
                int result = binding.Size % 64;
				totalSize += result == 0 ? binding.Size : ((binding.Size / 64 )+ 1)* 64;
			}
			Data.resize(totalSize, 0);
        }
        uint8_t* GetData() { return Data.data(); }
        size_t Num() const { return Data.size(); }
        
        void MarkDirty() { bDirty = true; }
        void MarkClean() { bDirty = false; }
        bool IsDirty() const { return bDirty; }
    };


    class VulkanCommonPipelineDescriptorState
    {
    public:

        VulkanCommonPipelineDescriptorState(VulkanDevice* device, VulkanPipelineBase* pipeline,VulkanCommandContext* context)
            : _device(device)
            , _pipeline(pipeline)
            , _context(context)
        {
            _sets.resize(m_MaxDescriptorSets);
			ParsePipelineLayout(*(pipeline->GetLayout()));
        }

        virtual ~VulkanCommonPipelineDescriptorState() = default;

    public:

        void Reset()
        {
            for (auto& set : _sets)
            {
                set.Reset();
            }
        }

    public:

        //---------------------------------------------------------
        // Texture
        //---------------------------------------------------------

        void SetTexture(
            uint32_t setIndex,
            uint32_t binding,
            uint32_t element,
            VulkanTexture* texture)
        {
            auto& set = _sets[setIndex];
            DescriptorBindingState state;
            state.Kind = DescriptorBindingState::EKind::Image;
            state.Type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;

            state.Image.imageView =
                texture->GetImageView();

            state.Image.imageLayout =
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            state.Image.sampler =
                VK_NULL_HANDLE;
            state.Binding = binding;
            state.ElementIndex = element;
            UpdateBindingState(
                set,
                binding,
                element,
                state);
        }
        
		void SetSampler(
			uint32_t setIndex,
			uint32_t binding,
            uint32_t element,
			VulkanSampler* sampler)
		{
			auto& set = _sets[setIndex];
            DescriptorBindingState state;
            state.Kind =
                DescriptorBindingState::EKind::Image;

            state.Type =
                VK_DESCRIPTOR_TYPE_SAMPLER;

            state.Image.sampler =
                sampler->GetSampler();

            state.Image.imageView =
                VK_NULL_HANDLE;

            state.Image.imageLayout =
                VK_IMAGE_LAYOUT_UNDEFINED;
            state.Binding = binding;
            state.ElementIndex = element;
            UpdateBindingState(
                set,
                binding,
                element,
                state);
		}

    public:

        //---------------------------------------------------------
        // SRV
        //---------------------------------------------------------

        void SetSRV(
            uint32_t setIndex,
            uint32_t binding,
            uint32_t element,
            VulkanShaderResourceView* srv)
        {
            auto& set = _sets[setIndex];

            DescriptorBindingState state;

            if (srv->IsTexture())
            {
                state.Kind =
                    DescriptorBindingState::EKind::Image;

                state.Type =
                    srv->GetDescriptorType();

                state.Image.imageView =
                    srv->GetTextureView().View;

                state.Image.imageLayout =
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            }
            else
            {
                auto hasBufferView =
                    srv->GetBufferView().View !=
                    VK_NULL_HANDLE;

                if (!hasBufferView)
                {
                    state.Kind =
                        DescriptorBindingState::EKind::Buffer;

                    state.Type =
                        srv->GetDescriptorType();

                    state.Buffer.buffer =
                        srv->GetBufferView().Buffer;

                    state.Buffer.offset =
                        srv->GetBufferView().Offset;

                    state.Buffer.range =
                        srv->GetBufferView().Size;
                }
                else
                {
                    state.Kind =
                        DescriptorBindingState::EKind::TexelBuffer;

                    state.Type =
                        srv->GetDescriptorType();

                    state.BufferView =
                        srv->GetBufferView().View;
                }
            }
            state.Binding = binding;
            state.ElementIndex = element;
            UpdateBindingState(
                set,
                binding,
                element,
                state);
        }

    public:

        //---------------------------------------------------------
        // UAV
        //---------------------------------------------------------

        void SetUAV(
            uint32_t setIndex,
            uint32_t binding,
            uint32_t element,
            VulkanUnorderedAccessView* uav)
        {
            auto& set = _sets[setIndex];

            DescriptorBindingState state;

            if (uav->IsTexture())
            {
                state.Kind =
                    DescriptorBindingState::EKind::Image;

                state.Type =
                    uav->GetDescriptorType();

                state.Image.imageView =
                    uav->GetTextureView().View;

                state.Image.imageLayout =
                    VK_IMAGE_LAYOUT_GENERAL;
            }
            else
            {
                auto hasBufferView =
                    uav->GetBufferView().View !=
                    VK_NULL_HANDLE;

                if (!hasBufferView)
                {
                    state.Kind =
                        DescriptorBindingState::EKind::Buffer;

                    state.Type =
                        uav->GetDescriptorType();

                    state.Buffer.buffer =
                        uav->GetBufferView().Buffer;

                    state.Buffer.offset =
                        uav->GetBufferView().Offset;

                    state.Buffer.range =
                        uav->GetBufferView().Size;
                }
                else
                {
                    state.Kind =
                        DescriptorBindingState::EKind::TexelBuffer;

                    state.Type =
                        uav->GetDescriptorType();

                    state.BufferView =
                        uav->GetBufferView().View;
                }
            }
            state.Binding = binding;
            state.ElementIndex = element;
            UpdateBindingState(
                set,
                binding,
                element,
                state);

        }

    public:

        //---------------------------------------------------------
        // UniformBuffer
        //---------------------------------------------------------

        void SetUniformBuffer(
            uint32_t setIndex,
            uint32_t binding,
            uint32_t element,
            VkBuffer buffer,
            VkDeviceSize offset,
            VkDeviceSize size)
        {
            auto& set = _sets[setIndex];

            DescriptorBindingState state;

            state.Kind =
                DescriptorBindingState::EKind::Buffer;

            state.Type =
                VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;

            state.Buffer.buffer = buffer;
            state.Buffer.offset = offset;
            state.Buffer.range = size;
            state.Binding = binding;
            state.ElementIndex = element;
            UpdateBindingState(
                set,
                binding,
                element,
                state);
        }

        void SetAccelerationStructure(
            uint32_t setIndex,
            uint32_t binding,
            uint32_t element,
            VulkanRayTracingInstance* accelerationStructure)
        {
            auto& set = _sets[setIndex];

            DescriptorBindingState state;
            state.Kind = DescriptorBindingState::EKind::AccelerationStructure;
            state.Type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
            state.AccelerationStructure = accelerationStructure;
            state.Binding = binding;
            state.ElementIndex = element;
            UpdateBindingState(set, binding, element, state);
        }

    public:
        //
        bool UpdateDescriptorSetsIfDirty()
        {
            bool isUpdate = false;
            VulkanDescriptorWriter batchWriter;
            const auto& sets = _pipeline->GetLayout()->GetInfo().GetAllLayouts();
            for (uint32_t i = 0;
                i < sets.size();
                ++i)
            {
                auto& set = _sets[i];

                //---------------------------------
                // Skip
                //---------------------------------

                if (!set.bDirty)
                {
                    continue;
                }

                set.DescriptorSet =
                    _device
                    ->GetDescriptorSetManager()
                    ->GetDescriptorSet(
                        sets[i]);

                //---------------------------------
                // No Dirty
                //---------------------------------

                if (!set.bDirty)
                    continue;

                //---------------------------------
                // Build Writes
                //---------------------------------

                for (auto& [key, state]
                    : set.BindingStates)
                {
                    switch (state.Kind)
                    {
                    case DescriptorBindingState::EKind::Image:
                        batchWriter.WriteImage(set.DescriptorSet,
                            state.Binding,
                            state.ElementIndex,
                            state.Type,
                            state.Image.imageView,
                            state.Image.imageLayout,
                            state.Image.sampler);
                        break;

                    case DescriptorBindingState::EKind::Buffer:
                        batchWriter.WriteBuffer(
                            set.DescriptorSet,
                            state.Binding,
                            state.ElementIndex,
                            state.Type,
                            state.Buffer.buffer,
                            state.Buffer.offset,
                            state.Buffer.range);
                        break;

                    case DescriptorBindingState::EKind::TexelBuffer:
                        batchWriter.WriteTexelBuffer(
                            set.DescriptorSet,
                            state.Binding,
                            state.ElementIndex,
                            state.Type,
                            state.BufferView);
                        break;

                    case DescriptorBindingState::EKind::AccelerationStructure:
                        batchWriter.WriteAccelerationStructure(
                            set.DescriptorSet,
                            state.Binding,
                            state.ElementIndex,
                            state.Type,
                            state.AccelerationStructure ? state.AccelerationStructure->GetHandle() : VK_NULL_HANDLE);
                        break;
                    }
                }

                set.bDirty = false;
            }

            //---------------------------------
            // Batch Update
            //---------------------------------
            bool isUpdated = batchWriter.IsDirty();
            batchWriter.Update(
                _device->GetHandle());
            return isUpdated;
        }


    public:

        VkDescriptorSet GetDescriptorSet(uint32_t setIndex) const
        {
            return _sets[setIndex].DescriptorSet;
        }

    private:

        static constexpr uint32_t m_MaxDescriptorSets = 8;

    protected:
        struct DescriptorBindingState
        {
            VkDescriptorType Type{};

            enum class EKind
            {
                None,
                Image,
                Buffer,
                TexelBuffer,
                AccelerationStructure
            } Kind = EKind::None;

            VkDescriptorImageInfo Image{};
            VkDescriptorBufferInfo Buffer{};
            VkBufferView BufferView = VK_NULL_HANDLE;
            VulkanRayTracingInstance* AccelerationStructure = nullptr;
            uint32_t Binding = 0;
            uint32_t ElementIndex = 0;

            bool operator==(const DescriptorBindingState& rhs) const
            {
                if (Kind != rhs.Kind || Type != rhs.Type)
                {
                    return false;
                }

                switch (Kind)
                {
                case EKind::Image:
                    return
                        Image.imageView == rhs.Image.imageView &&
                        Image.imageLayout == rhs.Image.imageLayout &&
                        Image.sampler == rhs.Image.sampler &&
                        Binding == rhs.Binding &&
                        ElementIndex == rhs.ElementIndex;

                case EKind::Buffer:
                    return
                        Buffer.buffer == rhs.Buffer.buffer &&
                        Buffer.offset == rhs.Buffer.offset &&
                        Buffer.range == rhs.Buffer.range &&
                        Binding == rhs.Binding &&
                        ElementIndex == rhs.ElementIndex;

                case EKind::TexelBuffer:
                    return BufferView == rhs.BufferView &&
                        Binding == rhs.Binding &&
                        ElementIndex == rhs.ElementIndex;

                case EKind::AccelerationStructure:
                    return AccelerationStructure == rhs.AccelerationStructure &&
                        Binding == rhs.Binding &&
                        ElementIndex == rhs.ElementIndex;

                default:
                    return true;
                }
            }
        };

        inline uint64_t MakeDescriptorKey(uint32_t binding, uint32_t arrayIndex = 0)
        {
            return (static_cast<uint64_t>(binding) << 32) | arrayIndex;
        }
        struct VulkanPerSetDescriptorState
        {

            VkDescriptorSet DescriptorSet = VK_NULL_HANDLE;
            std::unordered_map<uint64_t, DescriptorBindingState> BindingStates;
            bool bDirty = true;

            void Reset()
            {
                DescriptorSet = VK_NULL_HANDLE;
                BindingStates.clear();
                bDirty = true;
            }
        };

        VulkanDevice* _device = nullptr;

        std::vector<VulkanPerSetDescriptorState> _sets;
        VulkanPipelineBase* _pipeline;
        bool UpdateBindingState(
            VulkanPerSetDescriptorState& set,
            uint32_t binding,
            uint32_t element,
            const DescriptorBindingState& newState)
        {
            uint64_t key = MakeDescriptorKey(binding, element);
            auto it = set.BindingStates.find(key);

            if (it != set.BindingStates.end() &&
                it->second == newState)
            {
                return false;
            }

            set.BindingStates[key] = newState;
            set.bDirty = true;

            return true;
        }
    private:
        void ParsePipelineLayout(const VulkanPipelineLayout& layout)
        {
            for (const auto& pair : layout.GetInfo().SetLayoutsByFrequency)
            {
                ERHIShaderFrequency freq = static_cast<ERHIShaderFrequency>(pair.first);
                const PipelineLayoutInfo::ShaderFrequencyLayoutInfo& freqInfo = pair.second;

                // 如果该shader阶段有全局uniform buffer，则创建对应的PackedUniformBuffer
                if (!freqInfo.UniformBufferLayouts.empty()) {
					// 计算该频率的PackedUniformBuffer大小
					std::vector<PackedUniformBuffer::PackedUniformBinding> bindings;
					for (const auto& ubLayout : freqInfo.UniformBufferLayouts)
					{
						PackedUniformBuffer::PackedUniformBinding binding;
                        binding.SetIndex = ubLayout.SetIndex;
						binding.Binding = ubLayout.BindingIndex;
						binding.Size = ubLayout.Size;
						bindings.push_back(binding);
					}
					_packedUniformBuffersByFrequency[freq] = PackedUniformBuffer(bindings);
                }
                // 构建shader参数的binding映射
                for (size_t i = 0; i < freqInfo.ResourceParameterLayouts.size(); ++i)
                {
                    for (const auto& binding : freqInfo.ResourceParameterLayouts)
                    {
                        ShaderParameterKey key{ freq, binding.BindingIndex };
                        ShaderParameterBinding value{ binding.SetIndex, binding.BindingIndex };
                        m_ParameterBindingMap[key] = value;
                    }
                }
            }

        }
        
        struct GlobalUniformBufferDesc
        {
            uint32_t SetIndex;
            uint32_t BindingIndex;
        };
        
        //std::unordered_map<ERHIShaderFrequency, GlobalUniformBufferDesc> GlobalUniformBufferInfo;
        std::unordered_map<
            ShaderParameterKey,
            ShaderParameterBinding,
            ShaderParameterKeyHash> m_ParameterBindingMap;
    public:
        // Expose the descriptor set and binding.
        bool GetBinding(
            ERHIShaderFrequency frequency,
            uint32_t parameterId,
            uint32_t& outSetIndex,
            uint32_t& outBinding) const
        {
            ShaderParameterKey key{ frequency, parameterId };
            auto it = m_ParameterBindingMap.find(key);
            if (it == m_ParameterBindingMap.end())
                return false;

            outSetIndex = it->second.SetIndex;
            outBinding = it->second.Binding;
            return true;
        }

        void FlushAndBind(VulkanCommandBuffer* cmd);
        void MarkUiformBufferUseExternal(ERHIShaderFrequency frequency, uint32_t set, uint32_t binding)
        {
            auto it = _packedUniformBuffersByFrequency.find(frequency);
            if (it == _packedUniformBuffersByFrequency.end())
                return;

            PackedUniformBuffer& buffer = it->second;
            for (auto& bindingInfo : buffer.Bindings)
            {
                if (bindingInfo.SetIndex == set && bindingInfo.Binding == binding)
                {
                    bindingInfo.bUseExternalBuffer = true;
                    return;
                }
            }
        }
    protected:
        std::unordered_map<ERHIShaderFrequency, PackedUniformBuffer> _packedUniformBuffersByFrequency;
        public:
        void SetPackedGlobalParameter(
            ERHIShaderFrequency frequency,
            uint32_t bufferIndex,
            uint32_t byteOffset,
            uint32_t numBytes,
            const void* newValue)
        {
            auto it = _packedUniformBuffersByFrequency.find(frequency);
            if (it == _packedUniformBuffersByFrequency.end())
                return;

            PackedUniformBuffer& buffer = it->second;
            //获取bufferIndex的信息
            int index = buffer.GetBindingInfoIndex(bufferIndex);
            if (index == -1) 
            {
                return;
            }
            const PackedUniformBuffer::PackedUniformBinding& binding = buffer.Bindings[index];
            assert(byteOffset + numBytes <= binding.Size);
            assert((numBytes & 3) == 0 && (byteOffset & 3) == 0);

            uint32_t* dst = reinterpret_cast<uint32_t*>(buffer.GetData() + byteOffset + binding.CPUOffset);
            const uint32_t* src = reinterpret_cast<const uint32_t*>(newValue);
            size_t count = numBytes / 4;

            for (size_t i = 0; i < count; ++i)
            {
                dst[i] = src[i];
            }
            
            // 标记缓冲区为dirty，需要更新到GPU
            buffer.MarkDirty();
        }
    protected:
        VkPipelineBindPoint _pipelineBindingPoint = VK_PIPELINE_BIND_POINT_COMPUTE;
		VulkanCommandContext* _context = nullptr;
    };




    class VulkanComputePipelineDescriptorState
        : public VulkanCommonPipelineDescriptorState
    {
    public:

        VulkanComputePipelineDescriptorState(
            VulkanDevice* device,
            VulkanComputePipelineState* pipeline,
            VulkanCommandContext* context)
            : VulkanCommonPipelineDescriptorState(device, pipeline,context)
            , m_Pipeline(pipeline)
        {
            _pipelineBindingPoint = VK_PIPELINE_BIND_POINT_COMPUTE;
        }
    private:
        VulkanComputePipelineState* m_Pipeline = nullptr;
    };

    class VulkanGraphicsPipelineDescriptorState
        : public VulkanCommonPipelineDescriptorState
    {
    public:
        VulkanGraphicsPipelineDescriptorState(
            VulkanDevice* device,
            VulkanGraphicsPipelineState* pipeline,
            VulkanCommandContext* context)
            : VulkanCommonPipelineDescriptorState(device, pipeline,context)
            , m_Pipeline(pipeline)
        {
            _pipelineBindingPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        }
    private:
        VulkanGraphicsPipelineState* m_Pipeline;
    };

    class VulkanRayTracingPipelineDescriptorState
        : public VulkanCommonPipelineDescriptorState
    {
    public:
        VulkanRayTracingPipelineDescriptorState(
            VulkanDevice* device,
            VulkanRayTracingPipeline* pipeline,
            VulkanCommandContext* context)
            : VulkanCommonPipelineDescriptorState(device, pipeline, context)
            , m_Pipeline(pipeline)
        {
            _pipelineBindingPoint = VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR;
        }
    private:
        VulkanRayTracingPipeline* m_Pipeline;
    };

    class VulkanPendingComputeState
    {
    public:
        VulkanPendingComputeState(VulkanDevice* device, VulkanCommandContext* context)
            : m_Device(device)
			, m_Context(context)
        {
        }

        void Reset()
        {
            m_CurrentPipeline = nullptr;
            m_CurrentState = nullptr;
            m_bDirtyPipelineState = false;
            m_CurrentPipeline = nullptr;
            m_CurrentState = nullptr;
            for (auto& state : m_States) {
                state.second->Reset();
            }
        }

        void SetPipeline(VulkanComputePipelineState* pipeline)
        {
            m_bDirtyPipelineState = true;
            if (m_CurrentPipeline == pipeline)
                return;
           
            m_CurrentPipeline = pipeline;

            auto it = m_States.find(pipeline);
            if (it == m_States.end())
            {
                auto state =
                    std::make_unique<
                    VulkanComputePipelineDescriptorState>(
                        m_Device, pipeline,m_Context);

                m_CurrentState = state.get();
                m_States[pipeline] = std::move(state);
            }
            else
            {
                m_CurrentState = it->second.get();
            }
        }

        void SetTexture(ERHIShaderFrequency frequency, uint32_t parameterId, uint32_t elementId, VulkanTexture* texture)
        {
            if (!m_CurrentState)
                return;

            uint32_t setIndex = 0;
            uint32_t binding = 0;
            if (m_CurrentState->GetBinding(frequency, parameterId, setIndex, binding))
            {
                m_CurrentState->SetTexture(setIndex, binding,elementId, texture);
            }
        }

		void SetSampler(ERHIShaderFrequency frequency, uint32_t parameterId, uint32_t elementId, VulkanSampler* sampler)
		{
			if (!m_CurrentState)
				return;
			uint32_t setIndex = 0;
			uint32_t binding = 0;
			if (m_CurrentState->GetBinding(frequency, parameterId, setIndex, binding))
			{
				m_CurrentState->SetSampler(setIndex, binding,elementId, sampler);
			}
		}


        //--------------------------------------------------------
        // Bind an SRV by shader frequency and parameter ID.
        //--------------------------------------------------------
        void SetSRV(ERHIShaderFrequency frequency, uint32_t parameterId, uint32_t elementId, VulkanShaderResourceView* srv)
        {
            if (!m_CurrentState)
                return;

            uint32_t setIndex = 0;
            uint32_t binding = 0;
            if (m_CurrentState->GetBinding(frequency, parameterId, setIndex, binding))
            {
                m_CurrentState->SetSRV(setIndex, binding,elementId, srv);
            }
        }

        //--------------------------------------------------------
        // Bind a UAV by shader frequency and parameter ID.
        //--------------------------------------------------------
        void SetUAV(ERHIShaderFrequency frequency, uint32_t parameterId, uint32_t elementId, VulkanUnorderedAccessView* uav)
        {
            if (!m_CurrentState)
                return;

            uint32_t setIndex = 0;
            uint32_t binding = 0;
            if (m_CurrentState->GetBinding(frequency, parameterId, setIndex, binding))
            {
                m_CurrentState->SetUAV(setIndex, binding,elementId, uav);
            }
        }

        //--------------------------------------------------------
        // Bind a uniform buffer by shader frequency and parameter ID.
        //--------------------------------------------------------
        void SetUniformBuffer(ERHIShaderFrequency frequency, uint32_t parameterId, uint32_t elementId, VulkanBuffer* uniformBuffer)
        {
            if (!m_CurrentState)
                return;

            uint32_t setIndex = 0;
            uint32_t binding = 0;
            if (m_CurrentState->GetBinding(frequency, parameterId, setIndex, binding))
            {
                m_CurrentState->MarkUiformBufferUseExternal(frequency,setIndex,binding);
                m_CurrentState->SetUniformBuffer(setIndex, binding,elementId, uniformBuffer->GetHandle(), 0, uniformBuffer->GetSize());
            }
        }

        void SetShaderParameter(ERHIShaderFrequency frequency,uint32_t bufferIndex, uint32_t baseIndex, uint32_t numBytes, const void* newValue) {
			if (!m_CurrentState)
				return;
            m_CurrentState->SetPackedGlobalParameter(frequency, bufferIndex, baseIndex, numBytes, newValue);
        }

        bool HasPipeline() const
        {
            return m_CurrentPipeline != nullptr;
        }

        void PrepareForDispatch(VulkanCommandBuffer* cmd)
        {
            if (m_bDirtyPipelineState) {
                m_CurrentPipeline->Bind(cmd);
                m_bDirtyPipelineState = false;
            }
            m_CurrentState->FlushAndBind(cmd);
        }

    private:
        VulkanDevice* m_Device;
        friend class VulkanCommandContext;
        bool m_bDirtyPipelineState = false;
        VkStridedDeviceAddressRegionKHR m_EmptyRegion{};
        VulkanComputePipelineState* m_CurrentPipeline = nullptr;
        VulkanComputePipelineDescriptorState* m_CurrentState = nullptr;
		VulkanCommandContext* m_Context = nullptr;

        std::unordered_map<
            VulkanComputePipelineState*,
            std::unique_ptr<VulkanComputePipelineDescriptorState>> m_States;
    };

    class VulkanPendingGfxState
    {
    public:
        VulkanPendingGfxState(VulkanDevice* device,VulkanCommandContext* context)
            : m_Device(device)
			, m_Context(context)
        {
            Reset();
        }

        void Reset()
        {
            m_Viewports.clear();
            m_Scissors.clear();
            
            m_StencilRef = 0;
            m_PrimitiveType = PT_Num;
            m_bDirtyVertexStreams = true;

            m_CurrentPipeline = nullptr;
            m_CurrentState = nullptr;
            m_bDirtyPipelineState = false;
            m_CurrentPipeline = nullptr;
            m_CurrentState = nullptr;
            for (auto& state : m_States) {
                state.second->Reset();
            }
        }

        // Pipeline
        void SetPipeline(VulkanGraphicsPipelineState* pipeline)
        {
            
            if (m_CurrentPipeline == pipeline)
                return;
            m_bDirtyPipelineState = true;
            m_CurrentPipeline = pipeline;
            auto it = m_States.find(pipeline);
            if (it == m_States.end())
            {
                auto state = std::make_unique<VulkanGraphicsPipelineDescriptorState>(
                    m_Device, pipeline,m_Context);
                m_CurrentState = state.get();
                m_States[pipeline] = std::move(state);
            }
            else
            {
                m_CurrentState = it->second.get();
            }
        }

        // Graphics pipeline resource bindings.
        //--------------------------------------------------------
        // Bind a texture by shader frequency and parameter ID.
        //--------------------------------------------------------
        void SetTexture(ERHIShaderFrequency frequency, uint32_t parameterId, uint32_t elementId, VulkanTexture* texture)
        {
            if (!m_CurrentState)
                return;

            uint32_t setIndex = 0;
            uint32_t binding = 0;
            if (m_CurrentState->GetBinding(frequency, parameterId, setIndex, binding))
            {
                m_CurrentState->SetTexture(setIndex, binding,elementId, texture);
            }
        }

        //--------------------------------------------------------
        // Bind a sampler by shader frequency and parameter ID.
        //--------------------------------------------------------
		void SetSampler(ERHIShaderFrequency frequency, uint32_t parameterId, uint32_t elementId, VulkanSampler* sampler)
		{
			if (!m_CurrentState)
				return;
			uint32_t setIndex = 0;
			uint32_t binding = 0;
			if (m_CurrentState->GetBinding(frequency, parameterId, setIndex, binding))
			{
				m_CurrentState->SetSampler(setIndex, binding,elementId, sampler);
			}
		}

        //--------------------------------------------------------
        // Bind an SRV by shader frequency and parameter ID.
        //--------------------------------------------------------
        void SetSRV(ERHIShaderFrequency frequency, uint32_t parameterId, uint32_t elementId, VulkanShaderResourceView* srv)
        {
            if (!m_CurrentState)
                return;

            uint32_t setIndex = 0;
            uint32_t binding = 0;
            if (m_CurrentState->GetBinding(frequency, parameterId, setIndex, binding))
            {
                m_CurrentState->SetSRV(setIndex, binding,elementId, srv);
            }
        }

        //--------------------------------------------------------
        // Bind a UAV by shader frequency and parameter ID.
        //--------------------------------------------------------
        void SetUAV(ERHIShaderFrequency frequency, uint32_t parameterId, uint32_t elementId, VulkanUnorderedAccessView* uav)
        {
            if (!m_CurrentState)
                return;

            uint32_t setIndex = 0;
            uint32_t binding = 0;
            if (m_CurrentState->GetBinding(frequency, parameterId, setIndex, binding))
            {
                m_CurrentState->SetUAV(setIndex, binding,elementId, uav);
            }
        }

        //--------------------------------------------------------
        // Bind a uniform buffer by shader frequency and parameter ID.
        //--------------------------------------------------------
        void SetUniformBuffer(ERHIShaderFrequency frequency, uint32_t parameterId, uint32_t elementId, VulkanBuffer* uniformBuffer)
        {
            if (!m_CurrentState)
                return;

            uint32_t setIndex = 0;
            uint32_t binding = 0;
            if (m_CurrentState->GetBinding(frequency, parameterId, setIndex, binding))
            {
                m_CurrentState->MarkUiformBufferUseExternal(frequency,setIndex,binding);
                m_CurrentState->SetUniformBuffer(setIndex, binding,elementId, uniformBuffer->GetHandle(), 0, uniformBuffer->GetSize());
            }
        }
        void SetShaderParameter(ERHIShaderFrequency frequency,uint32_t bufferIndex, uint32_t baseIndex, uint32_t numBytes, const void* newValue) {
            if (!m_CurrentState)
                return;
            m_CurrentState->SetPackedGlobalParameter(frequency,bufferIndex, baseIndex, numBytes, newValue);
        }

        bool HasPipeline() const
        {
            return m_CurrentPipeline != nullptr;
        }

        // Dynamic states
        void SetViewport(const VkViewport& viewport)
        {
            m_Viewports.clear();
            auto revertViewport = viewport;
            revertViewport.y =
                revertViewport.y + revertViewport.height;

            revertViewport.height =
                -revertViewport.height;
            m_Viewports.push_back(revertViewport);
            m_bViewportDiry = true;
        }

        void SetScissor(const VkRect2D& scissor)
        {
            m_Scissors.clear();
            m_Scissors.push_back(scissor);
            m_bScissorDirty = true;
        }

        void SetStencilRef(uint32_t ref)
        {
            m_StencilRef = ref;
        }

        void SetVertexStream(uint32_t index, VkBuffer buffer, uint32_t offset)
        {
            m_PendingStreams[index].Stream = buffer;
            m_PendingStreams[index].BufferOffset = offset;
            m_bDirtyVertexStreams = true;
        }

        // Prepare pipeline and resources before drawing.
        void PrepareForDraw(VulkanCommandBuffer* cmd)
        {
            if (m_bDirtyPipelineState) {
                // 1. Bind pipeline
                m_CurrentPipeline->Bind(cmd);
                m_bDirtyPipelineState = false;
            }
            // 2. Bind descriptor sets
            m_CurrentState->FlushAndBind(cmd);
            // 3. Bind vertex buffers
            if (m_bDirtyVertexStreams)
            {
                for (uint32_t i = 0; i < m_MaxVertexElementCount; ++i)
                {
                    auto& stream = m_PendingStreams[i];
                    if (stream.Stream != VK_NULL_HANDLE)
                    {
                        VkDeviceSize offsets[1] = { stream.BufferOffset };
                        VKFunc::CmdBindVertexBuffers(cmd->GetHandle(), i, 1, &stream.Stream, offsets);
                    }
                }
                m_bDirtyVertexStreams = false;
            }

            // 4. Set viewport / scissor
            if (!m_Viewports.empty() && m_bViewportDiry)
            {
                VKFunc::CmdSetViewport(cmd->GetHandle(), 0, (uint32_t)m_Viewports.size(), m_Viewports.data());
				m_bViewportDiry = false;
            }
                

            if (!m_Scissors.empty() && m_bScissorDirty)
            {
                VKFunc::CmdSetScissor(cmd->GetHandle(), 0, (uint32_t)m_Scissors.size(), m_Scissors.data());
				m_bScissorDirty = false;
            }
                

            // 5. Set stencil ref
            //vkCmdSetStencilReference(cmd->GetHandle(), VK_STENCIL_FRONT_AND_BACK, StencilRef);
        }

    private:
        friend class VulkanCommandContext;
        VulkanDevice* m_Device;
        bool m_bDirtyPipelineState = false;
        VulkanGraphicsPipelineState* m_CurrentPipeline = nullptr;
        VulkanGraphicsPipelineDescriptorState* m_CurrentState = nullptr;

        std::unordered_map<VulkanGraphicsPipelineState*, std::unique_ptr<VulkanGraphicsPipelineDescriptorState>> m_States;

        // Dynamic states
        std::vector<VkViewport> m_Viewports;
        std::vector<VkRect2D> m_Scissors;
        bool m_bViewportDiry = false;
        bool m_bScissorDirty = false;
        uint32_t m_StencilRef = 0;

        enum EPrimitiveType { PT_Num } m_PrimitiveType;

        struct FVertexStream
        {
            VkBuffer Stream = VK_NULL_HANDLE;
            uint32_t BufferOffset = 0;
        };
        constexpr static uint32_t m_MaxVertexElementCount = 8;
        FVertexStream m_PendingStreams[m_MaxVertexElementCount];
        bool m_bDirtyVertexStreams = true;
		VulkanCommandContext* m_Context = nullptr;
    };

    class VulkanPendingRayTracingState
    {
    public:
        VulkanPendingRayTracingState(VulkanDevice* device, VulkanCommandContext* context)
            : m_Device(device)
            , m_Context(context)
        {
        }

        void Reset()
        {
            m_CurrentPipeline = nullptr;
            m_CurrentState = nullptr;
            m_RetiredScratchBuffers.clear();
            m_CurrentAccelerationStructure = nullptr;
            m_ScratchBuffer.reset();
            m_ScratchBufferSize = 0;
        }

        void SetPipeline(VulkanRayTracingPipeline* pipeline)
        {
            m_CurrentPipeline = pipeline;
            auto it = m_States.find(pipeline);
            if (it == m_States.end())
            {
                auto state = std::make_unique<VulkanRayTracingPipelineDescriptorState>(
                    m_Device, pipeline, m_Context);
                m_CurrentState = state.get();
                m_States[pipeline] = std::move(state);
            }
            else
            {
                m_CurrentState = it->second.get();
            }
        }

        // Ray tracing pipeline resource bindings.
//--------------------------------------------------------
// Bind a texture by shader frequency and parameter ID.
//--------------------------------------------------------
        void SetTexture(ERHIShaderFrequency frequency, uint32_t parameterId, uint32_t elementId, VulkanTexture* texture)
        {
            if (!m_CurrentState)
                return;

            uint32_t setIndex = 0;
            uint32_t binding = 0;
            if (m_CurrentState->GetBinding(frequency, parameterId, setIndex, binding))
            {
                m_CurrentState->SetTexture(setIndex, binding, elementId, texture);
            }
        }

        //--------------------------------------------------------
        // Bind a sampler by shader frequency and parameter ID.
        //--------------------------------------------------------
        void SetSampler(ERHIShaderFrequency frequency, uint32_t parameterId, uint32_t elementId, VulkanSampler* sampler)
        {
            if (!m_CurrentState)
                return;
            uint32_t setIndex = 0;
            uint32_t binding = 0;
            if (m_CurrentState->GetBinding(frequency, parameterId, setIndex, binding))
            {
                m_CurrentState->SetSampler(setIndex, binding, elementId, sampler);
            }
        }

        //--------------------------------------------------------
        // Bind an SRV by shader frequency and parameter ID.
        //--------------------------------------------------------
        void SetSRV(ERHIShaderFrequency frequency, uint32_t parameterId, uint32_t elementId, VulkanShaderResourceView* srv)
        {
            if (!m_CurrentState)
                return;

            uint32_t setIndex = 0;
            uint32_t binding = 0;
            if (m_CurrentState->GetBinding(frequency, parameterId, setIndex, binding))
            {
                m_CurrentState->SetSRV(setIndex, binding, elementId, srv);
            }
        }

        //--------------------------------------------------------
        // Bind a UAV by shader frequency and parameter ID.
        //--------------------------------------------------------
        void SetUAV(ERHIShaderFrequency frequency, uint32_t parameterId, uint32_t elementId, VulkanUnorderedAccessView* uav)
        {
            if (!m_CurrentState)
                return;

            uint32_t setIndex = 0;
            uint32_t binding = 0;
            if (m_CurrentState->GetBinding(frequency, parameterId, setIndex, binding))
            {
                m_CurrentState->SetUAV(setIndex, binding, elementId, uav);
            }
        }

        //--------------------------------------------------------
        // Bind a uniform buffer by shader frequency and parameter ID.
        //--------------------------------------------------------
        void SetUniformBuffer(ERHIShaderFrequency frequency, uint32_t parameterId, uint32_t elementId, VulkanBuffer* uniformBuffer)
        {
            if (!m_CurrentState)
                return;

            uint32_t setIndex = 0;
            uint32_t binding = 0;
            if (m_CurrentState->GetBinding(frequency, parameterId, setIndex, binding))
            {
                m_CurrentState->MarkUiformBufferUseExternal(frequency, setIndex, binding);
                m_CurrentState->SetUniformBuffer(setIndex, binding, elementId, uniformBuffer->GetHandle(), 0, uniformBuffer->GetSize());
            }
        }
        void SetShaderParameter(ERHIShaderFrequency frequency, uint32_t bufferIndex, uint32_t baseIndex, uint32_t numBytes, const void* newValue) {
            if (!m_CurrentState)
                return;
            m_CurrentState->SetPackedGlobalParameter(frequency, bufferIndex, baseIndex, numBytes, newValue);
        }

        void SetAccelerationStructure(VulkanRayTracingInstance* accelerationStructure)
        {
            m_CurrentAccelerationStructure = accelerationStructure;
        }

        void SetAccelerationStructure(ERHIShaderFrequency frequency, uint32_t parameterId, uint32_t elementId, VulkanRayTracingInstance* accelerationStructure)
        {
            if (!m_CurrentState)
                return;

            uint32_t setIndex = 0;
            uint32_t binding = 0;
            if (m_CurrentState->GetBinding(frequency, parameterId, setIndex, binding))
            {
                m_CurrentState->SetAccelerationStructure(setIndex, binding, elementId, accelerationStructure);
            }
        }

        bool HasPipeline() const
        {
            return m_CurrentPipeline != nullptr;
        }

        bool HasAccelerationStructure() const
        {
            return m_CurrentAccelerationStructure != nullptr;
        }

        const VkStridedDeviceAddressRegionKHR& GetRayGenShaderBindingTableRegion() const
        {
            return m_CurrentPipeline ? m_CurrentPipeline->GetRayGenShaderBindingTableRegion() : m_EmptyRegion;
        }

        const VkStridedDeviceAddressRegionKHR& GetMissShaderBindingTableRegion() const
        {
            return m_CurrentPipeline ? m_CurrentPipeline->GetMissShaderBindingTableRegion() : m_EmptyRegion;
        }

        const VkStridedDeviceAddressRegionKHR& GetHitShaderBindingTableRegion() const
        {
            return m_CurrentPipeline ? m_CurrentPipeline->GetHitShaderBindingTableRegion() : m_EmptyRegion;
        }

        const VkStridedDeviceAddressRegionKHR& GetCallableShaderBindingTableRegion() const
        {
            return m_CurrentPipeline ? m_CurrentPipeline->GetCallableShaderBindingTableRegion() : m_EmptyRegion;
        }

        void PrepareForTraceRays(VulkanCommandBuffer* cmd)
        {
            if (!cmd || !m_CurrentPipeline)
            {
                return;
            }

            VKFunc::CmdBindPipeline(
                cmd->GetHandle(),
                VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
                m_CurrentPipeline->GetHandle());
            // 2. Bind descriptor sets
            m_CurrentState->FlushAndBind(cmd);
        }

        void BuildAccelerationStructure(VulkanCommandBuffer* cmd, RHIRayTracingAccelerationStructure* accelerationStructure, bool bUpdate)
        {
            if (!cmd || !accelerationStructure || !m_Device)
            {
                return;
            }

            VulkanRayTracingGeometry* blas = dynamic_cast<VulkanRayTracingGeometry*>(accelerationStructure);
            VulkanRayTracingInstance* tlas = dynamic_cast<VulkanRayTracingInstance*>(accelerationStructure);
            if (!blas && !tlas)
            {
                return;
            }

            VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
            buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
            buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
            buildInfo.mode = bUpdate ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR : VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
            buildInfo.geometryCount = 1;

            const VkAccelerationStructureBuildRangeInfoKHR* pRangeInfo = nullptr;
            VkDeviceSize requiredScratchSize = 0;

            if (blas)
            {
                buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
                buildInfo.flags = blas->GetBuildFlags();
                buildInfo.dstAccelerationStructure = blas->GetHandle();
                buildInfo.srcAccelerationStructure = bUpdate ? blas->GetHandle() : VK_NULL_HANDLE;
                buildInfo.pGeometries = &blas->GetGeometryInfo();
                pRangeInfo = &blas->GetBuildRangeInfo();
                requiredScratchSize = bUpdate ? blas->GetSizeInfo().UpdateScratchSize : blas->GetSizeInfo().BuildScratchSize;
            }
            else
            {
                buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
                buildInfo.flags |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
                buildInfo.dstAccelerationStructure = tlas->GetHandle();
                buildInfo.srcAccelerationStructure = bUpdate ? tlas->GetHandle() : VK_NULL_HANDLE;
                buildInfo.pGeometries = &tlas->GetGeometryInfo();
                pRangeInfo = &tlas->GetBuildRangeInfo();
                requiredScratchSize = bUpdate ? tlas->GetSizeInfo().UpdateScratchSize : tlas->GetSizeInfo().BuildScratchSize;
            }

            if (requiredScratchSize == 0 || !pRangeInfo)
            {
                return;
            }

            if (bUpdate && !(buildInfo.flags & VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR))
                throw std::runtime_error("Acceleration structure was not created with AllowUpdate");
            VkPhysicalDeviceAccelerationStructurePropertiesKHR asProperties{
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR };
            VkPhysicalDeviceProperties2 properties{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
            properties.pNext = &asProperties;
            VKFunc::GetPhysicalDeviceProperties2(m_Device->GetPhysicalDevice(), &properties);
            const VkDeviceSize alignment = asProperties.minAccelerationStructureScratchOffsetAlignment;
            EnsureScratchBuffer(requiredScratchSize + alignment - 1);
            if (!m_ScratchBuffer)
            {
                return;
            }

            VkBufferDeviceAddressInfo addressInfo{};
            addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
            addressInfo.buffer = m_ScratchBuffer->GetHandle();
            buildInfo.scratchData.deviceAddress = VKFunc::GetBufferDeviceAddress(m_Device->GetHandle(), &addressInfo);
            if (buildInfo.scratchData.deviceAddress == 0)
            {
                return;
            }

            buildInfo.scratchData.deviceAddress =
                (buildInfo.scratchData.deviceAddress + alignment - 1) / alignment * alignment;
            // Serial builds may reuse the same scratch range in this command buffer.
            VkMemoryBarrier scratchBarrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
            scratchBarrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
            scratchBarrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
            VKFunc::CmdPipelineBarrier(cmd->GetHandle(), VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, 0, 1, &scratchBarrier, 0, nullptr, 0, nullptr);
            const VkAccelerationStructureBuildRangeInfoKHR* buildRangeInfos[] = { pRangeInfo };
            VKFunc::CmdBuildAccelerationStructuresKHR(
                cmd->GetHandle(),
                1,
                &buildInfo,
                buildRangeInfos);
        }

    private:
        void EnsureScratchBuffer(VkDeviceSize requiredSize)
        {
            if (m_ScratchBuffer && m_ScratchBufferSize >= requiredSize)
            {
                return;
            }

            RHIBufferDesc desc{};
            desc.Size = requiredSize;
            desc.Stride = 0;
            desc.Usage = ERHIBufferUsageFlag::RawBuffer |
                ERHIBufferUsageFlag::UnorderedAccess |
                ERHIBufferUsageFlag::ShaderResource |
                ERHIBufferUsageFlag::TransferSrc |
                ERHIBufferUsageFlag::TransferDst;
            desc.bCPUAccessible = false;
            desc.InitialQueueType = EQueueType::Graphics;

            if (m_ScratchBuffer) m_RetiredScratchBuffers.push_back(m_ScratchBuffer);
            m_ScratchBuffer = std::make_shared<VulkanBuffer>(m_Device, desc);
            m_ScratchBufferSize = requiredSize;
        }

    private:
        VulkanDevice* m_Device = nullptr;
        VulkanCommandContext* m_Context = nullptr;
        VulkanRayTracingPipeline* m_CurrentPipeline = nullptr;
        VulkanRayTracingPipelineDescriptorState* m_CurrentState = nullptr;
        VulkanRayTracingInstance* m_CurrentAccelerationStructure = nullptr;
		VkStridedDeviceAddressRegionKHR m_EmptyRegion{};
        std::unordered_map<VulkanRayTracingPipeline*, std::unique_ptr<VulkanRayTracingPipelineDescriptorState>> m_States;
        std::shared_ptr < VulkanBuffer> m_ScratchBuffer;
        VkDeviceSize m_ScratchBufferSize = 0;
        std::vector<std::shared_ptr<VulkanBuffer>> m_RetiredScratchBuffers;
    };

}