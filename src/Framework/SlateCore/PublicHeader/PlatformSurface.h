#pragma once

#include <string>
#include <Math.hpp>

namespace SlateCore
{
    class PlatformSurfaceOwner;

    class SLATECORE_API PlatformSurface
    {
    public:
        PlatformSurface(
            int width,
            int height,
            std::string title,
            PlatformSurfaceOwner* owner);

        virtual ~PlatformSurface() = default;

    public:
        virtual bool Initialize() = 0;
        virtual void PollEvents() = 0;
        virtual void Shutdown() = 0;

        virtual void Show() = 0;
        virtual void Hide() = 0;
        virtual void Close() = 0;

        virtual void SetPosition(int x, int y) = 0;
        virtual void SetSize(int width, int height) = 0;

        virtual void* GetNativeHandle() const = 0;
        virtual Core::Int2 GetFramebufferSize() const = 0;

        PlatformSurfaceOwner* GetOwner() const
        {
            return _owner;
        }

        int GetWidth() const
        {
            return _width;
        }

        int GetHeight() const
        {
            return _height;
        }

        const std::string& GetTitle() const
        {
            return _title;
        }

    protected:
        int _width = 0;
        int _height = 0;
        std::string _title;
        PlatformSurfaceOwner* _owner = nullptr;
    };
}
