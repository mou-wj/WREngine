#pragma once
#include "Widget.h"
#include <memory>
namespace SlateCore {

    class SLATECORE_API SlateViewport : public Widget
    {
		DECLARE_TYPE_ID_DERIVED_TYPE(SlateViewport, Widget)
    public:
        virtual ~SlateViewport() = default;

        // 尺寸
        virtual int GetWidth() const = 0;
        virtual int GetHeight() const = 0;
        virtual void Resize(int width, int height) = 0;
        virtual void* GetViewportRenderTargetTexture() const = 0;

    };

    using SlateViewportSP = std::shared_ptr<SlateViewport>;

}