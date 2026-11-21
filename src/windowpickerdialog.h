#pragma once

#include "macautomation.h"

#include <QDialog>

class QButtonGroup;
class QLabel;
class QDialogButtonBox;
class QScrollArea;

class WindowPickerDialog : public QDialog {
    Q_OBJECT
public:
    explicit WindowPickerDialog(QWidget *parent = nullptr);

    MacWindowInfo selectedWindow() const { return m_selectedWindow; }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void reloadWindows();
    void selectWindow(int index);

    QScrollArea *m_scrollArea = nullptr;
    QLabel *m_summary = nullptr;
    QDialogButtonBox *m_buttons = nullptr;
    QButtonGroup *m_group = nullptr;
    QVector<MacWindowInfo> m_windows;
    MacWindowInfo m_selectedWindow;
};
