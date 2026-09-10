// Copyright (C) 2026 WideMelon contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "frontend/qt_sdl/PhoneProtocol.h"
#include "frontend/qt_sdl/PhoneSecurity.h"

#include <cstring>
#include <iostream>

#include <QJsonObject>
#include <QBuffer>
#include <QCoreApplication>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QtEndian>

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    if (!PhoneProtocol::IsPrivateIPv4(QHostAddress("10.1.2.3").toIPv4Address())) return 1;
    if (!PhoneProtocol::IsPrivateIPv4(QHostAddress("172.16.4.5").toIPv4Address())) return 2;
    if (!PhoneProtocol::IsPrivateIPv4(QHostAddress("192.168.1.9").toIPv4Address())) return 3;
    if (PhoneProtocol::IsPrivateIPv4(QHostAddress("8.8.8.8").toIPv4Address())) return 4;
    if (!PhoneProtocol::IsSameIPv4Subnet(QHostAddress("192.168.1.42").toIPv4Address(),
                                         QHostAddress("192.168.1.10").toIPv4Address(), 24)) return 32;
    if (PhoneProtocol::IsSameIPv4Subnet(QHostAddress("192.168.2.42").toIPv4Address(),
                                        QHostAddress("192.168.1.10").toIPv4Address(), 24)) return 33;
    if (!PhoneProtocol::IsSameIPv4Subnet(QHostAddress("10.0.0.1").toIPv4Address(),
                                         QHostAddress("10.0.0.1").toIPv4Address(), 32)) return 34;
    if (PhoneProtocol::IsSameIPv4Subnet(1, 1, 33)) return 35;

    const QByteArray host("192.168.1.10:24800");
    const QByteArray get = "GET / HTTP/1.1\r\nHost: " + host + "\r\n\r\n";
    if (PhoneProtocol::ParseHttpRequest(get, host).kind != PhoneProtocol::HttpRequestKind::Get) return 36;
    if (PhoneProtocol::ParseHttpRequest(get.left(get.size() - 2), host).kind
        != PhoneProtocol::HttpRequestKind::NeedMore) return 37;
    const QByteArray websocket = "GET /bridge HTTP/1.1\r\nHost: " + host
        + "\r\nOrigin: http://" + host
        + "\r\nUpgrade: websocket\r\nConnection: keep-alive, Upgrade\r\n"
          "Sec-WebSocket-Version: 13\r\nSec-WebSocket-Key: AAECAwQFBgcICQoLDA0ODw==\r\n\r\n";
    if (PhoneProtocol::ParseHttpRequest(websocket, host).kind
        != PhoneProtocol::HttpRequestKind::WebSocket) return 38;
    if (PhoneProtocol::ParseHttpRequest(websocket + "body", host).kind
        != PhoneProtocol::HttpRequestKind::Invalid) return 39;
    QByteArray badOrigin = websocket;
    badOrigin.replace("Origin: http://", "Origin: http://evil-");
    if (PhoneProtocol::ParseHttpRequest(badOrigin, host).kind
        != PhoneProtocol::HttpRequestKind::Invalid) return 40;
    QByteArray duplicateHost = get;
    duplicateHost.insert(duplicateHost.indexOf("\r\n\r\n"), "\r\nHost: " + host);
    if (PhoneProtocol::ParseHttpRequest(duplicateHost, host).kind
        != PhoneProtocol::HttpRequestKind::Invalid) return 41;
    const QByteArray bodyRequest = "GET / HTTP/1.1\r\nHost: " + host + "\r\nContent-Length: 1\r\n\r\nx";
    if (PhoneProtocol::ParseHttpRequest(bodyRequest, host).kind
        != PhoneProtocol::HttpRequestKind::Invalid) return 42;
    if (PhoneProtocol::ParseHttpRequest(QByteArray(PhoneProtocol::MaxHttpHeader + 1, 'x'), host).kind
        != PhoneProtocol::HttpRequestKind::Invalid) return 43;
    for (const QByteArray& field : {QByteArray(" Host: ") + host, QByteArray("Host : ") + host,
            QByteArray("X-Bad Header: value"), QByteArray("X-Test: a\rb"),
            QByteArray("X-Test: a\nb"), QByteArray("Content-Length: 0\r\nContent-Length: 0"),
            QByteArray("Sec-WebSocket-Key: a\r\nSec-WebSocket-Key: b"),
            QByteArray("Sec-WebSocket-Version: 13\r\nSec-WebSocket-Version: 12")})
    {
        QByteArray malformed = get;
        malformed.insert(malformed.indexOf("\r\n\r\n"), "\r\n" + field);
        if (PhoneProtocol::ParseHttpRequest(malformed, host).kind
            != PhoneProtocol::HttpRequestKind::Invalid) return 44;
    }

    QJsonObject touch{{"active", true}, {"x", 123}, {"y", 45}};
    QJsonObject input{{"v", PhoneProtocol::Version}, {"type", "input"}, {"seq", 7}, {"buttons", 0x411},
                      {"hotkeys", 1 << 4}, {"touch", touch}};
    melonDS::u32 keys = 0, hotkeys = 0, packedTouch = 0;
    quint32 sequence = 0;
    if (!PhoneProtocol::ParseInput(input, keys, hotkeys, packedTouch, sequence) || sequence != 7) return 5;
    if (keys != ((~0x411U) & 0xFFFU)) return 6;
    if (hotkeys != (1U << 4)) return 20;
    if (!(packedTouch & 0x80000000U) || (packedTouch & 0xFF) != 123 || ((packedTouch >> 8) & 0xFF) != 45) return 7;
    input["buttons"] = 0x1000;
    if (PhoneProtocol::ParseInput(input, keys, hotkeys, packedTouch, sequence)) return 8;
    input["buttons"] = 0;
    input["hotkeys"] = 1 << 23;
    if (PhoneProtocol::ParseInput(input, keys, hotkeys, packedTouch, sequence)) return 21;
    input["hotkeys"] = 0;
    input["touch"] = QJsonObject{{"active", true}, {"x", 256}, {"y", 0}};
    if (PhoneProtocol::ParseInput(input, keys, hotkeys, packedTouch, sequence)) return 9;
    input["touch"] = QJsonObject{{"active", false}, {"x", 0.5}, {"y", 0}};
    if (PhoneProtocol::ParseInput(input, keys, hotkeys, packedTouch, sequence)) return 17;

    const QByteArray jpeg("jpeg-data");
    const QByteArray frame = PhoneProtocol::BuildFrame(0x12345678U, 0x0102030405060708ULL, jpeg);
    if (frame.size() != PhoneProtocol::FrameHeaderSize + jpeg.size()) return 10;
    if (std::memcmp(frame.constData(), "WMF2", 4) != 0) return 11;
    if (qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(frame.constData() + 4)) != 0x12345678U) return 12;
    if (qFromLittleEndian<quint64>(reinterpret_cast<const uchar*>(frame.constData() + 8)) != 0x0102030405060708ULL) return 13;
    if (qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(frame.constData() + 16)) != 256) return 14;
    if (qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(frame.constData() + 18)) != 192) return 15;
    if (frame[20] != 1 || frame.mid(24) != jpeg) return 16;

    QImage testImage(256, 192, QImage::Format_RGB32);
    testImage.fill(QColor(20, 80, 160));
    QByteArray encoded;
    QBuffer output(&encoded);
    output.open(QIODevice::WriteOnly);
    QImageWriter writer(&output, "jpeg");
    writer.setQuality(85);
    if (!writer.write(testImage) || encoded.isEmpty()) return 18;
    QBuffer inputBuffer(&encoded);
    inputBuffer.open(QIODevice::ReadOnly);
    QImageReader reader(&inputBuffer, "jpeg");
    const QImage decoded = reader.read();
    if (decoded.size() != QSize(256, 192)) return 19;

    PhonePairingCredentials credentials;
    credentials.regenerate();
    const QByteArray firstSecret = credentials.secret();
    const QString firstCode = credentials.code();
    if (firstSecret.size() != 43 || firstCode.size() != 10) return 22;
    for (const QChar character : firstCode) if (!character.isDigit()) return 23;
    if (!credentials.matches(QString::fromLatin1(firstSecret)) || !credentials.matches(firstCode)) return 24;
    if (credentials.matches("00000000000") || credentials.matches("wrong")) return 25;

    PhoneAuthenticationLimiter limiter;
    for (int attempt = 0; attempt < PhoneAuthenticationLimiter::PeerFailureLimit - 1; attempt++)
    {
        limiter.recordFailure("peer-a", attempt);
        if (limiter.isBlocked("peer-a", attempt)) return 26;
    }
    limiter.recordFailure("peer-a", 4);
    limiter.recordFailure("peer-a", 50000);
    if (!limiter.isBlocked("peer-a", 4) || !limiter.isBlocked("peer-a", 60003)
        || limiter.isBlocked("peer-a", 60004)) return 27;
    if (credentials.secret() != firstSecret || credentials.code() != firstCode) return 28;

    limiter.clear();
    for (int attempt = 0; attempt < PhoneAuthenticationLimiter::GlobalFailureLimit; attempt++)
        limiter.recordFailure(QString("peer-%1").arg(attempt), attempt);
    limiter.recordFailure("unseen-peer", 50000);
    if (!limiter.isBlocked("unseen-peer", 20) || limiter.isBlocked("unseen-peer", 60019)) return 29;
    if (credentials.secret() != firstSecret || credentials.code() != firstCode) return 30;
    credentials.regenerate();
    if (credentials.secret() == firstSecret || credentials.code() == firstCode
        || credentials.matches(QString::fromLatin1(firstSecret)) || credentials.matches(firstCode)) return 31;

    std::cout << "Phone protocol, pairing, and rate limits passed\n";
    return 0;
}
