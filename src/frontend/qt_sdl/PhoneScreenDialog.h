// Native configurator for WideMelon's phone bridge.
#pragma once

#include <QDialog>

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

private:
    void loadControls();
    void applyControls();
    bool confirmUnsafeStart();

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
    QLabel* metrics;
    QLabel* warning;
    QPlainTextEdit* logs;
    QPushButton* startButton;
    QPushButton* stopButton;
    QTimer* refreshTimer;
};

namespace WideMelon
{
bool PhoneBridgeRequestedForSession();
void ClearPhoneBridgeSessionRequest();
void OpenPhoneScreenSettings(PhoneBridgeManager* manager, QWidget* parent);
}
