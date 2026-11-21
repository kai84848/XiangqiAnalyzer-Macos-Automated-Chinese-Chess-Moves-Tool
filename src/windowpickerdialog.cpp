#include "windowpickerdialog.h"

#include <QApplication>
#include <QButtonGroup>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFontMetrics>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

WindowPickerDialog::WindowPickerDialog(QWidget *parent)
    : QDialog(parent)
    , m_group(new QButtonGroup(this))
{
    setObjectName(QStringLiteral("windowPickerDialog"));
    setWindowTitle(QStringLiteral("选择对弈窗口"));
    resize(1040, 720);
    setMinimumSize(760, 520);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(22, 20, 22, 20);
    layout->setSpacing(14);
    auto *header = new QHBoxLayout;
    auto *titles = new QVBoxLayout;
    auto *title = new QLabel(QStringLiteral("选择要自动对弈的窗口"), this);
    title->setStyleSheet(QStringLiteral("font-size:20px;font-weight:600;"));
    auto *hint = new QLabel(QStringLiteral(
        "支持天天象棋、JJ象棋以及其他象棋应用；只会读取和点击所选窗口。"), this);
    hint->setProperty("muted", true);
    hint->setWordWrap(true);
    titles->addWidget(title);
    titles->addWidget(hint);
    header->addLayout(titles, 1);
    auto *refresh = new QPushButton(QStringLiteral("刷新"), this);
    refresh->setObjectName(QStringLiteral("refreshWindowsButton"));
    connect(refresh, &QPushButton::clicked, this, &WindowPickerDialog::reloadWindows);
    header->addWidget(refresh, 0, Qt::AlignTop);
    layout->addLayout(header);

    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    layout->addWidget(m_scrollArea, 1);

    m_summary = new QLabel(QStringLiteral("正在读取可共享的窗口…"), this);
    m_summary->setProperty("muted", true);
    layout->addWidget(m_summary);
    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("使用此窗口"));
    m_buttons->button(QDialogButtonBox::Ok)->setObjectName(QStringLiteral("windowPickerOk"));
    m_buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    m_buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
    connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(m_buttons);
    connect(m_group, &QButtonGroup::idClicked, this, &WindowPickerDialog::selectWindow);
    setStyleSheet(QStringLiteral(
        "QToolButton[windowCard=true] { background:#ffffff; border:2px solid #dce3df; "
        "border-radius:10px; padding:8px; color:#27332e; }"
        "QToolButton[windowCard=true]:hover { border-color:#69a899; background:#f5faf8; }"
        "QToolButton[windowCard=true]:checked { border-color:#278b78; background:#eaf5f1; }"));

    QTimer::singleShot(0, this, &WindowPickerDialog::reloadWindows);
}

void WindowPickerDialog::reloadWindows()
{
    m_summary->setText(QStringLiteral("正在读取窗口缩略图…"));
    m_buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString error;
    m_windows = MacAutomation::availableWindows(&error);
    QApplication::restoreOverrideCursor();

    if (QWidget *previous = m_scrollArea->takeWidget())
        previous->deleteLater();
    auto *container = new QWidget;
    container->setObjectName(QStringLiteral("windowPickerGrid"));
    auto *grid = new QGridLayout(container);
    grid->setContentsMargins(2, 2, 12, 12);
    grid->setHorizontalSpacing(14);
    grid->setVerticalSpacing(14);
    constexpr int columns = 3;
    int preferredIndex = -1;
    for (int index = 0; index < m_windows.size(); ++index) {
        const MacWindowInfo &window = m_windows[index];
        auto *card = new QToolButton(container);
        card->setProperty("windowCard", true);
        card->setProperty("windowIndex", index);
        card->setCheckable(true);
        card->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        card->setIconSize(QSize(280, 170));
        card->setFixedSize(306, 224);
        card->setText(QFontMetrics(card->font()).elidedText(
            window.displayName(), Qt::ElideMiddle, 278));
        card->setToolTip(window.displayName());
        if (!window.thumbnail.isNull()) {
            card->setIcon(QIcon(QPixmap::fromImage(window.thumbnail)));
        } else {
            QPixmap placeholder(280, 170);
            placeholder.fill(QColor(QStringLiteral("#e8eeeb")));
            QPainter painter(&placeholder);
            painter.setPen(QColor(QStringLiteral("#6d7b75")));
            painter.drawText(placeholder.rect(), Qt::AlignCenter, QStringLiteral("无法预览窗口"));
            card->setIcon(QIcon(placeholder));
        }
        card->installEventFilter(this);
        m_group->addButton(card, index);
        grid->addWidget(card, index / columns, index % columns);
        if (preferredIndex < 0 || window.title.contains(QStringLiteral("天天象棋")))
            preferredIndex = window.title.contains(QStringLiteral("天天象棋")) ? index : preferredIndex;
    }
    if (m_windows.isEmpty()) {
        auto *empty = new QLabel(error.isEmpty()
            ? QStringLiteral("没有找到可共享的窗口。请先打开象棋游戏窗口，再点击“刷新”。")
            : error, container);
        empty->setAlignment(Qt::AlignCenter);
        empty->setWordWrap(true);
        grid->addWidget(empty, 0, 0, 1, columns);
    } else {
        grid->setRowStretch(m_windows.size() / columns + 1, 1);
        if (preferredIndex < 0)
            preferredIndex = 0;
        if (QAbstractButton *button = m_group->button(preferredIndex))
            button->setChecked(true);
        selectWindow(preferredIndex);
    }
    m_scrollArea->setWidget(container);
    if (!m_windows.isEmpty())
        m_summary->setText(QStringLiteral("共 %1 个窗口 · 单击选择，双击可直接使用").arg(m_windows.size()));
    else
        m_summary->setText(QStringLiteral("请确认已授予棋析“屏幕录制与系统录音”权限"));
}

void WindowPickerDialog::selectWindow(int index)
{
    if (index < 0 || index >= m_windows.size()) {
        m_selectedWindow = {};
        m_buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
        return;
    }
    m_selectedWindow = m_windows[index];
    m_buttons->button(QDialogButtonBox::Ok)->setEnabled(true);
    m_summary->setText(QStringLiteral("已选择：%1").arg(m_selectedWindow.displayName()));
}

bool WindowPickerDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonDblClick) {
        const int index = watched->property("windowIndex").toInt();
        if (index >= 0 && index < m_windows.size()) {
            selectWindow(index);
            accept();
            return true;
        }
    }
    return QDialog::eventFilter(watched, event);
}
