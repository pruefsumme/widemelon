// Exercise the production bridge over real loopback or selected-LAN HTTP/WebSocket sockets.
// Copyright (C) 2026 WideMelon contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "frontend/qt_sdl/PhoneBridge.h"
#include "frontend/qt_sdl/PhoneProtocol.h"
#include "frontend/qt_sdl/PhoneScreenDialog.h"

#include <QApplication>
#include <QLabel>
#include <QGroupBox>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QTemporaryDir>
#include <QFile>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonDocument>
#include <QNetworkRequest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>
#include <QWebSocket>
#include <QWizard>
#include <functional>
#include <iostream>

namespace
{
bool waitUntil(const std::function<bool()>& condition, int timeoutMs = 1500)
{
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < timeoutMs)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QThread::msleep(1);
    }
    return condition();
}
void send(QWebSocket& socket, const QJsonObject& message)
{
    socket.sendTextMessage(QString::fromUtf8(QJsonDocument(message).toJson(QJsonDocument::Compact)));
}
QJsonObject input(int sequence = 1)
{
    return {{"v", 2}, {"type", "input"}, {"seq", sequence}, {"buttons", 1}, {"hotkeys", 16},
        {"touch", QJsonObject{{"active", true}, {"x", 12}, {"y", 34}}}};
}
bool released(const PhoneBridgeManager& bridge)
{
    return bridge.remoteKeyMask() == 0xFFF && bridge.remoteHotkeyMask() == 0
        && bridge.remoteTouchSnapshot() == 0;
}
}

#define CHECK(condition) do { if (!(condition)) { \
    std::cerr << "Line " << __LINE__ << ": " << #condition << '\n'; return 1; } } while (false)

int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    const QString requestedAddress = qEnvironmentVariable("WIDEMELON_PHONE_TEST_ADDRESS");
    const bool loopbackTest = requestedAddress.isEmpty();
    const QHostAddress testAddress = loopbackTest ? QHostAddress::LocalHost : QHostAddress(requestedAddress);
    CHECK(!testAddress.isNull());
    QTcpServer portReservation;
    CHECK(portReservation.listen(testAddress, 0));
    const quint16 port = portReservation.serverPort();
    portReservation.close();
    PhoneBridgeManager bridge;
    auto settings = bridge.settings();
    settings.address = loopbackTest ? QStringLiteral("127.0.0.1") : requestedAddress;
    settings.basePort = port;
    if (qEnvironmentVariableIsSet("WIDEMELON_BENCH_QUALITY"))
    {
        settings.jpegQuality = qEnvironmentVariableIntValue("WIDEMELON_BENCH_QUALITY");
        CHECK(settings.jpegQuality >= 30 && settings.jpegQuality <= 100);
    }
    settings.consoleLog = settings.fileLog = false;
    bridge.setSettings(settings);
    bridge.setCaptureAvailable(true);
    CHECK(bridge.start());
    if (application.arguments().contains("--browser-smoke"))
    {
        // Optional real-browser harness: loopback only, no emulator or ROM.
        // Report the exact snapshots consumed by the emulator input path.
        std::cout << QJsonDocument(QJsonObject{{"url", bridge.pairingUrl()}})
            .toJson(QJsonDocument::Compact).constData() << std::endl;
        std::unique_ptr<PhoneScreenDialog> benchmarkDialog;
        if (application.arguments().contains("--benchmark-dialog"))
        {
            benchmarkDialog = std::make_unique<PhoneScreenDialog>(&bridge, false);
            benchmarkDialog->show();
        }
        QTimer observer;
        QElapsedTimer eventClock;
        eventClock.start();
        qint64 lastObserver = 0;
        QObject::connect(&observer, &QTimer::timeout, [&] {
            const auto metrics = bridge.metrics();
            const qint64 now = eventClock.elapsed();
            const QJsonObject state{{"connected", bridge.isConnected()},
                {"keys", int(bridge.remoteKeyMask())}, {"touch", double(bridge.remoteTouchSnapshot())},
                {"framesAcked", double(metrics.framesAcked)}, {"framesOffered", double(metrics.framesOffered)},
                {"framesEncoded", double(metrics.framesEncoded)}, {"framesSent", double(metrics.framesSent)},
                {"framesDropped", double(metrics.framesDropped)}, {"encodeMs", metrics.averageEncodeMs},
                {"guiTickMs", double(now - lastObserver)}};
            lastObserver = now;
            std::cout << QJsonDocument(state).toJson(QJsonDocument::Compact).constData() << std::endl;
        });
        observer.start(10);
        bridge.setTestPattern(true);
        QTimer::singleShot(90000, &application, &QCoreApplication::quit);
        return application.exec();
    }
    const QString originalCode = bridge.pairingCode();
    const QString originalUrl = bridge.pairingUrl();
    PhoneScreenDialog dialog(&bridge, false);
    QLabel* qrLabel = nullptr;
    for (QLabel* label : dialog.findChildren<QLabel*>())
        if (!label->pixmap(Qt::ReturnByValue).isNull()) qrLabel = label;
    CHECK(qrLabel);
    const auto originalQr = qrLabel->pixmap(Qt::ReturnByValue).cacheKey();
    CHECK(QMetaObject::invokeMethod(&dialog, "updateUi", Qt::DirectConnection));
    CHECK(qrLabel->pixmap(Qt::ReturnByValue).cacheKey() == originalQr);
    dialog.show();
    for (const QSize& size : {QSize(560, 680), QSize(480, 640)})
    {
        dialog.resize(size);
        application.processEvents();
        CHECK(dialog.height() <= size.height());
        auto bounds = [&](QWidget* widget) {
            return QRect(widget->mapTo(&dialog, QPoint()), widget->size());
        };
        CHECK(qrLabel->width() <= 168 && qrLabel->height() <= 168);
        CHECK(qrLabel->pixmap(Qt::ReturnByValue).width() <= qrLabel->width());
        for (QLabel* label : qrLabel->parentWidget()->findChildren<QLabel*>())
        {
            CHECK(dialog.rect().contains(bounds(label)));
            if (label != qrLabel) CHECK(!bounds(label).intersects(bounds(qrLabel)));
        }
        for (QGroupBox* group : dialog.findChildren<QGroupBox*>())
            if (group->isCheckable()) group->setChecked(true);
        application.processEvents();
        for (QPushButton* button : dialog.findChildren<QPushButton*>())
            if (button->parentWidget() == &dialog) CHECK(dialog.rect().contains(bounds(button)));
    }
    if (qEnvironmentVariableIsSet("WIDEMELON_PHONE_TEST_SCREENSHOT"))
    {
        dialog.show();
        application.processEvents();
        CHECK(dialog.grab().save(qEnvironmentVariable("WIDEMELON_PHONE_TEST_SCREENSHOT")));
    }
    QPushButton* firewallGuide = nullptr;
    for (QPushButton* button : dialog.findChildren<QPushButton*>())
        if (button->text() == "Firewall setup guide…") firewallGuide = button;
    CHECK(firewallGuide && firewallGuide->isEnabled());
    QElapsedTimer guideTimer;
    guideTimer.start();
    firewallGuide->click();
    CHECK(guideTimer.elapsed() < 500);
    auto wizard = dialog.findChild<QWizard*>("phoneFirewallGuide");
    CHECK(wizard && !wizard->isModal());
    firewallGuide->click();
    CHECK(dialog.findChildren<QWizard*>("phoneFirewallGuide").size() == 1);
    if (loopbackTest)
        for (QPlainTextEdit* text : wizard->findChildren<QPlainTextEdit*>())
            CHECK(!text->toPlainText().contains("sudo")); // loopback needs no rule
    if (qEnvironmentVariableIsSet("WIDEMELON_PHONE_FIREWALL_SCREENSHOT"))
    {
        const QString screenshot = qEnvironmentVariable("WIDEMELON_PHONE_FIREWALL_SCREENSHOT");
        wizard->next();
        wizard->next();
        QApplication::processEvents();
        CHECK(wizard->grab().save(screenshot));
    }
    wizard->reject();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QTemporaryDir diagnostics;
    CHECK(diagnostics.isValid());
    CHECK(bridge.exportDiagnostics(diagnostics.filePath("diagnostics.json"), {}));
    QFile report(diagnostics.filePath("diagnostics.json"));
    CHECK(report.open(QIODevice::ReadOnly));
    const QByteArray reportData = report.readAll();
    CHECK(!reportData.contains(originalCode.toUtf8()) && !reportData.contains(originalUrl.toUtf8()));
    CHECK(!reportData.contains(settings.address.toUtf8()));
    const QByteArray host = settings.address.toUtf8() + ':' + QByteArray::number(port);
    auto request = [&] {
        QNetworkRequest result(QUrl("ws://" + QString::fromLatin1(host) + "/bridge"));
        result.setRawHeader("Origin", "http://" + host);
        return result;
    };
    auto open = [&](QWebSocket& socket) {
        socket.open(request());
        return waitUntil([&] { return socket.state() == QAbstractSocket::ConnectedState; });
    };
    auto auth = [&](QWebSocket& socket, const QString& code) {
        send(socket, {{"v", 2}, {"type", "auth"}, {"credential", code}});
    };
    auto http = [&](const QByteArray& path, bool head = false) {
        QTcpSocket socket;
        socket.connectToHost(testAddress, port);
        if (!waitUntil([&] { return socket.state() == QAbstractSocket::ConnectedState; })) return QByteArray();
        socket.write((head ? "HEAD " : "GET ") + path + " HTTP/1.1\r\nHost: " + host + "\r\n\r\n");
        waitUntil([&] { return socket.state() == QAbstractSocket::UnconnectedState; });
        return socket.readAll();
    };
    CHECK(http("/").startsWith("HTTP/1.1 200"));
    CHECK(http("/layout.json").startsWith("HTTP/1.1 404"));
    for (const QByteArray& path : {QByteArray("/"), QByteArray("/missing")})
    {
        const QByteArray reply = http(path, true);
        CHECK(reply.indexOf("\r\n\r\n") == reply.size() - 4);
        CHECK(reply.contains("Content-Length: ") && !reply.contains("Content-Length: 0\r\n"));
    }

    QImage frame(256, 192, QImage::Format_RGB32);
    frame.fill(Qt::red);
    QWebSocket unauthorized;
    int unauthorizedMessages = 0;
    QObject::connect(&unauthorized, &QWebSocket::textMessageReceived, [&] { unauthorizedMessages++; });
    QObject::connect(&unauthorized, &QWebSocket::binaryMessageReceived, [&] { unauthorizedMessages++; });
    CHECK(open(unauthorized));
    bridge.submitFrame(frame);
    send(unauthorized, input());
    CHECK(waitUntil([&] { return unauthorized.state() == QAbstractSocket::UnconnectedState; }));
    CHECK(!bridge.isConnected() && released(bridge) && unauthorizedMessages == 0);

    // Two sockets can finish the handshake before either authenticates.
    QWebSocket first, second;
    int firstMessages = 0, secondMessages = 0, frameCount = 0;
    quint32 receivedSequence = 0;
    QObject::connect(&first, &QWebSocket::textMessageReceived, [&] { firstMessages++; });
    QObject::connect(&second, &QWebSocket::textMessageReceived, [&] { secondMessages++; });
    QObject::connect(&first, &QWebSocket::binaryMessageReceived, [&](const QByteArray& packet) {
        frameCount++;
        receivedSequence = qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(packet.constData() + 4));
    });
    CHECK(open(first) && open(second));
    auth(first, originalCode);
    auth(second, originalCode);
    CHECK(waitUntil([&] { return bridge.isConnected() && firstMessages == 1
        && second.state() == QAbstractSocket::UnconnectedState; }));
    CHECK(secondMessages == 0);
    bridge.submitFrame(frame, 2.5);
    CHECK(waitUntil([&] { return frameCount == 1; }));
    const quint32 firstSequence = receivedSequence;
    // Stall GUI delivery while the encoder produces frames. Only the newest
    // result may reach the socket once the first frame is acknowledged.
    for (int i = 0; i < 12; i++)
    {
        bridge.submitFrame(frame);
        QThread::msleep(3);
    }
    CHECK(bridge.metrics().framesDropped > 0);
    CHECK(frameCount == 1);
    send(first, {{"v", 2}, {"type", "frameAck"}, {"seq", double(firstSequence)}, {"decodeMs", 7.5}});
    CHECK(waitUntil([&] { return frameCount == 2; }));
    CHECK(receivedSequence == bridge.metrics().framesOffered);
    CHECK(bridge.metrics().maxCaptureMs == 2.5 && bridge.metrics().browserDecodeMs == 7.5);
    CHECK(bridge.metrics().frameAckMs >= 30 && bridge.metrics().maxDeliveryMs > 0);
    // Optional, untrusted timing data cannot block an otherwise valid ACK.
    send(first, {{"v", 2}, {"type", "frameAck"}, {"seq", double(receivedSequence)}, {"decodeMs", 1e100}});
    CHECK(waitUntil([&] { return bridge.metrics().framesAcked == 2; }));
    CHECK(bridge.metrics().browserDecodeMs == 7.5);
    std::cout << "JPEG average " << bridge.metrics().averageEncodeMs << " ms; latest-frame replacement passed\n";
    send(first, input());
    CHECK(waitUntil([&] { return bridge.remoteKeyMask() == 0xFFE; }));
    CHECK(bridge.remoteHotkeyMask() == 16 && bridge.remoteTouchSnapshot() != 0);

    // Complete button snapshots preserve simultaneous holds and independent
    // releases across WebSocket parsing and the emulator's active-low mask.
    int inputSequence = 2;
    for (int buttons : {0x400, 0x401, 0x400, 0x401, 0x001, 0x000})
    {
        auto snapshot = input(inputSequence++);
        snapshot["buttons"] = buttons;
        send(first, snapshot);
        CHECK(waitUntil([&] { return bridge.remoteKeyMask() == (0xFFFU ^ melonDS::u32(buttons)); }));
    }
    const auto framesBeforeStroke = bridge.metrics().framesSent;
    for (int position = 0; position < 64; position++)
    {
        auto snapshot = input(inputSequence++);
        snapshot["buttons"] = 0x401;
        snapshot["touch"] = QJsonObject{{"active", true}, {"x", position * 4}, {"y", position * 3}};
        send(first, snapshot);
        const melonDS::u32 expectedTouch = 0x80000000U | melonDS::u32(position * 4)
            | (melonDS::u32(position * 3) << 8);
        CHECK(waitUntil([&] { return bridge.remoteTouchSnapshot() == expectedTouch; }));
        CHECK(bridge.remoteKeyMask() == (0xFFFU ^ 0x401U));
    }
    CHECK(bridge.isConnected() && bridge.metrics().framesSent == framesBeforeStroke);
    QObject::connect(&first, &QWebSocket::textMessageReceived, [&](const QString& text) {
        const auto message = QJsonDocument::fromJson(text.toUtf8()).object();
        if (message.value("type").toString() == "ping")
            send(first, {{"v", 2}, {"type", "pong"}, {"sent", message.value("sent")}});
    });
    CHECK(waitUntil([&] { return bridge.metrics().offeredFps > 0; }));
    CHECK(bridge.exportDiagnostics(diagnostics.filePath("timing.json"), {}));
    QFile timingReport(diagnostics.filePath("timing.json"));
    CHECK(timingReport.open(QIODevice::ReadOnly));
    const auto samples = QJsonDocument::fromJson(timingReport.readAll()).object().value("performanceSamples").toArray();
    CHECK(!samples.isEmpty() && samples.size() <= 60);
    const auto sample = samples.last().toObject();
    CHECK(sample.value("maxCaptureMs").toDouble() == 2.5);
    CHECK(sample.value("inputsPerSecond").toDouble() > 0);
    CHECK(sample.value("sentFps").toDouble() > 0 && sample.value("ackedFps").toDouble() > 0);

    // Protocol rejection must end authorization immediately, before the peer
    // completes its close handshake. Later buffered input must be ignored.
    first.sendTextMessage("invalid json");
    send(first, input(inputSequence));
    CHECK(waitUntil([&] { return !bridge.isConnected(); }));
    CHECK(released(bridge) && !bridge.hasUsableClient());
    CHECK(bridge.connectedClientLabel().isEmpty());

    QWebSocket reconnect;
    CHECK(open(reconnect));
    auth(reconnect, originalCode);
    CHECK(waitUntil([&] { return bridge.isConnected(); }));
    send(reconnect, input());
    CHECK(waitUntil([&] { return !released(bridge); }));
    bridge.regeneratePairing();
    CHECK(!bridge.isConnected() && released(bridge));
    CHECK(bridge.pairingUrl() != originalUrl);
    CHECK(qrLabel->pixmap(Qt::ReturnByValue).cacheKey() != originalQr);
    CHECK(bridge.connectedClientLabel().isEmpty());

    QWebSocket stale;
    CHECK(open(stale));
    auth(stale, originalCode);
    CHECK(waitUntil([&] { return stale.state() == QAbstractSocket::UnconnectedState; }));
    CHECK(!bridge.isConnected());

    QWebSocket invalidPong;
    CHECK(open(invalidPong));
    auth(invalidPong, bridge.pairingCode());
    CHECK(waitUntil([&] { return bridge.isConnected(); }));
    send(invalidPong, input());
    CHECK(waitUntil([&] { return !released(bridge); }));
    // This used to convert an out-of-range double to qint64 before validation.
    send(invalidPong, {{"v", 2}, {"type", "pong"}, {"sent", 1e100}});
    CHECK(waitUntil([&] { return !bridge.isConnected(); }));
    CHECK(released(bridge));

    QWebSocket idle;
    CHECK(open(idle));
    auth(idle, bridge.pairingCode());
    CHECK(waitUntil([&] { return bridge.isConnected(); }));
    send(idle, input());
    CHECK(waitUntil([&] { return !released(bridge); }));
    CHECK(waitUntil([&] { return !bridge.isConnected(); }, 1800));
    CHECK(released(bridge));

    QWebSocket pending;
    CHECK(open(pending));
    bridge.stop();
    CHECK(bridge.pairingCode().isEmpty() && bridge.pairingUrl().isEmpty());
    CHECK(QMetaObject::invokeMethod(&dialog, "updateUi", Qt::DirectConnection));
    CHECK(qrLabel->pixmap(Qt::ReturnByValue).isNull());
    auth(pending, originalCode);
    CHECK(waitUntil([&] { return pending.state() == QAbstractSocket::UnconnectedState; }));
    CHECK(!bridge.isConnected() && released(bridge));
    CHECK(bridge.start());
    QWebSocket restarted;
    CHECK(open(restarted));
    auth(restarted, bridge.pairingCode());
    CHECK(waitUntil([&] { return bridge.isConnected(); }));
    send(restarted, input());
    CHECK(waitUntil([&] { return !released(bridge); }));
    bridge.stop();
    CHECK(!bridge.isConnected() && released(bridge));
    std::cout << "Production bridge authorization, slots, input cleanup and restart passed\n";
}
