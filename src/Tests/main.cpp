// main.cpp
#include <iostream>
#include "TestBase.h"
#include "RHICaptureHelper.h"
#  include <Windows.h>
#include "vulkan/vulkan.h"
#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>
int main(int argc, char** argv)
{
    //_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
    //_CrtSetBreakAlloc(17506);
    //RHI::RHICaptureHelper::GetInstance();
    std::string testName = "RHIRenderTriangleTest"; // 默认
    if (argc > 1)
        testName = argv[1];

    auto test = Test::RenderTestRegistry::Create(testName);
    if (!test)
    {
        std::cerr << "Test not found: " << testName << std::endl;
        return -1;
    }

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