#include "SlateRHIModule.h"
#include "SlateRHIRenderer.h"
namespace SlateRHIRenderer {
    SlateRHIRendererModule* GSlateRHIRendererModule = nullptr;
    SlateRHIRendererModule::SlateRHIRendererModule():m_Renderer(nullptr) {
        GSlateRHIRendererModule = this;
    }
    void SlateRHIRendererModule::StartupModule()
    {
        m_bLoaded = true;
    }

    void SlateRHIRendererModule::ShutdownModule()
    {
        delete m_Renderer;
        m_bLoaded = false;
    }

    bool SlateRHIRendererModule::IsLoaded() const
    {
        return m_bLoaded;
    }

    SlateCore::SlateRenderer* SlateRHIRendererModule::CreateSlateRenderer()
    {
        // UE 风格：只创建一次
        if (!m_Renderer)
        {
            m_Renderer = new SlateRHIRenderer();
        }

        return m_Renderer;
    }
    IMPLEMENT_SIMPLE_MODULE(SlateRHIRendererModule, "SlateRHIRenderer")
}