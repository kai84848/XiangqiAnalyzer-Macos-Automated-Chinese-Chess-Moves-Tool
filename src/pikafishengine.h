#pragma once

#include "position.h"

#include <QElapsedTimer>
#include <QMap>
#include <QObject>
#include <QProcess>
#include <QVector>

struct EngineLine {
    int multiPv = 1;
    int depth = 0;
    int selectiveDepth = 0;
    int scoreCp = 0;
    int mate = 0;
    qint64 nodes = 0;
    qint64 nps = 0;
    int timeMs = 0;
    QStringList pv;
    Move firstMove;
    QString notation;
};

class PikafishEngine : public QObject {
    Q_OBJECT
public:
    explicit PikafishEngine(QObject *parent = nullptr);
    ~PikafishEngine() override;

    void setEnginePath(const QString &path);
    QString enginePath() const { return m_enginePath; }
    void setMoveTime(int milliseconds) { m_moveTimeMs = qBound(200, milliseconds, 60000); }
    void setMultiPv(int count) { m_multiPv = qBound(1, count, 8); if (m_ready) send(QStringLiteral("setoption name MultiPV value %1").arg(m_multiPv)); }
    void setThreads(int threads) { m_threads = qBound(1, threads, 16); if (m_ready) send(QStringLiteral("setoption name Threads value %1").arg(m_threads)); }
    void setHash(int megabytes) { m_hashMb = qBound(16, megabytes, 1024); if (m_ready) send(QStringLiteral("setoption name Hash value %1").arg(m_hashMb)); }

    void analyze(const Position &position, bool playBestMove = false);
    void stop();
    void shutdown();
    bool isThinking() const { return m_thinking || m_stopPending; }
    bool isReady() const { return m_ready; }

    static QString defaultEnginePath();
    static QString engineDisplayName();

signals:
    void statusChanged(const QString &status, bool thinking);
    void analysisUpdated(const QVector<EngineLine> &lines, bool redPerspective);
    void analysisFinished(const Move &bestMove, const QString &bestMoveText, bool playMove);
    void errorOccurred(const QString &message);

private:
    void launch();
    void beginSearch();
    void send(const QString &command);
    void parseLine(const QString &line);
    void handleProcessError(QProcess::ProcessError error);
    void handleFinished(int exitCode, QProcess::ExitStatus status);
    void clearLines();

    QProcess *m_process = nullptr;
    QString m_enginePath;
    QString m_readBuffer;
    QStringList m_diagnosticLines;
    Position m_requestedPosition;
    Position m_searchPosition;
    bool m_requestedPlayMove = false;
    bool m_searchPlayMove = false;
    bool m_redPerspective = true;
    bool m_ready = false;
    bool m_protocolSent = false;
    bool m_thinking = false;
    bool m_stopPending = false;
    bool m_hasRequest = false;
    bool m_shuttingDown = false;
    bool m_restartAfterExit = false;
    int m_moveTimeMs = 3000;
    int m_multiPv = 3;
    int m_threads = 2;
    int m_hashMb = 128;
    QMap<int, EngineLine> m_lines;
    QElapsedTimer m_searchTimer;
};
