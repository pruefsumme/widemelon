// Pure protocol helpers shared by the WideMelon bridge and renderer tests.
// Copyright (C) 2026 WideMelon contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QHostAddress>
#include <QJsonObject>
#include <QtEndian>

#include "types.h"

namespace PhoneProtocol
{
constexpr int Version = 2;
constexpr int MaxControlMessage = 4096;
constexpr int FrameHeaderSize = 24;
constexpr int MaxHttpHeader = 8192;

enum class HttpRequestKind
{
    NeedMore,
    Invalid,
    Get,
    Head,
    WebSocket,
};

struct HttpRequest
{
    HttpRequestKind kind = HttpRequestKind::Invalid;
    QByteArray path;
};

inline bool HeaderContainsToken(const QByteArray& value, const QByteArray& token)
{
    for (const QByteArray& part : value.toLower().split(','))
        if (part.trimmed() == token) return true;
    return false;
}

inline HttpRequest ParseHttpRequest(const QByteArray& data, const QByteArray& expectedHost)
{
    if (data.size() > MaxHttpHeader) return {};
    const int end = data.indexOf("\r\n\r\n");
    if (end < 0) return {HttpRequestKind::NeedMore, {}};
    const QList<QByteArray> lines = data.left(end).split('\n');
    const QList<QByteArray> first = lines.value(0).trimmed().split(' ');
    if (first.size() != 3 || first[2] != "HTTP/1.1") return {};

    QByteArray host, origin, upgrade, connection;
    int hostCount = 0, originCount = 0, upgradeCount = 0, connectionCount = 0;
    bool invalidBody = data.size() != end + 4;
    for (int i = 1; i < lines.size(); i++)
    {
        const QByteArray line = lines[i].trimmed();
        const QByteArray lower = line.toLower();
        if (lower.startsWith("host:")) { host = line.mid(5).trimmed(); hostCount++; }
        else if (lower.startsWith("origin:")) { origin = line.mid(7).trimmed(); originCount++; }
        else if (lower.startsWith("upgrade:")) { upgrade = line.mid(8).trimmed().toLower(); upgradeCount++; }
        else if (lower.startsWith("connection:")) { connection = line.mid(11).trimmed(); connectionCount++; }
        else if (lower.startsWith("transfer-encoding:")) invalidBody = true;
        else if (lower.startsWith("content-length:") && line.mid(15).trimmed() != "0") invalidBody = true;
    }
    if (hostCount != 1 || host != expectedHost || invalidBody) return {};

    const bool upgradeRequested = upgradeCount != 0 || HeaderContainsToken(connection, "upgrade");
    if (first[0] == "GET" && first[1] == "/bridge" && upgradeCount == 1 && connectionCount == 1
        && upgrade == "websocket" && HeaderContainsToken(connection, "upgrade"))
    {
        if (originCount != 1 || origin != "http://" + expectedHost) return {};
        return {HttpRequestKind::WebSocket, first[1]};
    }
    if (upgradeRequested || originCount != 0) return {};
    if (first[0] == "GET") return {HttpRequestKind::Get, first[1]};
    if (first[0] == "HEAD") return {HttpRequestKind::Head, first[1]};
    return {};
}

inline bool IsPrivateIPv4(quint32 address)
{
    return (address & 0xFF000000U) == 0x0A000000U
        || (address & 0xFFF00000U) == 0xAC100000U
        || (address & 0xFFFF0000U) == 0xC0A80000U;
}

inline bool IsSameIPv4Subnet(quint32 address, quint32 networkAddress, int prefixLength)
{
    if (prefixLength < 0 || prefixLength > 32) return false;
    const quint32 mask = prefixLength == 0 ? 0U : (0xFFFFFFFFU << (32 - prefixLength));
    return (address & mask) == (networkAddress & mask);
}

inline bool ParseInput(const QJsonObject& object, melonDS::u32& activeLowKeys,
                       melonDS::u32& activeHotkeys, melonDS::u32& packedTouch, quint32& sequence)
{
    if (object.value("v").toInt() != Version || object.value("type").toString() != "input"
        || !object.value("seq").isDouble() || !object.value("buttons").isDouble()
        || !object.value("hotkeys").isDouble()
        || !object.value("touch").isObject()) return false;
    const double sequenceValue = object.value("seq").toDouble(-1);
    if (sequenceValue < 1 || sequenceValue > 0xFFFFFFFFU || sequenceValue != quint32(sequenceValue)) return false;
    sequence = quint32(sequenceValue);
    const double pressedValue = object.value("buttons").toDouble(-1);
    const int pressed = object.value("buttons").toInt(-1);
    const double hotkeyValue = object.value("hotkeys").toDouble(-1);
    const int hotkeys = object.value("hotkeys").toInt(-1);
    const QJsonObject touch = object.value("touch").toObject();
    if (!touch.value("active").isBool() || !touch.value("x").isDouble() || !touch.value("y").isDouble()) return false;
    const double xValue = touch.value("x").toDouble(-1);
    const double yValue = touch.value("y").toDouble(-1);
    const int x = touch.value("x").toInt(-1);
    const int y = touch.value("y").toInt(-1);
    const bool active = touch.value("active").toBool();
    if (pressedValue != pressed || hotkeyValue != hotkeys || xValue != x || yValue != y
        || pressed < 0 || pressed > 0xFFF || hotkeys < 0 || hotkeys > 0x7FFFFF
        || x < 0 || x > 255 || y < 0 || y > 191) return false;
    activeLowKeys = (~melonDS::u32(pressed)) & 0xFFF;
    activeHotkeys = melonDS::u32(hotkeys);
    packedTouch = active ? (0x80000000U | melonDS::u32(x) | (melonDS::u32(y) << 8)) : 0U;
    return true;
}

inline QByteArray BuildFrame(quint32 sequence, quint64 capturedUs, const QByteArray& jpeg)
{
    QByteArray packet;
    packet.reserve(FrameHeaderSize + jpeg.size());
    packet.append("WMF2", 4);
    const quint32 littleSequence = qToLittleEndian(sequence);
    const quint64 littleTime = qToLittleEndian(capturedUs);
    const quint16 littleWidth = qToLittleEndian<quint16>(256);
    const quint16 littleHeight = qToLittleEndian<quint16>(192);
    packet.append(reinterpret_cast<const char*>(&littleSequence), sizeof(littleSequence));
    packet.append(reinterpret_cast<const char*>(&littleTime), sizeof(littleTime));
    packet.append(reinterpret_cast<const char*>(&littleWidth), sizeof(littleWidth));
    packet.append(reinterpret_cast<const char*>(&littleHeight), sizeof(littleHeight));
    packet.append(char(1));
    packet.append("\0\0\0", 3);
    packet += jpeg;
    return packet;
}
}
