#include "variationpreviewdialog.h"

#include "boardwidget.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>

VariationPreviewDialog::VariationPreviewDialog(const Position &initial, const QStringList &pv,
                                               int currentPly, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("变化局面预览"));
    setAttribute(Qt::WA_DeleteOnClose);
    setModal(false);
    resize(540, 720);

    m_positions.append(initial);
    Position position = initial;
    for (const QString &uci : pv) {
        const Move move = Position::moveFromUci(uci);
        if (!position.isLegalMove(move))
            break;
        m_notations.append(position.moveNotation(move));
        m_moves.append(move);
        position.applyMove(move);
        m_positions.append(position);
    }

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 16, 18, 16);
    layout->setSpacing(10);
    m_titleLabel = new QLabel(this);
    m_titleLabel->setStyleSheet(QStringLiteral("font-size:17px; font-weight:600; color:#237f6d;"));
    layout->addWidget(m_titleLabel);

    m_board = new BoardWidget(this);
    m_board->setEnabled(false);
    m_board->setMinimumSize(430, 490);
    layout->addWidget(m_board, 1);

    m_moveLabel = new QLabel(this);
    m_moveLabel->setAlignment(Qt::AlignCenter);
    m_moveLabel->setStyleSheet(QStringLiteral("font-size:17px; font-weight:600;"));
    layout->addWidget(m_moveLabel);

    m_slider = new QSlider(Qt::Horizontal, this);
    m_slider->setRange(0, qMax(0, m_positions.size() - 1));
    m_slider->setTickPosition(QSlider::TicksBelow);
    m_slider->setTickInterval(1);
    m_slider->setToolTip(QStringLiteral("拖动查看变化中的任意一步"));
    layout->addWidget(m_slider);

    auto *navigation = new QHBoxLayout;
    m_previousButton = new QPushButton(QStringLiteral("‹ 上一步"), this);
    m_nextButton = new QPushButton(QStringLiteral("下一步 ›"), this);
    m_counterLabel = new QLabel(this);
    m_counterLabel->setAlignment(Qt::AlignCenter);
    m_counterLabel->setProperty("muted", true);
    navigation->addWidget(m_previousButton);
    navigation->addStretch();
    navigation->addWidget(m_counterLabel);
    navigation->addStretch();
    navigation->addWidget(m_nextButton);
    layout->addLayout(navigation);

    m_applyButton = new QPushButton(QStringLiteral("走到棋盘上"), this);
    m_applyButton->setObjectName(QStringLiteral("secondaryAccent"));
    m_applyButton->setToolTip(QStringLiteral("从当前主局面起，依次走出预览中当前步及之前的着法"));
    layout->addWidget(m_applyButton);

    connect(m_slider, &QSlider::valueChanged, this, &VariationPreviewDialog::showPly);
    connect(m_previousButton, &QPushButton::clicked, this, [this]() { m_slider->setValue(m_slider->value() - 1); });
    connect(m_nextButton, &QPushButton::clicked, this, [this]() { m_slider->setValue(m_slider->value() + 1); });
    connect(m_applyButton, &QPushButton::clicked, this, [this]() {
        emit applyRequested(m_moves.mid(0, m_slider->value()));
        accept();
    });

    showPly(qBound(0, currentPly, m_positions.size() - 1));
}

void VariationPreviewDialog::showPly(int ply)
{
    if (m_positions.isEmpty())
        return;
    ply = qBound(0, ply, m_positions.size() - 1);
    if (m_slider->value() != ply) {
        QSignalBlocker blocker(m_slider);
        m_slider->setValue(ply);
    }
    m_board->setPosition(m_positions[ply]);
    m_board->setLastMove(ply > 0 ? m_moves[ply - 1] : Move{});
    m_titleLabel->setText(ply == 0 ? QStringLiteral("变化前的局面")
                                   : QStringLiteral("第 %1 步 · %2").arg(ply).arg(m_positions[ply - 1].redTurn ? QStringLiteral("红方") : QStringLiteral("黑方")));
    m_moveLabel->setText(ply == 0 ? QStringLiteral("尚未走子") : m_notations[ply - 1]);
    m_moveLabel->setStyleSheet(QStringLiteral("font-size:17px; font-weight:600; color:%1;")
        .arg(ply > 0 && m_positions[ply - 1].redTurn ? QStringLiteral("#a33d3d") : QStringLiteral("#252a2d")));
    m_counterLabel->setText(QStringLiteral("%1 / %2").arg(ply).arg(m_positions.size() - 1));
    m_previousButton->setEnabled(ply > 0);
    m_nextButton->setEnabled(ply < m_positions.size() - 1);
    m_applyButton->setEnabled(ply > 0);
}
