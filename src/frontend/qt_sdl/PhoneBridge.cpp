// WideMelon phone bottom-screen and controller bridge.
// Copyright (C) 2026 WideMelon contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "PhoneBridge.h"

#include <algorithm>
#include <cstdio>

#include <QBuffer>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QImageWriter>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QMutexLocker>
#include <QNetworkInterface>
#include <QPainter>
#include <QSaveFile>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>
#include <QWaitCondition>
#include <QWebSocket>
#include <QWebSocketCorsAuthenticator>
#include <QWebSocketServer>

#include "Config.h"
#include "PhoneLayout.h"
#include "PhoneProtocol.h"
#include "PhoneFirewall.h"
#include "Platform.h"

namespace
{
constexpr int kHeartbeatTimeoutMs = 1000;
constexpr int kAuthenticationTimeoutMs = 3000;
constexpr int kMaxPendingClients = 4;
constexpr int kMaxLiveLogs = 1000;
constexpr qint64 kMaxLogBytes = 2 * 1024 * 1024;

QString levelName(int level)
{
    switch (level)
    {
    case 0: return "ERROR";
    case 1: return "INFO";
    case 2: return "DEBUG";
    default: return "TRACE";
    }
}

QByteArray resource(const char* path)
{
    QFile file(QString::fromLatin1(path));
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}

QByteArray httpReply(int status, const QByteArray& reason, const QByteArray& type,
                     const QByteArray& body, const QByteArray& csp = {}, qint64 declaredLength = -1)
{
    QByteArray reply = "HTTP/1.1 " + QByteArray::number(status) + " " + reason + "\r\n";
    reply += "Content-Type: " + type + "\r\n";
    reply += "Content-Length: " + QByteArray::number(declaredLength >= 0 ? declaredLength : body.size()) + "\r\n";
    reply += "Connection: close\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n";
    reply += "Referrer-Policy: no-referrer\r\nCross-Origin-Resource-Policy: same-origin\r\n";
    reply += "Permissions-Policy: camera=(), microphone=(), geolocation=(), payment=(), usb=()\r\n";
    if (!csp.isEmpty()) reply += "Content-Security-Policy: " + csp + "\r\n";
    reply += "\r\n";
    reply += body;
    return reply;
}

QString sanitizedAddress(const QHostAddress& address)
{
    if (address.isLoopback()) return "this computer";
    const QStringList parts = address.toString().split('.');
    if (parts.size() == 4) return parts[0] + '.' + parts[1] + ".x.x";
    return "local network device";
}

}

class PhoneBridgeManager::EncoderThread final : public QThread
{
public:
    explicit EncoderThread(PhoneBridgeManager* owner) : owner(owner) {}

    ~EncoderThread() override
    {
        {
            QMutexLocker lock(&mutex);
            quitting = true;
            condition.wakeOne();
        }
        wait();
    }

    bool submit(const QImage& image, quint32 generation, quint32 sequence, int quality)
    {
        QMutexLocker lock(&mutex);
        const bool replaced = hasPending;
        pendingImage = image;
        pendingGeneration = generation;
        pendingSequence = sequence;
        pendingQuality = quality;
        hasPending = true;
        condition.wakeOne();
        return replaced;
    }

protected:
    void run() override
    {
        for (;;)
        {
            QImage image;
            quint32 generation;
            quint32 sequence;
            int quality;
            {
                QMutexLocker lock(&mutex);
                while (!hasPending && !quitting) condition.wait(&mutex);
                if (quitting) return;
                image = pendingImage;
                generation = pendingGeneration;
                sequence = pendingSequence;
                quality = pendingQuality;
                hasPending = false;
            }

            QElapsedTimer timer;
            timer.start();
            QByteArray bytes;
            QBuffer buffer(&bytes);
            buffer.open(QIODevice::WriteOnly);
            QImageWriter writer(&buffer, "jpeg");
            writer.setQuality(quality);
            writer.setOptimizedWrite(false);
            const bool ok = writer.write(image);
            const double elapsed = timer.nsecsElapsed() / 1000000.0;
            const QString error = ok ? QString() : writer.errorString();
            QMetaObject::invokeMethod(owner, [this, generation, sequence, bytes, elapsed, error]
            {
                if (!error.isEmpty())
                    owner->log(Error, "encode", "JPEG encoding failed: " + error);
                else
                    owner->encodedFrameReady(generation, sequence, bytes, elapsed);
            }, Qt::QueuedConnection);
        }
    }

private:
    PhoneBridgeManager* owner;
    QMutex mutex;
    QWaitCondition condition;
    QImage pendingImage;
    quint32 pendingGeneration = 0;
    quint32 pendingSequence = 0;
    int pendingQuality = 85;
    bool hasPending = false;
    bool quitting = false;
};

PhoneBridgeManager::PhoneBridgeManager(QObject* parent) : QObject(parent)
{
    currentSettings = loadSettings();
    currentStatus = "Off";
    httpServer = new QTcpServer(this);
    httpServer->setMaxPendingConnections(8);
    webSocketServer = new QWebSocketServer("WideMelon phone bridge", QWebSocketServer::NonSecureMode, this);
    webSocketServer->setMaxPendingConnections(kMaxPendingClients);
    heartbeatTimer = new QTimer(this);
    heartbeatTimer->setInterval(250);
    testPatternTimer = new QTimer(this);
    testPatternTimer->setInterval(33);
    encoder = std::make_unique<EncoderThread>(this);
    encoder->start();

    connect(httpServer, &QTcpServer::newConnection, this, &PhoneBridgeManager::acceptTcpConnections);
    connect(webSocketServer, &QWebSocketServer::newConnection, this, &PhoneBridgeManager::acceptWebSocket);
    connect(webSocketServer, &QWebSocketServer::originAuthenticationRequired, this,
            [this](QWebSocketCorsAuthenticator* auth)
    {
        const QUrl origin(auth->origin());
        const bool allowed = origin.scheme() == "http"
            && origin.host() == currentSettings.address
            && origin.port(80) == currentSettings.basePort;
        auth->setAllowed(allowed);
        if (!allowed) log(Debug, "websocket", "Rejected origin " + auth->origin());
    });
    connect(heartbeatTimer, &QTimer::timeout, this, &PhoneBridgeManager::checkHeartbeat);
    connect(testPatternTimer, &QTimer::timeout, this, &PhoneBridgeManager::emitTestPattern);
}

PhoneBridgeManager::~PhoneBridgeManager()
{
    stop();
    encoder.reset();
}

PhoneBridgeSettings PhoneBridgeManager::loadSettings()
{
    PhoneBridgeSettings value;
    auto cfg = Config::GetGlobalTable();
    value.address = QString::fromStdString(cfg.GetString("WideMelon.Phone.Interface"));
    const int port = cfg.GetInt("WideMelon.Phone.BasePort");
    const int quality = cfg.GetInt("WideMelon.Phone.JpegQuality");
    const int level = cfg.GetInt("WideMelon.Phone.LogLevel");
    if (port >= 1024 && port <= 65534) value.basePort = quint16(port);
    if (quality >= 30 && quality <= 100) value.jpegQuality = quality;
    if (level >= 0 && level <= 3) value.logLevel = level;
    value.consoleLog = cfg.GetBool("WideMelon.Phone.ConsoleLog");
    value.fileLog = cfg.GetBool("WideMelon.Phone.FileLog");
    value.synchronousCapture = cfg.GetBool("WideMelon.Phone.SynchronousCapture");
    value.layoutJson = PhoneControllerLayout::fromJson(
        QString::fromStdString(cfg.GetString("WideMelon.Phone.Layout"))).toJson();

    const QString envLevel = qEnvironmentVariable("WIDEMELON_PHONE_LOG_LEVEL").toLower();
    if (envLevel == "error") value.logLevel = 0;
    else if (envLevel == "info") value.logLevel = 1;
    else if (envLevel == "debug") value.logLevel = 2;
    else if (envLevel == "trace") value.logLevel = 3;
    if (qEnvironmentVariableIsSet("WIDEMELON_PHONE_LOG_FILE"))
        value.fileLog = qEnvironmentVariableIntValue("WIDEMELON_PHONE_LOG_FILE") != 0;

    const QStringList addresses = availableIPv4Addresses(false);
    if (!addresses.contains(value.address))
    {
        value.address.clear();
        for (const QString& address : addresses)
            if (isPrivateAddress(address)) { value.address = address; break; }
        if (value.address.isEmpty() && !addresses.isEmpty()) value.address = addresses.first();
    }
    return value;
}

void PhoneBridgeManager::saveSettings(const PhoneBridgeSettings& value)
{
    auto cfg = Config::GetGlobalTable();
    cfg.SetString("WideMelon.Phone.Interface", value.address.toStdString());
    cfg.SetInt("WideMelon.Phone.BasePort", value.basePort);
    cfg.SetInt("WideMelon.Phone.JpegQuality", value.jpegQuality);
    cfg.SetInt("WideMelon.Phone.LogLevel", value.logLevel);
    cfg.SetBool("WideMelon.Phone.ConsoleLog", value.consoleLog);
    cfg.SetBool("WideMelon.Phone.FileLog", value.fileLog);
    cfg.SetBool("WideMelon.Phone.SynchronousCapture", value.synchronousCapture);
    cfg.SetString("WideMelon.Phone.Layout", value.layoutJson.toStdString());
    Config::Save();
}

QStringList PhoneBridgeManager::availableIPv4Addresses(bool includeLoopback)
{
    QStringList result;
    for (const QNetworkInterface& interface : QNetworkInterface::allInterfaces())
    {
        if (!(interface.flags() & QNetworkInterface::IsUp)
            || !(interface.flags() & QNetworkInterface::IsRunning)) continue;
        for (const QNetworkAddressEntry& entry : interface.addressEntries())
        {
            const QHostAddress address = entry.ip();
            if (address.protocol() != QAbstractSocket::IPv4Protocol) continue;
            if (!includeLoopback && address.isLoopback()) continue;
            if (!address.isLoopback() && !PhoneProtocol::IsPrivateIPv4(address.toIPv4Address())) continue;
            const QString text = address.toString();
            if (!result.contains(text)) result.append(text);
        }
    }
    return result;
}

bool PhoneBridgeManager::isPrivateAddress(const QString& text)
{
    return PhoneProtocol::IsPrivateIPv4(QHostAddress(text).toIPv4Address());
}

PhoneBridgeSettings PhoneBridgeManager::settings() const
{
    QMutexLocker lock(&stateMutex);
    return currentSettings;
}

void PhoneBridgeManager::setSettings(const PhoneBridgeSettings& value)
{
    bool layoutChanged = false;
    QMutexLocker lock(&stateMutex);
    layoutChanged = currentSettings.layoutJson != value.layoutJson;
    if (isListening())
    {
        PhoneBridgeSettings live = value;
        live.address = currentSettings.address;
        live.basePort = currentSettings.basePort;
        live.synchronousCapture = currentSettings.synchronousCapture;
        currentSettings = live;
    }
    else
    {
        currentSettings = value;
    }
    lock.unlock();
    if (layoutChanged) sendLayout();
}

bool PhoneBridgeManager::start()
{
    if (isListening()) return true;
    errorText.clear();
    if (currentSettings.address.isEmpty())
    {
        errorText = "No usable IPv4 network interface is available.";
        currentStatus = "No network";
        log(Error, "network", errorText);
        emit statusChanged();
        return false;
    }
    const QHostAddress bindAddress(currentSettings.address);
    if (bindAddress.isNull() || (!bindAddress.isLoopback() && !isPrivateAddress(currentSettings.address)))
    {
        errorText = "The selected address is not a private IPv4 interface.";
        currentStatus = "Error";
        log(Error, "network", errorText);
        emit statusChanged();
        return false;
    }
    if (!httpServer->listen(bindAddress, currentSettings.basePort))
    {
        errorText = "HTTP port could not be opened: " + httpServer->errorString();
        currentStatus = "Error";
        log(Error, "network", errorText);
        emit statusChanged();
        return false;
    }
    generatePairingCredentials();
    heartbeatClock.start();
    lastHeartbeatMs = heartbeatClock.elapsed();
    controlRateWindowMs = lastHeartbeatMs;
    controlMessagesInWindow = 0;
    currentStatus = "Waiting for paired phone";
    heartbeatTimer->start();
    if (testPattern.load()) testPatternTimer->start();
    log(Info, "lifecycle", QString("Listening for a paired phone at %1").arg(url()));
    emit statusChanged();
    return true;
}

void PhoneBridgeManager::stop()
{
    testPatternTimer->stop();
    heartbeatTimer->stop();
    disconnectClient();
    const QSet<QWebSocket*> pending = pendingClients;
    for (QWebSocket* socket : pending)
        closePendingClient(socket, QWebSocketProtocol::CloseCodeGoingAway, "Bridge stopped");
    if (httpServer->isListening()) httpServer->close();
    const QSet<QTcpSocket*> tcpSockets = pendingTcpSockets;
    for (QTcpSocket* socket : tcpSockets) socket->disconnectFromHost();
    pendingTcpSockets.clear();
    clearPairingCredentials();
    if (currentStatus != "Off") log(Info, "lifecycle", "Phone bridge stopped");
    currentStatus = "Off";
    errorText.clear();
    emit statusChanged();
}

void PhoneBridgeManager::disconnectClient()
{
    if (client)
    {
        QWebSocket* old = client;
        client = nullptr;
        old->close(QWebSocketProtocol::CloseCodeNormal, "Disconnected by WideMelon");
        old->deleteLater();
    }
    pendingPacket.clear();
    frameInFlight = false;
    resetRemoteInput();
    setConnected(false);
    if (isListening()) currentStatus = "Waiting for paired phone";
    emit statusChanged();
}

bool PhoneBridgeManager::isListening() const
{
    return httpServer && httpServer->isListening();
}

QString PhoneBridgeManager::statusText() const { return currentStatus; }
QString PhoneBridgeManager::url() const
{
    return QString("http://%1:%2/").arg(currentSettings.address).arg(currentSettings.basePort);
}
QString PhoneBridgeManager::pairingUrl() const
{
    if (!isListening() || pairingCredentials.isEmpty()) return {};
    return url() + "#pair=" + QString::fromLatin1(pairingCredentials.secret());
}
QString PhoneBridgeManager::pairingCode() const { return pairingCredentials.code(); }
QString PhoneBridgeManager::connectedClientLabel() const { return clientAddressLabel; }
QString PhoneBridgeManager::lastError() const { return errorText; }

void PhoneBridgeManager::regeneratePairing()
{
    if (!isListening()) return;
    disconnectClient();
    const QSet<QWebSocket*> pending = pendingClients;
    for (QWebSocket* socket : pending)
        closePendingClient(socket, QWebSocketProtocol::CloseCodePolicyViolated, "Pairing changed");
    generatePairingCredentials();
    currentStatus = "Waiting for paired phone";
    log(Info, "security", "Pairing credentials regenerated");
    emit statusChanged();
}
melonDS::u32 PhoneBridgeManager::remoteKeyMask() const { return remoteKeys.load(); }
melonDS::u32 PhoneBridgeManager::remoteHotkeyMask() const { return remoteHotkeys.load(); }
melonDS::u32 PhoneBridgeManager::remoteTouchSnapshot() const { return remoteTouch.load(); }

void PhoneBridgeManager::submitFrame(const QImage& image)
{
    if (!wantsFrames() || image.isNull()) return;
    const quint32 sequence = frameSequence.fetch_add(1) + 1;
    int quality;
    {
        QMutexLocker lock(&stateMutex);
        currentMetrics.framesOffered++;
        quality = currentSettings.jpegQuality;
    }
    if (encoder->submit(image, connectionGeneration.load(), sequence, quality))
    {
        QMutexLocker lock(&stateMutex);
        currentMetrics.framesDropped++;
    }
}

void PhoneBridgeManager::setTestPattern(bool enabled)
{
    const bool wasUsable = hasUsableClient();
    testPattern.store(enabled);
    if (enabled && isListening()) testPatternTimer->start();
    else testPatternTimer->stop();
    log(Info, "diagnostics", enabled ? "Test pattern enabled" : "Test pattern disabled");
    if (wasUsable != hasUsableClient()) emit connectionChanged(hasUsableClient());
}

void PhoneBridgeManager::setCaptureAvailable(bool available, const QString& reason)
{
    const bool wasUsable = hasUsableClient();
    const bool changed = captureAvailable.exchange(available) != available;
    if (changed)
        log(available ? Info : (reason.isEmpty() ? Debug : Error), "capture",
            available ? "OpenGL phone capture is ready"
            : (reason.isEmpty() ? "OpenGL phone capture is unavailable" : reason));
    if (wasUsable != hasUsableClient()) emit connectionChanged(hasUsableClient());
}

void PhoneBridgeManager::reportCaptureDrop(const QString& reason)
{
    {
        QMutexLocker lock(&stateMutex);
        currentMetrics.framesDropped++;
    }
    log(Debug, "capture", reason);
}

PhoneBridgeMetrics PhoneBridgeManager::metrics() const
{
    QMutexLocker lock(&stateMutex);
    return currentMetrics;
}

QStringList PhoneBridgeManager::logLines() const
{
    QMutexLocker lock(&stateMutex);
    return recentLogs;
}

bool PhoneBridgeManager::exportDiagnostics(const QString& path, QString* error) const
{
    QJsonObject root;
    const PhoneBridgeMetrics m = metrics();
    root["generated"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    root["status"] = statusText();
    root["address"] = currentSettings.address.isEmpty() ? QString() : QString("x.x.x.%1").arg(currentSettings.address.section('.', -1));
    root["basePort"] = int(currentSettings.basePort);
    root["jpegQuality"] = currentSettings.jpegQuality;
    root["framesOffered"] = qint64(m.framesOffered);
    root["framesEncoded"] = qint64(m.framesEncoded);
    root["framesSent"] = qint64(m.framesSent);
    root["framesAcked"] = qint64(m.framesAcked);
    root["framesDropped"] = qint64(m.framesDropped);
    root["bytesSent"] = qint64(m.bytesSent);
    root["protocolErrors"] = qint64(m.protocolErrors);
    root["authenticationFailures"] = qint64(m.authenticationFailures);
    root["lastEncodeMs"] = m.lastEncodeMs;
    root["averageEncodeMs"] = m.averageEncodeMs;
    root["roundTripMs"] = m.roundTripMs;
    const PhoneFirewallResult firewall = InspectPhoneFirewall(currentSettings.address, currentSettings.basePort);
    root["firewallDetected"] = firewall.detected;
    root["firewall"] = firewall.status == PhoneFirewallStatus::Allowed ? "allowed"
        : firewall.status == PhoneFirewallStatus::Blocked ? "blocked" : "unknown";
    root["possibleVpnInterface"] = IsLikelyVpnInterface(currentSettings.address);
    const QStringList lines = logLines();
    root["logs"] = QJsonArray::fromStringList(lines.mid(std::max(0, int(lines.size()) - 500)));
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(root).toJson()) < 0 || !file.commit())
    {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}

void PhoneBridgeManager::acceptTcpConnections()
{
    while (httpServer->hasPendingConnections())
    {
        QTcpSocket* socket = httpServer->nextPendingConnection();
        if (pendingTcpSockets.size() >= 8)
        {
            socket->disconnectFromHost();
            socket->deleteLater();
            continue;
        }
        handleTcpSocket(socket);
    }
}

void PhoneBridgeManager::handleTcpSocket(QTcpSocket* socket)
{
    socket->setParent(this);
    if (!peerAllowed(socket->peerAddress()))
    {
        socket->disconnectFromHost();
        socket->deleteLater();
        log(Debug, "security", "Rejected a connection outside the selected local subnet");
        return;
    }
    pendingTcpSockets.insert(socket);
    QTimer* timeout = new QTimer(socket);
    timeout->setSingleShot(true);
    timeout->start(3000);
    connect(timeout, &QTimer::timeout, socket, &QTcpSocket::disconnectFromHost);
    connect(socket, &QTcpSocket::disconnected, this, [this, socket]
    {
        pendingTcpSockets.remove(socket);
        socket->deleteLater();
    });
    connect(socket, &QTcpSocket::readyRead, this, [this, socket]
    {
        if (!socket->property("routed").toBool()) routeTcpSocket(socket);
    });
}

void PhoneBridgeManager::routeTcpSocket(QTcpSocket* socket)
{
    const QByteArray data = socket->peek(PhoneProtocol::MaxHttpHeader + 1);
    if (data.size() > PhoneProtocol::MaxHttpHeader)
    {
        socket->write(httpReply(431, "Request Header Fields Too Large", "text/plain", "Request too large\n"));
        socket->disconnectFromHost();
        return;
    }
    const QByteArray expected = currentSettings.address.toUtf8() + ':' + QByteArray::number(currentSettings.basePort);
    const PhoneProtocol::HttpRequest request = PhoneProtocol::ParseHttpRequest(data, expected);
    if (request.kind == PhoneProtocol::HttpRequestKind::NeedMore) return;
    if (request.kind == PhoneProtocol::HttpRequestKind::Invalid)
    {
        socket->write(httpReply(400, "Bad Request", "text/plain", "Invalid request\n"));
        socket->disconnectFromHost();
        return;
    }

    if (request.kind == PhoneProtocol::HttpRequestKind::WebSocket)
    {
        socket->setProperty("routed", true);
        if (QTimer* timeout = socket->findChild<QTimer*>()) timeout->stop();
        pendingTcpSockets.remove(socket);
        socket->disconnect(this);
        webSocketServer->handleConnection(socket);
        return;
    }
    serveHttpSocket(socket, data, data.indexOf("\r\n\r\n"));
}

void PhoneBridgeManager::serveHttpSocket(QTcpSocket* socket, const QByteArray& data, int headerEnd)
{
    socket->readAll();
    const QList<QByteArray> request = data.left(headerEnd).split('\n').value(0).trimmed().split(' ');
    const bool head = request.value(0) == "HEAD";
    if (!head && request.value(0) != "GET")
    {
        socket->write(httpReply(405, "Method Not Allowed", "text/plain", "Only GET and HEAD are supported\n"));
        socket->disconnectFromHost();
        return;
    }
    QByteArray body, type;
    const QByteArray path = request[1];
    if (path == "/" || path == "/index.html") { body = resource(":/phone/index.html"); type = "text/html; charset=utf-8"; }
    else if (path == "/app.js") { body = resource(":/phone/app.js"); type = "text/javascript; charset=utf-8"; }
    else if (path == "/app.css") { body = resource(":/phone/app.css"); type = "text/css; charset=utf-8"; }
    else
    {
        socket->write(httpReply(404, "Not Found", "text/plain", "Not found\n"));
        socket->disconnectFromHost();
        return;
    }
    const QByteArray expected = currentSettings.address.toUtf8() + ':' + QByteArray::number(currentSettings.basePort);
    const QByteArray csp = "default-src 'self'; connect-src 'self' ws://" + expected + " wss://" + expected
        + "; img-src 'self' blob:; style-src 'self'; script-src 'self'; frame-ancestors 'none'; base-uri 'none'; form-action 'none'";
    socket->write(httpReply(200, "OK", type, head ? QByteArray() : body, csp, body.size()));
    socket->disconnectFromHost();
    log(Debug, "http", QString("Served %1").arg(QString::fromUtf8(path)));
}

void PhoneBridgeManager::acceptWebSocket()
{
    while (webSocketServer->hasPendingConnections())
    {
        QWebSocket* socket = webSocketServer->nextPendingConnection();
        socket->setMaxAllowedIncomingFrameSize(PhoneProtocol::MaxControlMessage);
        socket->setMaxAllowedIncomingMessageSize(PhoneProtocol::MaxControlMessage);
        if (socket->requestUrl().path() != "/bridge" || !socket->requestUrl().query().isEmpty()
            || !peerAllowed(socket->peerAddress()))
        {
            socket->close(QWebSocketProtocol::CloseCodePolicyViolated, "Invalid endpoint");
            socket->deleteLater();
            continue;
        }
        const qint64 now = heartbeatClock.elapsed();
        if (client || pendingClients.size() >= kMaxPendingClients
            || authenticationTemporarilyBlocked(socket->peerAddress(), now))
        {
            socket->close(QWebSocketProtocol::CloseCodePolicyViolated, "Pairing unavailable");
            socket->deleteLater();
            log(Debug, "security", "Rejected a new pairing connection");
            continue;
        }
        socket->setParent(this);
        pendingClients.insert(socket);
        connect(socket, &QWebSocket::textMessageReceived, this, [this, socket](const QString& message)
        {
            if (pendingClients.contains(socket)) handlePendingMessage(socket, message);
            else if (client == socket) handleTextMessage(message);
        });
        connect(socket, &QWebSocket::binaryMessageReceived, this, [this, socket](const QByteArray&)
        {
            { QMutexLocker lock(&stateMutex); currentMetrics.protocolErrors++; }
            if (pendingClients.contains(socket))
                closePendingClient(socket, QWebSocketProtocol::CloseCodeDatatypeNotSupported, "Authentication required");
            else if (client == socket)
            {
                resetRemoteInput();
                client->close(QWebSocketProtocol::CloseCodeDatatypeNotSupported, "Client binary messages are not supported");
            }
        });
        connect(socket, &QWebSocket::disconnected, this, [this, socket]
        {
            const bool authenticated = client == socket;
            pendingClients.remove(socket);
            if (authenticated) client = nullptr;
            socket->deleteLater();
            if (authenticated)
            {
                pendingPacket.clear();
                frameInFlight = false;
                clientAddressLabel.clear();
                resetRemoteInput();
                setConnected(false);
                if (isListening()) currentStatus = "Waiting for paired phone";
                log(Info, "websocket", "Phone disconnected; restored desktop bottom screen");
                emit statusChanged();
            }
        });
        QTimer::singleShot(kAuthenticationTimeoutMs, socket, [this, socket]
        {
            if (pendingClients.contains(socket))
                closePendingClient(socket, QWebSocketProtocol::CloseCodePolicyViolated, "Authentication timeout");
        });
    }
}

void PhoneBridgeManager::handlePendingMessage(QWebSocket* socket, const QString& message)
{
    if (message.toUtf8().size() > PhoneProtocol::MaxControlMessage)
    {
        recordAuthenticationFailure(socket->peerAddress(), heartbeatClock.elapsed());
        closePendingClient(socket, QWebSocketProtocol::CloseCodeTooMuchData, "Authentication failed");
        return;
    }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(message.toUtf8(), &error);
    const QJsonObject object = document.object();
    const QString credential = object.value("credential").toString();
    const bool validShape = error.error == QJsonParseError::NoError && document.isObject()
        && object.value("v").toInt() == PhoneProtocol::Version
        && object.value("type").toString() == "auth"
        && object.value("credential").isString() && credential.size() <= 64;
    const bool validCredential = pairingCredentials.matches(credential);
    if (!validShape || !validCredential || authenticationTemporarilyBlocked(socket->peerAddress(), heartbeatClock.elapsed()))
    {
        recordAuthenticationFailure(socket->peerAddress(), heartbeatClock.elapsed());
        closePendingClient(socket, QWebSocketProtocol::CloseCodePolicyViolated, "Authentication failed");
        return;
    }
    authenticateClient(socket);
}

void PhoneBridgeManager::authenticateClient(QWebSocket* socket)
{
    if (!pendingClients.remove(socket) || client)
    {
        socket->close(QWebSocketProtocol::CloseCodePolicyViolated, "Pairing unavailable");
        return;
    }
    client = socket;
    clientAddressLabel = sanitizedAddress(socket->peerAddress());
    lastHeartbeatMs = heartbeatClock.elapsed();
    controlRateWindowMs = lastHeartbeatMs;
    controlMessagesInWindow = 0;
    currentStatus = "Connected to paired phone";
    setConnected(true);
    QJsonObject hello{{"v", PhoneProtocol::Version}, {"type", "hello"}, {"width", 256}, {"height", 192}, {"fps", 30}};
    hello["layout"] = QJsonDocument::fromJson(currentSettings.layoutJson.toUtf8()).object();
    client->sendTextMessage(QString::fromUtf8(QJsonDocument(hello).toJson(QJsonDocument::Compact)));
    log(Info, "security", "Paired phone authenticated (" + clientAddressLabel + ")");
    emit statusChanged();
}

void PhoneBridgeManager::closePendingClient(QWebSocket* socket, QWebSocketProtocol::CloseCode code, const QString& reason)
{
    pendingClients.remove(socket);
    socket->close(code, reason);
}

bool PhoneBridgeManager::peerAllowed(const QHostAddress& peer) const
{
    if (peer.protocol() != QAbstractSocket::IPv4Protocol) return false;
    const QHostAddress selected(currentSettings.address);
    if (selected.isLoopback()) return peer.isLoopback();
    for (const QNetworkInterface& interface : QNetworkInterface::allInterfaces())
    {
        if (!(interface.flags() & QNetworkInterface::IsUp)
            || !(interface.flags() & QNetworkInterface::IsRunning)) continue;
        for (const QNetworkAddressEntry& entry : interface.addressEntries())
        {
            if (entry.ip() == selected && entry.prefixLength() >= 0)
                return PhoneProtocol::IsSameIPv4Subnet(peer.toIPv4Address(), entry.ip().toIPv4Address(),
                                                       entry.prefixLength());
        }
    }
    return false;
}

bool PhoneBridgeManager::authenticationTemporarilyBlocked(const QHostAddress& peer, qint64 now)
{
    return authenticationLimiter.isBlocked(peer.toString(), now);
}

void PhoneBridgeManager::recordAuthenticationFailure(const QHostAddress& peer, qint64 now)
{
    authenticationLimiter.recordFailure(peer.toString(), now);
    {
        QMutexLocker lock(&stateMutex);
        currentMetrics.authenticationFailures++;
    }
    log(Debug, "security", "Rejected invalid pairing credentials from " + sanitizedAddress(peer));
}

void PhoneBridgeManager::generatePairingCredentials()
{
    clearPairingCredentials();
    pairingCredentials.regenerate();
    authenticationLimiter.clear();
    emit pairingChanged();
}

void PhoneBridgeManager::clearPairingCredentials()
{
    pairingCredentials.clear();
}

void PhoneBridgeManager::handleTextMessage(const QString& message)
{
    const qint64 messageTime = heartbeatClock.elapsed();
    if (messageTime - controlRateWindowMs >= 1000)
    {
        controlRateWindowMs = messageTime;
        controlMessagesInWindow = 0;
    }
    if (++controlMessagesInWindow > 240)
    {
        log(Error, "protocol", "Control-message rate limit exceeded");
        resetRemoteInput();
        if (client) client->close(QWebSocketProtocol::CloseCodePolicyViolated, "Message rate limit");
        return;
    }
    if (message.toUtf8().size() > PhoneProtocol::MaxControlMessage)
    {
        log(Error, "protocol", "Oversized control message");
        resetRemoteInput();
        if (client) client->close(QWebSocketProtocol::CloseCodeTooMuchData, "Control message too large");
        return;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(message.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        { QMutexLocker lock(&stateMutex); currentMetrics.protocolErrors++; }
        log(Debug, "protocol", "Rejected malformed JSON");
        resetRemoteInput();
        if (client) client->close(QWebSocketProtocol::CloseCodeProtocolError, "Protocol violation");
        return;
    }
    const QJsonObject object = document.object();
    if (object.value("v").toInt() != PhoneProtocol::Version || !object.value("type").isString())
    {
        { QMutexLocker lock(&stateMutex); currentMetrics.protocolErrors++; }
        log(Debug, "protocol", "Rejected unknown protocol version or message type");
        resetRemoteInput();
        if (client) client->close(QWebSocketProtocol::CloseCodeProtocolError, "Protocol violation");
        return;
    }
    const QString type = object.value("type").toString();
    if (type == "input")
    {
        melonDS::u32 keys = 0, hotkeys = 0, touch = 0;
        quint32 sequence = 0;
        if (!PhoneProtocol::ParseInput(object, keys, hotkeys, touch, sequence) || sequence <= lastInputSequence)
        {
            { QMutexLocker lock(&stateMutex); currentMetrics.protocolErrors++; }
            log(Debug, "protocol", "Rejected invalid input snapshot");
            resetRemoteInput();
            if (client) client->close(QWebSocketProtocol::CloseCodeProtocolError, "Protocol violation");
            return;
        }
        lastInputSequence = sequence;
        lastHeartbeatMs = messageTime;
        remoteKeys.store(keys);
        remoteHotkeys.store(hotkeys);
        remoteTouch.store(touch);
        log(Trace, "input", QString("active-low buttons=0x%1 hotkeys=0x%2 touch=%3")
            .arg(keys, 3, 16, QLatin1Char('0')).arg(hotkeys, 6, 16, QLatin1Char('0'))
            .arg(bool(touch & 0x80000000U)));
    }
    else if (type == "frameAck")
    {
        const double sequenceValue = object.value("seq").toDouble(-1);
        if (!object.value("seq").isDouble() || sequenceValue < 1 || sequenceValue > 0xFFFFFFFFU
            || sequenceValue != quint32(sequenceValue))
        {
            { QMutexLocker lock(&stateMutex); currentMetrics.protocolErrors++; }
            resetRemoteInput();
            if (client) client->close(QWebSocketProtocol::CloseCodeProtocolError, "Protocol violation");
            return;
        }
        lastHeartbeatMs = messageTime;
        const quint32 sequence = quint32(sequenceValue);
        if (frameInFlight && sequence == inFlightSequence)
        {
            frameInFlight = false;
            { QMutexLocker lock(&stateMutex); currentMetrics.framesAcked++; }
            sendPendingFrame();
        }
    }
    else if (type == "pong")
    {
        const double sentValue = object.value("sent").toDouble(-1);
        const qint64 sent = qint64(sentValue);
        if (object.value("sent").isDouble() && sent > 0 && sentValue == sent && sent == lastPingSentMs)
        {
            lastHeartbeatMs = messageTime;
            QMutexLocker lock(&stateMutex);
            currentMetrics.roundTripMs = std::max<qint64>(0, heartbeatClock.elapsed() - sent);
        }
        else
        {
            { QMutexLocker lock(&stateMutex); currentMetrics.protocolErrors++; }
            resetRemoteInput();
            if (client) client->close(QWebSocketProtocol::CloseCodeProtocolError, "Protocol violation");
        }
    }
    else if (type == "visibility" && object.value("hidden").isBool())
    {
        lastHeartbeatMs = messageTime;
        if (object.value("hidden").toBool()) resetRemoteInput();
    }
    else
    {
        { QMutexLocker lock(&stateMutex); currentMetrics.protocolErrors++; }
        log(Debug, "protocol", "Rejected unexpected authenticated-client message");
        resetRemoteInput();
        if (client) client->close(QWebSocketProtocol::CloseCodeProtocolError, "Protocol violation");
    }
}

void PhoneBridgeManager::checkHeartbeat()
{
    if (isListening() && !QHostAddress(currentSettings.address).isLoopback()
        && !availableIPv4Addresses(false).contains(currentSettings.address))
    {
        log(Error, "network", "Selected network interface disappeared; stopping bridge and restoring fallback");
        stop();
        return;
    }
    if (!client) return;
    const qint64 now = heartbeatClock.elapsed();
    if (frameInFlight && now - inFlightSentMs > 500)
    {
        frameInFlight = false;
        { QMutexLocker lock(&stateMutex); currentMetrics.framesDropped++; }
        log(Debug, "stream", "Frame acknowledgement timed out; sending latest frame");
        sendPendingFrame();
    }
    if (now - lastHeartbeatMs > kHeartbeatTimeoutMs)
    {
        log(Error, "websocket", "Heartbeat timed out; releasing all phone input");
        client->close(QWebSocketProtocol::CloseCodeGoingAway, "Heartbeat timeout");
        resetRemoteInput();
        return;
    }
    if (now - lastPingSentMs >= 500)
    {
        lastPingSentMs = now;
        QJsonObject ping{{"v", PhoneProtocol::Version}, {"type", "ping"}, {"sent", double(now)}};
        client->sendTextMessage(QString::fromUtf8(QJsonDocument(ping).toJson(QJsonDocument::Compact)));
    }
}

void PhoneBridgeManager::emitTestPattern()
{
    if (!connected.load()) return;
    QImage image(256, 192, QImage::Format_RGB32);
    image.fill(QColor(15, 20, 30));
    QPainter painter(&image);
    painter.setPen(QColor(55, 80, 100));
    for (int x = 0; x < 256; x += 16) painter.drawLine(x, 0, x, 191);
    for (int y = 0; y < 192; y += 16) painter.drawLine(0, y, 255, y);
    painter.setPen(Qt::white);
    painter.drawText(image.rect(), Qt::AlignCenter,
                     QString("WideMelon phone test\n%1").arg(QDateTime::currentDateTime().toString("hh:mm:ss.zzz")));
    submitFrame(image);
}

void PhoneBridgeManager::encodedFrameReady(quint32 generation, quint32 sequence,
                                           const QByteArray& jpeg, double encodeMs)
{
    if (!connected.load() || generation != connectionGeneration.load()) return;
    QByteArray packet = PhoneProtocol::BuildFrame(sequence,
        quint64(heartbeatClock.nsecsElapsed() / 1000), jpeg);
    {
        QMutexLocker lock(&stateMutex);
        currentMetrics.framesEncoded++;
        currentMetrics.lastEncodeMs = encodeMs;
        currentMetrics.averageEncodeMs += (encodeMs - currentMetrics.averageEncodeMs) / double(currentMetrics.framesEncoded);
        currentMetrics.lastFrameBytes = jpeg.size();
    }
    if (frameInFlight)
    {
        if (!pendingPacket.isEmpty()) { QMutexLocker lock(&stateMutex); currentMetrics.framesDropped++; }
        pendingPacket = packet;
        pendingSequence = sequence;
        return;
    }
    pendingPacket = packet;
    pendingSequence = sequence;
    sendPendingFrame();
}

void PhoneBridgeManager::sendPendingFrame()
{
    if (!client || pendingPacket.isEmpty() || frameInFlight) return;
    const QByteArray packet = std::move(pendingPacket);
    pendingPacket.clear();
    inFlightSequence = pendingSequence;
    inFlightSentMs = heartbeatClock.elapsed();
    frameInFlight = true;
    client->sendBinaryMessage(packet);
    QMutexLocker lock(&stateMutex);
    currentMetrics.framesSent++;
    currentMetrics.bytesSent += packet.size();
}

void PhoneBridgeManager::sendLayout()
{
    if (!client) return;
    QJsonObject message{{"v", PhoneProtocol::Version}, {"type", "layout"}};
    message["layout"] = QJsonDocument::fromJson(currentSettings.layoutJson.toUtf8()).object();
    client->sendTextMessage(QString::fromUtf8(QJsonDocument(message).toJson(QJsonDocument::Compact)));
}

void PhoneBridgeManager::resetRemoteInput()
{
    remoteKeys.store(0xFFF);
    remoteHotkeys.store(0);
    remoteTouch.store(0);
    lastInputSequence = 0;
}

void PhoneBridgeManager::setConnected(bool value)
{
    const bool wasUsable = hasUsableClient();
    const bool changed = connected.exchange(value) != value;
    if (changed) connectionGeneration.fetch_add(1);
    if (wasUsable != hasUsableClient()) emit connectionChanged(hasUsableClient());
}

void PhoneBridgeManager::log(LogLevel level, const QString& category, const QString& message) const
{
    if (QThread::currentThread() != thread())
    {
        QMetaObject::invokeMethod(const_cast<PhoneBridgeManager*>(this),
            [this, level, category, message] { log(level, category, message); }, Qt::QueuedConnection);
        return;
    }
    PhoneBridgeSettings logSettings;
    {
        QMutexLocker lock(&stateMutex);
        logSettings = currentSettings;
    }
    if (level > logSettings.logLevel) return;
    const QString line = QString("%1 [%2] [%3] %4")
        .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs), levelName(level), category, message);
    {
        QMutexLocker lock(&stateMutex);
        recentLogs.append(line);
        while (recentLogs.size() > kMaxLiveLogs) recentLogs.removeFirst();
    }
    if (logSettings.consoleLog) std::fprintf(stderr, "%s\n", line.toUtf8().constData());
    if (logSettings.fileLog)
    {
        rotateLogIfNeeded();
        QFile file(QString::fromStdString(melonDS::Platform::GetLocalFilePath("phone-bridge.log")));
        if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
            file.write(line.toUtf8() + '\n');
    }
    emit const_cast<PhoneBridgeManager*>(this)->logAdded(line);
}

void PhoneBridgeManager::rotateLogIfNeeded() const
{
    const QString base = QString::fromStdString(melonDS::Platform::GetLocalFilePath("phone-bridge.log"));
    if (QFileInfo(base).size() < kMaxLogBytes) return;
    QFile::remove(base + ".3");
    QFile::rename(base + ".2", base + ".3");
    QFile::rename(base + ".1", base + ".2");
    QFile::rename(base, base + ".1");
}
