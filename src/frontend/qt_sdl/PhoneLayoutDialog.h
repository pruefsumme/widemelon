// Mouse-first editor for the phone controller layout.
#pragma once

#include <QDialog>
#include <QStringList>

#include "PhoneLayout.h"

class QComboBox;
class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class PhoneBridgeManager;
class PhoneLayoutCanvas;

class PhoneLayoutDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit PhoneLayoutDialog(PhoneBridgeManager* manager, QWidget* parent = nullptr);

private:
    void selectItem(int index);
    void updateInspector();
    void inspectorChanged();
    void addAction(const QPointF& position = QPointF(-1, -1));
    void applyLayout();
    void rememberForUndo();
    void undo();
    void redo();
    void restoreLayout(const QString& snapshot);

    PhoneBridgeManager* manager;
    PhoneControllerLayout layout;
    PhoneLayoutCanvas* canvas;
    QLabel* selectedName;
    QLineEdit* labelEdit;
    QComboBox* actionBox;
    QComboBox* directionalStyle;
    QCheckBox* showHud;
    QDoubleSpinBox* xValue;
    QDoubleSpinBox* yValue;
    QDoubleSpinBox* widthValue;
    QDoubleSpinBox* heightValue;
    QPushButton* removeButton;
    QStringList undoHistory;
    QStringList redoHistory;
    bool updatingInspector = false;
};
