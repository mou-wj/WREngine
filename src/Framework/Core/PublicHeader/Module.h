#pragma once

#include <string>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#if defined(_WIN32)
#include <windows.h>
#include <libloaderapi.h>   
#else
#include <dlfcn.h>
#endif
#include "PathInfo.h"

namespace Core {

// 简单的模块接口，仿 UE 的 Module
class CORE_API Module
{
public:

    virtual ~Module() = default;
    // Called when the module is loaded / should start up.
    virtual void StartupModule() = 0;

    // Called when the module is unloaded / should shut down.
    virtual void ShutdownModule() = 0;

    // Called before the module is unloaded when hot-reloading / reload ordering is needed.
    virtual void PreUnloadCallback() { }

    // Called after the module has been loaded and initialized.
    virtual void PostLoadCallback() { }

    // Query functions
    virtual bool IsLoaded() const = 0;

};



using ModulePtr = std::shared_ptr<Module>;

// 简单的模块管理器，负责注册/查找模块实例
class CORE_API ModuleManager
{
public:
    static ModuleManager& Get()
    {
        static ModuleManager instance;
        return instance;
    }

    //加载模块
    void LoadModule(const std::string& name) {
        #if defined(_WIN32)
            std::string path = GetExecutableDir() + "/" + name + ".dll";
            ::LoadLibraryA(path.c_str());
        #else
            std::string path = GetExecutableDir() + "/" + name + ".so";
            return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        #endif
    }

    // 注册模块实例（通常在模块创建时调用）
    void RegisterModule(const std::string& name, ModulePtr module)
    {
        if (!module) return;
        std::lock_guard<std::recursive_mutex> lg(m_Mutex);
        m_Modules[name] = module;
    }

    // 注销模块
    void UnregisterModule(const std::string& name)
    {
        std::lock_guard<std::recursive_mutex> lg(m_Mutex);
        m_Modules.erase(name);
    }

    // 新增接口：为已注册模块添加依赖
    void AddModuleDependency(const std::string& moduleName, const std::string& dependencyName)
    {
        auto it = m_Modules.find(moduleName);
        if (it != m_Modules.end())
        {
            auto& deps = m_ModuleDependencies[moduleName];
            if (std::find(deps.begin(), deps.end(), dependencyName) == deps.end())
            {
                deps.push_back(dependencyName);
            }
        }
    }

    // 获取已注册模块（未找到返回 nullptr）
    ModulePtr GetModule(const std::string& name)
    {
        std::lock_guard<std::recursive_mutex> lg(m_Mutex);
        auto it = m_Modules.find(name);
        return it != m_Modules.end() ? it->second : nullptr;
    }

    // 启动所有已注册模块
    void StartupAll()
    {
        std::lock_guard<std::recursive_mutex> lg(m_Mutex);
        
        std::unordered_set<std::string> visited;
        std::unordered_set<std::string> visiting;

        for (auto& kv : m_Modules)
        {
            if (visited.find(kv.first) == visited.end())
            {
                if (!TopologicalSort(kv.first, visited, visiting, m_SortedOrderedModules))
                {
                    printf("Failed to sort modules due to circular dependency.\n");
                    return;
                }
            }
        }

        // 按拓扑排序顺序启动模块
        for (const auto& name : m_SortedOrderedModules)
        {
            auto& mod = m_Modules[name];
            if (mod && !mod->IsLoaded())
                mod->StartupModule();
        }

    }

    // 关闭所有已注册模块
    void ShutdownAll()
    {
        std::lock_guard<std::recursive_mutex> lg(m_Mutex);
        for (auto it = m_SortedOrderedModules.rbegin(); it != m_SortedOrderedModules.rend(); ++it)
        {
            auto& mod = m_Modules[*it];
            if (mod && mod->IsLoaded())
            {
                mod->ShutdownModule();

            }
                
        } 
    }

private:
    ModuleManager() = default;
    ~ModuleManager() = default;

    // 拓扑排序递归函数
    bool TopologicalSort(const std::string& name,
        std::unordered_set<std::string>& visited,
        std::unordered_set<std::string>& visiting,
        std::vector<std::string>& sortedModules)
    {
        if (visiting.find(name) != visiting.end())
        {
            printf("Circular dependency detected at module: %s\n", name.c_str());
            return false;
        }

        if (visited.find(name) != visited.end())
            return true;

        visiting.insert(name);

        auto it = m_Modules.find(name);
        if (it != m_Modules.end())
        {
            for (const auto& depName : m_ModuleDependencies[name])
            {
                if (!TopologicalSort(depName, visited, visiting, sortedModules))
                    return false;
            }
        }

        visiting.erase(name);
        visited.insert(name);
        sortedModules.push_back(name); // 拓扑排序完成后加入
        return true;
    }
    std::vector<std::string> m_SortedOrderedModules;
    std::unordered_map<std::string, ModulePtr> m_Modules;
    std::unordered_map<std::string, std::vector<std::string>> m_ModuleDependencies;
    std::recursive_mutex m_Mutex;
};

// 方便宏：在模块实现文件中快速注册一个模块实例
#define IMPLEMENT_SIMPLE_MODULE(ModuleClass, ModuleName)                         \
    namespace {                                                                  \
        struct ModuleClass##_Registrar                                           \
        {                                                                        \
            ModuleClass##_Registrar()                                            \
            {                                                                    \
                auto inst = std::make_shared<ModuleClass>();           \
                Core::ModuleManager::Get().RegisterModule(ModuleName, inst);     \
                /* 不自动 Startup，由使用方调用 ModuleManager::StartupAll */  \
            }                                                                    \
            ~ModuleClass##_Registrar()                                           \
            {                                                                    \
                Core::ModuleManager::Get().UnregisterModule(ModuleName);         \
            }                                                                    \
        } ModuleClass##_Registrar_Instance;                                      \
    }

} // namespace Core