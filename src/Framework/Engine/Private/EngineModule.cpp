#include "EngineModule.h"
#include "IEngine.h"
#include "DistanceFieldMgr.h"
#include "AssetManager.h"
namespace Engine {

    std::unique_ptr<Engine> EngineObj;
    EngineModule::EngineModule() {

    }
    EngineModule::~EngineModule() {

    }

    void EngineModule::StartupModule()
    {
        EngineObj = std::make_unique<IEngine>();
        EngineObj->Init();
        GDistanceFieldMgr.Initialize();

        isLoaded = true;
    }

    void EngineModule::ShutdownModule()
    {
        AssetManager::Get().Shutdown();
        if (EngineObj)
        {
            EngineObj->Shutdown();
            EngineObj.reset();
        }
        GDistanceFieldMgr.Release();
        isLoaded = false;
    }

    bool EngineModule::IsLoaded() const {
        return isLoaded;
    }


    IMPLEMENT_SIMPLE_MODULE(EngineModule, "Engine")

    Engine* GetEngineInstance() {
        return EngineObj.get();
    }
}
