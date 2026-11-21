#include "tiantiancontroller.h"

#include "boardrecognizer.h"
#include "macautomation.h"

#include <QImage>
#include <QSettings>
#include <QStringList>

namespace {
constexpr qint64 opponentMoveTimeoutMs = 3 * 60 * 1000;
}

TianTianController::TianTianController(QObject *parent)
    : QObject(parent)
    , m_recognizer(new BoardRecognizer(this))
    , m_engine(new PikafishEngine(this))
{
    m_timer.setInterval(700);
    connect(&m_timer, &QTimer::timeout, this, &TianTianController::tick);
    connect(m_recognizer, &BoardRecognizer::boardLocated, this,
            [this](const QPolygonF &corners) {
        if (m_running)
            m_boardCorners = corners;
    });
    connect(m_recognizer, &BoardRecognizer::recognitionFinished, this,
            &TianTianController::recognitionFinished);
    connect(m_recognizer, &BoardRecognizer::recognitionFailed, this,
            &TianTianController::recognitionFailed);
    connect(m_engine, &PikafishEngine::analysisFinished, this,
            [this](const Move &move, const QString &notation, bool) {
        analysisFinished(move, notation);
    });
    connect(m_engine, &PikafishEngine::errorOccurred, this, [this](const QString &message) {
        if (m_running)
            fail(message);
    });
}

void TianTianController::start(const TianTianOptions &options)
{
    if (m_running)
        stop();
    if (!options.window.id) {
        fail(QStringLiteral("请先选择要自动对弈的窗口"));
        return;
    }
    if (!MacAutomation::hasScreenCaptureAccess() && !MacAutomation::requestScreenCaptureAccess()) {
        fail(QStringLiteral("请在 macOS“屏幕录制与系统录音”中允许棋析，重新打开应用后再启动自动对弈。"));
        return;
    }
    if (!MacAutomation::hasAccessibilityAccess() && !MacAutomation::requestAccessibilityAccess()) {
        fail(QStringLiteral("请在 macOS“辅助功能”中允许棋析，然后再次点击“自动对弈”。"));
        return;
    }
    if (!m_tempDirectory.isValid()) {
        fail(QStringLiteral("无法创建自动对弈临时目录"));
        return;
    }

    m_options = options;
    m_options.moveTimeMs = qBound(200, m_options.moveTimeMs, 60000);
    m_expected = Position::initial();
    m_afterOwnMove = {};
    m_lastError.clear();
    m_busy = false;
    m_initialized = false;
    m_activationNeeded = true;
    m_captureDelayMs = 280;
    m_recognitionAttempts = 0;
    m_unstableFrames = 0;
    m_illegalStableFrames = 0;
    m_stability.reset();
    m_moveNumber = 0;
    m_boardCorners.clear();
    m_captureSize = {};

    QSettings settings;
    m_engine->setEnginePath(settings.value(QStringLiteral("engine/path"),
        PikafishEngine::defaultEnginePath()).toString());
    m_engine->setMoveTime(m_options.moveTimeMs);
    m_engine->setMultiPv(1);
    m_engine->setThreads(settings.value(QStringLiteral("engine/threads"), 2).toInt());
    m_engine->setHash(settings.value(QStringLiteral("engine/hash"), 128).toInt());

    m_running = true;
    m_lastProgress.start();
    emit runningChanged(true);
    emit statusChanged(QStringLiteral("自动对弈已启动，正在读取所选窗口中的棋盘…"));
    m_timer.start();
    tick();
}

void TianTianController::stop()
{
    const bool wasRunning = m_running;
    m_running = false;
    m_busy = false;
    m_timer.stop();
    m_recognizer->cancel();
    m_engine->stop();
    if (wasRunning) {
        emit runningChanged(false);
        emit statusChanged(QStringLiteral("自动对弈已停止"));
    }
}

void TianTianController::fail(const QString &message)
{
    stop();
    emit errorOccurred(message);
}

void TianTianController::finishGame(bool redWinner)
{
    if (!m_running)
        return;
    m_busy = false;
    stop();
    const QString result = redWinner ? QStringLiteral("红方胜") : QStringLiteral("黑方胜");
    emit statusChanged(result);
    emit gameFinished(result);
}

void TianTianController::tick()
{
    if (!m_running || m_busy)
        return;
    if (m_lastProgress.elapsed() > opponentMoveTimeoutMs) {
        fail(QStringLiteral("等待棋局变化超过 3 分钟：%1").arg(
            m_lastError.isEmpty() ? QStringLiteral("棋盘尚未变化，对局可能已经结束") : m_lastError));
        return;
    }

    QString error;
    if (m_activationNeeded) {
        if (!MacAutomation::activateWindow(m_options.window, &error)) {
            fail(error);
            return;
        }
        m_activationNeeded = false;
    }
    m_busy = true;
    emit statusChanged(QStringLiteral("正在读取所选窗口中的棋盘…"));
    QTimer::singleShot(m_captureDelayMs, this, [this]() {
        if (m_running)
            capture();
    });
}

void TianTianController::capture()
{
    if (!m_running)
        return;
    const QString path = m_tempDirectory.filePath(QStringLiteral("current.png"));
    QString error;
    if (!MacAutomation::captureWindow(m_options.window.id, path, &error)) {
        retryOrFail(error);
        return;
    }
    const QImage image(path);
    if (image.isNull()) {
        retryOrFail(QStringLiteral("无法读取自动对弈截图"));
        return;
    }
    m_captureSize = image.size();
    m_boardCorners.clear();
    m_recognizer->recognize(image, m_options.redBottom
        ? BoardOrientation::RedBottom : BoardOrientation::RedTop);
}

void TianTianController::retryOrFail(const QString &message)
{
    m_busy = false;
    m_lastError = message;
    m_activationNeeded = true;
    m_captureDelayMs = 80;
    m_stability.reset();
    m_unstableFrames = 0;
    m_illegalStableFrames = 0;
    ++m_recognitionAttempts;
    const int maximumAttempts = m_initialized ? 6 : 4;
    if (m_recognitionAttempts >= maximumAttempts) {
        fail(QStringLiteral("连续快速重拍仍无法确认棋盘：%1").arg(message));
        return;
    }
    emit statusChanged(QStringLiteral("画面未确认，正在快速重拍（%1/%2）…")
        .arg(m_recognitionAttempts).arg(maximumAttempts));
    m_timer.start(180);
}

void TianTianController::recognitionFinished(const Position &position, const QString &details,
                                              const QVector<double> &confidences,
                                              const QVector<int> &colorCorrectedSquares)
{
    Q_UNUSED(details)
    Q_UNUSED(colorCorrectedSquares)
    if (!m_running)
        return;
    m_busy = false;
    m_lastError.clear();

    QStringList uncertainPieces;
    for (int square = 0; square < 90 && square < confidences.size(); ++square) {
        const char piece = position.piece(square);
        if (confidences[square] >= 0.65)
            continue;
        const QString location = QStringLiteral("第%1行第%2列")
            .arg(Position::rowOf(square) + 1)
            .arg(Position::colOf(square) + 1);
        uncertainPieces.append(piece == Piece::Empty
            ? QStringLiteral("%1是否有棋子不确定（%2%）")
                .arg(location).arg(confidences[square] * 100.0, 0, 'f', 1)
            : QStringLiteral("%1识别为%2%3（%4%）")
                .arg(location, Piece::isRed(piece) ? QStringLiteral("红") : QStringLiteral("黑"),
                     Piece::displayName(piece))
                .arg(confidences[square] * 100.0, 0, 'f', 1));
    }
    if (confidences.size() < 90)
        uncertainPieces.append(QStringLiteral("识别结果缺少部分棋盘格"));
    if (!uncertainPieces.isEmpty()) {
        QString diagnostic = uncertainPieces.mid(0, 3).join(QStringLiteral("；"));
        if (uncertainPieces.size() > 3)
            diagnostic += QStringLiteral("等 %1 格").arg(uncertainPieces.size());
        retryOrFail(QStringLiteral("棋子识别置信度不足：%1。请保持棋盘完整、清晰且不被遮挡")
            .arg(diagnostic));
        return;
    }

    // Never initialize or accept a move from one frame. This rejects animation
    // frames and isolated detector errors before legal-move validation runs.
    if (!m_stability.observe(position)) {
        ++m_unstableFrames;
        m_illegalStableFrames = 0;
        m_captureDelayMs = 80;
        m_timer.start(180);
        if (m_unstableFrames >= 10) {
            fail(QStringLiteral("快速重拍后画面仍无法连续两帧一致，请暂停走子动画并保持棋盘无遮挡。"));
            return;
        }
        emit statusChanged(QStringLiteral("正在快速确认下一帧（%1/10）…").arg(m_unstableFrames));
        return;
    }
    m_recognitionAttempts = 0;
    m_unstableFrames = 0;
    m_captureDelayMs = 280;
    m_timer.start(700);

    if (!m_initialized) {
        if (m_options.fromCurrent) {
            m_expected = position;
            m_expected.redTurn = m_options.red;
        } else {
            if (position.board != Position::initial().board) {
                fail(QStringLiteral("连续两帧确认当前不是初始局面。请新开一局，或选择“从当前局面继续”。"));
                return;
            }
            m_expected = Position::initial();
        }
        m_initialized = true;
        m_illegalStableFrames = 0;
        m_lastProgress.restart();
        emit positionChanged(m_expected);
        emit statusChanged(m_options.fromCurrent
            ? QStringLiteral("连续两帧已确认当前局面，假定现在轮到己方")
            : QStringLiteral("连续两帧已确认初始局面"));
        if (!m_expected.hasAnyLegalMove()) {
            finishGame(!m_expected.redTurn);
            return;
        }
        if (m_expected.redTurn == m_options.red)
            beginAnalysis();
        return;
    }

    if (position.board == m_expected.board) {
        m_illegalStableFrames = 0;
        if (!m_expected.hasAnyLegalMove()) {
            finishGame(!m_expected.redTurn);
            return;
        }
        if (m_expected.redTurn == m_options.red)
            beginAnalysis();
        else
            emit statusChanged(QStringLiteral("棋盘已稳定，正在等待对方走棋…"));
        return;
    }

    Move move;
    Position after;
    if (findSingleLegalTransition(m_expected, position, &move, &after)) {
        const QString description = QStringLiteral("对手：%1（%2）")
            .arg(m_expected.moveNotation(move), Position::moveToUci(move));
        m_expected = after;
        m_illegalStableFrames = 0;
        m_lastProgress.restart();
        m_lastError.clear();
        emit movePlayed(description);
        emit positionChanged(m_expected);
        emit statusChanged(description);
        if (!m_expected.hasAnyLegalMove()) {
            finishGame(!m_expected.redTurn);
            return;
        }
        if (m_expected.redTurn == m_options.red)
            beginAnalysis();
        return;
    }

    // The image remains unchanged: legality can reject it, but must never be
    // used to alter pieces to make a move fit. Fail promptly if it persists.
    ++m_illegalStableFrames;
    m_lastError = QStringLiteral("连续两帧一致，但棋盘变化不是当前局面的一步合法走法");
    m_activationNeeded = true;
    if (m_illegalStableFrames >= 4) {
        fail(QStringLiteral("棋盘变化持续不合法：请确认识别方向、行棋方和棋子是否被遮挡；应用未自动改动识别棋子。"));
        return;
    }
    emit statusChanged(QStringLiteral("稳定画面未通过合法走法校验，正在复核（%1/4）…")
        .arg(m_illegalStableFrames));
}

void TianTianController::recognitionFailed(const QString &message)
{
    if (m_running)
        retryOrFail(message);
}

void TianTianController::beginAnalysis()
{
    if (!m_running || m_busy)
        return;
    if (!m_expected.hasAnyLegalMove()) {
        finishGame(!m_expected.redTurn);
        return;
    }
    const QString error = m_expected.validateForAnalysis();
    if (!error.isEmpty()) {
        fail(QStringLiteral("自动对弈局面无法分析：%1").arg(error));
        return;
    }
    m_busy = true;
    emit statusChanged(QStringLiteral("轮到己方，正在计算最佳走法…"));
    m_engine->analyze(m_expected, false);
}

void TianTianController::analysisFinished(const Move &bestMove, const QString &notation)
{
    if (!m_running)
        return;
    if (!bestMove.isValid()) {
        finishGame(!m_expected.redTurn);
        return;
    }

    const QString uci = Position::moveToUci(bestMove);
    m_afterOwnMove = m_expected.afterMove(bestMove);
    const QPointF source = pointFor(uci.left(2));
    const QPointF target = pointFor(uci.mid(2, 2));
    if (source.isNull() || target.isNull()) {
        fail(QStringLiteral("无法根据棋盘四角计算点击位置，请重新选择窗口并保持棋盘完整可见"));
        return;
    }
    QString error;
    m_activationNeeded = false;
    if (!MacAutomation::activateWindow(m_options.window, &error)) {
        fail(error);
        return;
    }
    MacAutomation::click(source);
    QTimer::singleShot(180, this, [this, target, notation, uci]() {
        if (!m_running)
            return;
        MacAutomation::click(target);
        m_expected = m_afterOwnMove;
        m_stability.reset();
        m_illegalStableFrames = 0;
        m_recognitionAttempts = 0;
        m_lastProgress.restart();
        m_lastError.clear();
        ++m_moveNumber;
        m_busy = false;
        const QString description = QStringLiteral("第 %1 手：%2（%3）")
            .arg(m_moveNumber).arg(notation, uci);
        emit movePlayed(description);
        emit positionChanged(m_expected);
        emit statusChanged(description);
        if (!m_expected.hasAnyLegalMove())
            finishGame(!m_expected.redTurn);
    });
}

QPointF TianTianController::pointFor(const QString &uciSquare) const
{
    if (m_captureSize.width() <= 0 || m_captureSize.height() <= 0)
        return {};
    const QPointF imagePoint = boardImagePointForUci(
        uciSquare, m_boardCorners, m_options.redBottom);

    QRectF bounds = MacAutomation::windowBounds(m_options.window.id);
    if (bounds.isEmpty())
        bounds = m_options.window.bounds;
    if (bounds.isEmpty())
        return {};
    return bounds.topLeft() + QPointF(imagePoint.x() * bounds.width() / m_captureSize.width(),
                                      imagePoint.y() * bounds.height() / m_captureSize.height());
}
