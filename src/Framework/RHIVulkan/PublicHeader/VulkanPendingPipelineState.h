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
        uint64_t ResourceHash = 0; // SRV/UAV/Sampler Ψһ��ʶ
        bool Dirty = true;
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
		int GetBindingInfoIndex(uint32_t BufferIndex) const
		{
			for (size_t i = 0; i < Bindings.size(); i++)
			{
				if (Bindings[i].Binding == BufferIndex)
				{
					return static_cast<int>(i);
				}
			}
			return -1; // 没有找到
		}
		PackedUniformBuffer() = default;
        PackedUniformBuffer(size_t InSize)
            : Data(InSize, 0), GPUBufferOffset(0), bDirty(true)
        {
        }
        PackedUniformBuffer(const std::vector<PackedUniformBinding>& BindingInfo)
            : GPUBufferOffset(0), bDirty(true)
        {
			Bindings = BindingInfo;
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
            : Device(device)
            , pipeline(pipeline)
            , Context(context)
        {
            Sets.resize(MaxDescriptorSets);
			ParsePipelineLayout(*(pipeline->GetLayout()));
        }

        virtual ~VulkanCommonPipelineDescriptorState() = default;

    public:

        void Reset()
        {
            for (auto& set : Sets)
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
            auto& set = Sets[setIndex];
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
			auto& set = Sets[setIndex];
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
            auto& set = Sets[setIndex];

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
            auto& set = Sets[setIndex];

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
            auto& set = Sets[setIndex];

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
            auto& set = Sets[setIndex];

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
            const auto& sets = pipeline->GetLayout()->GetInfo().GetAllLayouts();
            for (uint32_t i = 0;
                i < sets.size();
                ++i)
            {
                auto& set = Sets[i];

                //---------------------------------
                // Skip
                //---------------------------------

                if (!set.bDirty)
                {
                    continue;
                }

                set.DescriptorSet =
                    Device
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
                Device->GetHandle());
            return isUpdated;
        }


    public:

        VkDescriptorSet GetDescriptorSet(uint32_t setIndex) const
        {
            return Sets[setIndex].DescriptorSet;
        }

    private:

        static constexpr uint32_t MaxDescriptorSets = 8;

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

        VulkanDevice* Device = nullptr;

        std::vector<VulkanPerSetDescriptorState> Sets;
        VulkanPipelineBase* pipeline;
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
            for (const auto& pair : layout.GetInfo().setLayoutsByFrequency)
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
					PackedUniformBuffersByFrequency[freq] = PackedUniformBuffer(bindings);
                }
                // 构建shader参数的binding映射
                for (size_t i = 0; i < freqInfo.ResourceParameterLayouts.size(); ++i)
                {
                    for (const auto& binding : freqInfo.ResourceParameterLayouts)
                    {
                        ShaderParameterKey key{ freq, binding.BindingIndex };
                        ShaderParameterBinding value{ binding.SetIndex, binding.BindingIndex };
                        ParameterBindingMap[key] = value;
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
            ShaderParameterKeyHash> ParameterBindingMap;
    public:
        // �ⲿ��ȡ set + binding
        bool GetBinding(
            ERHIShaderFrequency frequency,
            uint32_t parameterId,
            uint32_t& outSetIndex,
            uint32_t& outBinding) const
        {
            ShaderParameterKey key{ frequency, parameterId };
            auto it = ParameterBindingMap.find(key);
            if (it == ParameterBindingMap.end())
                return false;

            outSetIndex = it->second.SetIndex;
            outBinding = it->second.Binding;
            return true;
        }

        void FlushAndBind(VulkanCommandBuffer* cmd);
        void MarkUiformBufferUseExternal(ERHIShaderFrequency frequency, uint32_t set, uint32_t binding)
        {
            auto it = PackedUniformBuffersByFrequency.find(frequency);
            if (it == PackedUniformBuffersByFrequency.end())
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
        std::unordered_map<ERHIShaderFrequency, PackedUniformBuffer> PackedUniformBuffersByFrequency;
        public:
        void SetPackedGlobalParameter(
            ERHIShaderFrequency frequency,
            uint32_t BufferIndex,
            uint32_t ByteOffset,
            uint32_t NumBytes,
            const void* NewValue)
        {
            auto it = PackedUniformBuffersByFrequency.find(frequency);
            if (it == PackedUniformBuffersByFrequency.end())
                return;

            PackedUniformBuffer& buffer = it->second;
            //获取bufferIndex的信息
            int index = buffer.GetBindingInfoIndex(BufferIndex);
            if (index == -1) 
            {
                return;
            }
            const PackedUniformBuffer::PackedUniformBinding& binding = buffer.Bindings[index];
            assert(ByteOffset + NumBytes <= binding.Size);
            assert((NumBytes & 3) == 0 && (ByteOffset & 3) == 0);

            uint32_t* dst = reinterpret_cast<uint32_t*>(buffer.GetData() + ByteOffset + binding.CPUOffset);
            const uint32_t* src = reinterpret_cast<const uint32_t*>(NewValue);
            size_t count = NumBytes / 4;

            for (size_t i = 0; i < count; ++i)
            {
                dst[i] = src[i];
            }
            
            // 标记缓冲区为dirty，需要更新到GPU
            buffer.MarkDirty();
        }
    protected:
        VkPipelineBindPoint pipelineBindingPoint = VK_PIPELINE_BIND_POINT_COMPUTE;
		VulkanCommandContext* Context = nullptr;
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
            , Pipeline(pipeline)
        {
            pipelineBindingPoint = VK_PIPELINE_BIND_POINT_COMPUTE;
        }
    private:
        VulkanComputePipelineState* Pipeline = nullptr;
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
            , Pipeline(pipeline)
        {
            pipelineBindingPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        }
    private:
        VulkanGraphicsPipelineState* Pipeline;
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
            , Pipeline(pipeline)
        {
            pipelineBindingPoint = VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR;
        }
    private:
        VulkanRayTracingPipeline* Pipeline;
    };

    class VulkanPendingComputeState
    {
    public:
        VulkanPendingComputeState(VulkanDevice* device, VulkanCommandContext* context)
            : Device(device)
			, Context(context)
        {
        }

        void Reset()
        {
            CurrentPipeline = nullptr;
            CurrentState = nullptr;
            bDirtyPipelineState = false;
            CurrentPipeline = nullptr;
            CurrentState = nullptr;
            for (auto& state : States) {
                state.second->Reset();
            }
        }

        void SetPipeline(VulkanComputePipelineState* pipeline)
        {
            bDirtyPipelineState = true;
            if (CurrentPipeline == pipeline)
                return;
           
            CurrentPipeline = pipeline;

            auto it = States.find(pipeline);
            if (it == States.end())
            {
                auto state =
                    std::make_unique<
                    VulkanComputePipelineDescriptorState>(
                        Device, pipeline,Context);

                CurrentState = state.get();
                States[pipeline] = std::move(state);
            }
            else
            {
                CurrentState = it->second.get();
            }
        }

        void SetTexture(ERHIShaderFrequency frequency, uint32_t parameterId, uint32_t elementId, VulkanTexture* texture)
        {
            if (!CurrentState)
                return;

            uint32_t setIndex = 0;
            uint32_t binding = 0;
            if (CurrentState->GetBinding(frequency, parameterId, setIndex, binding))
            {
                CurrentState->SetTexture(setIndex, binding,elementId, texture);
            }
        }

		void SetSampler(ERHIShaderFrequency frequency, uint32_t parameterId, uint32_t elementId, VulkanSampler* sampler)
		{
			if (!CurrentState)
				return;
			uint32_t setIndex = 0;
			uint32_t binding = 0;
			if (CurrentState->GetBinding(frequency, parameterId, setIndex, binding))
			{
				CurrentState->SetSampler(setIndex, binding,elementId, sampler);
			}
		}


        //--------------------------------------------------------
        // ͨ�� ShaderFrequency + parameterId ���� SRV
        //--------------------------------------------------------
        void SetSRV(ERHIShaderFrequency frequency, uint32_t parameterId, uint32_t elementId, VulkanShaderResourceView* srv)
        {
            if (!CurrentState)
                return;

            uint32_t setIndex = 0;
            uint32_t binding = 0;
            if (CurrentState->GetBinding(frequency, parameterId, setIndex, binding))
            {
                CurrentState->SetSRV(setIndex, binding,elementId, srv);
            }
        }

        //--------------------------------------------------------
        // ͨ�� ShaderFrequency + parameterId ���� UAV
        //--------------------------------------------------------
        void SetUAV(ERHIShaderFrequency frequency, uint32_t parameterId, uint32_t elementId, VulkanUnorderedAccessView* uav)
        {
            if (!CurrentState)
                return;

            uint32_t setIndex = 0;
            uint32_t binding = 0;
            if (CurrentState->GetBinding(frequency, parameterId, setIndex, binding))
            {
                CurrentState->SetUAV(setIndex, binding,elementId, uav);
            }
        }

        //--------------------------------------------------------
        // ͨ�� ShaderFrequency + parameterId ���� UniformBuffer
        //--------------------------------------------------------
        void SetUniformBuffer(ERHIShaderFrequency frequency, uint32_t parameterId, uint32_t elementId, VulkanBuffer* uniformBuffer)
        {
            if (!CurrentState)
                return;

            uint32_t setIndex = 0;
            uint32_t binding = 0;
            if (CurrentState->GetBinding(frequency, parameterId, setIndex, binding))
            {
                CurrentState->MarkUiformBufferUseExternal(frequency,setIndex,binding);
                CurrentState->SetUniformBuffer(setIndex, binding,elementId, uniformBuffer->GetHandle(), 0, uniformBuffer->GetSize());
            }
        }

        void SetShaderParameter(ERHIShaderFrequency frequency,uint32_t BufferIndex, uint32_t BaseIndex, uint32_t NumBytes, const void* NewValue) {
			if (!CurrentState)
				return;
            CurrentState->SetPackedGlobalParameter(frequency, BufferIndex, BaseIndex, NumBytes, NewValue);
        }

        bool HasPipeline() const
        {
            return CurrentPipeline != nullptr;
        }

        void PrepareForDispatch(VulkanCommandBuffer* cmd)
        {
            if (bDirtyPipelineState) {
                CurrentPipeline->Bind(cmd);
                bDirtyPipelineState = false;
            }
            CurrentState->FlushAndBind(cmd);
        }

    private:
        VulkanDevice* Device;
        friend class VulkanCommandContext;
        bool bDirtyPipelineState = false;
        VkStridedDeviceAddressRegionKHR EmptyRegion{};
        VulkanComputePipelineState* CurrentPipeline = nullptr;
        VulkanComputePipelineDescriptorState* CurrentState = nullptr;
		VulkanCommandContext* Context = nullptr;

        std::unordered_map<
            VulkanComputePipelineState*,
            std::unique_ptr<VulkanComputePipelineDescriptorState>> States;
    };

    class VulkanPendingGfxState
    {
    public:
        VulkanPendingGfxState(VulkanDevice* device,VulkanCommandContext* context)
            : Device(device)
			, Context(context)
        {
            Reset();
        }

        void Reset()
        {
            Viewports.clear();
            Scissors.clear();
            
            StencilRef = 0;
            PrimitiveType = PT_Num;
            bDirtyVertexStreams = true;

            CurrentPipeline = nullptr;
            CurrentState = nullptr;
            bDirtyPipelineState = false;
            CurrentPipeline = nullptr;
            CurrentState = nullptr;
            for (auto& state : States) {
                state.second->Reset();
            }
        }

        // Pipeline
        void SetPipeline(VulkanGraphicsPipelineState* pipeline)
        {
            
            if (CurrentPipeline == pipeline)
                return;
            bDirtyPipelineState = true;
            CurrentPipeline = pipeline;
            auto it = States.find(pipeline);
            if (it == States.end())
            {
                auto state = std::make_unique<VulkanGraphicsPipelineDescriptorState>(
                    Device, pipeline,Context);
                CurrentState = state.get();
                States[pipeline] = std::move(state);
            }
            else
            {
                CurrentState = it->second.get();
            }
        }

        // �� VulkanPendingGfxState ��
        //--------------------------------------------------------
        // ʹ�� ShaderFrequency + parameterId ���� Texture
        //--------------------------------------------------------
        void SetTexture(ERHIShaderFrequency frequency, uint32_t parameterId, uint32_t elementId, VulkanTexture* texture)
        {
            if (!CurrentState)
                return;

            uint32_t setIndex = 0;
            uint32_t binding = 0;
            if (CurrentState->GetBinding(frequency, parameterId, setIndex, binding))
            {
                CurrentState->SetTexture(setIndex, binding,elementId, texture);
            }
        }

        //--------------------------------------------------------
        // ʹ�� ShaderFrequency + parameterId ���� sampler
        //--------------------------------------------------------
		void SetSampler(ERHIShaderFrequency frequency, uint32_t parameterId, uint32_t elementId, VulkanSampler* sampler)
		{
			if (!CurrentState)
				return;
			uint32_t setIndex = 0;
			uint32_t binding = 0;
			if (CurrentState->GetBinding(frequency, parameterId, setIndex, binding))
			{
				CurrentState->SetSampler(setIndex, binding,elementId, sampler);
			}
		}

        //--------------------------------------------------------
        // ʹ�� ShaderFrequency + parameterId ���� SRV
        //--------------------------------------------------------
        void SetSRV(ERHIShaderFrequency frequency, uint32_t parameterId, uint32_t elementId, VulkanShaderResourceView* srv)
        {
            if (!CurrentState)
                return;

            uint32_t setIndex = 0;
            uint32_t binding = 0;
            if (CurrentState->GetBinding(frequency, parameterId, setIndex, binding))
            {
                CurrentState->SetSRV(setIndex, binding,elementId, srv);
            }
        }

        //--------------------------------------------------------
        // ʹ�� ShaderFrequency + parameterId ���� UAV
        //--------------------------------------------------------
        void SetUAV(ERHIShaderFrequency frequency, uint32_t parameterId, uint32_t elementId, VulkanUnorderedAccessView* uav)
        {
            if (!CurrentState)
                return;

            uint32_t setIndex = 0;
            uint32_t binding = 0;
            if (CurrentState->GetBinding(frequency, parameterId, setIndex, binding))
            {
                CurrentState->SetUAV(setIndex, binding,elementId, uav);
            }
        }

        //--------------------------------------------------------
        // ʹ�� ShaderFrequency + parameterId ���� UniformBuffer
        //--------------------------------------------------------
        void SetUniformBuffer(ERHIShaderFrequency frequency, uint32_t parameterId, uint32_t elementId, VulkanBuffer* uniformBuffer)
        {
            if (!CurrentState)
                return;

            uint32_t setIndex = 0;
            uint32_t binding = 0;
            if (CurrentState->GetBinding(frequency, parameterId, setIndex, binding))
            {
                CurrentState->MarkUiformBufferUseExternal(frequency,setIndex,binding);
                CurrentState->SetUniformBuffer(setIndex, binding,elementId, uniformBuffer->GetHandle(), 0, uniformBuffer->GetSize());
            }
        }
        void SetShaderParameter(ERHIShaderFrequency frequency,uint32_t BufferIndex, uint32_t BaseIndex, uint32_t NumBytes, const void* NewValue) {
            if (!CurrentState)
                return;
            CurrentState->SetPackedGlobalParameter(frequency,BufferIndex, BaseIndex, NumBytes, NewValue);
        }

        bool HasPipeline() const
        {
            return CurrentPipeline != nullptr;
        }

        // Dynamic states
        void SetViewport(const VkViewport& viewport)
        {
            Viewports.clear();
            auto revertViewport = viewport;
            revertViewport.y =
                revertViewport.y + revertViewport.height;

            revertViewport.height =
                -revertViewport.height;
            Viewports.push_back(revertViewport);
            ViewportDiry = true;
        }

        void SetScissor(const VkRect2D& scissor)
        {
            Scissors.clear();
            Scissors.push_back(scissor);
            ScissorDirty = true;
        }

        void SetStencilRef(uint32_t ref)
        {
            StencilRef = ref;
        }

        void SetVertexStream(uint32_t index, VkBuffer buffer, uint32_t offset)
        {
            PendingStreams[index].Stream = buffer;
            PendingStreams[index].BufferOffset = offset;
            bDirtyVertexStreams = true;
        }

        // Drawǰ��
        void PrepareForDraw(VulkanCommandBuffer* cmd)
        {
            if (bDirtyPipelineState) {
                // 1. Bind pipeline
                CurrentPipeline->Bind(cmd);
                bDirtyPipelineState = false;
            }
            // 2. Bind descriptor sets
            CurrentState->FlushAndBind(cmd);
            // 3. Bind vertex buffers
            if (bDirtyVertexStreams)
            {
                for (uint32_t i = 0; i < MaxVertexElementCount; ++i)
                {
                    auto& stream = PendingStreams[i];
                    if (stream.Stream != VK_NULL_HANDLE)
                    {
                        VkDeviceSize offsets[1] = { stream.BufferOffset };
                        VKFunc::CmdBindVertexBuffers(cmd->GetHandle(), i, 1, &stream.Stream, offsets);
                    }
                }
                bDirtyVertexStreams = false;
            }

            // 4. Set viewport / scissor
            if (!Viewports.empty() && ViewportDiry)
            {
                VKFunc::CmdSetViewport(cmd->GetHandle(), 0, (uint32_t)Viewports.size(), Viewports.data());
				ViewportDiry = false;
            }
                

            if (!Scissors.empty() && ScissorDirty)
            {
                VKFunc::CmdSetScissor(cmd->GetHandle(), 0, (uint32_t)Scissors.size(), Scissors.data());
				ScissorDirty = false;
            }
                

            // 5. Set stencil ref
            //vkCmdSetStencilReference(cmd->GetHandle(), VK_STENCIL_FRONT_AND_BACK, StencilRef);
        }

    private:
        friend class VulkanCommandContext;
        VulkanDevice* Device;
        bool bDirtyPipelineState = false;
        VulkanGraphicsPipelineState* CurrentPipeline = nullptr;
        VulkanGraphicsPipelineDescriptorState* CurrentState = nullptr;

        std::unordered_map<VulkanGraphicsPipelineState*, std::unique_ptr<VulkanGraphicsPipelineDescriptorState>> States;

        // Dynamic states
        std::vector<VkViewport> Viewports;
        std::vector<VkRect2D> Scissors;
        bool ViewportDiry = false;
        bool ScissorDirty = false;
        uint32_t StencilRef = 0;

        enum EPrimitiveType { PT_Num } PrimitiveType;

        struct FVertexStream
        {
            VkBuffer Stream = VK_NULL_HANDLE;
            uint32_t BufferOffset = 0;
        };
        constexpr static uint32_t MaxVertexElementCount = 8;
        FVertexStream PendingStreams[MaxVertexElementCount];
        bool bDirtyVertexStreams = true;
		VulkanCommandContext* Context = nullptr;
    };

    class VulkanPendingRayTracingState
    {
    public:
        VulkanPendingRayTracingState(VulkanDevice* device, VulkanCommandContext* context)
            : Device(device)
            , Context(context)
        {
        }

        void Reset()
        {
            CurrentPipeline = nullptr;
            CurrentState = nullptr;
            RetiredScratchBuffers.clear();
            CurrentAccelerationStructure = nullptr;
            ScratchBuffer.reset();
            ScratchBufferSize = 0;
        }

        void SetPipeline(VulkanRayTracingPipeline* pipeline)
        {
            CurrentPipeline = pipeline;
            auto it = States.find(pipeline);
            if (it == States.end())
            {
                auto state = std::make_unique<VulkanRayTracingPipelineDescriptorState>(
                    Device, pipeline, Context);
                CurrentState = state.get();
                States[pipeline] = std::move(state);
            }
            else
            {
                CurrentState = it->second.get();
            }
        }

        // �� VulkanPendingGfxState ��
//--------------------------------------------------------
// ʹ�� ShaderFrequency + parameterId ���� Texture
//--------------------------------------------------------
        void SetTexture(ERHIShaderFrequency frequency, uint32_t parameterId, uint32_t elementId, VulkanTexture* texture)
        {
            if (!CurrentState)
                return;

            uint32_t setIndex = 0;
            uint32_t binding = 0;
            if (CurrentState->GetBinding(frequency, parameterId, setIndex, binding))
            {
                CurrentState->SetTexture(setIndex, binding, elementId, texture);
            }
        }

        //--------------------------------------------------------
        // ʹ�� ShaderFrequency + parameterId ���� sampler
        //--------------------------------------------------------
        void SetSampler(ERHIShaderFrequency frequency, uint32_t parameterId, uint32_t elementId, VulkanSampler* sampler)
        {
            if (!CurrentState)
                return;
            uint32_t setIndex = 0;
            uint32_t binding = 0;
            if (CurrentState->GetBinding(frequency, parameterId, setIndex, binding))
            {
                CurrentState->SetSampler(setIndex, binding, elementId, sampler);
            }
        }

        //--------------------------------------------------------
        // ʹ�� ShaderFrequency + parameterId ���� SRV
        //--------------------------------------------------------
        void SetSRV(ERHIShaderFrequency frequency, uint32_t parameterId, uint32_t elementId, VulkanShaderResourceView* srv)
        {
            if (!CurrentState)
                return;

            uint32_t setIndex = 0;
            uint32_t binding = 0;
            if (CurrentState->GetBinding(frequency, parameterId, setIndex, binding))
            {
                CurrentState->SetSRV(setIndex, binding, elementId, srv);
            }
        }

        //--------------------------------------------------------
        // ʹ�� ShaderFrequency + parameterId ���� UAV
        //--------------------------------------------------------
        void SetUAV(ERHIShaderFrequency frequency, uint32_t parameterId, uint32_t elementId, VulkanUnorderedAccessView* uav)
        {
            if (!CurrentState)
                return;

            uint32_t setIndex = 0;
            uint32_t binding = 0;
            if (CurrentState->GetBinding(frequency, parameterId, setIndex, binding))
            {
                CurrentState->SetUAV(setIndex, binding, elementId, uav);
            }
        }

        //--------------------------------------------------------
        // ʹ�� ShaderFrequency + parameterId ���� UniformBuffer
        //--------------------------------------------------------
        void SetUniformBuffer(ERHIShaderFrequency frequency, uint32_t parameterId, uint32_t elementId, VulkanBuffer* uniformBuffer)
        {
            if (!CurrentState)
                return;

            uint32_t setIndex = 0;
            uint32_t binding = 0;
            if (CurrentState->GetBinding(frequency, parameterId, setIndex, binding))
            {
                CurrentState->MarkUiformBufferUseExternal(frequency, setIndex, binding);
                CurrentState->SetUniformBuffer(setIndex, binding, elementId, uniformBuffer->GetHandle(), 0, uniformBuffer->GetSize());
            }
        }
        void SetShaderParameter(ERHIShaderFrequency frequency, uint32_t BufferIndex, uint32_t BaseIndex, uint32_t NumBytes, const void* NewValue) {
            if (!CurrentState)
                return;
            CurrentState->SetPackedGlobalParameter(frequency, BufferIndex, BaseIndex, NumBytes, NewValue);
        }

        void SetAccelerationStructure(VulkanRayTracingInstance* accelerationStructure)
        {
            CurrentAccelerationStructure = accelerationStructure;
        }

        void SetAccelerationStructure(ERHIShaderFrequency frequency, uint32_t parameterId, uint32_t elementId, VulkanRayTracingInstance* accelerationStructure)
        {
            if (!CurrentState)
                return;

            uint32_t setIndex = 0;
            uint32_t binding = 0;
            if (CurrentState->GetBinding(frequency, parameterId, setIndex, binding))
            {
                CurrentState->SetAccelerationStructure(setIndex, binding, elementId, accelerationStructure);
            }
        }

        bool HasPipeline() const
        {
            return CurrentPipeline != nullptr;
        }

        bool HasAccelerationStructure() const
        {
            return CurrentAccelerationStructure != nullptr;
        }

        const VkStridedDeviceAddressRegionKHR& GetRayGenShaderBindingTableRegion() const
        {
            return CurrentPipeline ? CurrentPipeline->GetRayGenShaderBindingTableRegion() : EmptyRegion;
        }

        const VkStridedDeviceAddressRegionKHR& GetMissShaderBindingTableRegion() const
        {
            return CurrentPipeline ? CurrentPipeline->GetMissShaderBindingTableRegion() : EmptyRegion;
        }

        const VkStridedDeviceAddressRegionKHR& GetHitShaderBindingTableRegion() const
        {
            return CurrentPipeline ? CurrentPipeline->GetHitShaderBindingTableRegion() : EmptyRegion;
        }

        const VkStridedDeviceAddressRegionKHR& GetCallableShaderBindingTableRegion() const
        {
            return CurrentPipeline ? CurrentPipeline->GetCallableShaderBindingTableRegion() : EmptyRegion;
        }

        void PrepareForTraceRays(VulkanCommandBuffer* cmd)
        {
            if (!cmd || !CurrentPipeline)
            {
                return;
            }

            VKFunc::CmdBindPipeline(
                cmd->GetHandle(),
                VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
                CurrentPipeline->GetHandle());
            // 2. Bind descriptor sets
            CurrentState->FlushAndBind(cmd);
        }

        void BuildAccelerationStructure(VulkanCommandBuffer* cmd, RHIRayTracingAccelerationStructure* accelerationStructure, bool bUpdate)
        {
            if (!cmd || !accelerationStructure || !Device)
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
            VKFunc::GetPhysicalDeviceProperties2(Device->GetPhysicalDevice(), &properties);
            const VkDeviceSize alignment = asProperties.minAccelerationStructureScratchOffsetAlignment;
            EnsureScratchBuffer(requiredScratchSize + alignment - 1);
            if (!ScratchBuffer)
            {
                return;
            }

            VkBufferDeviceAddressInfo addressInfo{};
            addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
            addressInfo.buffer = ScratchBuffer->GetHandle();
            buildInfo.scratchData.deviceAddress = VKFunc::GetBufferDeviceAddress(Device->GetHandle(), &addressInfo);
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
            if (ScratchBuffer && ScratchBufferSize >= requiredSize)
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

            if (ScratchBuffer) RetiredScratchBuffers.push_back(ScratchBuffer);
            ScratchBuffer = std::make_shared<VulkanBuffer>(Device, desc);
            ScratchBufferSize = requiredSize;
        }

    private:
        VulkanDevice* Device = nullptr;
        VulkanCommandContext* Context = nullptr;
        VulkanRayTracingPipeline* CurrentPipeline = nullptr;
        VulkanRayTracingPipelineDescriptorState* CurrentState = nullptr;
        VulkanRayTracingInstance* CurrentAccelerationStructure = nullptr;
		VkStridedDeviceAddressRegionKHR EmptyRegion{};
        std::unordered_map<VulkanRayTracingPipeline*, std::unique_ptr<VulkanRayTracingPipelineDescriptorState>> States;
        std::shared_ptr < VulkanBuffer> ScratchBuffer;
        VkDeviceSize ScratchBufferSize = 0;
        std::vector<std::shared_ptr<VulkanBuffer>> RetiredScratchBuffers;
    };

}