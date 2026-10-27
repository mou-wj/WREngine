#include "EditorApplication.h"
#include "Module.h"
#include <memory>
#include <stdexcept>

namespace Editor
{
    class EditorModule final : public Core::Module
    {
    public:
        void StartupModule() override
        {
            auto application = std::make_unique<EditorApplication>();
            if (!application->Initialize()) throw std::runtime_error("Editor initialization failed");
            SlateCore::ApplicationBase::SetApplication(application.get());
            m_Application = std::move(application);
        }
        void ShutdownModule() override { m_Application.reset(); }
        bool IsLoaded() const override { return m_Application != nullptr; }
    private:
        std::unique_ptr<EditorApplication> m_Application;
    };
    IMPLEMENT_SIMPLE_MODULE(EditorModule, "Editor")
}
