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

        m_bLoaded = true;
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
        m_bLoaded = false;
    }

    bool EngineModule::IsLoaded() const {
        return m_bLoaded;
    }


    IMPLEMENT_SIMPLE_MODULE(EngineModule, "Engine")

    Engine* GetEngineInstance() {
        return EngineObj.get();
    }
}
