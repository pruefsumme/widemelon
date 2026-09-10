// Mouse-first editor for the phone controller layout.
// Copyright (C) 2026 WideMelon contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "PhoneLayoutDialog.h"

#include <algorithm>
#include <cmath>
#include <functional>

#include <QComboBox>
#include <QCheckBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QShortcut>
#include <QSplitter>
#include <QVBoxLayout>
#include <QWheelEvent>

#include "PhoneBridge.h"

namespace
{
QPointF mousePosition(QMouseEvent* event)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return event->position();
#else
    return event->localPos();
#endif
}

QPointF wheelPosition(QWheelEvent* event)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return event->position();
#else
    return event->posF();
#endif
}

qreal lockedNormalizedRatio(PhoneLayoutItem::Kind kind)
{
    if (kind == PhoneLayoutItem::Screen) return (4.0 / 3.0) * (420.0 / 900.0);
    if (kind == PhoneLayoutItem::Directional || kind == PhoneLayoutItem::FaceButtons)
        return 420.0 / 900.0;
    return 0.0;
}
}

class PhoneLayoutCanvas final : public QWidget
{
public:
    explicit PhoneLayoutCanvas(PhoneControllerLayout* layout, QWidget* parent = nullptr)
        : QWidget(parent), layout(layout)
    {
        setMinimumSize(620, 300);
        setMouseTracking(true);
        setFocusPolicy(Qt::StrongFocus);
    }

    std::function<void(int)> selectionChanged;
    std::function<void()> editStarted;
    std::function<void()> layoutEdited;
    std::function<void(QPointF)> addRequested;

    int selection() const { return selected; }
    void setSelection(int value)
    {
        selected = value;
        update();
        if (selectionChanged) selectionChanged(selected);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), QColor(20, 21, 23));
        const QRectF area = contentRect();
        painter.fillRect(area, Qt::black);
        if (layout->showHud)
        {
            painter.setPen(QColor(128, 131, 136));
            painter.drawText(area.adjusted(8, 6, -40, -6), Qt::AlignLeft | Qt::AlignTop,
                             "Connected  ·  30.0 FPS  ·  frame 120");
        }
        painter.setPen(QPen(QColor(150, 153, 158), 1.5));
        const QPointF corner(area.right() - 17, area.top() + 15);
        painter.drawLine(corner + QPointF(-5, -4), corner + QPointF(0, -4));
        painter.drawLine(corner + QPointF(0, -4), corner + QPointF(0, 1));
        painter.drawLine(corner + QPointF(-9, 5), corner + QPointF(-14, 5));
        painter.drawLine(corner + QPointF(-14, 5), corner + QPointF(-14, 0));

        for (int index = 0; index < layout->items.size(); index++)
        {
            const PhoneLayoutItem& item = layout->items[index];
            const QRectF box = itemRect(item);
            if (item.kind == PhoneLayoutItem::Screen)
            {
                painter.setPen(QPen(QColor(90, 93, 98), 2));
                painter.setBrush(QColor(4, 5, 6));
                painter.drawRoundedRect(box, 5, 5);
                painter.setPen(QColor(100, 104, 110));
                painter.drawText(box, Qt::AlignCenter, "256 × 192\nTouch screen");
            }
            else if (item.kind == PhoneLayoutItem::Directional)
            {
                painter.setPen(QPen(QColor(205, 207, 210), 1));
                if (item.appearance == "analog")
                {
                    painter.setBrush(QColor(174, 176, 180));
                    painter.drawEllipse(box);
                    painter.setBrush(QColor(211, 212, 214));
                    painter.drawEllipse(box.adjusted(box.width() * .19, box.height() * .19,
                                                     -box.width() * .19, -box.height() * .19));
                }
                else
                {
                    painter.setBrush(QColor(174, 176, 180));
                    const qreal arm = std::min(box.width(), box.height()) * .32;
                    painter.drawRoundedRect(QRectF(box.left(), box.center().y() - arm / 2,
                                                   box.width(), arm), 5, 5);
                    painter.drawRoundedRect(QRectF(box.center().x() - arm / 2, box.top(),
                                                   arm, box.height()), 5, 5);
                }
            }
            else if (item.kind == PhoneLayoutItem::FaceButtons)
            {
                QLinearGradient gradient(box.topLeft(), box.bottomRight());
                gradient.setColorAt(0, QColor(226, 227, 229));
                gradient.setColorAt(1, QColor(154, 157, 161));
                painter.setPen(QPen(QColor(220, 222, 225), 1));
                painter.setBrush(gradient);
                const qreal diameter = std::min(box.width(), box.height()) * .36;
                const QRectF xButton(box.center().x() - diameter / 2, box.top(), diameter, diameter);
                const QRectF yButton(box.left(), box.center().y() - diameter / 2, diameter, diameter);
                const QRectF aButton(box.right() - diameter, box.center().y() - diameter / 2, diameter, diameter);
                const QRectF bButton(box.center().x() - diameter / 2, box.bottom() - diameter, diameter, diameter);
                for (const auto& button : {std::pair<QRectF, QString>(xButton, "X"), {yButton, "Y"},
                                           {aButton, "A"}, {bButton, "B"}})
                {
                    painter.drawEllipse(button.first);
                    painter.setPen(QColor(28, 29, 31));
                    painter.drawText(button.first, Qt::AlignCenter, button.second);
                    painter.setPen(QPen(QColor(220, 222, 225), 1));
                }
            }
            else
            {
                painter.setPen(QPen(QColor(220, 222, 225), 1));
                QLinearGradient gradient(box.topLeft(), box.bottomRight());
                gradient.setColorAt(0, QColor(226, 227, 229));
                gradient.setColorAt(1, QColor(154, 157, 161));
                painter.setBrush(gradient);
                painter.drawRoundedRect(box, std::min<qreal>(9, box.height() / 3),
                                        std::min<qreal>(9, box.height() / 3));
                painter.setPen(QColor(28, 29, 31));
                painter.drawText(box.adjusted(3, 2, -3, -2), Qt::AlignCenter | Qt::TextWordWrap, item.label);
            }

            if (index == selected)
            {
                painter.setBrush(Qt::NoBrush);
                painter.setPen(QPen(QColor(62, 179, 255), 2));
                painter.drawRect(box.adjusted(-3, -3, 3, 3));
                painter.fillRect(handleRect(box), QColor(62, 179, 255));
            }
        }
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() != Qt::LeftButton) return;
        const QPointF position = mousePosition(event);
        if (selected >= 0 && handleRect(itemRect(layout->items[selected])).adjusted(-4, -4, 4, 4).contains(position))
        {
            if (editStarted) editStarted();
            resizing = true;
            dragStart = position;
            originalRect = layout->items[selected].rect;
            return;
        }
        const int hit = itemAt(position);
        setSelection(hit);
        if (hit >= 0)
        {
            if (editStarted) editStarted();
            dragging = true;
            dragStart = position;
            originalRect = layout->items[hit].rect;
        }
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (selected < 0 || (!dragging && !resizing)) return;
        const QRectF area = contentRect();
        const QPointF delta = mousePosition(event) - dragStart;
        QRectF next = originalRect;
        if (dragging)
        {
            next.moveLeft(std::clamp(originalRect.x() + delta.x() / area.width(), 0.0,
                                     1.0 - originalRect.width()));
            next.moveTop(std::clamp(originalRect.y() + delta.y() / area.height(), 0.0,
                                    1.0 - originalRect.height()));
        }
        else
        {
            next.setWidth(std::clamp(originalRect.width() + delta.x() / area.width(), .025,
                                     1.0 - originalRect.x()));
            next.setHeight(std::clamp(originalRect.height() + delta.y() / area.height(), .04,
                                      1.0 - originalRect.y()));
            const qreal lockedRatio = lockedNormalizedRatio(layout->items[selected].kind);
            if (lockedRatio > 0)
            {
                const qreal horizontalDelta = delta.x() / area.width();
                const qreal verticalDelta = delta.y() / area.height();
                qreal width = std::abs(verticalDelta) > std::abs(horizontalDelta)
                    ? (originalRect.height() + verticalDelta) * lockedRatio
                    : originalRect.width() + horizontalDelta;
                width = std::max<qreal>(.025, width);
                width = std::min(width, (1.0 - next.y()) * lockedRatio);
                width = std::min(width, 1.0 - next.x());
                next.setSize(QSizeF(width, width / lockedRatio));
            }
        }
        layout->items[selected].rect = next;
        update();
        if (layoutEdited) layoutEdited();
    }

    void mouseReleaseEvent(QMouseEvent*) override
    {
        dragging = false;
        resizing = false;
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override
    {
        const QPointF position = mousePosition(event);
        if (itemAt(position) >= 0 || !contentRect().contains(position)) return;
        const QRectF area = contentRect();
        if (addRequested) addRequested(QPointF((position.x() - area.x()) / area.width(),
                                               (position.y() - area.y()) / area.height()));
    }

    void wheelEvent(QWheelEvent* event) override
    {
        if (selected < 0 || !itemRect(layout->items[selected]).contains(wheelPosition(event))) return;
        if (editStarted) editStarted();
        const qreal factor = event->angleDelta().y() > 0 ? 1.06 : 1.0 / 1.06;
        QRectF next = layout->items[selected].rect;
        const QPointF center = next.center();
        const qreal lockedRatio = lockedNormalizedRatio(layout->items[selected].kind);
        if (lockedRatio > 0)
        {
            qreal width = std::clamp(next.width() * factor, .025, .95 * lockedRatio);
            next.setSize(QSizeF(width, width / lockedRatio));
        }
        else
        {
            next.setSize(QSizeF(std::clamp(next.width() * factor, .025, .95),
                                std::clamp(next.height() * factor, .04, .95)));
        }
        next.moveCenter(center);
        next.moveLeft(std::clamp(next.x(), 0.0, 1.0 - next.width()));
        next.moveTop(std::clamp(next.y(), 0.0, 1.0 - next.height()));
        layout->items[selected].rect = next;
        update();
        if (layoutEdited) layoutEdited();
        event->accept();
    }

private:
    QRectF contentRect() const
    {
        QRectF area = rect().adjusted(14, 14, -14, -14);
        const qreal ratio = 900.0 / 420.0;
        if (area.width() / area.height() > ratio)
        {
            const qreal width = area.height() * ratio;
            area.setLeft(area.center().x() - width / 2);
            area.setWidth(width);
        }
        else
        {
            const qreal height = area.width() / ratio;
            area.setTop(area.center().y() - height / 2);
            area.setHeight(height);
        }
        return area;
    }

    QRectF itemRect(const PhoneLayoutItem& item) const
    {
        const QRectF area = contentRect();
        return QRectF(area.x() + item.rect.x() * area.width(), area.y() + item.rect.y() * area.height(),
                      item.rect.width() * area.width(), item.rect.height() * area.height());
    }

    QRectF handleRect(const QRectF& box) const
    {
        return QRectF(box.right() - 5, box.bottom() - 5, 10, 10);
    }

    int itemAt(const QPointF& position) const
    {
        for (int index = layout->items.size() - 1; index >= 0; index--)
            if (itemRect(layout->items[index]).contains(position)) return index;
        return -1;
    }

    PhoneControllerLayout* layout;
    int selected = -1;
    bool dragging = false;
    bool resizing = false;
    QPointF dragStart;
    QRectF originalRect;
};

PhoneLayoutDialog::PhoneLayoutDialog(PhoneBridgeManager* manager, QWidget* parent)
    : QDialog(parent, Qt::Window), manager(manager)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle("Phone controller layout");
    resize(1040, 580);
    const PhoneBridgeSettings settings = manager ? manager->settings() : PhoneBridgeManager::loadSettings();
    layout = PhoneControllerLayout::fromJson(settings.layoutJson);

    auto root = new QVBoxLayout(this);
    auto help = new QLabel("Click a control to select it. Drag to move, drag the blue corner to resize, "
                           "or use the mouse wheel over it to scale. Double-click empty space to add an action. "
                           "Use Ctrl+Z to undo and Ctrl+Shift+Z to redo.");
    help->setWordWrap(true);
    root->addWidget(help);

    auto splitter = new QSplitter;
    canvas = new PhoneLayoutCanvas(&layout);
    splitter->addWidget(canvas);

    auto inspector = new QWidget;
    inspector->setMinimumWidth(260);
    inspector->setMaximumWidth(330);
    auto inspectorLayout = new QVBoxLayout(inspector);
    selectedName = new QLabel("No control selected");
    QFont selectedFont = selectedName->font();
    selectedFont.setBold(true);
    selectedName->setFont(selectedFont);
    inspectorLayout->addWidget(selectedName);

    auto form = new QFormLayout;
    labelEdit = new QLineEdit;
    labelEdit->setMaxLength(24);
    actionBox = new QComboBox;
    for (const PhoneHotkeyAction& action : PhoneLayoutHotkeyActions())
        actionBox->addItem(QString::fromLatin1(action.label), action.id);
    actionBox->setCurrentIndex(std::max(0, actionBox->findData(PhoneLayoutHotkeyActions().at(3).id)));
    directionalStyle = new QComboBox;
    directionalStyle->addItem("D-pad", "dpad");
    directionalStyle->addItem("Analog stick", "analog");
    xValue = new QDoubleSpinBox;
    yValue = new QDoubleSpinBox;
    widthValue = new QDoubleSpinBox;
    heightValue = new QDoubleSpinBox;
    for (QDoubleSpinBox* spin : {xValue, yValue, widthValue, heightValue})
    {
        spin->setRange(0, 100);
        spin->setDecimals(1);
        spin->setSuffix("%");
        spin->setSingleStep(.5);
    }
    form->addRow("Label", labelEdit);
    form->addRow("Emulator action", actionBox);
    form->addRow("Directional control", directionalStyle);
    form->addRow("Left", xValue);
    form->addRow("Top", yValue);
    form->addRow("Width", widthValue);
    form->addRow("Height", heightValue);
    inspectorLayout->addLayout(form);

    showHud = new QCheckBox("Show connection, FPS, and frame text");
    showHud->setChecked(layout.showHud);
    inspectorLayout->addWidget(showHud);

    auto addButton = new QPushButton("Add action button");
    removeButton = new QPushButton("Remove selected");
    auto resetButton = new QPushButton("Reset layout");
    inspectorLayout->addWidget(addButton);
    inspectorLayout->addWidget(removeButton);
    inspectorLayout->addStretch();
    inspectorLayout->addWidget(resetButton);
    splitter->addWidget(inspector);
    splitter->setStretchFactor(0, 1);
    root->addWidget(splitter, 1);

    auto buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    auto apply = buttons->addButton("Apply", QDialogButtonBox::ApplyRole);
    root->addWidget(buttons);

    canvas->selectionChanged = [this](int index) { selectItem(index); };
    canvas->editStarted = [this] { rememberForUndo(); };
    canvas->layoutEdited = [this] { updateInspector(); };
    canvas->addRequested = [this](QPointF position) { addAction(position); };
    connect(labelEdit, &QLineEdit::textEdited, this, [this] { inspectorChanged(); });
    connect(actionBox, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { inspectorChanged(); });
    connect(directionalStyle, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { inspectorChanged(); });
    connect(showHud, &QCheckBox::toggled, this, [this](bool checked)
    {
        if (updatingInspector) return;
        rememberForUndo();
        layout.showHud = checked;
        canvas->update();
    });
    for (QDoubleSpinBox* spin : {xValue, yValue, widthValue, heightValue})
        connect(spin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this] { inspectorChanged(); });
    connect(addButton, &QPushButton::clicked, this, [this] { addAction(); });
    connect(removeButton, &QPushButton::clicked, this, [this]
    {
        const int index = canvas->selection();
        if (index >= 0 && layout.items[index].removable)
        {
            rememberForUndo();
            layout.items.removeAt(index);
            canvas->setSelection(-1);
        }
    });
    connect(resetButton, &QPushButton::clicked, this, [this]
    {
        rememberForUndo();
        layout = PhoneControllerLayout::defaults();
        canvas->setSelection(0);
        canvas->update();
    });
    connect(apply, &QPushButton::clicked, this, &PhoneLayoutDialog::applyLayout);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] { applyLayout(); accept(); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto undoShortcut = new QShortcut(QKeySequence::Undo, this);
    auto redoShortcut = new QShortcut(QKeySequence::Redo, this);
    connect(undoShortcut, &QShortcut::activated, this, &PhoneLayoutDialog::undo);
    connect(redoShortcut, &QShortcut::activated, this, &PhoneLayoutDialog::redo);
    selectItem(0);
}

void PhoneLayoutDialog::selectItem(int index)
{
    Q_UNUSED(index);
    updateInspector();
}

void PhoneLayoutDialog::updateInspector()
{
    updatingInspector = true;
    const int index = canvas->selection();
    const bool selected = index >= 0 && index < layout.items.size();
    selectedName->setText(selected ? layout.items[index].label : "No control selected");
    labelEdit->setEnabled(selected && layout.items[index].kind == PhoneLayoutItem::Button);
    actionBox->setEnabled(selected && layout.items[index].removable);
    directionalStyle->setEnabled(selected && layout.items[index].kind == PhoneLayoutItem::Directional);
    removeButton->setEnabled(selected && layout.items[index].removable);
    xValue->setEnabled(selected);
    yValue->setEnabled(selected);
    widthValue->setEnabled(selected);
    heightValue->setEnabled(selected && lockedNormalizedRatio(layout.items[index].kind) == 0);
    showHud->setChecked(layout.showHud);
    if (selected)
    {
        const PhoneLayoutItem& item = layout.items[index];
        labelEdit->setText(item.label);
        const int actionIndex = actionBox->findData(item.hotkey);
        if (actionIndex >= 0) actionBox->setCurrentIndex(actionIndex);
        const int styleIndex = directionalStyle->findData(item.appearance);
        directionalStyle->setCurrentIndex(std::max(0, styleIndex));
        xValue->setValue(item.rect.x() * 100);
        yValue->setValue(item.rect.y() * 100);
        widthValue->setValue(item.rect.width() * 100);
        heightValue->setValue(item.rect.height() * 100);
    }
    updatingInspector = false;
}

void PhoneLayoutDialog::inspectorChanged()
{
    if (updatingInspector) return;
    const int index = canvas->selection();
    if (index < 0 || index >= layout.items.size()) return;
    rememberForUndo();
    PhoneLayoutItem& item = layout.items[index];
    if (item.kind == PhoneLayoutItem::Button && !labelEdit->text().trimmed().isEmpty())
        item.label = labelEdit->text().trimmed();
    if (item.removable) item.hotkey = actionBox->currentData().toInt();
    if (item.kind == PhoneLayoutItem::Directional)
        item.appearance = directionalStyle->currentData().toString();
    const qreal x = std::clamp(xValue->value() / 100.0, 0.0, .975);
    const qreal y = std::clamp(yValue->value() / 100.0, 0.0, .96);
    QRectF rect(x, y,
                std::clamp(widthValue->value() / 100.0, .025, 1.0 - x),
                std::clamp(heightValue->value() / 100.0, .04, 1.0 - y));
    const qreal lockedRatio = lockedNormalizedRatio(item.kind);
    if (lockedRatio > 0)
    {
        const qreal width = std::min(rect.width(), (1.0 - y) * lockedRatio);
        rect.setSize(QSizeF(width, width / lockedRatio));
    }
    item.rect = rect;
    canvas->update();
    updateInspector();
}

void PhoneLayoutDialog::addAction(const QPointF& position)
{
    if (layout.items.size() >= 32) return;
    rememberForUndo();
    PhoneLayoutItem item;
    item.id = QString("custom-%1").arg(QDateTime::currentMSecsSinceEpoch());
    item.label = actionBox->currentText();
    item.kind = PhoneLayoutItem::Button;
    item.hotkey = actionBox->currentData().toInt();
    item.removable = true;
    item.rect = QRectF(position.x() >= 0 ? std::clamp(position.x() - .055, 0.0, .89) : .445,
                       position.y() >= 0 ? std::clamp(position.y() - .055, 0.0, .89) : .78,
                       .11, .11);
    layout.items.append(item);
    canvas->setSelection(layout.items.size() - 1);
}

void PhoneLayoutDialog::applyLayout()
{
    PhoneBridgeSettings settings = manager ? manager->settings() : PhoneBridgeManager::loadSettings();
    settings.layoutJson = layout.toJson();
    PhoneBridgeManager::saveSettings(settings);
    if (manager) manager->setSettings(settings);
}

void PhoneLayoutDialog::rememberForUndo()
{
    const QString snapshot = layout.toJson();
    if (undoHistory.isEmpty() || undoHistory.constLast() != snapshot)
        undoHistory.append(snapshot);
    while (undoHistory.size() > 60) undoHistory.removeFirst();
    redoHistory.clear();
}

void PhoneLayoutDialog::restoreLayout(const QString& snapshot)
{
    layout = PhoneControllerLayout::fromJson(snapshot);
    const int selection = std::min(canvas->selection(), int(layout.items.size()) - 1);
    canvas->setSelection(selection);
    canvas->update();
}

void PhoneLayoutDialog::undo()
{
    const QString current = layout.toJson();
    while (!undoHistory.isEmpty() && undoHistory.constLast() == current)
        undoHistory.removeLast();
    if (undoHistory.isEmpty()) return;
    redoHistory.append(current);
    restoreLayout(undoHistory.takeLast());
}

void PhoneLayoutDialog::redo()
{
    if (redoHistory.isEmpty()) return;
    const QString current = layout.toJson();
    if (undoHistory.isEmpty() || undoHistory.constLast() != current)
        undoHistory.append(current);
    restoreLayout(redoHistory.takeLast());
}
