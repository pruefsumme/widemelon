// Native configurator for WideMelon's phone bridge.
// Copyright (C) 2026 WideMelon contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDialog>
#include "PhoneFirewall.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTimer;
class PhoneBridgeManager;

class PhoneScreenDialog final : public QDialog
{
    Q_OBJECT

public:
    PhoneScreenDialog(PhoneBridgeManager* manager, bool startup, QWidget* parent = nullptr);

    void positionBeside(QWidget* owner);

private slots:
    void refreshInterfaces();
    void updateUi();
    void startOrArm();
    void stopOrDisarm();
    void exportDiagnostics();
    void regeneratePairing();

private:
    void loadControls();
    void applyControls();
    bool confirmUnsafeStart();
    void checkFirewall();

    PhoneBridgeManager* manager;
    bool startup;
    QComboBox* interfaceBox;
    QSpinBox* port;
    QSpinBox* quality;
    QComboBox* logLevel;
    QCheckBox* consoleLog;
    QCheckBox* fileLog;
    QCheckBox* synchronousCapture;
    QCheckBox* testPattern;
    QLabel* status;
    QLabel* address;
    QLabel* pairingQr;
    QLabel* pairingCode;
    QLabel* connectedClient;
    QLabel* metrics;
    QLabel* warning;
    QPlainTextEdit* logs;
    QPushButton* startButton;
    QPushButton* stopButton;
    QTimer* refreshTimer;
    bool firewallChecked = false;
    PhoneFirewallResult firewallResult;
    QString displayedPairingUrl;
};

namespace WideMelon
{
bool PhoneBridgeRequestedForSession();
void ClearPhoneBridgeSessionRequest();
void OpenPhoneScreenSettings(PhoneBridgeManager* manager, QWidget* parent);
}
