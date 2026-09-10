// WideMelon phone bottom-screen and controller bridge.
// Copyright (C) 2026 WideMelon contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <memory>

#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QList>
#include <QMutex>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QWebSocketProtocol>

#include "types.h"
#include "PhoneSecurity.h"
#include "PhoneFirewall.h"

class QTcpServer;
class QTcpSocket;
class QTimer;
class QWebSocket;
class QWebSocketServer;
class QHostAddress;

struct PhoneBridgeSettings
{
    QString address;
    quint16 basePort = 24800;
    int jpegQuality = 85;
    int logLevel = 1; // 0=error, 1=info, 2=debug, 3=trace
    bool consoleLog = true;
    bool fileLog = false;
    bool synchronousCapture = false;
    QString layoutJson;
};

struct PhoneBridgeMetrics
{
    quint64 framesOffered = 0;
    quint64 framesEncoded = 0;
    quint64 framesSent = 0;
    quint64 framesAcked = 0;
    quint64 framesDropped = 0;
    quint64 bytesSent = 0;
    quint64 protocolErrors = 0;
    quint64 authenticationFailures = 0;
    double lastEncodeMs = 0.0;
    double averageEncodeMs = 0.0;
    double roundTripMs = 0.0;
    int lastFrameBytes = 0;
};

class PhoneBridgeManager final : public QObject
{
    Q_OBJECT

public:
    explicit PhoneBridgeManager(QObject* parent = nullptr);
    ~PhoneBridgeManager() override;

    static PhoneBridgeSettings loadSettings();
    static void saveSettings(const PhoneBridgeSettings& settings);
    static QStringList availableIPv4Addresses(bool includeLoopback = true);
    static bool isPrivateAddress(const QString& address);

    PhoneBridgeSettings settings() const;
    void setSettings(const PhoneBridgeSettings& settings);

    bool start();
    void stop();
    void disconnectClient();
    bool isListening() const;
    bool isConnected() const { return connected.load(std::memory_order_relaxed); }
    bool hasUsableClient() const
    {
        return isConnected() && (captureAvailable.load(std::memory_order_relaxed)
            || testPattern.load(std::memory_order_relaxed));
    }
    void setCaptureAvailable(bool available, const QString& reason = {});
    void reportCaptureDrop(const QString& reason);
    QString statusText() const;
    QString url() const;
    QString pairingUrl() const;
    QString pairingCode() const;
    QString connectedClientLabel() const;
    QString lastError() const;
    void regeneratePairing();

    melonDS::u32 remoteKeyMask() const;
    melonDS::u32 remoteHotkeyMask() const;
    melonDS::u32 remoteTouchSnapshot() const;

    // Safe to call from the emulator/render thread. Work is replaced, never queued.
    void submitFrame(const QImage& image);
    bool wantsFrames() const { return connected.load(std::memory_order_relaxed) || testPattern.load(std::memory_order_relaxed); }
    bool testPatternEnabled() const { return testPattern.load(std::memory_order_relaxed); }
    void setTestPattern(bool enabled);

    PhoneBridgeMetrics metrics() const;
    QStringList logLines() const;
    bool exportDiagnostics(const QString& path, const PhoneFirewallResult& firewall, QString* error = nullptr) const;

signals:
    void statusChanged();
    void connectionChanged(bool connected);
    void logAdded(const QString& line);
    void pairingChanged();

private slots:
    void acceptTcpConnections();
    void acceptWebSocket();
    void checkHeartbeat();
    void emitTestPattern();

private:
    class EncoderThread;

    enum LogLevel { Error = 0, Info = 1, Debug = 2, Trace = 3 };
    void log(LogLevel level, const QString& category, const QString& message) const;
    void resetRemoteInput();
    void closeClient(QWebSocketProtocol::CloseCode code, const QString& reason);
    void setConnected(bool value);
    void handleTcpSocket(class QTcpSocket* socket);
    void routeTcpSocket(class QTcpSocket* socket);
    void serveHttpSocket(class QTcpSocket* socket, const QByteArray& data, int headerEnd);
    void handlePendingMessage(QWebSocket* socket, const QString& message);
    void authenticateClient(QWebSocket* socket);
    void closePendingClient(QWebSocket* socket, QWebSocketProtocol::CloseCode code, const QString& reason);
    bool peerAllowed(const QHostAddress& peer) const;
    bool authenticationTemporarilyBlocked(const QHostAddress& peer, qint64 now);
    void recordAuthenticationFailure(const QHostAddress& peer, qint64 now);
    void generatePairingCredentials();
    void clearPairingCredentials();
    void handleTextMessage(const QString& message);
    void encodedFrameReady(quint32 generation, quint32 sequence, const QByteArray& jpeg);
    void sendPendingFrame();
    void sendLayout();
    void rotateLogIfNeeded() const;

    mutable QMutex stateMutex;
    PhoneBridgeSettings currentSettings;
    QString currentStatus;
    QString errorText;
    mutable QStringList recentLogs;
    PhoneBridgeMetrics currentMetrics;

    QTcpServer* httpServer = nullptr;
    QWebSocketServer* webSocketServer = nullptr;
    QWebSocket* client = nullptr;
    QSet<QTcpSocket*> pendingTcpSockets;
    QSet<QWebSocket*> pendingClients;
    QTimer* heartbeatTimer = nullptr;
    QTimer* testPatternTimer = nullptr;
    QElapsedTimer heartbeatClock;
    qint64 lastHeartbeatMs = 0;
    qint64 lastPingSentMs = 0;
    qint64 controlRateWindowMs = 0;
    int controlMessagesInWindow = 0;
    quint32 lastInputSequence = 0;
    PhonePairingCredentials pairingCredentials;
    QString clientAddressLabel;
    PhoneAuthenticationLimiter authenticationLimiter;

    std::unique_ptr<EncoderThread> encoder;
    std::atomic<bool> connected {false};
    std::atomic<bool> captureAvailable {false};
    std::atomic<bool> testPattern {false};
    std::atomic<melonDS::u32> remoteKeys {0xFFF};
    std::atomic<melonDS::u32> remoteHotkeys {0};
    std::atomic<melonDS::u32> remoteTouch {0};
    std::atomic<quint32> frameSequence {0};
    std::atomic<quint32> connectionGeneration {0};

    bool frameInFlight = false;
    quint32 inFlightSequence = 0;
    qint64 inFlightSentMs = 0;
    QByteArray pendingPacket;
    quint32 pendingSequence = 0;
};
