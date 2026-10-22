#include "Module.h"
#include "EngineExport.h"
#include "Engine.h"
#include <memory>
namespace Engine {

    class ENGINE_API EngineModule : public Core::Module
    {
    public:
        EngineModule();
        ~EngineModule();

        void StartupModule() override;
        void ShutdownModule() override;
        bool IsLoaded() const override;

    private:
        bool m_bLoaded = false;
    };



}
