// Native configurator for WideMelon's phone bridge.
// Copyright (C) 2026 WideMelon contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "PhoneScreenDialog.h"

#include <algorithm>

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
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPixmap>
#include <QPushButton>
#include <QScreen>
#include <QScrollBar>
#include <QScrollArea>
#include <QSizePolicy>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>
#include <QWizard>
#include <QWizardPage>
#include <QtConcurrentRun>

#include "PhoneBridge.h"
#include "PhoneFirewall.h"
#include "PhoneLayoutDialog.h"
#include "qrcodegen/qrcodegen.hpp"

namespace
{
constexpr int kPairingQrSize = 120;

QString addressLabel(const QString& value)
{
    if (QHostAddress(value).isLoopback()) return value + " (this computer only)";
    if (PhoneBridgeManager::isPrivateAddress(value))
    {
        const QString kind = IsLikelyVpnInterface(value) ? "private; possible VPN or virtual adapter"
                                                          : "private LAN";
        const QString interface = FindPhoneFirewallNetwork(value).interface;
        return interface.isEmpty() ? value + " (" + kind + ')'
                                   : value + " (" + kind + " · " + interface + ')';
    }
    return value + " (non-private; unsafe)";
}

}

QPixmap WideMelon::CreatePhonePairingQrCode(const QString& text, int size)
{
    if (text.isEmpty()) return {};
    const QByteArray utf8 = text.toUtf8();
    const qrcodegen::QrCode code = qrcodegen::QrCode::encodeText(
        utf8.constData(), qrcodegen::QrCode::Ecc::MEDIUM);
    constexpr int border = 4;
    const int scale = std::max(1, size / (code.getSize() + border * 2));
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

namespace
{

void showFirewallGuide(QWidget* parent, const PhoneFirewallResult& detected,
                       const QString& address, quint16 port)
{
    if (auto existing = parent->findChild<QWizard*>("phoneFirewallGuide"))
    {
        existing->show();
        existing->raise();
        existing->activateWindow();
        return;
    }
    auto wizard = new QWizard(parent);
    wizard->setObjectName("phoneFirewallGuide");
    wizard->setProperty("phoneEndpoint", address + ':' + QString::number(port));
    wizard->setAttribute(Qt::WA_DeleteOnClose);
    wizard->setWindowTitle("Set up the firewall");
    wizard->setWizardStyle(QWizard::ModernStyle);
    wizard->resize(640, 560);

    auto introduction = new QWizardPage;
    introduction->setTitle("Check the selected network");
    auto introductionLayout = new QVBoxLayout(introduction);
    const QString firewallHint = detected.guidance.isEmpty() ? QString()
        : "\n\nFirewall check: " + detected.guidance;
    auto explanation = new QLabel(
        QString("WideMelon's server is listening at %1:%2. If your phone can already open the pairing page, "
                "no new rule is needed.\n\nUse only your trusted home network. WideMelon will not run "
                "administrator commands; you review and run them in your terminal or system settings.")
            .arg(address).arg(port) + firewallHint);
    explanation->setTextFormat(Qt::PlainText);
    explanation->setWordWrap(true);
    introductionLayout->addWidget(explanation);
    auto firewall = new QComboBox;
    firewall->addItem("System firewall / another security product", "");
#ifdef Q_OS_LINUX
    firewall->addItem("firewalld", "firewalld");
    firewall->addItem("UFW", "UFW");
#endif
    const int selected = firewall->findData(detected.name);
    if (selected >= 0) firewall->setCurrentIndex(selected);
    introductionLayout->addWidget(new QLabel("Use the firewall that manages this network:"));
    introductionLayout->addWidget(firewall);

    auto instructions = new QLabel;
    instructions->setTextFormat(Qt::PlainText);
    instructions->setWordWrap(true);
    introductionLayout->addWidget(instructions);
    auto preparation = new QPlainTextEdit;
    preparation->setReadOnly(true);
    preparation->setMaximumHeight(95);
    introductionLayout->addWidget(preparation);
    auto copyPreparation = new QPushButton("Copy zone checks");
    introductionLayout->addWidget(copyPreparation, 0, Qt::AlignRight);
    QObject::connect(copyPreparation, &QPushButton::clicked, wizard, [preparation]
    {
        QApplication::clipboard()->setText(preparation->toPlainText());
    });
    auto zoneLabel = new QLabel("firewalld zone (from the checks above):");
    auto zone = new QLineEdit;
    zone->setObjectName("firewalldZone");
    zone->setMaxLength(64);
    introductionLayout->addWidget(zoneLabel);
    introductionLayout->addWidget(zone);
    introductionLayout->addStretch();
    wizard->addPage(introduction);

    auto setup = new QWizardPage;
    setup->setTitle("Review and add the rule");
    auto setupLayout = new QVBoxLayout(setup);
    auto scope = new QLabel;
    scope->setTextFormat(Qt::PlainText);
    scope->setWordWrap(true);
    setupLayout->addWidget(scope);
    auto commands = new QPlainTextEdit;
    commands->setReadOnly(true);
    commands->setMinimumHeight(130);
    setupLayout->addWidget(commands);
    auto copy = new QPushButton("Copy commands");
    setupLayout->addWidget(copy, 0, Qt::AlignRight);
    QObject::connect(copy, &QPushButton::clicked, wizard, [commands]
    {
        QApplication::clipboard()->setText(commands->toPlainText());
    });
    wizard->addPage(setup);

    auto verify = new QWizardPage;
    verify->setTitle("Verify and keep the rule");
    auto verifyLayout = new QVBoxLayout(verify);
    auto verifyText = new QLabel(
        "After applying the rule, retry the pairing page on your phone. Opening it proves reachability "
        "at that moment. The checks below also confirm the saved rule; test again after a restart.\n\n"
        "If it still fails, check that both devices share the home network and that Wi-Fi client isolation "
        "or another security product is not blocking them. Do not forward the port on your router.\n\n"
        "Repeat setup if the host address, subnet, interface, zone, or port changes. Remove an old rule first.");
    verifyText->setWordWrap(true);
    verifyLayout->addWidget(verifyText);
    auto verificationHint = new QLabel;
    verificationHint->setTextFormat(Qt::PlainText);
    verificationHint->setWordWrap(true);
    verifyLayout->addWidget(verificationHint);
    auto verification = new QPlainTextEdit;
    verification->setReadOnly(true);
    verification->setMaximumHeight(90);
    verifyLayout->addWidget(verification);
    auto copyCheck = new QPushButton("Copy check commands");
    verifyLayout->addWidget(copyCheck, 0, Qt::AlignRight);
    QObject::connect(copyCheck, &QPushButton::clicked, wizard, [verification]
    {
        QApplication::clipboard()->setText(verification->toPlainText());
    });
    auto removalLabel = new QLabel("To remove only this rule later:");
    verifyLayout->addWidget(removalLabel);
    auto removal = new QPlainTextEdit;
    removal->setReadOnly(true);
    removal->setMaximumHeight(90);
    verifyLayout->addWidget(removal);
    auto copyRemoval = new QPushButton("Copy removal commands");
    verifyLayout->addWidget(copyRemoval, 0, Qt::AlignRight);
    QObject::connect(copyRemoval, &QPushButton::clicked, wizard, [removal]
    {
        QApplication::clipboard()->setText(removal->toPlainText());
    });
    wizard->addPage(verify);

    const auto refresh = [=]
    {
        PhoneFirewallResult selectedFirewall;
        selectedFirewall.name = firewall->currentData().toString();
        const auto guide = BuildPhoneFirewallGuide(selectedFirewall, FindPhoneFirewallNetwork(address), port, zone->text());
        const bool needsZone = !guide.preparation.isEmpty();
        instructions->setText(guide.instructions);
        preparation->setPlainText(guide.preparation);
        preparation->setVisible(needsZone);
        copyPreparation->setVisible(needsZone);
        zoneLabel->setVisible(needsZone);
        zone->setVisible(needsZone);
        scope->setText(guide.commands.isEmpty() ? (needsZone
            ? "Go Back and enter the firewalld zone from the terminal checks before generating a rule."
            : guide.instructions)
            : guide.scope + "\n\nCopy and run these commands in your terminal. Enter your administrator password "
              "there if asked. They add this rule for the current session and future restarts, without reloading "
              "or disabling your firewall.");
        commands->setPlainText(guide.commands);
        commands->setVisible(!guide.commands.isEmpty());
        copy->setVisible(!guide.commands.isEmpty());
        verificationHint->setText(guide.verificationHint);
        verification->setPlainText(guide.verification);
        verification->setVisible(!guide.verification.isEmpty());
        copyCheck->setVisible(!guide.verification.isEmpty());
        removal->setPlainText(guide.removal);
        removal->setVisible(!guide.removal.isEmpty());
        removalLabel->setVisible(!guide.removal.isEmpty());
        copyRemoval->setVisible(!guide.removal.isEmpty());
    };
    QObject::connect(firewall, qOverload<int>(&QComboBox::currentIndexChanged), wizard, refresh);
    QObject::connect(zone, &QLineEdit::textChanged, wizard, refresh);
    QObject::connect(wizard, &QWizard::currentIdChanged, wizard, refresh);
    refresh();
    // Non-modal: no blocking probes or nested event loop on the streaming thread.
    wizard->show();
}
}

PhoneScreenDialog::PhoneScreenDialog(PhoneBridgeManager* manager, QWidget* parent)
    : QDialog(parent, Qt::Tool), manager(manager)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle("Phone screen & controller");
    setModal(false);
    resize(560, 680);

    auto root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(8);

    warning = new QLabel("Use only on a private home network you trust. Pairing prevents other devices from connecting, but the connection is not encrypted.");
    warning->setWordWrap(true);
    warning->setStyleSheet("QLabel { color: #9a5a00; background: #fff0c2; padding: 8px; border-radius: 4px; }");
    root->addWidget(warning);

    auto sessionBox = new QGroupBox("Session");
    auto sessionLayout = new QGridLayout(sessionBox);
    auto sessionDetails = new QFormLayout;
    status = new QLabel;
    address = new QLabel;
    address->setWordWrap(true);
    // URLs have no natural word break. Let the form shrink them instead of
    // making Qt 5 widen the entire dialog to their unbroken size hint.
    address->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    address->setTextInteractionFlags(Qt::TextSelectableByMouse);
    sessionDetails->addRow("Status", status);
    auto addressRow = new QHBoxLayout;
    addressRow->addWidget(address, 1);
    auto copy = new QPushButton("Copy URL");
    addressRow->addWidget(copy);
    sessionDetails->addRow("Phone URL", addressRow);
    pairingQr = new QLabel;
    pairingQr->setAlignment(Qt::AlignCenter);
    pairingQr->setFixedSize(kPairingQrSize, kPairingQrSize);
    pairingQr->setWordWrap(true);
    pairingQr->setText("Start the server to create a pairing code.");
    pairingCode = new QLabel;
    pairingCode->setTextInteractionFlags(Qt::TextSelectableByMouse);
    QFont codeFont = pairingCode->font();
    codeFont.setBold(true);
    codeFont.setPointSize(codeFont.pointSize() + 4);
    pairingCode->setFont(codeFont);
    sessionDetails->addRow("Manual code", pairingCode);
    reuseLastPairingCode = new QCheckBox("Reuse last code");
    reuseLastPairingCode->setToolTip("Use the previous manual pairing code when the server starts.");
    sessionDetails->addRow(QString(), reuseLastPairingCode);
    connectedClient = new QLabel("None");
    sessionDetails->addRow("Connected phone", connectedClient);
    sessionLayout->addLayout(sessionDetails, 0, 0, 2, 1);
    auto pairingLabel = new QLabel("Scan to pair");
    pairingLabel->setAlignment(Qt::AlignCenter);
    sessionLayout->addWidget(pairingLabel, 0, 1);
    sessionLayout->addWidget(pairingQr, 1, 1, Qt::AlignTop | Qt::AlignHCenter);
    sessionLayout->setColumnStretch(0, 1);
    root->addWidget(sessionBox);

    auto networkBox = new QGroupBox("Network && stream");
    networkBox->setObjectName("phoneNetworkSettings");
    auto form = new QGridLayout(networkBox);
    auto interfaceRow = new QHBoxLayout;
    interfaceBox = new QComboBox;
    // Adapter descriptions can be long. Qt 5 otherwise uses the longest item
    // as the minimum width and prevents the dialog from fitting small screens.
    interfaceBox->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    interfaceBox->setMinimumContentsLength(18);
    interfaceRow->addWidget(interfaceBox, 1);
    auto refresh = new QPushButton("Refresh");
    interfaceRow->addWidget(refresh);
    form->addWidget(new QLabel("IPv4 interface"), 0, 0);
    form->addLayout(interfaceRow, 0, 1, 1, 3);
    port = new QSpinBox;
    port->setRange(1024, 65534);
    port->setToolTip("The web page, video, and controls share this TCP port.");
    form->addWidget(new QLabel("Base port"), 1, 0);
    form->addWidget(port, 1, 1);
    quality = new QSpinBox;
    quality->setRange(30, 100);
    quality->setSuffix("%");
    form->addWidget(new QLabel("JPEG quality"), 1, 2);
    form->addWidget(quality, 1, 3);
    auto fps = new QLabel("30 FPS · 256 × 192 · latest frame wins");
    form->addWidget(new QLabel("Stream"), 2, 0);
    form->addWidget(fps, 2, 1, 1, 3);
    auto layoutButton = new QPushButton("Controller layout…");
    layoutButton->setToolTip("Arrange and resize the phone screen and controls with the mouse.");
    auto securityButton = new QPushButton("Security…");
    firewallButton = new QPushButton("Firewall help…");
    auto networkActions = new QHBoxLayout;
    networkActions->addWidget(layoutButton);
    networkActions->addWidget(securityButton);
    networkActions->addWidget(firewallButton);
    form->addLayout(networkActions, 3, 0, 1, 4);
    form->setColumnStretch(1, 1);
    form->setColumnStretch(3, 1);
    root->addWidget(networkBox);

    // Everyday connection settings stay visible. Only expandable diagnostics
    // and logs scroll when the dialog is constrained on a smaller screen.
    auto settingsArea = new QScrollArea;
    settingsArea->setWidgetResizable(true);
    settingsArea->setFrameShape(QFrame::NoFrame);
    auto settingsContents = new QWidget;
    auto settingsLayout = new QVBoxLayout(settingsContents);
    settingsLayout->setContentsMargins(0, 0, 0, 0);
    settingsArea->setWidget(settingsContents);
    root->addWidget(settingsArea, 1);

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
    settingsLayout->addWidget(diagnosticsBox);

    logs = new QPlainTextEdit;
    logs->setReadOnly(true);
    logs->setMaximumBlockCount(1000);
    logs->setPlaceholderText("Bridge events will appear here after the session is enabled.");
    settingsLayout->addWidget(logs, 1);

    auto actions = new QGridLayout;
    startButton = new QPushButton("Start server");
    stopButton = new QPushButton("Stop server");
    auto disconnect = new QPushButton("Disconnect phone");
    auto regenerate = new QPushButton("Generate new code");
    auto revoke = new QPushButton("Revoke pairing");
    auto exportButton = new QPushButton("Export diagnostics…");
    actions->addWidget(startButton, 0, 0);
    actions->addWidget(stopButton, 0, 1);
    actions->addWidget(disconnect, 0, 2);
    actions->addWidget(regenerate, 1, 0);
    actions->addWidget(revoke, 1, 1);
    actions->addWidget(exportButton, 1, 2);
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
    connect(firewallButton, &QPushButton::clicked, this, [this]
    {
        if (!this->manager || !this->manager->isListening()) return;
        const auto settings = this->manager->settings();
        showFirewallGuide(this, firewallResult, settings.address, settings.basePort);
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
    connect(reuseLastPairingCode, &QCheckBox::toggled, this, [this] { applyControls(); });
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
    reuseLastPairingCode->setChecked(value.reuseLastPairingCode);
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
    value.reuseLastPairingCode = reuseLastPairingCode->isChecked();
    PhoneBridgeManager::saveSettings(value);
    if (manager) manager->setSettings(value);
}

bool PhoneScreenDialog::confirmUnsafeStart()
{
    return QMessageBox::warning(this, "Start phone bridge on trusted network",
        "Pairing prevents ordinary devices from connecting, but video and controls are not encrypted. "
        "Only continue on a private home network you trust. The phone and computer must be on the same local "
        "network; guest Wi-Fi and wireless client isolation can prevent the connection.\n\nThe bridge stops when "
        "WideMelon exits.",
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
    if (QHostAddress(interfaceBox->currentData().toString()).isLoopback())
    {
        QMessageBox::information(this, "Computer-only address selected",
            "The selected 127.0.0.0/8 address can only be opened on this computer. Select the private LAN address "
            "that is on the same Wi-Fi or wired network as your phone, then start the server again.");
        return;
    }
    if (!confirmUnsafeStart()) return;
    applyControls();
    if (manager && !manager->start())
        QMessageBox::critical(this, "Phone bridge failed", manager->lastError());
    else
        checkFirewall();
    updateUi();
}

void PhoneScreenDialog::checkFirewall()
{
    if (!manager || !manager->isListening() || firewallChecked) return;
    firewallChecked = true;
    const PhoneBridgeSettings settings = manager->settings();
    auto watcher = new QFutureWatcher<PhoneFirewallResult>(this);
    connect(watcher, &QFutureWatcher<PhoneFirewallResult>::finished, this, [this, watcher, settings]
    {
        const PhoneFirewallResult firewall = watcher->result();
        watcher->deleteLater();
        if (!manager || !manager->isListening()) return;
        const auto current = manager->settings();
        if (current.address != settings.address || current.basePort != settings.basePort) return;
        firewallResult = firewall;
        firewallButton->setToolTip(firewall.guidance.isEmpty()
            ? "If the phone cannot open the pairing page, check the firewall setup guide."
            : firewall.guidance);
    });
    watcher->setFuture(QtConcurrent::run([settings]
    {
        return InspectPhoneFirewall(settings.address, settings.basePort);
    }));
}

void PhoneScreenDialog::stopOrDisarm()
{
    if (manager) manager->stop();
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
    if (auto wizard = findChild<QWizard*>("phoneFirewallGuide"))
    {
        const auto settings = manager ? manager->settings() : PhoneBridgeSettings{};
        if (!listening || wizard->property("phoneEndpoint").toString()
                != settings.address + ':' + QString::number(settings.basePort)) wizard->close();
    }
    if (listening && !firewallChecked) checkFirewall();
    if (!listening)
    {
        firewallChecked = false;
        firewallResult = {};
    }
    status->setText(manager ? manager->statusText() : "Unavailable");
    const QString host = interfaceBox->currentData().toString();
    address->setText(host.isEmpty() ? "Unavailable" : QString("http://%1:%2/").arg(host).arg(port->value()));
    const QString pairUrl = listening ? manager->pairingUrl() : QString();
    if (pairUrl != displayedPairingUrl)
    {
        displayedPairingUrl = pairUrl;
        // QLabel::setText clears a pixmap, even when the new text is empty.
        if (pairUrl.isEmpty()) pairingQr->setText("Start the server to create a pairing code.");
        else pairingQr->setPixmap(WideMelon::CreatePhonePairingQrCode(pairUrl, kPairingQrSize));
    }
    pairingCode->setText(listening ? manager->pairingCode() : QStringLiteral("—"));
    connectedClient->setText(connected ? manager->connectedClientLabel() : QStringLiteral("None"));
    startButton->setEnabled(!host.isEmpty() && !listening);
    stopButton->setEnabled(listening);
    firewallButton->setEnabled(listening);
    interfaceBox->setEnabled(!listening);
    port->setEnabled(!listening);
    synchronousCapture->setEnabled(!listening);
    reuseLastPairingCode->setEnabled(!listening);
    testPattern->setEnabled(listening && connected);
    if (manager)
    {
        const PhoneBridgeMetrics m = manager->metrics();
        metrics->setText(QString("offered %1 · encoded %2 · sent %3 · acked %4 · dropped %5\n"
                                 "JPEG %6 bytes · encode %7 ms avg · RTT %8 ms · rejected pairings %9")
                         .arg(m.framesOffered).arg(m.framesEncoded).arg(m.framesSent)
                         .arg(m.framesAcked).arg(m.framesDropped).arg(m.lastFrameBytes)
                         .arg(m.averageEncodeMs, 0, 'f', 2).arg(m.roundTripMs, 0, 'f', 1)
                         .arg(m.authenticationFailures)
            + QString("\nFPS: capture %1 · sent %2 · acked %3\n"
                      "GPU %4 ms · delivery %5 ms · frame ACK %6 ms · phone decode %7 ms")
                .arg(m.offeredFps, 0, 'f', 1).arg(m.sentFps, 0, 'f', 1).arg(m.ackedFps, 0, 'f', 1)
                .arg(m.captureMs, 0, 'f', 1).arg(m.deliveryMs, 0, 'f', 1)
                .arg(m.frameAckMs, 0, 'f', 1).arg(m.browserDecodeMs, 0, 'f', 1));
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
    if (!manager->exportDiagnostics(path, firewallResult, &error)) QMessageBox::critical(this, "Export failed", error);
}

namespace WideMelon
{
void OpenPhoneScreenSettings(PhoneBridgeManager* manager, QWidget* parent)
{
    auto dialog = new PhoneScreenDialog(manager, parent);
    dialog->show();
    dialog->positionBeside(parent);
    dialog->raise();
    dialog->activateWindow();
}
}
