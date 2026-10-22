#pragma once

#include "RHIResource.h"
#include <memory>

namespace RHIOpenGL
{
    class OpenGLGraphicsPipelineState : public RHI::RHIGraphicsPipelineState
    {
    public:
        explicit OpenGLGraphicsPipelineState(const RHI::RHIGraphicsPipelineStateDesc& desc);
        ~OpenGLGraphicsPipelineState() override;

        uint32_t GetProgramHandle() const { return m_ProgramHandle; }
        void SetProgramHandle(uint32_t handle) { m_ProgramHandle = handle; }

    private:
        uint32_t m_ProgramHandle = 0;
    };
    using OpenGLGraphicsPipelineStateSP = std::shared_ptr<OpenGLGraphicsPipelineState>;

    class OpenGLComputePipelineState : public RHI::RHIComputePipelineState
    {
    public:
        explicit OpenGLComputePipelineState(const RHI::RHIComputePipelineStateDesc& desc);
        ~OpenGLComputePipelineState() override;

        uint32_t GetProgramHandle() const { return m_ProgramHandle; }
        void SetProgramHandle(uint32_t handle) { m_ProgramHandle = handle; }

    private:
        uint32_t m_ProgramHandle = 0;
    };
    using OpenGLComputePipelineStateSP = std::shared_ptr<OpenGLComputePipelineState>;
}
