#include "Module.h"
namespace Editor
{
    class NodeEditorModule final : public Core::Module
    {
    public:
        void StartupModule() override { m_bLoaded = true; }
        void ShutdownModule() override { m_bLoaded = false; }
        bool IsLoaded() const override { return m_bLoaded; }
    private:
        bool m_bLoaded = false;
    };
    IMPLEMENT_SIMPLE_MODULE(NodeEditorModule,"NodeEditor")
}
