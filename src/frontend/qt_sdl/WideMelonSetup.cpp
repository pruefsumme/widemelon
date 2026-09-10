// WideMelon's small native startup dialog.
#include "WideMelonSetup.h"

#include <algorithm>

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include "EmuInstance.h"
#include "WideMelon.h"
#include "ScreenLayout.h"

namespace
{

constexpr int kDefaultViewWidth = 342;
constexpr int kDefaultScale = 4;
constexpr int kDefaultWindowWidth = 1600;
constexpr int kDefaultWindowHeight = 900;

bool validViewWidth(int width)
{
    return width >= 256 && width <= 768 && (width % 2) == 0;
}

int envInt(const char* name, int fallback)
{
    bool ok = false;
    const int value = qEnvironmentVariable(name).toInt(&ok);
    return ok ? value : fallback;
}

int fixedResolutionWidth(int index)
{
    switch (index)
    {
    case 0: return 1280;
    case 1: return 1600;
    case 2: return 1920;
    case 3: return 2560;
    case 4: return 3440;
    case 5: return 3840;
    default: return kDefaultWindowWidth;
    }
}

int fixedResolutionHeight(int index)
{
    switch (index)
    {
    case 0: return 720;
    case 1: return 900;
    case 2: return 1080;
    case 3: return 1440;
    case 4: return 1440;
    case 5: return 2160;
    default: return kDefaultWindowHeight;
    }
}

void setDefaultKey(Config::Table& keys, const char* name, int key)
{
    if (keys.GetInt(name) == -1)
        keys.SetInt(name, key);
}

void applyProfile(int viewWidth, int scale, int windowWidth, int windowHeight,
                  bool integerScaling, bool fullscreen)
{
    viewWidth = validViewWidth(viewWidth) ? viewWidth : kDefaultViewWidth;
    scale = std::clamp(scale, 1, 8);
    windowWidth = std::clamp(windowWidth, 640, 7680);
    windowHeight = std::clamp(windowHeight, 480, 4320);

    qputenv("WIDEMELON_VIEW_WIDTH", QByteArray::number(viewWidth));
    qputenv("WIDEMELON_SCALE", QByteArray::number(scale));
    qputenv("WIDEMELON_WINDOW_WIDTH", QByteArray::number(windowWidth));
    qputenv("WIDEMELON_WINDOW_HEIGHT", QByteArray::number(windowHeight));
    qputenv("WIDEMELON_INTEGER", integerScaling ? "1" : "0");

    auto global = Config::GetGlobalTable();
    global.SetInt("3D.Renderer", renderer3D_OpenGL);
    global.SetInt("3D.GL.ScaleFactor", scale);
    global.SetBool("Screen.Filter", false);
    global.SetBool("Emu.DirectBoot", true);
    global.SetInt("Emu.ConsoleType", 0);
    global.SetBool("Emu.ExternalBIOSEnable", false);
    global.SetInt("WideMelon.ViewWidth", viewWidth);
    global.SetInt("WideMelon.Scale", scale);
    global.SetInt("WideMelon.WindowWidth", windowWidth);
    global.SetInt("WideMelon.WindowHeight", windowHeight);
    global.SetBool("WideMelon.IntegerScaling", integerScaling);
    global.SetBool("WideMelon.Fullscreen", fullscreen);

    auto local = Config::GetLocalTable(0);
    auto window = local.GetTable("Window0");
    window.SetBool("ScreenFilter", false);
    window.SetInt("Width", windowWidth);
    window.SetInt("Height", windowHeight);
    window.SetInt("ScreenLayout", screenLayout_Horizontal);
    window.SetInt("ScreenSizing", screenSizing_EmphTop);
    window.SetInt("ScreenAspectTop", 0);
    window.SetInt("ScreenAspectBot", 0);
    window.SetBool("IntegerScaling", integerScaling);

    auto keys = local.GetTable("Keyboard");
    setDefaultKey(keys, "A", Qt::Key_X);
    setDefaultKey(keys, "B", Qt::Key_Z);
    setDefaultKey(keys, "X", Qt::Key_S);
    setDefaultKey(keys, "Y", Qt::Key_A);
    setDefaultKey(keys, "L", Qt::Key_Q);
    setDefaultKey(keys, "R", Qt::Key_W);
    setDefaultKey(keys, "Up", Qt::Key_Up);
    setDefaultKey(keys, "Down", Qt::Key_Down);
    setDefaultKey(keys, "Left", Qt::Key_Left);
    setDefaultKey(keys, "Right", Qt::Key_Right);
    setDefaultKey(keys, "Start", Qt::Key_Return);
    setDefaultKey(keys, "Select", Qt::Key_Backspace);
    setDefaultKey(keys, "HK_FullscreenToggle", Qt::Key_F11);
    setDefaultKey(keys, "HK_Pause", Qt::Key_P);
    setDefaultKey(keys, "HK_FastForward", Qt::Key_Tab);
}

class SetupDialog final : public QDialog
{
public:
    explicit SetupDialog(bool startFullscreen, bool startup, QWidget* parent = nullptr)
        : QDialog(parent), startup(startup)
    {
        setWindowTitle("WideMelon settings");
        setModal(true);
        setMinimumWidth(500);

        auto root = new QVBoxLayout(this);
        root->setContentsMargins(18, 18, 18, 18);
        root->setSpacing(12);

        auto videoBox = new QGroupBox("Video");
        auto videoLayout = new QFormLayout(videoBox);
        videoLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

        viewport = new QComboBox;
        viewport->addItem("Native 4:3", 256);
        viewport->addItem("16:10", 308);
        viewport->addItem("16:9", 342);
        viewport->addItem("Ultrawide 21:9", 448);
        viewport->addItem("Superwide 32:9", 682);
        viewport->addItem("Custom", 0);
        videoLayout->addRow("World viewport", viewport);

        customWidth = new QSpinBox;
        customWidth->setRange(256, 768);
        customWidth->setSingleStep(2);
        customWidth->setSuffix(" × 192");
        videoLayout->addRow("Custom viewport", customWidth);

        resolution = new QComboBox;
        resolution->addItems({
            "1280 × 720", "1600 × 900", "1920 × 1080", "2560 × 1440",
            "3440 × 1440", "3840 × 2160", "Custom window"
        });
        videoLayout->addRow("Window resolution", resolution);

        auto dimensions = new QHBoxLayout;
        windowWidth = new QSpinBox;
        windowWidth->setRange(640, 7680);
        windowHeight = new QSpinBox;
        windowHeight->setRange(480, 4320);
        dimensions->addWidget(windowWidth);
        dimensions->addWidget(new QLabel("×"));
        dimensions->addWidget(windowHeight);
        dimensions->addStretch();
        videoLayout->addRow("Custom window", dimensions);

        scale = new QComboBox;
        for (int i = 1; i <= 8; i++)
            scale->addItem(QString::number(i) + "×", i);
        videoLayout->addRow("3D render scale", scale);

        integerScaling = new QCheckBox("Integer scaling");
        videoLayout->addRow("Presentation", integerScaling);

        summary = new QLabel;
        summary->setWordWrap(true);
        videoLayout->addRow(QString(), summary);
        root->addWidget(videoBox);

        fullscreen = new QCheckBox("Start fullscreen");
        root->addWidget(fullscreen);

        doNotShowAgain = new QCheckBox("Do not show this window again");
        root->addWidget(doNotShowAgain);

        auto buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, Qt::Horizontal, this);
        buttons->addButton(startup ? "Start melonDS" : "Save", QDialogButtonBox::AcceptRole);
        root->addWidget(buttons);

        auto global = Config::GetGlobalTable();
        const int savedWidth = global.GetInt("WideMelon.ViewWidth");
        const int viewWidth = validViewWidth(savedWidth) ? savedWidth : kDefaultViewWidth;
        const int savedScale = global.GetInt("WideMelon.Scale");
        const int savedResolution = global.GetInt("WideMelon.Resolution");
        const int savedWindowWidth = global.GetInt("WideMelon.WindowWidth");
        const int savedWindowHeight = global.GetInt("WideMelon.WindowHeight");

        int viewportIndex = viewport->findData(viewWidth);
        if (viewportIndex < 0)
            viewportIndex = viewport->count() - 1;
        viewport->setCurrentIndex(viewportIndex);
        customWidth->setValue(viewWidth);
        scale->setCurrentIndex(qBound(0, (savedScale > 0 ? savedScale : kDefaultScale) - 1, 7));
        resolution->setCurrentIndex(qBound(0, savedResolution, 6));
        windowWidth->setValue(std::clamp(savedWindowWidth > 0 ? savedWindowWidth : kDefaultWindowWidth, 640, 7680));
        windowHeight->setValue(std::clamp(savedWindowHeight > 0 ? savedWindowHeight : kDefaultWindowHeight, 480, 4320));
        integerScaling->setChecked(global.GetBool("WideMelon.IntegerScaling"));
        fullscreen->setChecked(startFullscreen || global.GetBool("WideMelon.Fullscreen"));
        doNotShowAgain->setChecked(global.GetBool("WideMelon.SetupDismissed"));
        connect(viewport, qOverload<int>(&QComboBox::currentIndexChanged), this,
                [this] { updateViewportControls(); });
        connect(resolution, qOverload<int>(&QComboBox::currentIndexChanged), this,
                [this] { updateResolutionControls(); });
        connect(customWidth, qOverload<int>(&QSpinBox::valueChanged), this,
                [this](int value)
        {
            if (value % 2 != 0)
                customWidth->setValue(value - 1);
            updateSummary();
        });
        connect(scale, qOverload<int>(&QComboBox::currentIndexChanged), this,
                [this] { updateSummary(); });
        connect(windowWidth, qOverload<int>(&QSpinBox::valueChanged), this,
                [this] { updateSummary(); });
        connect(windowHeight, qOverload<int>(&QSpinBox::valueChanged), this,
                [this] { updateSummary(); });
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);

        updateViewportControls();
        updateResolutionControls();
    }

    bool apply()
    {
        const int viewWidth = viewport->currentData().toInt() == 0
            ? customWidth->value() : viewport->currentData().toInt();
        const int resolutionIndex = resolution->currentIndex();
        const int outputWidth = resolutionIndex < 6 ? fixedResolutionWidth(resolutionIndex) : windowWidth->value();
        const int outputHeight = resolutionIndex < 6 ? fixedResolutionHeight(resolutionIndex) : windowHeight->value();

        applyProfile(viewWidth, scale->currentData().toInt(), outputWidth, outputHeight,
                     integerScaling->isChecked(), fullscreen->isChecked());
        auto global = Config::GetGlobalTable();
        global.SetInt("WideMelon.Resolution", resolutionIndex);
        const bool newlyDismissed = doNotShowAgain->isChecked()
            && !global.GetBool("WideMelon.SetupDismissed");
        global.SetBool("WideMelon.SetupDismissed", doNotShowAgain->isChecked());
        Config::Save();
        return newlyDismissed;
    }

    bool startFullscreen() const { return fullscreen->isChecked(); }

private:
    void updateViewportControls()
    {
        const bool custom = viewport->currentData().toInt() == 0;
        customWidth->setEnabled(custom);
        if (!custom)
            customWidth->setValue(viewport->currentData().toInt());
        updateSummary();
    }

    void updateResolutionControls()
    {
        const bool custom = resolution->currentIndex() >= 6;
        windowWidth->setEnabled(custom);
        windowHeight->setEnabled(custom);
        if (!custom)
        {
            windowWidth->setValue(fixedResolutionWidth(resolution->currentIndex()));
            windowHeight->setValue(fixedResolutionHeight(resolution->currentIndex()));
        }
        updateSummary();
    }

    void updateSummary()
    {
        const int viewWidth = viewport->currentData().toInt() == 0
            ? customWidth->value() : viewport->currentData().toInt();
        const int renderScale = scale->currentData().toInt();
        const int extraPercent = qRound((viewWidth / 256.0 - 1.0) * 100.0);
        const int resolutionIndex = resolution->currentIndex();
        const int outputWidth = resolutionIndex < 6 ? fixedResolutionWidth(resolutionIndex) : windowWidth->value();
        const int outputHeight = resolutionIndex < 6 ? fixedResolutionHeight(resolutionIndex) : windowHeight->value();
        summary->setText(QString("%1 × 192 world view · %2 × %3 3D framebuffer · +%4% horizontal view · %5 × %6 output")
                         .arg(viewWidth).arg(viewWidth * renderScale).arg(192 * renderScale)
                         .arg(extraPercent).arg(outputWidth).arg(outputHeight));
    }

    QLabel* summary;
    QComboBox* viewport;
    QSpinBox* customWidth;
    QComboBox* resolution;
    QSpinBox* windowWidth;
    QSpinBox* windowHeight;
    QComboBox* scale;
    QCheckBox* integerScaling;
    QCheckBox* fullscreen;
    QCheckBox* doNotShowAgain;
    bool startup;
};

}

namespace WideMelon
{

void ApplyEnvironmentProfile()
{
    const int viewWidth = WideMelon::Width();
    const int scale = envInt("WIDEMELON_SCALE", kDefaultScale);
    const int windowWidth = envInt("WIDEMELON_WINDOW_WIDTH", kDefaultWindowWidth);
    const int windowHeight = envInt("WIDEMELON_WINDOW_HEIGHT", kDefaultWindowHeight);
    const bool integerScaling = envInt("WIDEMELON_INTEGER", 0) != 0;
    applyProfile(viewWidth, scale, windowWidth, windowHeight, integerScaling, false);
}

bool Configure(CLI::CommandLineOptions& options)
{
    if (qEnvironmentVariableIsSet("WIDEMELON_VIEW_WIDTH"))
    {
        ApplyEnvironmentProfile();
        return true;
    }

    auto global = Config::GetGlobalTable();
    if (global.GetBool("WideMelon.SetupDismissed"))
    {
        const int savedWidth = global.GetInt("WideMelon.ViewWidth");
        const int savedScale = global.GetInt("WideMelon.Scale");
        const int savedWindowWidth = global.GetInt("WideMelon.WindowWidth");
        const int savedWindowHeight = global.GetInt("WideMelon.WindowHeight");
        const bool savedFullscreen = global.GetBool("WideMelon.Fullscreen");
        applyProfile(savedWidth, savedScale, savedWindowWidth, savedWindowHeight,
                     global.GetBool("WideMelon.IntegerScaling"), savedFullscreen);
        options.fullscreen = options.fullscreen || savedFullscreen;
        return true;
    }

    SetupDialog dialog(options.fullscreen, true);
    if (dialog.exec() != QDialog::Accepted)
        return false;

    const bool newlyDismissed = dialog.apply();
    options.fullscreen = dialog.startFullscreen();
    if (newlyDismissed)
    {
        QMessageBox::information(nullptr, "WideMelon settings saved",
            "WideMelon will open directly from now on. You can change these settings "
            "at any time from Config > WideMelon settings. Viewport and resolution "
            "changes take effect the next time WideMelon starts.");
    }
    return true;
}

void OpenSettings(QWidget* parent)
{
    SetupDialog dialog(false, false, parent);
    if (dialog.exec() != QDialog::Accepted)
        return;

    dialog.apply();
    QMessageBox::information(parent, "WideMelon settings saved",
        "Your settings will take effect the next time WideMelon starts.");
}

}
