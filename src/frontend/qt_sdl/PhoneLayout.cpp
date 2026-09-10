// Configurable phone-controller layout shared by the native editor and web client.
#include "PhoneLayout.h"

#include <algorithm>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include "EmuInstance.h"

namespace
{
PhoneLayoutItem item(const char* id, const char* label, PhoneLayoutItem::Kind kind,
                     qreal x, qreal y, qreal width, qreal height, int dsButton = -1)
{
    PhoneLayoutItem value;
    value.id = QString::fromLatin1(id);
    value.label = QString::fromLatin1(label);
    value.kind = kind;
    value.rect = QRectF(x, y, width, height);
    value.dsButton = dsButton;
    return value;
}

QString kindName(PhoneLayoutItem::Kind kind)
{
    if (kind == PhoneLayoutItem::Screen) return "screen";
    if (kind == PhoneLayoutItem::Directional) return "directional";
    if (kind == PhoneLayoutItem::FaceButtons) return "face";
    return "button";
}

bool finiteRect(const QRectF& rect)
{
    return std::isfinite(rect.x()) && std::isfinite(rect.y())
        && std::isfinite(rect.width()) && std::isfinite(rect.height())
        && rect.x() >= 0.0 && rect.y() >= 0.0
        && rect.width() >= 0.025 && rect.height() >= 0.04
        && rect.right() <= 1.0 && rect.bottom() <= 1.0;
}
}

const QList<PhoneHotkeyAction>& PhoneLayoutHotkeyActions()
{
    static const QList<PhoneHotkeyAction> actions {
        {HK_Pause, "Pause / resume"},
        {HK_Reset, "Reset"},
        {HK_FrameStep, "Frame step"},
        {HK_FastForward, "Fast forward"},
        {HK_FastForwardToggle, "Toggle fast forward"},
        {HK_SlowMo, "Slow motion"},
        {HK_SlowMoToggle, "Toggle slow motion"},
        {HK_FrameLimitToggle, "Toggle FPS limit"},
        {HK_FullscreenToggle, "Toggle desktop fullscreen"},
        {HK_SwapScreens, "Swap screens"},
        {HK_SwapScreenEmphasis, "Swap screen emphasis"},
        {HK_Lid, "Close / open lid"},
        {HK_Mic, "Microphone"},
        {HK_AudioMuteToggle, "Toggle audio mute"},
        {HK_PowerButton, "DSi power"},
        {HK_VolumeUp, "DSi volume up"},
        {HK_VolumeDown, "DSi volume down"},
    };
    return actions;
}

PhoneControllerLayout PhoneControllerLayout::defaults()
{
    PhoneControllerLayout layout;
    layout.items = {
        item("screen", "Touch screen", PhoneLayoutItem::Screen, .22, .05, .56, .90),
        item("l", "L", PhoneLayoutItem::Button, .025, .08, .14, .08, 9),
        item("r", "R", PhoneLayoutItem::Button, .835, .08, .14, .08, 8),
        item("dpad", "Directional control", PhoneLayoutItem::Directional, .035, .36, .13, .278),
        item("face", "ABXY", PhoneLayoutItem::FaceButtons, .835, .31, .165, .358),
        item("select", "Select", PhoneLayoutItem::Button, .055, .86, .085, .06, 2),
        item("start", "Start", PhoneLayoutItem::Button, .86, .86, .085, .06, 3),
    };
    layout.items[3].appearance = "dpad";
    return layout;
}

PhoneControllerLayout PhoneControllerLayout::fromJson(const QString& json, bool* valid)
{
    if (valid) *valid = false;
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(json.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) return defaults();
    const QJsonObject root = document.object();
    const QJsonArray array = root.value("items").toArray();
    const int version = root.value("version").toInt();
    if ((version != 1 && version != 2) || array.size() < 7 || array.size() > 32) return defaults();

    PhoneControllerLayout result;
    result.showHud = root.value("showHud").toBool(true);
    QSet<QString> ids;
    const QSet<QString> required {"screen", "l", "r", "dpad", "face", "select", "start"};
    int screens = 0;
    int dpads = 0;
    for (const QJsonValue& entry : array)
    {
        if (!entry.isObject()) return defaults();
        const QJsonObject object = entry.toObject();
        PhoneLayoutItem value;
        value.id = object.value("id").toString();
        value.label = object.value("label").toString().left(24);
        const QString kind = object.value("kind").toString();
        if (kind == "screen") { value.kind = PhoneLayoutItem::Screen; screens++; }
        else if (kind == "dpad" || kind == "directional") { value.kind = PhoneLayoutItem::Directional; dpads++; }
        else if (kind == "face") value.kind = PhoneLayoutItem::FaceButtons;
        else if (kind == "button") value.kind = PhoneLayoutItem::Button;
        else return defaults();
        value.rect = QRectF(object.value("x").toDouble(-1), object.value("y").toDouble(-1),
                            object.value("w").toDouble(-1), object.value("h").toDouble(-1));
        value.dsButton = object.value("dsButton").toInt(-1);
        value.hotkey = object.value("hotkey").toInt(-1);
        value.removable = object.value("removable").toBool(false);
        value.appearance = object.value("appearance").toString();
        if (value.kind == PhoneLayoutItem::Directional && value.appearance != "analog")
            value.appearance = "dpad";
        const bool validAction = value.kind != PhoneLayoutItem::Button
            || (value.dsButton >= 0 && value.dsButton < 12)
            || (value.hotkey >= 0 && value.hotkey < HK_MAX);
        if (value.id.isEmpty() || value.id.size() > 40 || ids.contains(value.id)
            || value.label.isEmpty() || !finiteRect(value.rect) || !validAction) return defaults();
        if ((value.id == "screen" && value.kind != PhoneLayoutItem::Screen)
            || (value.id == "dpad" && value.kind != PhoneLayoutItem::Directional)) return defaults();
        value.removable = !required.contains(value.id) && value.kind == PhoneLayoutItem::Button
            && value.hotkey >= 0;
        ids.insert(value.id);
        result.items.append(value);
    }

    if (!ids.contains("face") && ids.contains("x") && ids.contains("y")
        && ids.contains("a") && ids.contains("b"))
    {
        QRectF faceRect;
        for (int index = result.items.size() - 1; index >= 0; index--)
        {
            if (result.items[index].id == "x" || result.items[index].id == "y"
                || result.items[index].id == "a" || result.items[index].id == "b")
            {
                faceRect = faceRect.isNull() ? result.items[index].rect : faceRect.united(result.items[index].rect);
                ids.remove(result.items[index].id);
                result.items.removeAt(index);
            }
        }
        PhoneLayoutItem face = item("face", "ABXY", PhoneLayoutItem::FaceButtons,
                                    faceRect.x(), faceRect.y(), faceRect.width(), faceRect.height());
        result.items.append(face);
        ids.insert("face");
    }
    if (screens != 1 || dpads != 1) return defaults();
    for (const QString& id : required)
        if (!ids.contains(id)) return defaults();
    if (valid) *valid = true;
    return result;
}

QString PhoneControllerLayout::toJson() const
{
    QJsonArray array;
    for (const PhoneLayoutItem& value : items)
    {
        QJsonObject object {
            {"id", value.id}, {"label", value.label}, {"kind", kindName(value.kind)},
            {"x", value.rect.x()}, {"y", value.rect.y()},
            {"w", value.rect.width()}, {"h", value.rect.height()},
            {"dsButton", value.dsButton}, {"hotkey", value.hotkey},
            {"removable", value.removable}, {"appearance", value.appearance},
        };
        array.append(object);
    }
    return QString::fromUtf8(QJsonDocument(QJsonObject{{"version", 2}, {"showHud", showHud}, {"items", array}})
                                 .toJson(QJsonDocument::Compact));
}
