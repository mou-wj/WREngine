#include "Loop.h"
#include "Module.h"
#include "ApplicationBase.h"
#include "Engine.h"
#include "Timer.h"
using namespace SlateCore;
void Loop::Init() {
	Core::ModuleManager::Get().LoadModule("RHIVulkan");
	Core::ModuleManager::Get().LoadModule("Renderer");
	Core::ModuleManager::Get().LoadModule("Engine");
	Core::ModuleManager::Get().LoadModule("SlateRHIRenderer");
	Core::ModuleManager::Get().LoadModule("Application");
	Core::ModuleManager::Get().AddModuleDependency("Renderer", "RHIVulkan");
	Core::ModuleManager::Get().AddModuleDependency("Engine", "RHIVulkan");
	Core::ModuleManager::Get().AddModuleDependency("SlateRHIRenderer", "Engine");
	Core::ModuleManager::Get().AddModuleDependency("Application", "Engine");
	Core::ModuleManager::Get().AddModuleDependency("Application", "Renderer");
	Core::ModuleManager::Get().AddModuleDependency("Application", "SlateRHIRenderer");

	Core::ModuleManager::Get().StartupAll();

}
void Loop::Run() {
	while (!ApplicationBase::GetApplication()->RequestExit()) {
		float deltaTime = Core::Timer::GetGlobalInstance().GetDelta();
		//tick引擎，更新场景等
		Engine::GetEngineInstance()->Tick(deltaTime);
		//tick应用，更新UI等，tick该应用的绘制逻辑
		ApplicationBase::GetApplication()->TickFrame();
	}
}

void Loop::Shutdown() {
	Core::ModuleManager::Get().ShutdownAll();

}

