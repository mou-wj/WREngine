#include "Loop.h"
#include "Module.h"
#include "ApplicationBase.h"
#include "Engine.h"
#include "Timer.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <unordered_set>

bool Loop::Init(int argc, const char* const* argv)
{
    const auto path = Core::GetExecutableDir() + "/apps.json";
    std::ifstream file(path);
    if (!file) throw std::runtime_error("Cannot open app configuration: " + path);
    const auto configuration = nlohmann::json::parse(file);
    const auto& apps = configuration.at("apps");
    if (!apps.is_object() || apps.empty()) throw std::runtime_error("apps must be a non-empty object");
    std::string appName = configuration.at("defaultApp").get<std::string>();
    bool hasApp = false;
    for (int index = 1; index < argc; ++index)
    {
        const std::string argument = argv[index];
        if (argument == "-help" || argument == "--help")
        {
            std::cout << "Usage: Langcher -app=<name>\nDefault: " << appName << "\nAvailable apps:\n";
            for (const auto& [name, app] : apps.items()) std::cout << "  " << name << '\n';
            return false;
        }
        if (!argument.starts_with("-app=") || hasApp)
            throw std::runtime_error("Unknown or duplicate argument: " + argument);
        appName = argument.substr(5);
        if (appName.size() >= 2 && appName.front() == '"' && appName.back() == '"')
            appName = appName.substr(1, appName.size() - 2);
        hasApp = true;
    }
    if (!apps.contains(appName)) throw std::runtime_error("Unknown app: " + appName + "; use -help to list apps");
    const auto& app = apps.at(appName);
    const auto modules = app.at("modules").get<std::vector<std::string>>();
    const auto& dependencies = app.at("dependencies");
    if (modules.empty() || !dependencies.is_object()) throw std::runtime_error("Invalid app module configuration");
    std::unordered_set<std::string> selected;
    for (const auto& module : modules)
        if (module.empty() || !selected.insert(module).second)
            throw std::runtime_error("Empty or duplicate module: " + module);
    for (const auto& [name, values] : dependencies.items())
    {
        if (!selected.contains(name)) throw std::runtime_error("Dependency owner is not selected: " + name);
        for (const auto& dependency : values.get<std::vector<std::string>>())
            if (!selected.contains(dependency)) throw std::runtime_error("Dependency is not selected: " + dependency);
    }
    // Reject an invalid graph before loading any app DLL or creating a device/window.
    std::unordered_set<std::string> visiting, visited;
    std::function<void(const std::string&)> visit = [&](const auto& name) {
        if (visited.contains(name)) return;
        if (!visiting.insert(name).second) throw std::runtime_error("Circular app dependency: " + name);
        if (dependencies.contains(name))
            for (const auto& value : dependencies.at(name)) visit(value.get<std::string>());
        visiting.erase(name);
        visited.insert(name);
    };
    for (const auto& module : modules) visit(module);

    auto& manager = Core::ModuleManager::Get();
    for (const auto& module : modules)
    {
        manager.LoadModule(module);
        if (!manager.GetModule(module)) throw std::runtime_error("Module did not load/register: " + module);
    }
    for (const auto& [name, values] : dependencies.items())
        for (const auto& value : values) manager.AddModuleDependency(name, value.get<std::string>());
    std::cout << "[Launcher] app=" << appName << ", config=" << path << std::endl;
    manager.StartupAll();
    if (!SlateCore::ApplicationBase::GetApplication()) throw std::runtime_error("App did not create ApplicationBase: " + appName);
    return true;
}

void Loop::Run()
{
    auto* application = SlateCore::ApplicationBase::GetApplication();
    while (!application->RequestExit())
    {
        const float deltaTime = Core::Timer::GetGlobalInstance().GetDelta();
        Engine::GetEngineInstance()->Tick(deltaTime);
        application->TickFrame();
    }
}

void Loop::Shutdown()
{
    Core::ModuleManager::Get().ShutdownAll();
}

