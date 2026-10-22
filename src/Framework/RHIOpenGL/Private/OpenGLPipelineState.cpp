#include "OpenGLPipelineState.h"
#include "OpenGLResource.h"
#include "OpenGLQueue.h"
#include "glad/gl.h"
#include <cstdio>

namespace RHIOpenGL
{
    static bool LinkOpenGLProgram(GLuint programHandle)
    {
        if (programHandle == 0)
        {
            return false;
        }

        glLinkProgram(programHandle);

        GLint linkStatus = GL_FALSE;
        glGetProgramiv(programHandle, GL_LINK_STATUS, &linkStatus);
        if (linkStatus == GL_FALSE)
        {
            GLint infoLogLength = 0;
            glGetProgramiv(programHandle, GL_INFO_LOG_LENGTH, &infoLogLength);
            std::vector<char> infoLog(static_cast<size_t>(infoLogLength > 0 ? infoLogLength : 1));
            glGetProgramInfoLog(programHandle, static_cast<GLint>(infoLog.size()), nullptr, infoLog.data());
            std::fprintf(stderr, "[OpenGLRHI] Failed to link program: %s\n", infoLog.data());
            return false;
        }

        return true;
    }

    OpenGLGraphicsPipelineState::~OpenGLGraphicsPipelineState()
    {
        if (ProgramHandle != 0)
        {
            const GLuint handle = ProgramHandle;
            ProgramHandle = 0;
            EnqueueTask([handle]()
            {
                glDeleteProgram(handle);
            });
        }
    }

    OpenGLGraphicsPipelineState::OpenGLGraphicsPipelineState(const RHI::RHIGraphicsPipelineStateDesc& desc)
        : RHI::RHIGraphicsPipelineState(desc)
    {
        // program 创建/attach/link 全部转发到渲染线程；Wait 版保证构造返回时
        // ProgramHandle 已就绪，后续 GetProgramHandle() 可用。
        EnqueueTaskAndWait([this, &desc]()
        {
            ProgramHandle = glCreateProgram();

            if (desc.ShaderStages.VertexShader)
            {
                auto* vertexShader = dynamic_cast<OpenGLVertexShader*>(desc.ShaderStages.VertexShader);
                if (vertexShader && vertexShader->GetHandle() != 0)
                {
                    glAttachShader(ProgramHandle, vertexShader->GetHandle());
                }
            }

            if (desc.ShaderStages.FragmentShader)
            {
                auto* fragmentShader = dynamic_cast<OpenGLFragmentShader*>(desc.ShaderStages.FragmentShader);
                if (fragmentShader && fragmentShader->GetHandle() != 0)
                {
                    glAttachShader(ProgramHandle, fragmentShader->GetHandle());
                }
            }

            LinkOpenGLProgram(ProgramHandle);
        });
    }

    OpenGLComputePipelineState::~OpenGLComputePipelineState()
    {
        if (ProgramHandle != 0)
        {
            const GLuint handle = ProgramHandle;
            ProgramHandle = 0;
            EnqueueTask([handle]()
            {
                glDeleteProgram(handle);
            });
        }
    }

    OpenGLComputePipelineState::OpenGLComputePipelineState(const RHI::RHIComputePipelineStateDesc& desc)
        : RHI::RHIComputePipelineState(desc)
    {
        // 同 Graphics：转发到渲染线程并等待完成。
        EnqueueTaskAndWait([this, &desc]()
        {
            ProgramHandle = glCreateProgram();

            if (desc.ComputeShader)
            {
                auto* computeShader = dynamic_cast<OpenGLComputeShader*>(desc.ComputeShader);
                if (computeShader && computeShader->GetHandle() != 0)
                {
                    glAttachShader(ProgramHandle, computeShader->GetHandle());
                }
            }

            LinkOpenGLProgram(ProgramHandle);
        });
    }
}
