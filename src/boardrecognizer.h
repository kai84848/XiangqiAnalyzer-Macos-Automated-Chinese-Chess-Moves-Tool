#pragma once

#include "position.h"

#include <QImage>
#include <QObject>
#include <QPolygonF>
#include <QVector>

enum class BoardOrientation {
    Auto,
    RedBottom,
    RedTop
};

class BoardRecognizer : public QObject {
    Q_OBJECT
public:
    explicit BoardRecognizer(QObject *parent = nullptr);
    ~BoardRecognizer() override;

    void recognize(const QImage &image, BoardOrientation orientation = BoardOrientation::Auto);
    void cancel();
    bool isBusy() const;

    static QString modelDescription();

signals:
    void boardLocated(const QPolygonF &corners);
    void recognitionFinished(const Position &position, const QString &details,
                             const QVector<double> &confidences,
                             const QVector<int> &colorCorrectedSquares);
    void recognitionFailed(const QString &message);

private:
    struct Task;
    Task *m_task = nullptr;
};
