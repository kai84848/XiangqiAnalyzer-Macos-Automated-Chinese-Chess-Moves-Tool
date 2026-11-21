#include "pikafishengine.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

#ifndef DEFAULT_PIKAFISH_PATH
#define DEFAULT_PIKAFISH_PATH "Pikafish"
#endif

PikafishEngine::PikafishEngine(QObject *parent)
    : QObject(parent)
    , m_enginePath(defaultEnginePath())
{
}

PikafishEngine::~PikafishEngine()
{
    shutdown();
}

QString PikafishEngine::defaultEnginePath()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        appDir + QStringLiteral("/../Resources/engine/Pikafish-MacOS-universal"),
        appDir + QStringLiteral("/Pikafish"),
        appDir + QStringLiteral("/engine/Pikafish-MacOS-universal")
    };
    for (const QString &candidate : candidates)
        if (QFileInfo::exists(candidate))
            return QFileInfo(candidate).absoluteFilePath();
    const QString bundled = QStringLiteral(DEFAULT_PIKAFISH_PATH);
    if (QFileInfo::exists(bundled))
        return bundled;
    return bundled;
}

QString PikafishEngine::engineDisplayName()
{
    return QStringLiteral("本地离线分析");
}

void PikafishEngine::setEnginePath(const QString &path)
{
    if (path == m_enginePath)
        return;
    shutdown();
    m_enginePath = path;
    m_ready = false;
    m_hasRequest = false;
    m_process = nullptr;
}

void PikafishEngine::launch()
{
    if (m_process || m_shuttingDown)
        return;
    if (!QFileInfo::exists(m_enginePath) || !QFileInfo(m_enginePath).isFile()) {
        emit errorOccurred(QStringLiteral("找不到分析组件，请在“分析设置”中重新选择。"));
        return;
    }
    if (!QFileInfo(m_enginePath).isExecutable()) {
        emit errorOccurred(QStringLiteral("分析文件没有执行权限，请在“隐私与安全性”中允许运行，并检查文件权限。"));
        return;
    }

    clearLines();
    m_protocolSent = false;
    m_ready = false;
    m_readBuffer.clear();
    m_diagnosticLines.clear();
    m_process = new QProcess(this);
    m_process->setProgram(m_enginePath);
    m_process->setWorkingDirectory(QFileInfo(m_enginePath).absolutePath());
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    connect(m_process, &QProcess::started, this, [this]() {
        m_protocolSent = true;
        send(QStringLiteral("uci"));
    });
    connect(m_process, &QProcess::readyReadStandardOutput, this, [this]() {
        if (!m_process)
            return;
        m_readBuffer += QString::fromUtf8(m_process->readAllStandardOutput());
        int newline = -1;
        while ((newline = m_readBuffer.indexOf(QLatin1Char('\n'))) >= 0) {
            const QString line = m_readBuffer.left(newline).trimmed();
            m_readBuffer.remove(0, newline + 1);
            if (!line.isEmpty())
                parseLine(line);
        }
    });
    connect(m_process, &QProcess::readyReadStandardError, this, [this]() {
        if (!m_process)
            return;
        const QString lines = QString::fromUtf8(m_process->readAllStandardError());
        for (const QString &line : lines.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
            m_diagnosticLines.append(line.trimmed());
            while (m_diagnosticLines.size() > 8)
                m_diagnosticLines.removeFirst();
        }
    });
    connect(m_process, &QProcess::errorOccurred, this, &PikafishEngine::handleProcessError);
    connect(m_process, &QProcess::finished, this, &PikafishEngine::handleFinished);
    emit statusChanged(QStringLiteral("正在准备分析…"), false);
    m_process->start();
}

void PikafishEngine::send(const QString &command)
{
    if (!m_process || m_process->state() != QProcess::Running)
        return;
    m_process->write(command.toUtf8());
    m_process->write("\n");
}

void PikafishEngine::analyze(const Position &position, bool playBestMove)
{
    m_requestedPosition = position;
    m_redPerspective = position.redTurn;
    m_requestedPlayMove = playBestMove;
    m_hasRequest = true;
    clearLines();
    emit analysisUpdated({}, m_redPerspective);

    if (!m_process) {
        launch();
        return;
    }
    if (m_thinking) {
        if (!m_stopPending)
            send(QStringLiteral("stop"));
        m_stopPending = true;
        emit statusChanged(QStringLiteral("正在切换局面…"), true);
        return;
    }
    if (m_ready)
        beginSearch();
}

void PikafishEngine::beginSearch()
{
    if (!m_process || !m_ready || !m_hasRequest || m_thinking || m_stopPending)
        return;
    m_searchPosition = m_requestedPosition;
    m_searchPlayMove = m_requestedPlayMove;
    m_hasRequest = false;
    m_thinking = true;
    m_searchTimer.restart();
    send(QStringLiteral("position fen %1").arg(m_searchPosition.toFen()));
    send(QStringLiteral("go movetime %1").arg(m_moveTimeMs));
    emit statusChanged(m_searchPlayMove ? QStringLiteral("正在选择着法…") : QStringLiteral("正在分析…"), true);
}

void PikafishEngine::stop()
{
    m_hasRequest = false;
    if (m_thinking) {
        m_stopPending = true;
        send(QStringLiteral("stop"));
        emit statusChanged(QStringLiteral("正在停止…"), false);
    } else {
        emit statusChanged(m_ready ? QStringLiteral("分析就绪") : QStringLiteral("分析未运行"), false);
    }
}

void PikafishEngine::shutdown()
{
    m_shuttingDown = true;
    m_hasRequest = false;
    if (m_process) {
        QProcess *process = m_process;
        m_process = nullptr;
        disconnect(process, nullptr, this, nullptr);
        if (process->state() == QProcess::Running) {
            if (m_thinking)
                process->write("stop\n");
            process->write("quit\n");
            if (!process->waitForFinished(1200)) {
                process->kill();
                process->waitForFinished(300);
            }
        }
        delete process;
    }
    m_thinking = false;
    m_stopPending = false;
    m_ready = false;
    m_shuttingDown = false;
}

void PikafishEngine::clearLines()
{
    m_lines.clear();
}

void PikafishEngine::parseLine(const QString &line)
{
    if (line.startsWith(QStringLiteral("info string ERROR"))) {
        m_diagnosticLines.append(line);
        while (m_diagnosticLines.size() > 8)
            m_diagnosticLines.removeFirst();
    }
    if (line == QStringLiteral("uciok")) {
        m_ready = true;
        const QString evalFile = QFileInfo(m_enginePath).absoluteDir()
            .filePath(QStringLiteral("pikafish.nnue"));
        if (QFileInfo(evalFile).isFile()) {
            // Do not depend on the parent application's working directory or on
            // Pikafish's fallback search paths when loading the bundled network.
            send(QStringLiteral("setoption name EvalFile value %1")
                .arg(QDir::cleanPath(QFileInfo(evalFile).absoluteFilePath())));
        }
        send(QStringLiteral("setoption name Threads value %1").arg(m_threads));
        send(QStringLiteral("setoption name Hash value %1").arg(m_hashMb));
        send(QStringLiteral("setoption name MultiPV value %1").arg(m_multiPv));
        send(QStringLiteral("setoption name Repetition Rule value ChineseRule"));
        // Match the original Pikafish interface: keep its default Elo score
        // model and display the reported integer directly, without /100.
        send(QStringLiteral("setoption name ScoreType value Elo"));
        send(QStringLiteral("isready"));
        return;
    }
    if (line == QStringLiteral("readyok")) {
        if (!m_thinking)
            emit statusChanged(QStringLiteral("分析已就绪"), false);
        if (m_hasRequest)
            beginSearch();
        return;
    }
    if (line.startsWith(QStringLiteral("info ")) && m_thinking && !m_stopPending) {
        const QStringList tokens = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        EngineLine parsed;
        bool hasScore = false;
        for (int i = 1; i < tokens.size(); ++i) {
            const QString &key = tokens[i];
            if (key == QStringLiteral("depth") && i + 1 < tokens.size())
                parsed.depth = tokens[++i].toInt();
            else if (key == QStringLiteral("seldepth") && i + 1 < tokens.size())
                parsed.selectiveDepth = tokens[++i].toInt();
            else if (key == QStringLiteral("multipv") && i + 1 < tokens.size())
                parsed.multiPv = tokens[++i].toInt();
            else if (key == QStringLiteral("nodes") && i + 1 < tokens.size())
                parsed.nodes = tokens[++i].toLongLong();
            else if (key == QStringLiteral("nps") && i + 1 < tokens.size())
                parsed.nps = tokens[++i].toLongLong();
            else if (key == QStringLiteral("time") && i + 1 < tokens.size())
                parsed.timeMs = tokens[++i].toInt();
            else if (key == QStringLiteral("score") && i + 2 < tokens.size()) {
                const QString type = tokens[++i];
                const int value = tokens[++i].toInt();
                if (type == QStringLiteral("cp")) {
                    parsed.scoreCp = value;
                    parsed.mate = 0;
                    hasScore = true;
                } else if (type == QStringLiteral("mate")) {
                    parsed.mate = value;
                    parsed.scoreCp = 0;
                    hasScore = true;
                }
            } else if (key == QStringLiteral("pv")) {
                parsed.pv = tokens.mid(i + 1);
                break;
            }
        }
        if (!hasScore || parsed.pv.isEmpty())
            return;
        parsed.firstMove = Position::moveFromUci(parsed.pv.first());
        parsed.notation = m_searchPosition.moveNotation(parsed.firstMove);
        m_lines.insert(parsed.multiPv, parsed);
        emit analysisUpdated(m_lines.values().toVector(), m_redPerspective);
        return;
    }
    if (line.startsWith(QStringLiteral("bestmove"))) {
        const QStringList tokens = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        const Move best = tokens.size() >= 2 ? Position::moveFromUci(tokens[1]) : Move{};
        const bool wasStopping = m_stopPending;
        m_thinking = false;
        m_stopPending = false;
        if (wasStopping) {
            if (m_hasRequest)
                beginSearch();
            else
                emit statusChanged(QStringLiteral("已停止 · 分析就绪"), false);
            return;
        }
        const QString notation = best.isValid() ? m_searchPosition.moveNotation(best) : QStringLiteral("无合法着法");
        emit statusChanged(QStringLiteral("分析完成 · 深度 %1").arg(m_lines.value(1).depth), false);
        emit analysisFinished(best, notation, m_searchPlayMove);
        return;
    }
    if (line.startsWith(QStringLiteral("id name ")))
        emit statusChanged(QStringLiteral("本地分析已连接"), false);
}

void PikafishEngine::handleProcessError(QProcess::ProcessError error)
{
    if (m_shuttingDown)
        return;
    if (error == QProcess::FailedToStart) {
        emit errorOccurred(QStringLiteral("分析组件启动失败。请确认 macOS 已允许该程序运行，并在分析设置中重新选择文件。"));
        emit statusChanged(QStringLiteral("分析启动失败"), false);
    } else if (error == QProcess::Crashed) {
        emit errorOccurred(QStringLiteral("分析组件异常退出。若这是首次运行，请在 macOS“隐私与安全性”中允许该程序，然后重试。"));
        emit statusChanged(QStringLiteral("分析已退出"), false);
    }
}

void PikafishEngine::handleFinished(int exitCode, QProcess::ExitStatus)
{
    if (!m_process)
        return;
    m_process->deleteLater();
    m_process = nullptr;
    m_ready = false;
    m_thinking = false;
    m_stopPending = false;
    if (!m_shuttingDown && exitCode != 0) {
        QString message = QStringLiteral("分析组件已退出，代码 %1。").arg(exitCode);
        if (!m_diagnosticLines.isEmpty()) {
            message += QStringLiteral("\n\n引擎诊断：\n%1")
                .arg(m_diagnosticLines.join(QLatin1Char('\n')));
        } else {
            message += QStringLiteral("\n请检查内置引擎、NNUE 权重文件及 macOS 的“隐私与安全性”设置。");
        }
        emit errorOccurred(message);
        emit statusChanged(QStringLiteral("分析未运行"), false);
    }
}
