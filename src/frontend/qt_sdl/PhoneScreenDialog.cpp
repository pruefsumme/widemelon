// Native configurator for WideMelon's phone bridge.
// Copyright (C) 2026 WideMelon contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "PhoneScreenDialog.h"

#include <algorithm>
#include <atomic>

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFutureWatcher>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHostAddress>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPixmap>
#include <QPushButton>
#include <QScreen>
#include <QScrollBar>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrentRun>

#include "PhoneBridge.h"
#include "PhoneFirewall.h"
#include "PhoneLayoutDialog.h"
#include "qrcodegen/qrcodegen.hpp"

namespace
{
std::atomic<bool> requestedForSession {false};

QString addressLabel(const QString& value)
{
    if (QHostAddress(value).isLoopback()) return value + " (this computer only)";
    if (PhoneBridgeManager::isPrivateAddress(value))
        return value + (IsLikelyVpnInterface(value) ? " (private; possible VPN)" : " (private LAN)");
    return value + " (non-private; unsafe)";
}

QPixmap pairingQrCode(const QString& text)
{
    if (text.isEmpty()) return {};
    const QByteArray utf8 = text.toUtf8();
    const qrcodegen::QrCode code = qrcodegen::QrCode::encodeText(
        utf8.constData(), qrcodegen::QrCode::Ecc::MEDIUM);
    constexpr int border = 4;
    constexpr int scale = 5;
    const int pixels = (code.getSize() + border * 2) * scale;
    QImage image(pixels, pixels, QImage::Format_RGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::black);
    for (int y = 0; y < code.getSize(); y++)
        for (int x = 0; x < code.getSize(); x++)
            if (code.getModule(x, y))
                painter.drawRect((x + border) * scale, (y + border) * scale, scale, scale);
    return QPixmap::fromImage(image);
}
}

PhoneScreenDialog::PhoneScreenDialog(PhoneBridgeManager* manager, bool startup, QWidget* parent)
    : QDialog(parent, Qt::Tool), manager(manager), startup(startup)
{
    setAttribute(Qt::WA_DeleteOnClose, !startup);
    setWindowTitle("Phone screen & controller");
    setModal(false);
    resize(560, 680);

    auto root = new QVBoxLayout(this);
    root->setContentsMargins(14, 14, 14, 14);
    root->setSpacing(10);

    warning = new QLabel("Use only on a private home network you trust. Pairing prevents other devices from connecting, but the connection is not encrypted.");
    warning->setWordWrap(true);
    warning->setStyleSheet("QLabel { color: #9a5a00; background: #fff0c2; padding: 8px; border-radius: 4px; }");
    root->addWidget(warning);

    auto sessionBox = new QGroupBox("Session");
    auto sessionLayout = new QFormLayout(sessionBox);
    status = new QLabel;
    address = new QLabel;
    address->setTextInteractionFlags(Qt::TextSelectableByMouse);
    sessionLayout->addRow("Status", status);
    auto addressRow = new QHBoxLayout;
    addressRow->addWidget(address, 1);
    auto copy = new QPushButton("Copy URL");
    addressRow->addWidget(copy);
    sessionLayout->addRow("Phone URL", addressRow);
    pairingQr = new QLabel;
    pairingQr->setAlignment(Qt::AlignCenter);
    pairingQr->setMinimumHeight(210);
    pairingQr->setText("Start the server to create a pairing code.");
    sessionLayout->addRow("Scan to pair", pairingQr);
    pairingCode = new QLabel;
    pairingCode->setTextInteractionFlags(Qt::TextSelectableByMouse);
    QFont codeFont = pairingCode->font();
    codeFont.setBold(true);
    codeFont.setPointSize(codeFont.pointSize() + 4);
    pairingCode->setFont(codeFont);
    sessionLayout->addRow("Manual code", pairingCode);
    connectedClient = new QLabel("None");
    sessionLayout->addRow("Connected phone", connectedClient);
    root->addWidget(sessionBox);

    auto networkBox = new QGroupBox("Network & stream");
    auto form = new QFormLayout(networkBox);
    auto interfaceRow = new QHBoxLayout;
    interfaceBox = new QComboBox;
    interfaceRow->addWidget(interfaceBox, 1);
    auto refresh = new QPushButton("Refresh");
    interfaceRow->addWidget(refresh);
    form->addRow("IPv4 interface", interfaceRow);
    port = new QSpinBox;
    port->setRange(1024, 65534);
    port->setToolTip("The web page uses this port; WebSocket control uses the next port.");
    form->addRow("Base port", port);
    quality = new QSpinBox;
    quality->setRange(30, 100);
    quality->setSuffix("%");
    form->addRow("JPEG quality", quality);
    auto fps = new QLabel("30 FPS · 256 × 192 · latest frame wins");
    form->addRow("Stream", fps);
    auto layoutButton = new QPushButton("Edit controller layout…");
    layoutButton->setToolTip("Arrange and resize the phone screen and controls with the mouse.");
    form->addRow("Phone controls", layoutButton);
    auto securityButton = new QPushButton("Security details…");
    form->addRow("Advanced", securityButton);
    root->addWidget(networkBox);

    auto diagnosticsBox = new QGroupBox("Advanced diagnostics");
    diagnosticsBox->setCheckable(true);
    diagnosticsBox->setChecked(false);
    auto diagnosticsLayout = new QFormLayout(diagnosticsBox);
    logLevel = new QComboBox;
    logLevel->addItems({"Errors", "Info", "Debug", "Trace inputs"});
    diagnosticsLayout->addRow("Log level", logLevel);
    consoleLog = new QCheckBox("Write bridge logs to stderr/console");
    fileLog = new QCheckBox("Write rotating phone-bridge.log files (3 × 2 MiB)");
    synchronousCapture = new QCheckBox("Use synchronous GPU readback (diagnostic; may stutter)");
    testPattern = new QCheckBox("Send generated test pattern instead of game frames");
    diagnosticsLayout->addRow(consoleLog);
    diagnosticsLayout->addRow(fileLog);
    diagnosticsLayout->addRow(synchronousCapture);
    diagnosticsLayout->addRow(testPattern);
    metrics = new QLabel;
    metrics->setWordWrap(true);
    diagnosticsLayout->addRow("Live metrics", metrics);
    const QList<QWidget*> diagnosticWidgets = diagnosticsBox->findChildren<QWidget*>(
        QString(), Qt::FindDirectChildrenOnly);
    for (QWidget* widget : diagnosticWidgets) widget->setVisible(false);
    connect(diagnosticsBox, &QGroupBox::toggled, this, [diagnosticWidgets](bool visible)
    {
        for (QWidget* widget : diagnosticWidgets) widget->setVisible(visible);
    });
    root->addWidget(diagnosticsBox);

    logs = new QPlainTextEdit;
    logs->setReadOnly(true);
    logs->setMaximumBlockCount(1000);
    logs->setPlaceholderText("Bridge events will appear here after the session is enabled.");
    root->addWidget(logs, 1);

    auto actions = new QHBoxLayout;
    startButton = new QPushButton(startup ? "Enable for this session" : "Start server");
    stopButton = new QPushButton(startup ? "Disable for this session" : "Stop server");
    auto disconnect = new QPushButton("Disconnect phone");
    auto regenerate = new QPushButton("Generate new code");
    auto revoke = new QPushButton("Revoke pairing");
    auto exportButton = new QPushButton("Export diagnostics…");
    actions->addWidget(startButton);
    actions->addWidget(stopButton);
    actions->addWidget(disconnect);
    actions->addWidget(regenerate);
    actions->addWidget(revoke);
    actions->addStretch();
    actions->addWidget(exportButton);
    root->addLayout(actions);

    auto closeButton = new QDialogButtonBox(QDialogButtonBox::Close);
    root->addWidget(closeButton);

    connect(copy, &QPushButton::clicked, this, [this]
    {
        QApplication::clipboard()->setText(address->text());
    });
    connect(refresh, &QPushButton::clicked, this, &PhoneScreenDialog::refreshInterfaces);
    connect(startButton, &QPushButton::clicked, this, &PhoneScreenDialog::startOrArm);
    connect(stopButton, &QPushButton::clicked, this, &PhoneScreenDialog::stopOrDisarm);
    connect(disconnect, &QPushButton::clicked, this, [this] { if (this->manager) this->manager->disconnectClient(); });
    connect(regenerate, &QPushButton::clicked, this, &PhoneScreenDialog::regeneratePairing);
    connect(revoke, &QPushButton::clicked, this, &PhoneScreenDialog::regeneratePairing);
    connect(exportButton, &QPushButton::clicked, this, &PhoneScreenDialog::exportDiagnostics);
    connect(layoutButton, &QPushButton::clicked, this, [this]
    {
        auto editor = new PhoneLayoutDialog(this->manager, this);
        editor->show();
        editor->raise();
        editor->activateWindow();
    });
    connect(securityButton, &QPushButton::clicked, this, [this]
    {
        QMessageBox::information(this, "Phone bridge security",
            "Pairing blocks ordinary unauthorized devices on the selected home network. "
            "Video and controls use unencrypted HTTP and WebSocket traffic. It does not protect against "
            "traffic sniffing, active interception, compromised routers, or hostile shared networks.\n\n"
            "VPN identification and firewall checks are diagnostic hints, not security guarantees.");
    });
    connect(closeButton, &QDialogButtonBox::rejected, this, &QDialog::close);
    connect(testPattern, &QCheckBox::toggled, this, [this](bool value)
    {
        if (this->manager) this->manager->setTestPattern(value);
    });

    refreshTimer = new QTimer(this);
    refreshTimer->setInterval(500);
    connect(refreshTimer, &QTimer::timeout, this, &PhoneScreenDialog::updateUi);
    refreshTimer->start();
    refreshInterfaces();
    loadControls();
    connect(interfaceBox, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { updateUi(); });
    connect(port, qOverload<int>(&QSpinBox::valueChanged), this, [this] { updateUi(); });
    connect(quality, qOverload<int>(&QSpinBox::valueChanged), this, [this] { applyControls(); });
    connect(logLevel, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { applyControls(); });
    connect(consoleLog, &QCheckBox::toggled, this, [this] { applyControls(); });
    connect(fileLog, &QCheckBox::toggled, this, [this] { applyControls(); });
    if (manager) connect(manager, &PhoneBridgeManager::pairingChanged, this, &PhoneScreenDialog::updateUi);
    updateUi();
}

void PhoneScreenDialog::positionBeside(QWidget* owner)
{
    if (!owner) return;
    const QRect ownerGeometry = owner->frameGeometry();
    QScreen* screen = owner->screen();
    const QRect available = screen ? screen->availableGeometry() : QGuiApplication::primaryScreen()->availableGeometry();
    QPoint target(ownerGeometry.right() + 10, ownerGeometry.top() + 100);
    if (target.x() + width() > available.right()) target.setX(ownerGeometry.left() - width() - 10);
    target.setX(std::max(available.left(), std::min(target.x(), available.right() - width())));
    target.setY(std::max(available.top(), std::min(target.y(), available.bottom() - height())));
    move(target);
}

void PhoneScreenDialog::refreshInterfaces()
{
    const QString selected = interfaceBox->currentData().toString();
    interfaceBox->clear();
    const QStringList addresses = PhoneBridgeManager::availableIPv4Addresses(true);
    for (const QString& value : addresses) interfaceBox->addItem(addressLabel(value), value);
    if (addresses.isEmpty()) interfaceBox->addItem("No IPv4 network available", QString());
    int index = interfaceBox->findData(selected);
    if (index < 0 && manager) index = interfaceBox->findData(manager->settings().address);
    if (index < 0)
    {
        for (int i = 0; i < interfaceBox->count(); i++)
            if (PhoneBridgeManager::isPrivateAddress(interfaceBox->itemData(i).toString())) { index = i; break; }
    }
    interfaceBox->setCurrentIndex(std::max(0, index));
}

void PhoneScreenDialog::loadControls()
{
    const PhoneBridgeSettings value = manager ? manager->settings() : PhoneBridgeManager::loadSettings();
    int index = interfaceBox->findData(value.address);
    if (index >= 0) interfaceBox->setCurrentIndex(index);
    port->setValue(value.basePort);
    quality->setValue(value.jpegQuality);
    logLevel->setCurrentIndex(value.logLevel);
    consoleLog->setChecked(value.consoleLog);
    fileLog->setChecked(value.fileLog);
    synchronousCapture->setChecked(value.synchronousCapture);
}

void PhoneScreenDialog::applyControls()
{
    PhoneBridgeSettings value = manager ? manager->settings() : PhoneBridgeManager::loadSettings();
    value.address = interfaceBox->currentData().toString();
    value.basePort = quint16(port->value());
    value.jpegQuality = quality->value();
    value.logLevel = logLevel->currentIndex();
    value.consoleLog = consoleLog->isChecked();
    value.fileLog = fileLog->isChecked();
    value.synchronousCapture = synchronousCapture->isChecked();
    PhoneBridgeManager::saveSettings(value);
    if (manager) manager->setSettings(value);
}

bool PhoneScreenDialog::confirmUnsafeStart()
{
    return QMessageBox::warning(this, "Start phone bridge on trusted network",
        "Pairing prevents ordinary devices from connecting, but video and controls are not encrypted. "
        "Only continue on a private home network you trust.\n\nThe bridge stops when WideMelon exits.",
        QMessageBox::Cancel | QMessageBox::Ok, QMessageBox::Cancel) == QMessageBox::Ok;
}

void PhoneScreenDialog::startOrArm()
{
    if (interfaceBox->currentData().toString().isEmpty())
    {
        QMessageBox::information(this, "No network available",
            "Connect this computer and phone to the same Wi-Fi network, or create a hotspot using your operating system. "
            "WideMelon will keep showing the small bottom screen.");
        return;
    }
    if (!confirmUnsafeStart()) return;
    applyControls();
    if (startup)
        requestedForSession.store(true);
    else if (manager && !manager->start())
        QMessageBox::critical(this, "Phone bridge failed", manager->lastError());
    else
        checkFirewall();
    updateUi();
}

void PhoneScreenDialog::checkFirewall()
{
    if (!manager || !manager->isListening()) return;
    firewallChecked = true;
    const PhoneBridgeSettings settings = manager->settings();
    auto watcher = new QFutureWatcher<PhoneFirewallResult>(this);
    connect(watcher, &QFutureWatcher<PhoneFirewallResult>::finished, this, [this, watcher]
    {
        const PhoneFirewallResult firewall = watcher->result();
        watcher->deleteLater();
        if (!manager || !manager->isListening()) return;
        if (firewall.detected && firewall.status == PhoneFirewallStatus::Blocked)
            QMessageBox::information(this, "Firewall may block phone connection",
                firewall.guidance + "\n\nFirewall detection is advisory and cannot prove whether the phone can reach this computer.");
    });
    watcher->setFuture(QtConcurrent::run([settings]
    {
        return InspectPhoneFirewall(settings.address, settings.basePort);
    }));
}

void PhoneScreenDialog::stopOrDisarm()
{
    if (startup) requestedForSession.store(false);
    else if (manager) manager->stop();
    updateUi();
}

void PhoneScreenDialog::regeneratePairing()
{
    if (manager) manager->regeneratePairing();
    updateUi();
}

void PhoneScreenDialog::updateUi()
{
    const bool listening = manager && manager->isListening();
    const bool connected = manager && manager->isConnected();
    if (listening && !firewallChecked) checkFirewall();
    if (!listening) firewallChecked = false;
    if (startup)
        status->setText(requestedForSession.load() ? "Armed — starts after Start melonDS" : "Off");
    else
        status->setText(manager ? manager->statusText() : "Unavailable");
    const QString host = interfaceBox->currentData().toString();
    address->setText(host.isEmpty() ? "Unavailable" : QString("http://%1:%2/").arg(host).arg(port->value()));
    const QString pairUrl = listening ? manager->pairingUrl() : QString();
    pairingQr->setPixmap(pairUrl.isEmpty() ? QPixmap() : pairingQrCode(pairUrl));
    pairingQr->setText(pairUrl.isEmpty() ? "Start the server to create a pairing code." : QString());
    pairingCode->setText(listening ? manager->pairingCode() : QStringLiteral("—"));
    connectedClient->setText(connected ? manager->connectedClientLabel() : QStringLiteral("None"));
    startButton->setEnabled(!host.isEmpty() && (startup ? !requestedForSession.load() : !listening));
    stopButton->setEnabled(startup ? requestedForSession.load() : listening);
    interfaceBox->setEnabled(!listening);
    port->setEnabled(!listening);
    synchronousCapture->setEnabled(!listening);
    testPattern->setEnabled(listening && connected);
    if (manager)
    {
        const PhoneBridgeMetrics m = manager->metrics();
        metrics->setText(QString("offered %1 · encoded %2 · sent %3 · acked %4 · dropped %5\n"
                                 "JPEG %6 bytes · encode %7 ms avg · RTT %8 ms · rejected pairings %9")
                         .arg(m.framesOffered).arg(m.framesEncoded).arg(m.framesSent)
                         .arg(m.framesAcked).arg(m.framesDropped).arg(m.lastFrameBytes)
                         .arg(m.averageEncodeMs, 0, 'f', 2).arg(m.roundTripMs, 0, 'f', 1)
                         .arg(m.authenticationFailures));
        const QString text = manager->logLines().join('\n');
        if (logs->toPlainText() != text)
        {
            const bool atEnd = logs->verticalScrollBar()->value() == logs->verticalScrollBar()->maximum();
            logs->setPlainText(text);
            if (atEnd) logs->verticalScrollBar()->setValue(logs->verticalScrollBar()->maximum());
        }
    }
    else
    {
        metrics->setText("Metrics become available after the emulator starts.");
    }
}

void PhoneScreenDialog::exportDiagnostics()
{
    if (!manager)
    {
        QMessageBox::information(this, "Diagnostics unavailable", "Start WideMelon before exporting live diagnostics.");
        return;
    }
    const QString path = QFileDialog::getSaveFileName(this, "Export phone bridge diagnostics",
                                                       "widemelon-phone-diagnostics.json", "JSON (*.json)");
    if (path.isEmpty()) return;
    QString error;
    if (!manager->exportDiagnostics(path, &error)) QMessageBox::critical(this, "Export failed", error);
}

namespace WideMelon
{
bool PhoneBridgeRequestedForSession() { return requestedForSession.load(); }
void ClearPhoneBridgeSessionRequest() { requestedForSession.store(false); }

void OpenPhoneScreenSettings(PhoneBridgeManager* manager, QWidget* parent)
{
    auto dialog = new PhoneScreenDialog(manager, false, parent);
    dialog->show();
    dialog->positionBeside(parent);
    dialog->raise();
    dialog->activateWindow();
}
}
