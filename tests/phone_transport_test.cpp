// Copyright (C) 2026 WideMelon contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "frontend/qt_sdl/PhoneProtocol.h"
#include "frontend/qt_sdl/PhoneSecurity.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>
#include <QWebSocket>
#include <QWebSocketServer>

#include <functional>

namespace
{
bool waitUntil(const std::function<bool()>& condition, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < timeoutMs)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    return condition();
}
}

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    QTcpServer tcp;
    QWebSocketServer websocket("transport test", QWebSocketServer::NonSecureMode);
    PhonePairingCredentials credentials;
    credentials.regenerate();
    if (!tcp.listen(QHostAddress::LocalHost, 0)) return 1;
    const QByteArray host = "127.0.0.1:" + QByteArray::number(tcp.serverPort());
    bool authenticated = false;

    QObject::connect(&tcp, &QTcpServer::newConnection, [&]
    {
        while (tcp.hasPendingConnections())
        {
            QTcpSocket* socket = tcp.nextPendingConnection();
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket]
            {
                const QByteArray data = socket->peek(PhoneProtocol::MaxHttpHeader + 1);
                const PhoneProtocol::HttpRequest request = PhoneProtocol::ParseHttpRequest(data, host);
                if (request.kind == PhoneProtocol::HttpRequestKind::NeedMore) return;
                if (request.kind == PhoneProtocol::HttpRequestKind::WebSocket)
                {
                    socket->disconnect();
                    websocket.handleConnection(socket);
                    return;
                }
                socket->readAll();
                if (request.kind == PhoneProtocol::HttpRequestKind::Get)
                    socket->write("HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nok");
                else
                    socket->write("HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                socket->disconnectFromHost();
            });
        }
    });
    QObject::connect(&websocket, &QWebSocketServer::newConnection, [&]
    {
        QWebSocket* socket = websocket.nextPendingConnection();
        socket->setParent(&websocket);
        QObject::connect(socket, &QWebSocket::textMessageReceived, socket, [&, socket](const QString& text)
        {
            const QJsonObject message = QJsonDocument::fromJson(text.toUtf8()).object();
            if (message.value("v").toInt() == PhoneProtocol::Version
                && message.value("type").toString() == "auth"
                && credentials.matches(message.value("credential").toString()))
            {
                authenticated = true;
                socket->sendTextMessage("{\"v\":2,\"type\":\"hello\"}");
            }
            else
            {
                socket->close(QWebSocketProtocol::CloseCodePolicyViolated, "Authentication failed");
            }
        });
    });

    QTcpSocket http;
    http.connectToHost(QHostAddress::LocalHost, tcp.serverPort());
    if (!http.waitForConnected(1000)) return 2;
    http.write("GET / HTTP/1.1\r\nHost: " + host + "\r\n\r\n");
    http.flush();
    if (!waitUntil([&] { return http.bytesAvailable() > 0; }, 1000)
        || !http.readAll().startsWith("HTTP/1.1 200 OK")) return 3;

    auto request = [&]
    {
        QNetworkRequest value(QUrl("ws://" + QString::fromLatin1(host) + "/bridge"));
        value.setRawHeader("Origin", "http://" + host);
        return value;
    };

    QWebSocket rejected;
    bool rejectedConnected = false;
    bool rejectedClosed = false;
    QObject::connect(&rejected, &QWebSocket::connected, [&] { rejectedConnected = true; });
    QObject::connect(&rejected, &QWebSocket::disconnected, [&] { rejectedClosed = true; });
    rejected.open(request());
    if (!waitUntil([&] { return rejectedConnected; }, 1000)) return 4;
    rejected.sendTextMessage("{\"v\":2,\"type\":\"auth\",\"credential\":\"wrong\"}");
    if (!waitUntil([&] { return rejectedClosed; }, 1000) || authenticated) return 5;

    QWebSocket accepted;
    bool acceptedConnected = false;
    QString serverMessage;
    QObject::connect(&accepted, &QWebSocket::connected, [&] { acceptedConnected = true; });
    QObject::connect(&accepted, &QWebSocket::textMessageReceived,
                     [&](const QString& text) { serverMessage = text; });
    accepted.open(request());
    if (!waitUntil([&] { return acceptedConnected; }, 1000)) return 6;
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    if (!serverMessage.isEmpty() || authenticated) return 7;
    accepted.sendTextMessage(QString::fromUtf8(QJsonDocument(QJsonObject{
        {"v", PhoneProtocol::Version}, {"type", "auth"}, {"credential", credentials.code()}
    }).toJson(QJsonDocument::Compact)));
    if (!waitUntil([&] { return !serverMessage.isEmpty(); }, 1000) || !authenticated) return 8;
    const QJsonObject hello = QJsonDocument::fromJson(serverMessage.toUtf8()).object();
    if (hello.value("v").toInt() != PhoneProtocol::Version || hello.value("type") != "hello") return 9;
    accepted.close();
    return 0;
}
