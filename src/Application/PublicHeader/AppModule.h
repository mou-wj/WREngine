#pragma once 
#include "Module.h"
namespace App {
	class APPLICATION_API AppModule : public Core::Module
	{
	public:
		AppModule();
        ~AppModule() override;
		void StartupModule() override;
		void ShutdownModule() override;
		bool IsLoaded() const override;
	private:
		bool m_bIsLoaded;
	};
}