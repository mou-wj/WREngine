// main.cpp
#include <iostream>
#include "TestBase.h"
#include "VulkanRHIApi.h"
#include "OpenGLRHIApi.h"
#include "DirectX12RHIApi.h"
#include <cctype>
#include <optional>
#include <stdexcept>
#include "RHICaptureHelper.h"
#  include <Windows.h>
#include "vulkan/vulkan.h"
#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>
namespace
{
    std::optional<Test::TestRHIBackend> SelectedRHI;

    Test::TestRHIBackend ParseBackend(std::string name)
    {
        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
        if (name == "vulkan")
            return Test::TestRHIBackend::Vulkan;
        if (name == "opengl" || name == "gl")
            return Test::TestRHIBackend::OpenGL;
        if (name == "directx12" || name == "d3d12" || name == "dx12")
            return Test::TestRHIBackend::DirectX12;
        throw std::invalid_argument("Unsupported RHI: '" + name + "'. Use Vulkan, OpenGL or DirectX12.");
    }

    const char* BackendName(Test::TestRHIBackend backend)
    {
        switch (backend)
        {
        case Test::TestRHIBackend::Vulkan: return "Vulkan";
        case Test::TestRHIBackend::OpenGL: return "OpenGL";
        case Test::TestRHIBackend::DirectX12: return "DirectX12";
        }
        throw std::invalid_argument("Invalid test RHI backend");
    }

    void PrintUsage()
    {
        std::cout << "Usage: Tests.exe [-test=<name>] [-rhi=<Vulkan|OpenGL|DirectX12>]\n"
                     "       Tests.exe <name> [-rhi=<backend>]  (legacy test selection)\n"
                     "       Tests.exe --help\n"
                     "Default test: RHIShaderParameterTest\n"
                     "Default RHI: Vulkan\n"
                     "Backend names are case-insensitive; aliases: gl, d3d12, dx12.\n"
                     "Registered tests:\n";
        for (const auto& name : Test::RenderTestRegistry::GetNames())
            std::cout << "  " << name << '\n';
    }
}

namespace Test
{
    RHI::RHIApi* CreateTestRHIApi()
    {
        const auto backend = SelectedRHI.value_or(TestRHIBackend::Vulkan);
        std::cout << "RHI backend: " << BackendName(backend) << std::endl;
        switch (backend)
        {
        case TestRHIBackend::Vulkan: return new RHIVulkan::VulkanRHIApi();
        case TestRHIBackend::OpenGL: return new RHIOpenGL::OpenGLRHIApi();
        case TestRHIBackend::DirectX12: return new RHIDirectX12::DirectX12RHIApi();
        }
        throw std::invalid_argument("Invalid test RHI backend");
    }
}

int main(int argc, char** argv)
{
    //_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
    //_CrtSetBreakAlloc(17506);
    //RHI::RHICaptureHelper::GetInstance();
    std::string testName = "RHIShaderParameterTest"; // 默认
    try
    {
        bool hasTest = false;
        for (int i = 1; i < argc; ++i)
        {
            const std::string argument = argv[i];
            if (argument == "--help" || argument == "-help" || argument == "-h")
            {
                PrintUsage();
                return 0;
            }
            if (argument.rfind("-rhi=", 0) == 0)
            {
                if (SelectedRHI)
                    throw std::invalid_argument("Duplicate -rhi argument");
                SelectedRHI = ParseBackend(argument.substr(5));
            }
            else if (argument.rfind("-test=", 0) == 0 || (!argument.empty() && argument.front() != '-'))
            {
                if (hasTest)
                    throw std::invalid_argument("Duplicate test selection");
                testName = argument.rfind("-test=", 0) == 0 ? argument.substr(6) : argument;
                if (testName.empty())
                    throw std::invalid_argument("-test requires a nonempty test name");
                hasTest = true;
            }
            else
                throw std::invalid_argument("Unknown argument: '" + argument + "'");
        }
    }
    catch (const std::exception& ex)
    {
        std::cerr << "Invalid arguments: " << ex.what() << '\n';
        PrintUsage();
        return 2;
    }

    auto test = Test::RenderTestRegistry::Create(testName);
    if (!test)
    {
        std::cerr << "Test not found: " << testName << std::endl;
        PrintUsage();
        return 2;
    }

    std::cout << "Running test: " << testName << std::endl;

    try
    {
        test->Setup();
        test->Run();
        test->Teardown();
    }
    catch (const std::exception& ex)
    {
        std::cerr << "Test failed with exception: " << ex.what() << std::endl;
        return 1;
    }
    catch (...)
    {
        std::cerr << "Test failed with unknown exception" << std::endl;
        return 1;
    }

    return 0;
}
