#pragma once
#include <QString>
#include <QVector>

class QKeyEvent;

namespace imap
{

    enum class KeyCategory
    {
        Keyboard,
        Mouse,
        Gamepad
    };

    struct KeyInfo
    {
        QString name;
        QString display;
        KeyCategory category;
        bool axis2D; // digital keys output (0, 1, 0)
        int qtKey; // 0 = can't be captured
        bool keypad = false;
    };

    namespace KeyCatalog
    {

        const QVector<KeyInfo>& all();
        const KeyInfo* find(const QString& engineName);
        // Catalog spelling of a key the engine also accepts ("Keyboard_Left Shift" -> "Keyboard_LeftShift").
        QString canonicalName(const QString& engineName);
        QString categoryName(KeyCategory c);

        QString nameFromKeyEvent(const QKeyEvent* e);

    } // namespace KeyCatalog
} // namespace imap
