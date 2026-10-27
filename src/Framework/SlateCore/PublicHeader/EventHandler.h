#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace SlateCore
{
    enum class EInputEvent
    {
        Pressed,
        Released,
        Repeat
    };

    enum class EMouseButton
    {
        Left,
        Right,
        Middle,
        Thumb01,
        Thumb02
    };

    enum class EKey
    {
        Unknown,

        W,
        A,
        S,
        D,
        Q,
        E,

        Shift,
        Ctrl,
        Alt,

        Space,
        Escape,

        MouseLeft,
        MouseRight,
        MouseMiddle,

        Backspace,
        Delete,
        Enter,
        Tab,
        Left,
        Right,
        Up,
        Down,
        Home,
        End,

        C,
        V,
        X,
        Y,
        Z
    };

    struct ModifierKeys
    {
        bool bCtrl = false;
        bool bShift = false;
        bool bAlt = false;
    };

    struct MouseMoveEvent
    {
        int32_t X = 0;
        int32_t Y = 0;

        int32_t DeltaX = 0;
        int32_t DeltaY = 0;

        ModifierKeys Modifiers;
    };

    struct MouseButtonEvent
    {
        EMouseButton Button = EMouseButton::Left;
        EInputEvent Event = EInputEvent::Pressed;

        int32_t X = 0;
        int32_t Y = 0;

        ModifierKeys Modifiers;
    };

    struct MouseWheelEvent
    {
        float Delta = 0.0f;

        int32_t X = 0;
        int32_t Y = 0;

        ModifierKeys Modifiers;
    };

    struct KeyEvent
    {
        EKey Key = EKey::Unknown;
        EInputEvent Event = EInputEvent::Pressed;

        ModifierKeys Modifiers;
    };

    struct FileDropEvent
    {
        std::vector<std::filesystem::path> Paths;
        int32_t X = 0;
        int32_t Y = 0;
    };

    class SLATECORE_API EventHandler
    {
    public:
        virtual ~EventHandler();

    public:
        virtual bool OnMouseMove(
            const MouseMoveEvent& event);

        virtual bool OnMouseButton(
            const MouseButtonEvent& event);

        virtual bool OnMouseWheel(
            const MouseWheelEvent& event);

        virtual bool OnKeyDown(
            const KeyEvent& event);

        virtual bool OnKeyUp(
            const KeyEvent& event);

        // UTF-16 code units from the platform; default handlers ignore text.
        virtual bool OnTextInput(uint16_t character) { return false; }
        virtual bool OnFilesDropped(const FileDropEvent&) { return false; }

        virtual bool OnFocusReceived();

        virtual bool OnFocusLost();

        virtual bool OnResize(
            uint32_t width,
            uint32_t height);
    };
}
