// IRenderTest.h
#pragma once

#include <string>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>
#include <algorithm>

namespace RHI { class RHIApi; }

namespace Test{

enum class TestRHIBackend
{
    Vulkan,
    OpenGL,
    DirectX12
};

// All tests default to Vulkan unless overridden by the -rhi command-line argument.
RHI::RHIApi* CreateTestRHIApi();

class TestBase
{
public:
    virtual ~TestBase() = default;

    // 初始化资源
    virtual void Setup() = 0;

    // 每帧执行
    virtual void Run() = 0;

    // 清理资源
    virtual void Teardown() = 0;
};

class RenderTestRegistry
{
public:
    using Factory = std::function<std::unique_ptr<TestBase>()>;

    static void Register(const std::string& name, Factory factory)
    {
        GetMap()[name] = factory;
    }

    static std::unique_ptr<TestBase> Create(const std::string& name)
    {
        auto it = GetMap().find(name);
        if (it != GetMap().end())
            return it->second();
        return nullptr;
    }

    static std::vector<std::string> GetNames()
    {
        std::vector<std::string> names;
        for (const auto& entry : GetMap())
            names.push_back(entry.first);
        std::sort(names.begin(), names.end());
        return names;
    }

private:
    static std::unordered_map<std::string, Factory>& GetMap()
    {
        static std::unordered_map<std::string, Factory> map;
        return map;
    }
};

// 注册宏，放在每个 Test cpp 里
#define REGISTER_RENDER_TEST(Name, Type) \
    static bool _##Type##_registered = [](){ \
        RenderTestRegistry::Register(Name, [](){ return std::make_unique<Type>(); }); \
        return true; \
    }()


}
