#include "evaluationbar.h"

#include <QPainter>
#include <QPainterPath>
#include <QtMath>

EvaluationBar::EvaluationBar(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(10);
    setMaximumHeight(14);
}

void EvaluationBar::setEvaluation(int redCp, int redMate)
{
    m_redCp = redCp;
    m_redMate = redMate;
    update();
}

void EvaluationBar::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF bar = rect().adjusted(0, 1, 0, -1);
    double redShare = 0.5 + 0.5 * std::tanh(m_redCp / 650.0);
    redShare = qBound(0.06, redShare, 0.94);
    if (m_redMate > 0)
        redShare = 1.0;
    else if (m_redMate < 0)
        redShare = 0.0;

    QPainterPath clip;
    clip.addRoundedRect(bar, bar.height() / 2, bar.height() / 2);
    painter.setClipPath(clip);
    painter.fillRect(bar, QColor(QStringLiteral("#343b38")));
    painter.fillRect(QRectF(bar.left(), bar.top(), bar.width() * redShare, bar.height()),
                     QColor(QStringLiteral("#b83c35")));
    painter.setPen(QPen(QColor(255, 255, 255, 210), 2));
    const double center = bar.center().x();
    painter.drawLine(QPointF(center, bar.top()), QPointF(center, bar.bottom()));
}
