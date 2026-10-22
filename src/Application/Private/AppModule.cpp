#include "AppModule.h"
#include "Application.h"
namespace App {
	Application* app = nullptr;
	AppModule::AppModule() : m_bIsLoaded(false) {
	
	}
    AppModule::~AppModule() {
	
	}

	void AppModule::StartupModule() {
		// 模块启动时的初始化逻辑
		app = new Application();
		app->Initialize();
		
		m_bIsLoaded = true;

	}
	void AppModule::ShutdownModule() {
		// 模块关闭时的清理逻辑
		delete app;
	}
	bool AppModule::IsLoaded() const {
		return m_bIsLoaded; // 返回模块是否已加载
	}

	IMPLEMENT_SIMPLE_MODULE(AppModule, "Application");

}