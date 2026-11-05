#pragma once
#include "EditorTextureViewer.h"
namespace Editor
{
    class TextureViewerPreview
    {
    public:
        TextureViewerPreview()=default;
        ~TextureViewerPreview();
        static std::string UnsupportedReason(RenderCore::RenderTexture* texture);
        void Render(const TextureViewerSource& source,int mip,int slice);
        void Reset();
        RenderCore::RenderTexture* GetTexture() const { return m_Color.get(); }
    private:
        void WaitForLastDraw();
        TextureViewerSource m_Source;
        std::unique_ptr<RenderCore::RenderTexture> m_Color;
    };
}
