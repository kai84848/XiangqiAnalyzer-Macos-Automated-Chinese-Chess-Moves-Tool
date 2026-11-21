#pragma once

#include "position.h"

#include <QDialog>
#include <QVector>

class BoardWidget;
class QLabel;
class QPushButton;
class QSlider;

class VariationPreviewDialog : public QDialog {
    Q_OBJECT
public:
    VariationPreviewDialog(const Position &initial, const QStringList &pv,
                           int currentPly, QWidget *parent = nullptr);

signals:
    void applyRequested(const QVector<Move> &moves);

private:
    void showPly(int ply);

    QVector<Position> m_positions;
    QVector<Move> m_moves;
    QStringList m_notations;
    BoardWidget *m_board = nullptr;
    QLabel *m_titleLabel = nullptr;
    QLabel *m_moveLabel = nullptr;
    QLabel *m_counterLabel = nullptr;
    QPushButton *m_previousButton = nullptr;
    QPushButton *m_nextButton = nullptr;
    QPushButton *m_applyButton = nullptr;
    QSlider *m_slider = nullptr;
};
