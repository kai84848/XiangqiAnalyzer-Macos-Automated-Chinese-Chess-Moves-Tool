#pragma once

#include "autoplaylogic.h"
#include "macautomation.h"
#include "pikafishengine.h"
#include "position.h"

#include <QElapsedTimer>
#include <QObject>
#include <QPointF>
#include <QPolygonF>
#include <QSize>
#include <QTemporaryDir>
#include <QTimer>

class BoardRecognizer;

struct TianTianOptions {
    bool red = true;
    bool fromCurrent = true;
    bool redBottom = true;
    int moveTimeMs = 1000;
    MacWindowInfo window;
};

class TianTianController : public QObject {
    Q_OBJECT
public:
    explicit TianTianController(QObject *parent = nullptr);

    bool isRunning() const { return m_running; }
    void start(const TianTianOptions &options);
    void stop();

signals:
    void runningChanged(bool running);
    void statusChanged(const QString &status);
    void movePlayed(const QString &description);
    void positionChanged(const Position &position);
    void gameFinished(const QString &result);
    void errorOccurred(const QString &message);

private:
    void tick();
    void capture();
    void recognitionFinished(const Position &position, const QString &details,
                             const QVector<double> &confidences,
                             const QVector<int> &colorCorrectedSquares);
    void recognitionFailed(const QString &message);
    void analysisFinished(const Move &bestMove, const QString &notation);
    void beginAnalysis();
    void finishGame(bool redWinner);
    void fail(const QString &message);
    QPointF pointFor(const QString &uciSquare) const;
    void retryOrFail(const QString &message);

    QTimer m_timer;
    BoardRecognizer *m_recognizer = nullptr;
    PikafishEngine *m_engine = nullptr;
    QTemporaryDir m_tempDirectory;
    TianTianOptions m_options;
    Position m_expected;
    Position m_afterOwnMove;
    QPolygonF m_boardCorners;
    QSize m_captureSize;
    QElapsedTimer m_lastProgress;
    BoardObservationStability m_stability;
    QString m_lastError;
    bool m_running = false;
    bool m_busy = false;
    bool m_initialized = false;
    bool m_activationNeeded = true;
    int m_captureDelayMs = 280;
    int m_recognitionAttempts = 0;
    int m_unstableFrames = 0;
    int m_illegalStableFrames = 0;
    int m_moveNumber = 0;
};
