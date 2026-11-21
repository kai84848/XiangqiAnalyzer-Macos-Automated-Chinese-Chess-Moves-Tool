#include "pikafishengine.h"

#include <QEventLoop>
#include <QFile>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

#ifndef ENGINE_TEST_PATH
#define ENGINE_TEST_PATH "Pikafish"
#endif

class EngineIntegrationTest : public QObject {
    Q_OBJECT
private slots:
    void analyzesInitialPosition()
    {
        PikafishEngine engine;
        engine.setEnginePath(QStringLiteral(ENGINE_TEST_PATH));
        engine.setMoveTime(800);
        engine.setMultiPv(3);
        engine.setThreads(2);
        engine.setHash(64);

        QEventLoop loop;
        QTimer timeout;
        timeout.setSingleShot(true);
        QVector<EngineLine> latestLines;
        Move best;
        QString error;
        connect(&engine, &PikafishEngine::analysisUpdated, this, [&latestLines](const QVector<EngineLine> &lines, bool) {
            if (!lines.isEmpty())
                latestLines = lines;
        });
        connect(&engine, &PikafishEngine::analysisFinished, this, [&](const Move &move, const QString &, bool play) {
            best = move;
            QVERIFY(!play);
            loop.quit();
        });
        connect(&engine, &PikafishEngine::errorOccurred, this, [&](const QString &message) {
            error = message;
            loop.quit();
        });
        connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
        const Position position = Position::initial();
        engine.analyze(position, false);
        timeout.start(20000);
        loop.exec();
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY2(best.isValid(), "Pikafish did not return a best move before timeout");
        QVERIFY(position.isLegalMove(best));
        QCOMPARE(latestLines.size(), 3);
        for (const EngineLine &line : std::as_const(latestLines)) {
            QVERIFY(line.depth > 0);
            QVERIFY(position.isLegalMove(line.firstMove));
            QVERIFY(!line.notation.isEmpty());
            QVERIFY(!line.pv.isEmpty());
        }
        engine.shutdown();
    }

    void reportsMissingNetworkDiagnostics()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString enginePath = directory.filePath(QStringLiteral("Pikafish-MacOS-universal"));
        QVERIFY(QFile::copy(QStringLiteral(ENGINE_TEST_PATH), enginePath));
        QVERIFY(QFile::setPermissions(enginePath,
            QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));

        PikafishEngine engine;
        engine.setEnginePath(enginePath);
        engine.setMoveTime(200);
        QString error;
        connect(&engine, &PikafishEngine::errorOccurred, this,
                [&error](const QString &message) { error = message; });
        engine.analyze(Position::initial(), false);
        QTRY_VERIFY_WITH_TIMEOUT(!error.isEmpty(), 10000);
        QVERIFY2(error.contains(QStringLiteral("代码 1")), qPrintable(error));
        QVERIFY2(error.contains(QStringLiteral("EvalFile")), qPrintable(error));
        QVERIFY2(error.contains(QStringLiteral("network file")), qPrintable(error));
        engine.shutdown();
    }
};

QTEST_GUILESS_MAIN(EngineIntegrationTest)
#include "engine_integration_test.moc"
