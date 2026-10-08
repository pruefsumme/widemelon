// Exercise the production bridge over real loopback or selected-LAN HTTP/WebSocket sockets.
// Copyright (C) 2026 WideMelon contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "frontend/qt_sdl/PhoneBridge.h"
#include "frontend/qt_sdl/PhoneProtocol.h"
#include "frontend/qt_sdl/PhoneScreenDialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QLabel>
#include <QGroupBox>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QScrollArea>
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
// CI runners can delay WebSocket close delivery and the first one-second sample.
constexpr int kSlowBridgeTimeoutMs = 5000;

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

// Use the production bridge with delayed application replies. An undelayed
// loopback test cannot expose round-trip throughput limits or stale probes.
int testDelayedNetwork(PhoneBridgeManager& bridge, bool testFrames)
{
    QWebSocket phone;
    QUrl endpoint(bridge.url() + "bridge");
    endpoint.setScheme("ws");
    QNetworkRequest request(endpoint);
    request.setRawHeader("Origin", bridge.url().chopped(1).toUtf8());
    bool authenticated = false;
    bool acknowledgeFrames = true;
    int framesReceived = 0;
    int inputSequence = 0;
    quint32 latestFrame = 0;
    QObject::connect(&phone, &QWebSocket::textMessageReceived, &phone, [&](const QString& text) {
        const auto message = QJsonDocument::fromJson(text.toUtf8()).object();
        if (message.value("type") == "hello") authenticated = true;
        if (message.value("type") == "ping")
            QTimer::singleShot(testFrames ? 0 : 900, &phone, [&, sent = message.value("sent")] {
                send(phone, {{"v", 2}, {"type", "pong"}, {"sent", sent}});
            });
    });
    QObject::connect(&phone, &QWebSocket::binaryMessageReceived, &phone, [&](const QByteArray& packet) {
        framesReceived++;
        latestFrame = qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(packet.constData() + 4));
        if (acknowledgeFrames)
            QTimer::singleShot(180, &phone, [&, frame = latestFrame] {
                send(phone, {{"v", 2}, {"type", "frameAck"}, {"seq", double(frame)}, {"decodeMs", 3}});
            });
    });
    phone.open(request);
    CHECK(waitUntil([&] { return phone.state() == QAbstractSocket::ConnectedState; }));
    send(phone, {{"v", 2}, {"type", "auth"}, {"credential", bridge.pairingCode()}});
    CHECK(waitUntil([&] { return authenticated; }));
    QTimer inputTimer;
    QObject::connect(&inputTimer, &QTimer::timeout, [&] { send(phone, input(++inputSequence)); });
    inputTimer.start(200);
    QElapsedTimer elapsed;
    elapsed.start();

    if (!testFrames)
    {
        CHECK(waitUntil([&] { return elapsed.elapsed() >= 2200 || !bridge.isConnected(); }, 3000));
        CHECK(bridge.isConnected() && bridge.metrics().protocolErrors == 0);
        CHECK(bridge.metrics().roundTripMs >= 850);
        inputTimer.stop();
        CHECK(waitUntil([&] { return released(bridge); }, 1600));
        CHECK(bridge.isConnected()); // pongs keep the connection, but not held input, alive
        send(phone, input(++inputSequence));
        CHECK(waitUntil([&] { return !released(bridge); }));
        std::cout << "900 ms heartbeat replies remain valid; abandoned input releases independently\n";
    }
    else
    {
        QImage frame(256, 192, QImage::Format_RGB32);
        frame.fill(Qt::red);
        QTimer capture;
        QObject::connect(&capture, &QTimer::timeout, [&] { bridge.submitFrame(frame); });
        capture.setTimerType(Qt::PreciseTimer);
        capture.start(33);
        CHECK(waitUntil([&] { return elapsed.elapsed() >= 2500 || !bridge.isConnected(); }, 3500));
        std::cout << "180 ms frame ACK delay: " << framesReceived << " frames in " << elapsed.elapsed() << " ms\n";
        CHECK(bridge.isConnected() && framesReceived >= 65);
        capture.stop();
        CHECK(waitUntil([&] { return bridge.metrics().framesAcked == bridge.metrics().framesSent; }));

        // A stalled decoder fills the bounded window, even while controls
        // and heartbeat replies continue. Old timeout logic leaked send credit.
        acknowledgeFrames = false;
        const quint32 lastAcknowledgedFrame = latestFrame;
        const int beforeStall = framesReceived;
        capture.start(33);
        elapsed.restart();
        CHECK(waitUntil([&] { return elapsed.elapsed() >= 1100; }));
        CHECK(bridge.isConnected());
        CHECK(framesReceived - beforeStall > 1 && framesReceived - beforeStall <= 8);
        const int atCapacity = framesReceived;
        send(phone, {{"v", 2}, {"type", "frameAck"}, {"seq", double(lastAcknowledgedFrame)}});
        send(phone, {{"v", 2}, {"type", "frameAck"}, {"seq", double(0xFFFFFFFFU)}});
        elapsed.restart();
        CHECK(waitUntil([&] { return elapsed.elapsed() >= 100; }));
        CHECK(framesReceived == atCapacity); // stale and unknown ACKs grant no credit

        // The browser can skip frames while decoding. One latest-frame ACK
        // retires them and immediately delivers the newest pending frame.
        const auto ackedBeforeRecovery = bridge.metrics().framesAcked;
        send(phone, {{"v", 2}, {"type", "frameAck"}, {"seq", double(latestFrame)}});
        CHECK(waitUntil([&] { return framesReceived > atCapacity; }));
        CHECK(bridge.metrics().framesAcked == ackedBeforeRecovery + 1);
        CHECK(latestFrame > quint32(atCapacity + 10));
        elapsed.restart();
        CHECK(waitUntil([&] { return elapsed.elapsed() >= 1100; }));
        CHECK(framesReceived - atCapacity <= 8);
        CHECK(waitUntil([&] { return !bridge.isConnected(); }, 3500));
        CHECK(released(bridge) && bridge.metrics().acknowledgementTimeouts == 1);
        std::cout << "Stalled video stays bounded, recovers to the latest frame, and times out safely\n";
    }
    bridge.stop();
    return 0;
}

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
    settings.shortPairingCode = true;
    bridge.setSettings(settings);
    bridge.setCaptureAvailable(true);
    CHECK(bridge.start());
    if (application.arguments().contains("--delayed-pong")) return testDelayedNetwork(bridge, false);
    if (application.arguments().contains("--delayed-frames")) return testDelayedNetwork(bridge, true);
    if (application.arguments().contains("--browser-smoke"))
    {
        // Optional real-browser harness: loopback only, no emulator or ROM.
        // Report the exact snapshots consumed by the emulator input path.
        std::cout << QJsonDocument(QJsonObject{{"url", bridge.pairingUrl()}})
            .toJson(QJsonDocument::Compact).constData() << std::endl;
        std::unique_ptr<PhoneScreenDialog> benchmarkDialog;
        if (application.arguments().contains("--benchmark-dialog"))
        {
            benchmarkDialog = std::make_unique<PhoneScreenDialog>(&bridge);
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
    CHECK(originalCode.size() == 6);
    const QString originalUrl = bridge.pairingUrl();
    PhoneScreenDialog dialog(&bridge);
    QCheckBox* shortCodeToggle = nullptr;
    for (QCheckBox* checkBox : dialog.findChildren<QCheckBox*>())
        if (checkBox->text() == "Use shorter code") shortCodeToggle = checkBox;
    CHECK(shortCodeToggle && shortCodeToggle->isChecked() && !shortCodeToggle->isEnabled());
    QLabel* qrLabel = nullptr;
    for (QLabel* label : dialog.findChildren<QLabel*>())
        if (!label->pixmap(Qt::ReturnByValue).isNull()) qrLabel = label;
    CHECK(qrLabel);
    const auto originalQr = qrLabel->pixmap(Qt::ReturnByValue).cacheKey();
    CHECK(QMetaObject::invokeMethod(&dialog, "updateUi", Qt::DirectConnection));
    CHECK(qrLabel->pixmap(Qt::ReturnByValue).cacheKey() == originalQr);
    auto networkSettings = dialog.findChild<QGroupBox*>("phoneNetworkSettings");
    CHECK(networkSettings);
    for (QWidget* ancestor = networkSettings->parentWidget(); ancestor; ancestor = ancestor->parentWidget())
        CHECK(!qobject_cast<QScrollArea*>(ancestor));
    dialog.show();
    for (const QSize& size : {QSize(560, 680), QSize(480, 640)})
    {
        dialog.resize(size);
        application.processEvents();
        if (dialog.height() > size.height())
            std::cerr << "Phone dialog requested " << size.width() << 'x' << size.height()
                      << " but remained " << dialog.width() << 'x' << dialog.height() << '\n';
        CHECK(dialog.height() <= size.height());
        auto bounds = [&](QWidget* widget) {
            return QRect(widget->mapTo(&dialog, QPoint()), widget->size());
        };
        CHECK(qrLabel->width() <= 168 && qrLabel->height() <= 168);
        CHECK(qrLabel->pixmap(Qt::ReturnByValue).width() <= qrLabel->width());
        CHECK(dialog.rect().contains(bounds(networkSettings)));
        for (QWidget* child : networkSettings->findChildren<QWidget*>())
            if (child->isVisible()) CHECK(dialog.rect().contains(bounds(child)));
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
    for (QGroupBox* group : dialog.findChildren<QGroupBox*>())
        if (group->isCheckable()) group->setChecked(false);
    application.processEvents();
    if (qEnvironmentVariableIsSet("WIDEMELON_PHONE_TEST_SCREENSHOT"))
    {
        dialog.resize(560, 680);
        dialog.show();
        application.processEvents();
        CHECK(dialog.grab().save(qEnvironmentVariable("WIDEMELON_PHONE_TEST_SCREENSHOT")));
    }
    QPushButton* firewallGuide = nullptr;
    for (QPushButton* button : dialog.findChildren<QPushButton*>())
        if (button->text() == "Firewall help…") firewallGuide = button;
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
    int secondMessages = 0, frameCount = 0;
    bool firstReceivedHello = false;
    bool acknowledgeFrames = false;
    quint32 receivedSequence = 0;
    QObject::connect(&first, &QWebSocket::textMessageReceived, [&](const QString& text) {
        const auto message = QJsonDocument::fromJson(text.toUtf8()).object();
        const QString type = message.value("type").toString();
        if (type == "hello") firstReceivedHello = true;
        if (type == "ping")
            send(first, {{"v", 2}, {"type", "pong"}, {"sent", message.value("sent")}});
    });
    QObject::connect(&second, &QWebSocket::textMessageReceived, [&] { secondMessages++; });
    QObject::connect(&first, &QWebSocket::binaryMessageReceived, [&](const QByteArray& packet) {
        frameCount++;
        receivedSequence = qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(packet.constData() + 4));
        if (acknowledgeFrames)
            send(first, {{"v", 2}, {"type", "frameAck"}, {"seq", double(receivedSequence)}, {"decodeMs", 1.0}});
    });
    CHECK(open(first) && open(second));
    auth(first, originalCode);
    CHECK(waitUntil([&] { return bridge.isConnected() && firstReceivedHello; }, kSlowBridgeTimeoutMs));
    auth(second, originalCode);
    CHECK(waitUntil([&] { return second.state() == QAbstractSocket::UnconnectedState; }, kSlowBridgeTimeoutMs));
    CHECK(secondMessages == 0);
    bridge.submitFrame(frame, 2.5);
    CHECK(waitUntil([&] { return frameCount == 1; }));
    const quint32 firstSequence = receivedSequence;
    // Stall GUI delivery while the encoder produces frames. Only the newest
    // result may reach the socket once the GUI processes events again.
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
    CHECK(waitUntil([&]
    {
        const auto current = bridge.metrics();
        return current.maxCaptureMs >= 2.5 && current.browserDecodeMs >= 7.5;
    }));
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

    // Keep all telemetry counters active until one sampling interval contains
    // complete traffic. A one-shot frame can be replaced by a later zero-rate
    // sample on a slow runner before diagnostics are exported.
    QTimer telemetryTraffic;
    acknowledgeFrames = true;
    QObject::connect(&telemetryTraffic, &QTimer::timeout, [&] {
        bridge.submitFrame(frame);
        auto snapshot = input(inputSequence++);
        snapshot["buttons"] = inputSequence & 1;
        send(first, snapshot);
    });
    telemetryTraffic.start(20);
    QJsonObject completeSample;
    const QString timingPath = diagnostics.filePath("timing.json");
    CHECK(waitUntil([&] {
        if (!bridge.exportDiagnostics(timingPath, {})) return false;
        QFile currentReport(timingPath);
        if (!currentReport.open(QIODevice::ReadOnly)) return false;
        const auto currentSamples = QJsonDocument::fromJson(currentReport.readAll())
            .object().value("performanceSamples").toArray();
        if (currentSamples.isEmpty()) return false;
        const auto candidate = currentSamples.last().toObject();
        if (candidate.value("offeredFps").toDouble() <= 0
            || candidate.value("sentFps").toDouble() <= 0
            || candidate.value("ackedFps").toDouble() <= 0
            || candidate.value("inputsPerSecond").toDouble() <= 0)
            return false;
        completeSample = candidate;
        return true;
    }, kSlowBridgeTimeoutMs));
    telemetryTraffic.stop();
    acknowledgeFrames = false;

    // Export immediately, without processing another heartbeat that could
    // append an idle sample, and validate the exact sample just observed.
    CHECK(bridge.exportDiagnostics(diagnostics.filePath("timing.json"), {}));
    QFile timingReport(diagnostics.filePath("timing.json"));
    CHECK(timingReport.open(QIODevice::ReadOnly));
    const auto samples = QJsonDocument::fromJson(timingReport.readAll()).object().value("performanceSamples").toArray();
    CHECK(!samples.isEmpty() && samples.size() <= 60);
    const auto sample = samples.last().toObject();
    CHECK(sample == completeSample);
    CHECK(sample.value("maxCaptureMs").toDouble() == 2.5);
    CHECK(sample.value("offeredFps").toDouble() > 0);
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
    CHECK(waitUntil([&] { return released(bridge); }, 1800));
    CHECK(bridge.isConnected());
    CHECK(waitUntil([&] { return !bridge.isConnected(); }, 2500));
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
