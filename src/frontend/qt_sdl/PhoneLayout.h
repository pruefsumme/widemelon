// Configurable phone-controller layout shared by the native editor and web client.
#pragma once

#include <QList>
#include <QRectF>
#include <QString>

struct PhoneLayoutItem
{
    enum Kind { Screen, Directional, FaceButtons, Button };

    QString id;
    QString label;
    Kind kind = Button;
    QRectF rect;
    int dsButton = -1;
    int hotkey = -1;
    bool removable = false;
    QString appearance;
};

class PhoneControllerLayout
{
public:
    static PhoneControllerLayout defaults();
    static PhoneControllerLayout fromJson(const QString& json, bool* valid = nullptr);

    QString toJson() const;
    QList<PhoneLayoutItem> items;
    bool showHud = true;
};

struct PhoneHotkeyAction
{
    int id;
    const char* label;
};

const QList<PhoneHotkeyAction>& PhoneLayoutHotkeyActions();
