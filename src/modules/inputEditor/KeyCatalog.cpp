#include "KeyCatalog.h"

#include <QKeyEvent>

#if defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace imap
{
    namespace KeyCatalog
    {

        const QVector<KeyInfo>& all()
        {
            static const QVector<KeyInfo> keys = {
                // Letters
                { "Keyboard_A", "A", KeyCategory::Keyboard, false, Qt::Key_A },
                { "Keyboard_B", "B", KeyCategory::Keyboard, false, Qt::Key_B },
                { "Keyboard_C", "C", KeyCategory::Keyboard, false, Qt::Key_C },
                { "Keyboard_D", "D", KeyCategory::Keyboard, false, Qt::Key_D },
                { "Keyboard_E", "E", KeyCategory::Keyboard, false, Qt::Key_E },
                { "Keyboard_F", "F", KeyCategory::Keyboard, false, Qt::Key_F },
                { "Keyboard_G", "G", KeyCategory::Keyboard, false, Qt::Key_G },
                { "Keyboard_H", "H", KeyCategory::Keyboard, false, Qt::Key_H },
                { "Keyboard_I", "I", KeyCategory::Keyboard, false, Qt::Key_I },
                { "Keyboard_J", "J", KeyCategory::Keyboard, false, Qt::Key_J },
                { "Keyboard_K", "K", KeyCategory::Keyboard, false, Qt::Key_K },
                { "Keyboard_L", "L", KeyCategory::Keyboard, false, Qt::Key_L },
                { "Keyboard_M", "M", KeyCategory::Keyboard, false, Qt::Key_M },
                { "Keyboard_N", "N", KeyCategory::Keyboard, false, Qt::Key_N },
                { "Keyboard_O", "O", KeyCategory::Keyboard, false, Qt::Key_O },
                { "Keyboard_P", "P", KeyCategory::Keyboard, false, Qt::Key_P },
                { "Keyboard_Q", "Q", KeyCategory::Keyboard, false, Qt::Key_Q },
                { "Keyboard_R", "R", KeyCategory::Keyboard, false, Qt::Key_R },
                { "Keyboard_S", "S", KeyCategory::Keyboard, false, Qt::Key_S },
                { "Keyboard_T", "T", KeyCategory::Keyboard, false, Qt::Key_T },
                { "Keyboard_U", "U", KeyCategory::Keyboard, false, Qt::Key_U },
                { "Keyboard_V", "V", KeyCategory::Keyboard, false, Qt::Key_V },
                { "Keyboard_W", "W", KeyCategory::Keyboard, false, Qt::Key_W },
                { "Keyboard_X", "X", KeyCategory::Keyboard, false, Qt::Key_X },
                { "Keyboard_Y", "Y", KeyCategory::Keyboard, false, Qt::Key_Y },
                { "Keyboard_Z", "Z", KeyCategory::Keyboard, false, Qt::Key_Z },

                // Number row
                { "Keyboard_0", "0", KeyCategory::Keyboard, false, Qt::Key_0 },
                { "Keyboard_1", "1", KeyCategory::Keyboard, false, Qt::Key_1 },
                { "Keyboard_2", "2", KeyCategory::Keyboard, false, Qt::Key_2 },
                { "Keyboard_3", "3", KeyCategory::Keyboard, false, Qt::Key_3 },
                { "Keyboard_4", "4", KeyCategory::Keyboard, false, Qt::Key_4 },
                { "Keyboard_5", "5", KeyCategory::Keyboard, false, Qt::Key_5 },
                { "Keyboard_6", "6", KeyCategory::Keyboard, false, Qt::Key_6 },
                { "Keyboard_7", "7", KeyCategory::Keyboard, false, Qt::Key_7 },
                { "Keyboard_8", "8", KeyCategory::Keyboard, false, Qt::Key_8 },
                { "Keyboard_9", "9", KeyCategory::Keyboard, false, Qt::Key_9 },

                // Function keys
                { "Keyboard_F1", "F1", KeyCategory::Keyboard, false, Qt::Key_F1 },
                { "Keyboard_F2", "F2", KeyCategory::Keyboard, false, Qt::Key_F2 },
                { "Keyboard_F3", "F3", KeyCategory::Keyboard, false, Qt::Key_F3 },
                { "Keyboard_F4", "F4", KeyCategory::Keyboard, false, Qt::Key_F4 },
                { "Keyboard_F5", "F5", KeyCategory::Keyboard, false, Qt::Key_F5 },
                { "Keyboard_F6", "F6", KeyCategory::Keyboard, false, Qt::Key_F6 },
                { "Keyboard_F7", "F7", KeyCategory::Keyboard, false, Qt::Key_F7 },
                { "Keyboard_F8", "F8", KeyCategory::Keyboard, false, Qt::Key_F8 },
                { "Keyboard_F9", "F9", KeyCategory::Keyboard, false, Qt::Key_F9 },
                { "Keyboard_F10", "F10", KeyCategory::Keyboard, false, Qt::Key_F10 },
                { "Keyboard_F11", "F11", KeyCategory::Keyboard, false, Qt::Key_F11 },
                { "Keyboard_F12", "F12", KeyCategory::Keyboard, false, Qt::Key_F12 },
                { "Keyboard_F13", "F13", KeyCategory::Keyboard, false, Qt::Key_F13 },
                { "Keyboard_F14", "F14", KeyCategory::Keyboard, false, Qt::Key_F14 },
                { "Keyboard_F15", "F15", KeyCategory::Keyboard, false, Qt::Key_F15 },
                { "Keyboard_F16", "F16", KeyCategory::Keyboard, false, Qt::Key_F16 },
                { "Keyboard_F17", "F17", KeyCategory::Keyboard, false, Qt::Key_F17 },
                { "Keyboard_F18", "F18", KeyCategory::Keyboard, false, Qt::Key_F18 },
                { "Keyboard_F19", "F19", KeyCategory::Keyboard, false, Qt::Key_F19 },
                { "Keyboard_F20", "F20", KeyCategory::Keyboard, false, Qt::Key_F20 },
                { "Keyboard_F21", "F21", KeyCategory::Keyboard, false, Qt::Key_F21 },
                { "Keyboard_F22", "F22", KeyCategory::Keyboard, false, Qt::Key_F22 },
                { "Keyboard_F23", "F23", KeyCategory::Keyboard, false, Qt::Key_F23 },
                { "Keyboard_F24", "F24", KeyCategory::Keyboard, false, Qt::Key_F24 },

                // Modifiers
                { "Keyboard_LeftShift", "Left Shift", KeyCategory::Keyboard, false, Qt::Key_Shift },
                { "Keyboard_RightShift", "Right Shift", KeyCategory::Keyboard, false, Qt::Key_Shift },
                { "Keyboard_LeftCtrl", "Left Ctrl", KeyCategory::Keyboard, false, Qt::Key_Control },
                { "Keyboard_RightCtrl", "Right Ctrl", KeyCategory::Keyboard, false, Qt::Key_Control },
                { "Keyboard_LeftAlt", "Left Alt", KeyCategory::Keyboard, false, Qt::Key_Alt },
                { "Keyboard_RightAlt", "Right Alt", KeyCategory::Keyboard, false, Qt::Key_AltGr },
                { "Keyboard_LeftGUI", "Left Win/Cmd", KeyCategory::Keyboard, false, Qt::Key_Meta },
                { "Keyboard_RightGUI", "Right Win/Cmd", KeyCategory::Keyboard, false, Qt::Key_Meta },

                // Main keyboard
                { "Keyboard_Escape", "Escape", KeyCategory::Keyboard, false, Qt::Key_Escape },
                { "Keyboard_Tab", "Tab", KeyCategory::Keyboard, false, Qt::Key_Tab },
                { "Keyboard_CapsLock", "Caps Lock", KeyCategory::Keyboard, false, Qt::Key_CapsLock },
                { "Keyboard_Backspace", "Backspace", KeyCategory::Keyboard, false, Qt::Key_Backspace },
                { "Keyboard_Return", "Enter", KeyCategory::Keyboard, false, Qt::Key_Return },
                { "Keyboard_Space", "Space Bar", KeyCategory::Keyboard, false, Qt::Key_Space },
                { "Keyboard_Insert", "Insert", KeyCategory::Keyboard, false, Qt::Key_Insert },
                { "Keyboard_Delete", "Delete", KeyCategory::Keyboard, false, Qt::Key_Delete },
                { "Keyboard_Home", "Home", KeyCategory::Keyboard, false, Qt::Key_Home },
                { "Keyboard_End", "End", KeyCategory::Keyboard, false, Qt::Key_End },
                { "Keyboard_PageUp", "Page Up", KeyCategory::Keyboard, false, Qt::Key_PageUp },
                { "Keyboard_PageDown", "Page Down", KeyCategory::Keyboard, false, Qt::Key_PageDown },

                // Arrow keys
                { "Keyboard_Left", "Left Arrow", KeyCategory::Keyboard, false, Qt::Key_Left },
                { "Keyboard_Right", "Right Arrow", KeyCategory::Keyboard, false, Qt::Key_Right },
                { "Keyboard_Up", "Up Arrow", KeyCategory::Keyboard, false, Qt::Key_Up },
                { "Keyboard_Down", "Down Arrow", KeyCategory::Keyboard, false, Qt::Key_Down },

                // Punctuation, by position on a US layout
                { "Keyboard_Minus", "- (Minus)", KeyCategory::Keyboard, false, Qt::Key_Minus },
                { "Keyboard_Equals", "= (Equals)", KeyCategory::Keyboard, false, Qt::Key_Equal },
                { "Keyboard_LeftBracket", "[ (Left Bracket)", KeyCategory::Keyboard, false, Qt::Key_BracketLeft },
                { "Keyboard_RightBracket", "] (Right Bracket)", KeyCategory::Keyboard, false, Qt::Key_BracketRight },
                { "Keyboard_Backslash", "\\ (Backslash)", KeyCategory::Keyboard, false, Qt::Key_Backslash },
                { "Keyboard_Semicolon", "; (Semicolon)", KeyCategory::Keyboard, false, Qt::Key_Semicolon },
                { "Keyboard_Apostrophe", "' (Apostrophe)", KeyCategory::Keyboard, false, Qt::Key_Apostrophe },
                { "Keyboard_Grave", "` (Grave)", KeyCategory::Keyboard, false, Qt::Key_QuoteLeft },
                { "Keyboard_Comma", ", (Comma)", KeyCategory::Keyboard, false, Qt::Key_Comma },
                { "Keyboard_Period", ". (Period)", KeyCategory::Keyboard, false, Qt::Key_Period },
                { "Keyboard_Slash", "/ (Slash)", KeyCategory::Keyboard, false, Qt::Key_Slash },
                { "Keyboard_NonUSHash", "Non-US #", KeyCategory::Keyboard, false, 0 },
                { "Keyboard_NonUSBackslash", "Non-US \\", KeyCategory::Keyboard, false, 0 },

                // Lock / system keys
                { "Keyboard_NumLock", "Num Lock", KeyCategory::Keyboard, false, Qt::Key_NumLock },
                { "Keyboard_ScrollLock", "Scroll Lock", KeyCategory::Keyboard, false, Qt::Key_ScrollLock },
                { "Keyboard_PrintScreen", "Print Screen", KeyCategory::Keyboard, false, Qt::Key_Print },
                { "Keyboard_Pause", "Pause", KeyCategory::Keyboard, false, Qt::Key_Pause },
                { "Keyboard_Application", "Application (Context Menu)", KeyCategory::Keyboard, false, Qt::Key_Menu },

                // Keypad
                { "Keyboard_Keypad0", "Numpad 0", KeyCategory::Keyboard, false, Qt::Key_0, true },
                { "Keyboard_Keypad1", "Numpad 1", KeyCategory::Keyboard, false, Qt::Key_1, true },
                { "Keyboard_Keypad2", "Numpad 2", KeyCategory::Keyboard, false, Qt::Key_2, true },
                { "Keyboard_Keypad3", "Numpad 3", KeyCategory::Keyboard, false, Qt::Key_3, true },
                { "Keyboard_Keypad4", "Numpad 4", KeyCategory::Keyboard, false, Qt::Key_4, true },
                { "Keyboard_Keypad5", "Numpad 5", KeyCategory::Keyboard, false, Qt::Key_5, true },
                { "Keyboard_Keypad6", "Numpad 6", KeyCategory::Keyboard, false, Qt::Key_6, true },
                { "Keyboard_Keypad7", "Numpad 7", KeyCategory::Keyboard, false, Qt::Key_7, true },
                { "Keyboard_Keypad8", "Numpad 8", KeyCategory::Keyboard, false, Qt::Key_8, true },
                { "Keyboard_Keypad9", "Numpad 9", KeyCategory::Keyboard, false, Qt::Key_9, true },
                { "Keyboard_KeypadPeriod", "Numpad .", KeyCategory::Keyboard, false, Qt::Key_Period, true },
                { "Keyboard_KeypadDivide", "Numpad /", KeyCategory::Keyboard, false, Qt::Key_Slash, true },
                { "Keyboard_KeypadMultiply", "Numpad *", KeyCategory::Keyboard, false, Qt::Key_Asterisk, true },
                { "Keyboard_KeypadMinus", "Numpad -", KeyCategory::Keyboard, false, Qt::Key_Minus, true },
                { "Keyboard_KeypadPlus", "Numpad +", KeyCategory::Keyboard, false, Qt::Key_Plus, true },
                { "Keyboard_KeypadEnter", "Numpad Enter", KeyCategory::Keyboard, false, Qt::Key_Enter, true },
                { "Keyboard_KeypadEquals", "Numpad =", KeyCategory::Keyboard, false, Qt::Key_Equal, true },

                { "Mouse_Left", "Mouse Left Button", KeyCategory::Mouse, false, 0 },
                { "Mouse_Right", "Mouse Right Button", KeyCategory::Mouse, false, 0 },
                { "Mouse_Middle", "Mouse Middle Button", KeyCategory::Mouse, false, 0 },
                { "Mouse_Button4", "Mouse Button 4", KeyCategory::Mouse, false, 0 },
                { "Mouse_Button5", "Mouse Button 5", KeyCategory::Mouse, false, 0 },
                { "Mouse_WheelUp", "Mouse Wheel Up", KeyCategory::Mouse, false, 0 },
                { "Mouse_WheelDown", "Mouse Wheel Down", KeyCategory::Mouse, false, 0 },
                { "Mouse_2D_Axis", "Mouse XY 2D-Axis", KeyCategory::Mouse, true, 0 },

                // Thumbsticks
                { "Gamepad_LeftAxis_2D", "Gamepad Left Thumbstick 2D-Axis", KeyCategory::Gamepad, true, 0 },
                { "Gamepad_LeftAxis_X", "Gamepad Left Thumbstick X-Axis", KeyCategory::Gamepad, true, 0 },
                { "Gamepad_LeftAxis_Y", "Gamepad Left Thumbstick Y-Axis", KeyCategory::Gamepad, true, 0 },
                { "Gamepad_RightAxis_2D", "Gamepad Right Thumbstick 2D-Axis", KeyCategory::Gamepad, true, 0 },
                { "Gamepad_RightAxis_X", "Gamepad Right Thumbstick X-Axis", KeyCategory::Gamepad, true, 0 },
                { "Gamepad_RightAxis_Y", "Gamepad Right Thumbstick Y-Axis", KeyCategory::Gamepad, true, 0 },

                // Face buttons
                { "Gamepad_Face_Top", "Gamepad Face Button Top", KeyCategory::Gamepad, false, 0 },
                { "Gamepad_Face_Left", "Gamepad Face Button Left", KeyCategory::Gamepad, false, 0 },
                { "Gamepad_Face_Bottom", "Gamepad Face Button Bottom", KeyCategory::Gamepad, false, 0 },
                { "Gamepad_Face_Right", "Gamepad Face Button Right", KeyCategory::Gamepad, false, 0 },

                // Special buttons
                { "Gamepad_Special_Left", "Gamepad Special Left (Back/View)", KeyCategory::Gamepad, false, 0 },
                { "Gamepad_Special_Right", "Gamepad Special Right (Start/Menu)", KeyCategory::Gamepad, false, 0 },

                // Stick buttons
                { "Gamepad_LeftStick", "Gamepad Left Thumbstick Click", KeyCategory::Gamepad, false, 0 },
                { "Gamepad_RightStick", "Gamepad Right Thumbstick Click", KeyCategory::Gamepad, false, 0 },

                // Bumpers
                { "Gamepad_LeftBumper", "Gamepad Left Bumper", KeyCategory::Gamepad, false, 0 },
                { "Gamepad_RightBumper", "Gamepad Right Bumper", KeyCategory::Gamepad, false, 0 },

                // D-Pad
                { "Gamepad_DPad_Up", "Gamepad D-Pad Up", KeyCategory::Gamepad, false, 0 },
                { "Gamepad_DPad_Down", "Gamepad D-Pad Down", KeyCategory::Gamepad, false, 0 },
                { "Gamepad_DPad_Left", "Gamepad D-Pad Left", KeyCategory::Gamepad, false, 0 },
                { "Gamepad_DPad_Right", "Gamepad D-Pad Right", KeyCategory::Gamepad, false, 0 },

                // Triggers
                { "Gamepad_LeftTrigger", "Gamepad Left Trigger", KeyCategory::Gamepad, true, 0 },
                { "Gamepad_RightTrigger", "Gamepad Right Trigger", KeyCategory::Gamepad, true, 0 },
            };
            return keys;
        }

        const KeyInfo* find(const QString& engineName)
        {
            for (const KeyInfo& k : all())
                if (k.name == engineName) return &k;
            return nullptr;
        }

        QString canonicalName(const QString& engineName)
        {
            if (!engineName.startsWith(QStringLiteral("Keyboard_"))) return engineName;
            QString stripped = engineName;
            stripped.remove(QLatin1Char(' '));
            for (const KeyInfo& k : all())
                if (k.name.compare(stripped, Qt::CaseInsensitive) == 0) return k.name;
            return engineName;
        }

        QString categoryName(KeyCategory c)
        {
            switch (c)
            {
            case KeyCategory::Keyboard: return QStringLiteral("Keyboard");
            case KeyCategory::Mouse:    return QStringLiteral("Mouse");
            case KeyCategory::Gamepad:  return QStringLiteral("Gamepad");
            }
            return {};
        }

        // Qt gives the same key for both sides of a modifier, ask the OS which one it was
        static bool isRightSide(const QKeyEvent* e, int qtKey)
        {
#if defined(Q_OS_WIN)
            Q_UNUSED(e)
            int vk = VK_RSHIFT;
            if (qtKey == Qt::Key_Control) vk = VK_RCONTROL;
            else if (qtKey == Qt::Key_Alt) vk = VK_RMENU;
            else if (qtKey == Qt::Key_Meta) vk = VK_RWIN;
            return (GetKeyState(vk) & 0x8000) != 0;
#elif defined(Q_OS_MACOS)
            switch (qtKey)
            {
            case Qt::Key_Shift:   return e->nativeVirtualKey() == 60;
            case Qt::Key_Control: return e->nativeVirtualKey() == 62;
            case Qt::Key_Alt:     return e->nativeVirtualKey() == 61;
            case Qt::Key_Meta:    return e->nativeVirtualKey() == 54;
            }
            return false;
#elif defined(Q_OS_LINUX)
            switch (qtKey)
            {
            case Qt::Key_Shift:   return e->nativeScanCode() == 62;
            case Qt::Key_Control: return e->nativeScanCode() == 105;
            case Qt::Key_Alt:     return e->nativeScanCode() == 108;
            case Qt::Key_Meta:    return e->nativeScanCode() == 134;
            }
            return false;
#else
            Q_UNUSED(e)
            Q_UNUSED(qtKey)
            return false;
#endif
        }

        QString nameFromKeyEvent(const QKeyEvent* e)
        {
            int k = e->key();
#if defined(Q_OS_MACOS)
            // Qt reports Command as Control and Control as Meta
            if (k == Qt::Key_Control) k = Qt::Key_Meta;
            else if (k == Qt::Key_Meta) k = Qt::Key_Control;
#endif
            switch (k)
            {
            case Qt::Key_Shift:
                return isRightSide(e, k) ? QStringLiteral("Keyboard_RightShift") : QStringLiteral("Keyboard_LeftShift");
            case Qt::Key_Control:
                return isRightSide(e, k) ? QStringLiteral("Keyboard_RightCtrl") : QStringLiteral("Keyboard_LeftCtrl");
            case Qt::Key_Alt:
                return isRightSide(e, k) ? QStringLiteral("Keyboard_RightAlt") : QStringLiteral("Keyboard_LeftAlt");
            case Qt::Key_Meta:
                return isRightSide(e, k) ? QStringLiteral("Keyboard_RightGUI") : QStringLiteral("Keyboard_LeftGUI");
            case Qt::Key_Backtab:
                return QStringLiteral("Keyboard_Tab");
            default: break;
            }

            const bool keypad = e->modifiers().testFlag(Qt::KeypadModifier) || k == Qt::Key_Enter;
            for (const KeyInfo& info : all())
                if (info.qtKey != 0 && info.qtKey == k && info.keypad == keypad)
                    return info.name;
            // Keypad arrows/home/etc. with Num Lock off, and keys Qt doesn't flag
            for (const KeyInfo& info : all())
                if (info.qtKey != 0 && info.qtKey == k && !info.keypad)
                    return info.name;
            return {};
        }

    } // namespace KeyCatalog
} // namespace imap
